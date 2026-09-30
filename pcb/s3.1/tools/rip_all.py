import re
t=open('s3.1.kicad_pcb').read()
KEEP={"GND"}
nseg=[0];nvia=[0]
def keep(m):
    blk=m.group(0)
    if '(locked yes)' in blk: return blk
    n=re.search(r'\(net "([^"]+)"\)',blk)
    if not n or n.group(1) in KEEP: return blk
    nseg[0]+=1; return ""
t=re.sub(r"\(segment\n(?:\t\t[^\n]*\n)+?\t\)\n",keep,t)
def keepv(m):
    blk=m.group(0)
    if '(locked yes)' in blk: return blk
    n=re.search(r'\(net "([^"]+)"\)',blk)
    if not n or n.group(1) in KEEP: return blk
    nvia[0]+=1; return ""
t=re.sub(r"\(via\n(?:\t\t[^\n]*\n)+?\t\)\n",keepv,t)
open('s3.1.kicad_pcb','w').write(t)
print(f"ripped {nseg[0]} segs, {nvia[0]} vias")
