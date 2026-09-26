"""Summarize captured device evidence; never substitute host timing for hardware."""
import re
import sys
from pathlib import Path


def read(path):
    rows = {}
    phases = {}
    totals = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        fields = dict(re.findall(r"(\w+)=([^\s\x1b]+)", line))
        if "[CURVE]" in line:
            key = int(fields["fixture"])
            if fields["model"] == "BELL" and key < 8:
                key += 8  # First capture used model-relative IDs.
            rows[key, fields["class"]] = fields
        if "[PHASE]" in line:
            phases[int(fields["fixture"]), int(fields["phase"])] = float(fields["cycles_per_block"])
        if "[PROFILE_TOTAL]" in line:
            totals[int(fields["fixture"])] = float(fields["cycles_per_block"])
    return rows, phases, totals


def summary(path):
    rows, phases, totals = read(path)
    print(f"\n### {Path(path).name}\n")
    print("| Model/fixture | Class | N | Avg us | P99 us | Max us | Deadline | Bad voices/BLE lost |")
    print("|---|---|---:|---:|---:|---:|---:|---|")
    for (i, kind), r in sorted(rows.items()):
        print(f'| {r["model"]}/{i} ({r["voices"]} voices) | {kind} | {r["n"]} | {r["avg_us"]} | {r["p99_us"]} | {r["max_us"]} | {r["deadline"]} | {r["bad_voices"]}/{r["ble_lost"]} |')
    for offset, model in [(0, "PAN"), (8, "BELL")]:
        selected = [rows.get((offset + i, "steady")) for i in range(6)]
        if not all(selected):
            continue
        xs = [float(r["voices"]) for r in selected]
        ys = [float(r["avg_us"]) for r in selected]
        xm, ym = sum(xs) / 6, sum(ys) / 6
        slope = sum((x - xm) * (y - ym) for x, y in zip(xs, ys)) / sum((x - xm) ** 2 for x in xs)
        print(f"\n{model}: measured zero-voice fixed cost {ys[0]:.2f} us; least-squares intercept {ym - slope * xm:.2f} us, slope {slope:.2f} us/voice.")
        print("\nIncremental cost per additional voice:")
        for j in range(1, 6):
            print(f"- {xs[j-1]:g}->{xs[j]:g}: {(ys[j]-ys[j-1])/(xs[j]-xs[j-1]):.2f} us/voice")
    for i in sorted(totals):
        p = [phases.get((i, j), 0) for j in range(9)]
        # Allocator and mix include nested scopes; partition without double counting.
        modal = p[1]
        voice = p[0] - p[1]
        body = p[5]
        output = p[7] + p[8]
        other = totals[i] - p[0] - body - output
        print(f"\nProfile fixture {i}: total {totals[i]/240:.2f} us; modal {modal/totals[i]*100:.1f}%, voice/allocator residual {voice/totals[i]*100:.1f}%, body {body/totals[i]*100:.1f}%, limiter/PCM {output/totals[i]*100:.1f}%, other {other/totals[i]*100:.1f}%.")
        voices = float(rows[i, "steady"]["voices"])
        if voices:
            modes = 8 if i < 8 or i in (16, 18) else 9 if i == 17 else 10
            print(f"Modal diagnostic cost: {modal/240/voices:.2f} us/voice/block, {modal/240/voices/modes:.2f} us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.")


if __name__ == "__main__":
    for filename in sys.argv[1:]:
        summary(filename)
