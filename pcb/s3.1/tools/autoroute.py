#!/usr/bin/env python3
"""gen_pcb.py -> Specctra DSN -> freerouting -> SES merge -> width fix -> DRC.

Pipeline details:
- (locked yes) tracks/vias export as (type fix) and are echoed back untouched;
  we re-lock them after merging since the SES carries no lock flag.
- freerouting only registers connectivity at the ENDS of a fixed polyline,
  so fixed wires are split into 2-point wires in the DSN.
- The F.Cu GND plane is dropped from the DSN so GND pads get real via fanout
  to the B.Cu plane; afterwards the polygon is merged back as a fixed pour wire.
- freerouting necks fanout stubs below the 0.2 mm min -> we widen to class
  width, then surgically shrink only segments that then violate clearance.
- Clearance/unconnected leftovers: rip up all non-locked copper on the
  violating nets and rerun the whole export->route->merge cycle.

usage: autoroute.py [--passes N] [--rounds N] [--post]
"""
import argparse, os, re, subprocess, sys, time
from collections import Counter
DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(DIR, "s3.1.kicad_pcb")
WORK = "/tmp/pcbv/route"
DRC = os.path.join(DIR, "s3.1-drc.txt")
POWER = {"VBUS", "BAT", "VSYS", "VCC_3V3", "AVDD", "VDD_ADC", "GND"}
def WID(net): return 0.5 if net in POWER and net != "GND" else 0.25

ap = argparse.ArgumentParser()
ap.add_argument("--passes", type=int, default=60)
ap.add_argument("--resume", action="store_true",
                help="skip gen_pcb regen; continue from the current .kicad_pcb")
ap.add_argument("--rounds", type=int, default=4)
ap.add_argument("--post", action="store_true", help="only relock + width/DRC loop")
ap.add_argument("--jar", default="/tmp/pcbv/tools/freerouting.jar")
a = ap.parse_args()
os.makedirs(WORK, exist_ok=True)
dsn, ses = os.path.join(WORK, "s3.1.dsn"), os.path.join(WORK, "s3.1.ses")

import pcbnew  # noqa: E402

def run_drc():
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "report", "--output", DRC,
                    "--severity-all", "--refill-zones", "--save-board", "--schematic-parity", PCB],
                   capture_output=True, text=True)
    return open(DRC).read()

def counts(rt): return Counter(re.findall(r"^\[(\w+)\]", rt, re.M)).most_common()

def viol_coords(rt, kind):
    pts = set()
    for blk in re.findall(rf"(?ms)^\[{kind}\].*?(?=^\[|\Z)", rt):
        for x, y in re.findall(r"@\(([\d.]+) mm, ([\d.]+) mm\)", blk):
            pts.add((round(float(x), 2), round(float(y), 2)))
    return pts

def viol_nets(rt, kinds=("clearance", "unconnected_items", "copper_edge_clearance",
                         "track_width", "shorting_items", "via_dangling")):
    nets = set()
    for kind in kinds:
        for blk in re.findall(rf"(?ms)^\[{kind}\].*?(?=^\[|\Z)", rt):
            nets.update(re.findall(r"(?:走线|过孔|焊盘|填充区|过孔|via|track|pad)[^\[\]]*\[([^\]]+)\]", blk))
    return nets

def seg_hits(x1, y1, x2, y2, pts, tol=0.45):
    for x, y in pts:
        dx, dy = x2 - x1, y2 - y1
        L2 = dx * dx + dy * dy
        t = 0 if L2 == 0 else max(0, min(1, ((x - x1) * dx + (y - y1) * dy) / L2))
        if (x - (x1 + t * dx)) ** 2 + (y - (y1 + t * dy)) ** 2 < tol * tol:
            return True
    return False

