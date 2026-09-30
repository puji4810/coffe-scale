#!/usr/bin/env python3
"""Raw-capture recorder for coffee-scale.

Connects to the scale's USB-serial-JTAG console, toggles the 'r' raw
stream on, and writes every stream line verbatim to a capture file —
the input for `xmake run replay`.

    python3 tools/capture.py [--port /dev/ttyACM0] [-o file]

While running: type a label + Enter to drop a `M,<t_us>,<label>` marker
into the stream. Ctrl-C stops the stream on the device and closes.
"""

import argparse
import glob
import os
import sys
import threading
import time

import serial

STREAM_PREFIXES = ("W,", "A,", "E,", "T,", "C,")
HEADER_PREFIX = "# coffee-scale raw"


def find_port():
    for pat in ("/dev/ttyACM*", "/dev/ttyUSB*"):
        ports = sorted(glob.glob(pat))
        if ports:
            return ports[0]
    return None


def main():
    ap = argparse.ArgumentParser(description="capture raw scale stream")
    ap.add_argument("--port", help="serial port (default: first ttyACM/USB)")
    ap.add_argument("-o", "--out",
                    help="output file (default: captures/raw-<ts>.txt)")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("no serial port found — pass --port /dev/ttyACMx")

    here = os.path.dirname(os.path.abspath(__file__))
    out = args.out
    if not out:
        capdir = os.path.join(here, "..", "captures")
        os.makedirs(capdir, exist_ok=True)
        out = os.path.join(capdir,
                           time.strftime("raw-%Y%m%d-%H%M%S.txt"))

    # Construct unopened so DTR/RTS stay deasserted — opening the port
    # would otherwise reset the chip a second time.
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.1
    ser.write_timeout = 1
    ser.dtr = False
    ser.rts = False
    ser.open()

    def send(b):
        try:
            ser.write(b)
        except serial.SerialTimeoutException:
            ser.close()
            sys.exit("scale does not accept input (write timed out) — "
                     "reflash firmware with the console fix")

    # The open resets the chip: drain boot output until "firmware up"
    # shows, or the port has been quiet for 1.5 s (max 8 s).
    print("[capture] waiting for scale boot…", file=sys.stderr)
    boot_end = time.monotonic() + 8.0
    quiet_end = time.monotonic() + 1.5
    while time.monotonic() < boot_end:
        line = ser.readline().decode("utf-8", "replace").strip()
        if line:
            quiet_end = time.monotonic() + 1.5
            if "firmware up" in line:
                break
        elif time.monotonic() >= quiet_end:
            break
    print("[capture] scale ready", file=sys.stderr)

    # Start the stream: 'r' toggles, so a header may mean "now on" or a
    # second 'r' is needed if it was already on (toggle off = silence).
    # Hard 3 s deadline per attempt — a chatty device must not extend it.
    line = ""
    for attempt in (1, 2):
        send(b"r")
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            cand = ser.readline().decode("utf-8", "replace").strip()
            if cand.startswith(HEADER_PREFIX):
                line = cand
                break
        if line:
            break
    if not line:
        ser.close()
        sys.exit("no raw-stream header within 3 s x2 — is 'r' supported? "
                 "(reflash firmware)")

    fout = open(out, "w", buffering=1)
    fout.write(line + "\n")
    print(f"[capture] header ok -> {out}", file=sys.stderr)
    print("[capture] note: opening the port reset the scale — tare is "
          "cleared; tare again after placing gear", file=sys.stderr)
    print("[capture] Ctrl-C to stop; type a label + Enter to mark",
          file=sys.stderr)

    stats = {"w": 0, "a": 0, "e": 0, "t": 0, "c": 0, "m": 0, "other": 0}
    last_tus = [0]          # t_us of the last W/A line (for markers)
    stop = threading.Event()

    # stdin marker thread: a line of text becomes M,<t_us>,<label>
    def marker_thread():
        for text in sys.stdin:
            label = text.strip()
            if not label or stop.is_set():
                continue
            fout.write(f"M,{last_tus[0]},{label}\n")
            stats["m"] += 1
            print(f"[capture] marker: {label}", file=sys.stderr)

    t0 = time.monotonic()
    mt = threading.Thread(target=marker_thread, daemon=True)
    mt.start()

    def status():
        el = time.monotonic() - t0
        print(f"[capture] {el:6.0f}s  W {stats['w'] / el:5.1f} Hz  "
              f"A {stats['a'] / el:5.1f} Hz  E {stats['e']}  "
              f"T {stats['t']}  C {stats['c']}  "
              f"M {stats['m']}  other {stats['other']}", file=sys.stderr)

    try:
        nxt = t0 + 1.0
        while True:
            raw = ser.readline()
            if raw:
                line = raw.decode("utf-8", "replace").strip()
                if line.startswith(STREAM_PREFIXES):
                    fout.write(line + "\n")
                    tag = line[0]
                    stats["w" if tag == "W" else "a" if tag == "A"
                          else "t" if tag == "T" else "c" if tag == "C"
                          else "e"] += 1
                    if tag in "WA":
                        try:
                            last_tus[0] = int(line.split(",", 2)[1])
                        except ValueError:
                            pass
                elif line.startswith(HEADER_PREFIX):
                    fout.write(line + "\n")
                else:
                    stats["other"] += 1   # ESP log lines etc.
            if time.monotonic() >= nxt:
                status()
                nxt += 1.0
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        try:
            ser.write(b"r")      # stop the stream on the device
            time.sleep(0.1)
        except serial.SerialTimeoutException:
            print("[capture] warn: could not send stop byte",
                  file=sys.stderr)
        ser.close()
        status()
        print(f"[capture] saved {stats['w']} W + {stats['a']} A + "
              f"{stats['e']} E + {stats['t']} T + {stats['c']} C + "
              f"{stats['m']} M lines -> {out}",
              file=sys.stderr)
        fout.close()


if __name__ == "__main__":
    main()
