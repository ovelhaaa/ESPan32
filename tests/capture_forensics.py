"""Capture COM device output verbatim; never overwrite existing hardware evidence."""
import argparse
import time
import sys
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--port", default="COM10")
parser.add_argument("--seconds", type=float, default=420)
parser.add_argument("--rows", type=int, default=32)
parser.add_argument("--reset", action="store_true", help="Reset board via RTS on start")
args = parser.parse_args()
# Never let a completed hardware capture end with a traceback: the output
# directory is created first, and an existing evidence file is refused cleanly
# instead of raising FileNotFoundError / FileExistsError.
args.output.parent.mkdir(parents=True, exist_ok=True)
if args.output.exists():
    print(f"Refusing to overwrite existing evidence: {args.output}", flush=True)
    sys.exit(1)
with args.output.open("xb") as report, serial.Serial(args.port, 115200, timeout=1) as port:
    if args.reset:
        port.setDTR(False)
        port.setRTS(True)
        time.sleep(0.1)
        port.setRTS(False)
    end = time.monotonic() + args.seconds
    curves = 0
    while time.monotonic() < end:
        line = port.readline()
        report.write(line)
        report.flush()
        if b"[CURVE]" in line or b"[PHASE]" in line or b"Guru Meditation" in line:
            print(line.decode(errors="replace").strip(), flush=True)
        if b"[CURVE]" in line:
            curves += 1
            if curves == args.rows:
                # Allow final phase and I/O messages to be captured too.
                end = min(end, time.monotonic() + 6)
    print(f"Captured {curves}/{args.rows} curve rows to {args.output}", flush=True)
    if curves != args.rows:
        sys.exit(2)