def export_dsn():
    board = pcbnew.LoadBoard(PCB)
    if os.path.exists(dsn): os.remove(dsn)
    assert pcbnew.ExportSpecctraDSN(board, dsn)
    txt = open(dsn).read()
    plane = re.search(r"^    \(plane GND \(polygon F\.Cu (.*?)\)\)\n", txt, flags=re.M)
    if plane: txt = txt.replace(plane.group(0), "")
    def split_fixed(m):
        cd = m.group(3).split()
        pts = list(zip(cd[0::2], cd[1::2]))
        return "".join(f"    (wire (path {m.group(1)} {m.group(2)} {p[0]} {p[1]} {n[0]} {n[1]})(net {m.group(4)})(type fix))\n"
                       for p, n in zip(pts, pts[1:]))
    txt = re.sub(r"^    \(wire \(path (\S+) (\d+)\s+([-\d\s]+?)\)\(net (\S+)\)\(type fix\)\)\n",
                 split_fixed, txt, flags=re.M)
    txt = re.sub(r"\(class kicad_default[^()]*?(?=\s*\(circuit)",
                 lambda m: re.sub(r"\s+(VBUS|BAT|VSYS|VCC_3V3|AVDD|VDD_ADC|GND)(?=\s)", "", m.group(0)),
                 txt, count=1)
    analog = "LC_SIGP LC_SIGN SIG_P SIG_N VBG AVDD VDD_ADC"
    txt = txt.replace("    (class kicad_default",
                      f'    (class Power {" ".join(POWER)}\n      (circuit (use_via "Via[0-1]_700:350_um"))\n'
                      "      (rule (width 500) (clearance 200))\n    )\n"
                      f'    (class ANV {analog}\n      (circuit)\n      (rule (width 250) (clearance 200))\n    )\n'
                      "    (class kicad_default", 1)
    # pull analog nets out of the default class too
    txt = re.sub(r"\(class kicad_default[^()]*?(?=\s*\(circuit)",
                 lambda m: re.sub(r"\s+(LC_SIGP|LC_SIGN|SIG_P|SIG_N|VBG|AVDD|VDD_ADC)(?=\s)", "", m.group(0)),
                 txt, count=1)
    open(dsn, "w").write(txt)
    return txt, plane

def route():
    if os.path.exists(ses): os.remove(ses)
    # freerouting's save occasionally dies silently after the long mp-run; a
    # pre-generated .ses (e.g. short -mp foreground run on the same .dsn) can
    # be injected via USE_SES to skip the flaky java call entirely.
    premade = os.environ.get("USE_SES")
    if premade:
        import shutil
        shutil.copyfile(premade, ses)
        print(f"using premade ses {premade}")
        return
    # freerouting's save dies probabilistically (race in the writer); retry the
    # whole java call a few times — a fresh JVM usually saves in <1s.
    for attempt in range(3):
        r = subprocess.run(["java", "--enable-final-field-mutation=ALL-UNNAMED", "-jar", a.jar,
                            "-de", dsn, "-do", ses, "-mp", str(a.passes), "-mt", "1", "-l", "en"],
                           capture_output=True, text=True, timeout=2400)
        open(os.path.join(WORK, "freerouting.log"), "w").write(r.stdout + r.stderr)
        for l in (r.stdout + r.stderr).splitlines():
            if re.search(r"(?i)could not|unrouted connection|final score|exception", l):
                print(" ", l[:160])
        if os.path.exists(ses) and os.path.getsize(ses):
            return
        print(f"empty SES (attempt {attempt + 1}), retrying")
    sys.exit("empty SES")

