"""Capture a physical qualification run; no synthetic hardware results."""
import argparse
import pathlib
import time
import serial

p = argparse.ArgumentParser()
p.add_argument("output", type=pathlib.Path)
p.add_argument("--port", default="COM10")
p.add_argument("--seconds", type=float, default=80)
args = p.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
with serial.Serial(args.port, 115200, timeout=0.2) as device, args.output.open("wb") as out:
    start = time.monotonic()
    while time.monotonic() - start < args.seconds:
        data = device.read(16384)
        if data:
            out.write(data)
            out.flush()
print(f"Captured {args.output} ({args.output.stat().st_size} bytes)")
