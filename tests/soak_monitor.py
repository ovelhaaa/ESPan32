"""Production soak monitor for ESPan32.
Monitors serial output, records verbatim to file, and validates stability gates:
- zero deadline misses
- zero write timeouts / short writes / tx errors
- zero BLE reconnects
- zero MIDI drops
"""
import argparse
import re
import sys
import time
from pathlib import Path
import serial

AUDIO_RE = re.compile(
    r"\[AUDIO\] model=(\w+) blocks=(\d+) avg_us=(\d+) p99_us=(\d+) max_us=(\d+) cpu_load=([\d\.]+) deadline=(\d+) timeout=(\d+) tx_error=(\d+) short=(\d+)"
)
MIDI_RE = re.compile(r"\[MIDI\] push=(\d+) pop=(\d+) drop=(\d+) hwm=(\d+)")
BLE_RE = re.compile(r"\[BLE\] state=(\d+) interval_ms=([\d\.]+) latency=(\d+) rssi=(-?\d+) reconnects=(\d+) last_disconnect=(\d+)")
MEM_RE = re.compile(r"\[MEM\] internal_free=(\d+) largest_internal=(\d+)")

def main():
    parser = argparse.ArgumentParser(description="Run and monitor production soak test")
    parser.add_argument("output", type=Path, help="Output log path")
    parser.add_argument("--port", default="COM10", help="Serial port")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--seconds", type=float, default=1800.0, help="Soak duration in seconds (default 1800 = 30m)")
    parser.add_argument("--reset", action="store_true", help="Reset board via RTS on start")
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        args.output.unlink()

    print(f"Starting soak monitor on {args.port} for {args.seconds:.0f}s ({args.seconds/60:.1f} min)...", flush=True)
    print(f"Logging to {args.output}", flush=True)

    with args.output.open("wb") as report, serial.Serial(args.port, args.baud, timeout=1) as port:
        if args.reset:
            print("Resetting board via RTS...", flush=True)
            port.setDTR(False)
            port.setRTS(True)
            time.sleep(0.1)
            port.setRTS(False)

        start_time = time.monotonic()
        end_time = start_time + args.seconds
        last_progress_time = start_time

        total_lines = 0
        deadlines = 0
        timeouts = 0
        tx_errors = 0
        short_writes = 0
        midi_drops = 0
        ble_reconnects = 0
        crashes = 0

        latest_audio = {}
        latest_ble = {}
        latest_mem = {}

        while time.monotonic() < end_time:
            line_bytes = port.readline()
            if not line_bytes:
                continue

            report.write(line_bytes)
            report.flush()
            total_lines += 1

            line = line_bytes.decode("latin1", errors="replace").strip()

            if "Guru Meditation" in line or "abort()" in line or "Backtrace:" in line:
                print(f"\n[CRASH DETECTED] {line}", flush=True)
                crashes += 1

            m_audio = AUDIO_RE.search(line)
            if m_audio:
                latest_audio = {
                    "model": m_audio.group(1),
                    "blocks": int(m_audio.group(2)),
                    "avg_us": int(m_audio.group(3)),
                    "p99_us": int(m_audio.group(4)),
                    "max_us": int(m_audio.group(5)),
                    "cpu": float(m_audio.group(6)),
                    "deadline": int(m_audio.group(7)),
                    "timeout": int(m_audio.group(8)),
                    "tx_error": int(m_audio.group(9)),
                    "short": int(m_audio.group(10)),
                }
                deadlines = max(deadlines, latest_audio["deadline"])
                timeouts = max(timeouts, latest_audio["timeout"])
                tx_errors = max(tx_errors, latest_audio["tx_error"])
                short_writes = max(short_writes, latest_audio["short"])

            m_midi = MIDI_RE.search(line)
            if m_midi:
                drops = int(m_midi.group(3))
                midi_drops = max(midi_drops, drops)

            m_ble = BLE_RE.search(line)
            if m_ble:
                latest_ble = {
                    "state": int(m_ble.group(1)),
                    "interval_ms": float(m_ble.group(2)),
                    "rssi": int(m_ble.group(4)),
                    "reconnects": int(m_ble.group(5)),
                }
                ble_reconnects = max(ble_reconnects, latest_ble["reconnects"])

            m_mem = MEM_RE.search(line)
            if m_mem:
                latest_mem = {
                    "free": int(m_mem.group(1)),
                    "largest": int(m_mem.group(2)),
                }

            now = time.monotonic()
            if now - last_progress_time >= 60.0:
                elapsed = now - start_time
                remain = max(0.0, end_time - now)
                print(
                    f"[{elapsed/60:4.1f}m / {args.seconds/60:.0f}m] "
                    f"blocks={latest_audio.get('blocks', 0)} "
                    f"avg={latest_audio.get('avg_us', 0)}us "
                    f"p99={latest_audio.get('p99_us', 0)}us "
                    f"max={latest_audio.get('max_us', 0)}us "
                    f"cpu={latest_audio.get('cpu', 0):.1f}% "
                    f"dline={deadlines} t/o={timeouts} tx_err={tx_errors} short={short_writes} "
                    f"midi_drop={midi_drops} ble_state={latest_ble.get('state', -1)} reconn={ble_reconnects} "
                    f"mem_free={latest_mem.get('free', 0)}",
                    flush=True
                )
                last_progress_time = now

    elapsed_total = time.monotonic() - start_time
    print(f"\n=======================================================", flush=True)
    print(f"SOAK TEST COMPLETED in {elapsed_total:.1f}s ({elapsed_total/60:.2f} min)", flush=True)
    print(f"Total lines captured: {total_lines}", flush=True)
    print(f"Final audio: {latest_audio}", flush=True)
    print(f"Final BLE:   {latest_ble}", flush=True)
    print(f"Final MEM:   {latest_mem}", flush=True)
    print(f"Summary gates:", flush=True)
    print(f"  Deadlines:      {deadlines} (target: 0)", flush=True)
    print(f"  Write timeouts: {timeouts} (target: 0)", flush=True)
    print(f"  TX errors:      {tx_errors} (target: 0)", flush=True)
    print(f"  Short writes:   {short_writes} (target: 0)", flush=True)
    print(f"  MIDI drops:     {midi_drops} (target: 0)", flush=True)
    print(f"  BLE reconnects: {ble_reconnects} (target: 0)", flush=True)
    print(f"  Crashes:        {crashes} (target: 0)", flush=True)
    print(f"=======================================================", flush=True)

    passed = (
        deadlines == 0 and
        timeouts == 0 and
        tx_errors == 0 and
        short_writes == 0 and
        midi_drops == 0 and
        ble_reconnects == 0 and
        crashes == 0
    )
    if passed:
        print("RESULT: PASS - All stability gates satisfied!", flush=True)
        sys.exit(0)
    else:
        print("RESULT: FAIL - One or more stability gates failed!", flush=True)
        sys.exit(1)

if __name__ == "__main__":
    main()