def merge(txt, plane):
    data = open(ses).read()
    board = pcbnew.LoadBoard(PCB)
    def netcode(name):
        n = board.FindNet(name)
        if n is None: n = board.GetNetInfo().GetNetItem(name)
        return n.GetNetCode()
    nseg = nvia = 0
    for blk in re.split(r"(?m)^      \(net ", data)[1:]:
        net = re.match(r'"([^"]+)"|(\S+)', blk).group(0).strip('"')
        body = blk.split("\n      )")[0]
        for wm in re.finditer(r"\(wire\s+\(path (\S+) (\d+)\s+([-\d.\s]+?)\)", body):
            cd = wm.group(3).split()
            pts = [(float(cd[i]) * 1e-4, float(cd[i + 1]) * -1e-4) for i in range(0, len(cd) - 1, 2)]
            for p, q in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(int(p[0] * 1e6), int(p[1] * 1e6)))
                t.SetEnd(pcbnew.VECTOR2I(int(q[0] * 1e6), int(q[1] * 1e6)))
                t.SetWidth(int(float(wm.group(2)) * 1e-4 * 1e6))
                t.SetLayer(pcbnew.B_Cu if wm.group(1) == "B.Cu" else pcbnew.F_Cu)
                t.SetNetCode(netcode(net)); board.Add(t); nseg += 1
        for vm in re.finditer(r'\(via "Via\[0-1\]_700:350_um" (-?[\d.]+) (-?[\d.]+)', body):
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(int(float(vm.group(1)) * 1e-4 * 1e6),
                                          int(float(vm.group(2)) * -1e-4 * 1e6)))
            v.SetWidth(700000); v.SetDrill(350000); v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetNetCode(netcode(net)); board.Add(v); nvia += 1
    pcbnew.SaveBoard(PCB, board)
    print(f"merged {nseg} segs {nvia} vias")
    # dedupe identical segments/vias (SES echoes fixed wires back each round)
    pt = open(PCB).read()
    pt2, kept = [], set()
    last = 0
    for m in re.finditer(r"(\((?:segment|via)\n(?:\t\t[^\n]*\n)+?\t\)\n?)", pt):
        pt2.append(pt[last:m.start()]); last = m.end()
        blk = m.group(1)
        key = re.sub(r"\(uuid [^)]*\)", "", blk).replace("\t\t(locked yes)\n", "")
        if key not in kept:
            kept.add(key); pt2.append(blk)
    pt2.append(pt[last:])
    open(PCB, "w").write("".join(pt2))

def fixed_pts(txt):
    pts = set()
    for m in re.finditer(r"\(wire \(path \S+ \d+\s+([-\d.\s]+?)\)\(net (\S+)\)\(type fix\)", txt):
        cd = m.group(1).split()
        for i in range(0, len(cd) - 1, 2):
            pts.add((round(float(cd[i]) * 1e-3, 2), round(float(cd[i + 1]) * -1e-3, 2)))
    return pts

def relock(txt):
    fixed = fixed_pts(txt)
    pt = open(PCB).read()
    def rl(m):
        blk = m.group(0)
        if "(locked yes)" in blk: return blk
        s_ = re.search(r"\(start ([-\d.e]+) ([-\d.e]+)\)", blk)
        e_ = re.search(r"\(end ([-\d.e]+) ([-\d.e]+)\)", blk)
        if s_ and e_ and (round(float(s_.group(1)), 2), round(float(s_.group(2)), 2)) in fixed \
           and (round(float(e_.group(1)), 2), round(float(e_.group(2)), 2)) in fixed:
            return blk.replace("\t\t(net", "\t\t(locked yes)\n\t\t(net", 1)
        return blk
    pt = re.sub(r"\(segment\n(?:\t\t[^\n]*\n)+?\t\)", rl, pt)
    def rlv(m):
        blk = m.group(0)
        if "(locked yes)" in blk: return blk
        p = re.search(r"\(at ([-\d.e]+) ([-\d.e]+)\)", blk)
        if p and (round(float(p.group(1)), 2), round(float(p.group(2)), 2)) in fixed:
            return blk.replace("\t\t(layers", "\t\t(locked yes)\n\t\t(layers", 1)
        return blk
    # vias: freerouting never echoes fixed vias' coords inside wire paths, so
    # match via position against fixed via list is not possible; relock by net+pos
    open(PCB, "w").write(pt)

def cleanup_stubs():
    """delete non-locked micro-segments (<0.4 mm) that don't touch both ends of a net"""
    pt = open(PCB).read()
    def short(m):
        blk = m.group(0)
        if "(locked yes)" in blk: return blk
        s_ = re.search(r"\(start ([-\d.e]+) ([-\d.e]+)\)", blk)
        e_ = re.search(r"\(end ([-\d.e]+) ([-\d.e]+)\)", blk)
        if not s_ or not e_: return blk
        x1, y1, x2, y2 = map(float, s_.groups() + e_.groups())
        return "" if (x2 - x1) ** 2 + (y2 - y1) ** 2 < 0.12 ** 2 else blk
    open(PCB, "w").write(re.sub(r"\(segment\n(?:\t\t[^\n]*\n)+?\t\)\n", short, pt))

def fix_widths():
    pt = open(PCB).read()
    def widen(m):
        blk = m.group(0)
        net = re.search(r'\(net "([^"]+)"\)', blk)
        if not net: return blk
        w = float(re.search(r"\(width ([\d.e-]+)\)", blk).group(1))
        want = WID(net.group(1))
        return re.sub(r"\(width [\d.e-]+\)", f"(width {want})", blk) if w < want else blk
    open(PCB, "w").write(re.sub(r"\(segment\n(?:\t\t[^\n]*\n)+?\t\)", widen, pt))

def shrink_at(rt):
    bad = viol_coords(rt, "clearance")
    changed = []
    pt = open(PCB).read()
    def shrink(m):
        blk = m.group(0)
        net = re.search(r'\(net "([^"]+)"\)', blk)
        w = float(re.search(r"\(width ([\d.e-]+)\)", blk).group(1))
        if not net or w <= 0.25 or net.group(1) not in POWER: return blk
        s_ = re.search(r"\(start ([-\d.e]+) ([-\d.e]+)\)", blk)
        e_ = re.search(r"\(end ([-\d.e]+) ([-\d.e]+)\)", blk)
        if not s_ or not e_: return blk
        if seg_hits(float(s_.group(1)), float(s_.group(2)), float(e_.group(1)), float(e_.group(2)), bad):
            changed.append(1)
            return re.sub(r"\(width [\d.e-]+\)", "(width 0.25)", blk)
        return blk
    pt2 = re.sub(r"\(segment\n(?:\t\t[^\n]*\n)+?\t\)", shrink, pt)
    if pt2 != pt: open(PCB, "w").write(pt2)
    return changed

_uuid_i = [0x1000]
def _uuid():
    _uuid_i[0] += 1
    return f"b2a40000-0000-4000-8000-{_uuid_i[0]:012x}"

VIA_TMPL = '\t(via\n\t\t(at {x} {y})\n\t\t(size 0.7)\n\t\t(drill 0.35)\n\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "{net}")\n\t\t(uuid "{u}")\n\t)\n'
SEG_TMPL = '\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n\t\t(width {w})\n\t\t(layer "{lay}")\n\t\t(net "{net}")\n\t\t(uuid "{u}")\n\t)\n'

def _via_at(pt, x, y, tol=0.05):
    for m in re.finditer(r"\(via\n(?:\t\t[^\n]*\n)+?\t\)", pt):
        p = re.search(r"\(at ([-\d.e]+) ([-\d.e]+)\)", m.group(0))
        if p and abs(float(p.group(1)) - x) < tol and abs(float(p.group(2)) - y) < tol:
            return m
    return None

def _seg_at(pt, x1, y1, x2, y2, net=None, tol=0.15):
    for m in re.finditer(r"\(segment\n(?:\t\t[^\n]*\n)+?\t\)", pt):
        blk = m.group(0)
        if net and f'(net "{net}")' not in blk: continue
        s_ = re.search(r"\(start ([-\d.e]+) ([-\d.e]+)\)", blk)
        e_ = re.search(r"\(end ([-\d.e]+) ([-\d.e]+)\)", blk)
        a_ = (float(s_.group(1)), float(s_.group(2))); b_ = (float(e_.group(1)), float(e_.group(2)))
        for A, B in ((a_, b_), (b_, a_)):
            if abs(A[0] - x1) < tol and abs(A[1] - y1) < tol and abs(B[0] - x2) < tol and abs(B[1] - y2) < tol:
                return m
    return None

def post_route_fixes():
    """Deterministic hand fixes applied after each merge (idempotent).

    freerouting keeps producing these three problems; replay the fixes:
    - stray VBUS via at (47.0,28.0): nothing connects on F.Cu, and together
      with the (48.291,27.956) via it walls off the J5-CC2 corridor. Delete.
    - Net-(J5-CC2) R9.1 -> J5.B5: the only clear F.Cu path is a jog:
      drop at x47.6 between the two VBUS vias, then x47.78 into pad B5.
    - GND island welds: vias that tie each pour island to the connected
      net, plus the C7.2 pad stub and the U2 chain link.
    """
    pt = open(PCB).read()
    # 1) delete stray VBUS via
    m = _via_at(pt, 47.0, 28.0)
    if m: pt = pt.replace(m.group(0), "", 1)
    # 2) CC2 reroute
    if _seg_at(pt, 48.85, 25.3, 47.6, 26.55, "Net-(J5-CC2)") is None:
        pt = re.sub(r"\(segment\n(?:\t\t[^\n]*\n)+?\(net \"Net-\(J5-CC2\)\"\)(?:[^\n]*\n)+?\t\)\n", "", pt)
        ins = "".join(SEG_TMPL.format(x1=a_, y1=b_, x2=c_, y2=d_, w=0.25,
                                      lay="F.Cu", net="Net-(J5-CC2)", u=_uuid())
                      for a_, b_, c_, d_ in [(48.85, 25.3, 47.6, 26.55),
                                             (47.6, 26.55, 47.6, 29.3),
                                             (47.6, 29.3, 47.78, 30.1),
                                             (47.78, 30.1, 47.78, 30.955)])
        m = re.search(r"\(segment\n", pt)
        pt = pt[:m.start()] + ins + pt[m.start():]
    # 3) GND welds / stubs
    for x, y in [(30.95, 11.0), (31.9, 10.0), (31.0, 15.4), (42.0, 11.0),
                 (45.5, 26.5), (42.9, 17.0), (49.0, 8.0)]:
        if not _via_at(pt, x, y):
            ins = VIA_TMPL.format(x=x, y=y, net="GND", u=_uuid())
            m = re.search(r"\(via\n", pt)
            pt = pt[:m.start()] + ins + pt[m.start():]
    # 4) VCC_3V3 C12/U4 island -> main tree. The (53.45,8.5)->(49.85,12.1)
    #    ->(49.85,14.4) leg (C11/C12 + U4 3V3 pins) is left isolated by
    #    freerouting; bridge it to the R15/R16 stub junction along the north
    #    lane, then east of the BTN_MODE x49.49 vertical.
    if (_seg_at(pt, 49.85, 12.1, 49.85, 14.4, "VCC_3V3") is not None
            and _seg_at(pt, 44.5981, 9.5356, 44.2318, 9.5356, "VCC_3V3") is not None
            and _seg_at(pt, 48.5, 9.72, 50.6, 9.72, "VCC_3V3") is None):
        ins = "".join(SEG_TMPL.format(x1=a_, y1=b_, x2=c_, y2=d_, w=0.25,
                                      lay="F.Cu", net="VCC_3V3", u=_uuid())
                      for a_, b_, c_, d_ in [(44.5981, 9.5356, 44.9, 9.72),
                                             (44.9, 9.72, 48.5, 9.72),
                                             (48.5, 9.72, 50.6, 9.72),
                                             (50.6, 9.72, 50.6, 11.0),
                                             (50.6, 11.0, 49.85, 12.1)])
        m = re.search(r"\(segment\n", pt)
        pt = pt[:m.start()] + ins + pt[m.start():]
    for x1, y1, x2, y2 in [(30.95, 12.3, 30.95, 11.0),      # C7.2 pad stub
                           (31.0903, 16.0, 31.0, 15.4)]:   # U2 chain link
        if not _seg_at(pt, x1, y1, x2, y2, "GND"):
            ins = SEG_TMPL.format(x1=x1, y1=y1, x2=x2, y2=y2, w=0.25,
                                  lay="F.Cu", net="GND", u=_uuid())
            m = re.search(r"\(segment\n", pt)
            pt = pt[:m.start()] + ins + pt[m.start():]
    open(PCB, "w").write(pt)

def ripup(nets, rt):
    """delete non-locked segments/vias on violating nets near violation points"""
    bad = set()
    for kind in ("clearance", "unconnected_items", "via_dangling", "shorting_items",
                 "copper_edge_clearance", "track_width"):
        bad |= viol_coords(rt, kind)
    pt = open(PCB).read()
    def keep_seg(m):
        blk = m.group(0)
        if "(locked yes)" in blk: return blk
        net = re.search(r'\(net "([^"]+)"\)', blk)
        if not net or net.group(1) not in nets: return blk
        s_ = re.search(r"\(start ([-\d.e]+) ([-\d.e]+)\)", blk)
        e_ = re.search(r"\(end ([-\d.e]+) ([-\d.e]+)\)", blk)
        if not s_ or not e_: return blk
        hit = seg_hits(float(s_.group(1)), float(s_.group(2)),
                       float(e_.group(1)), float(e_.group(2)), bad, tol=3.0)
        # also drop if an ENDPOINT of the violation is this seg's endpoint
        return "" if hit else blk
    pt = re.sub(r"\(segment\n(?:\t\t[^\n]*\n)+?\t\)\n", keep_seg, pt)
    def keep_via(m):
        blk = m.group(0)
        if "(locked yes)" in blk: return blk
        net = re.search(r'\(net "([^"]+)"\)', blk)
        if not net or net.group(1) not in nets: return blk
        p_ = re.search(r"\(at ([-\d.e]+) ([-\d.e]+)\)", blk)
        if not p_: return blk
        x, y = float(p_.group(1)), float(p_.group(2))
        return "" if seg_hits(x, y, x, y, bad, tol=3.0) else blk
    pt = re.sub(r"\(via\n(?:\t\t[^\n]*\n)+?\t\)\n", keep_via, pt)
    open(PCB, "w").write(pt)
    print(f"ripped near {len(bad)} violation pts on: {sorted(nets)}")

# ================= run =================
if a.post:
    txt = open(dsn).read() if os.path.exists(dsn) else ""
    relock(txt)
    cleanup_stubs()
    post_route_fixes()
    fix_widths()
    for it in range(10):
        rt = run_drc()
        print(f"iter {it}: {counts(rt)}")
        if "[clearance]" not in rt and "[track_width]" not in rt: break
        if "[clearance]" in rt and not shrink_at(rt): break
    for l in run_drc().splitlines():
        if l.startswith("** Found"): print(l)
    sys.exit(0)

if not a.resume:
    subprocess.run([sys.executable, os.path.join(DIR, "tools", "gen_pcb.py")], check=True)
for rnd in range(a.rounds):
    print(f"===== round {rnd} =====")
    txt, plane = export_dsn()
    print("fixed wires:", txt.count("(type fix)"))
    t0 = time.time(); route(); print(f"freerouting {time.time()-t0:.0f}s")
    merge(txt, plane)
    relock(txt)
    cleanup_stubs()
    post_route_fixes()
    fix_widths()
    for it in range(8):
        rt = run_drc()
        print(f"  iter {it}: {counts(rt)}")
        if "[clearance]" not in rt and "[track_width]" not in rt: break
        if "[clearance]" in rt and not shrink_at(rt): break
    rt = run_drc()
    bad_kinds = [k for k in ("clearance", "unconnected_items", "copper_edge_clearance",
                           "track_width", "shorting_items", "via_dangling") if f"[{k}]" in rt]
    if not bad_kinds: break
    nets = viol_nets(rt)
    print("violating nets:", sorted(nets))
    if rnd == a.rounds - 1: break
    ripup(nets, rt)
print("=== final ===")
for l in run_drc().splitlines():
    if l.startswith("** Found"): print(l)
