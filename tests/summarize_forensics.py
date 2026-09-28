"""Summarize captured device evidence; never substitute host timing for hardware."""
import re
import sys
from pathlib import Path


# Phase values 0..8 are the M6.3.1 render probes.  M6.3.2 appends these
# trigger probes, keeping the original numeric values stable for old logs.
TRIGGER_PHASES = {
    9: "MIDI dispatch",
    10: "voice allocation",
    11: "register/pitch",
    12: "modal coefficients",
    13: "coupling",
    14: "exciter setup",
    15: "prepared lookup",
    16: "other trigger work",
}


def profile_class(fields):
    # M6.3.1 profile rows had no class because only sparse steady blocks were
    # sampled.  Treat them as steady so existing captures remain readable.
    return fields.get("class", "steady")


def read(path):
    rows = {}
    callback_rows = {}
    overheads = {}
    overruns = []
    phases = {}
    totals = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        fields = dict(re.findall(r"(\w+)=([^\s\x1b]+)", line))
        if "[CURVE]" in line:
            key = int(fields["fixture"])
            if fields["model"] == "BELL" and key < 8:
                key += 8  # First capture used model-relative IDs.
            rows[key, fields["class"]] = fields
        if "[CALLBACK_CURVE]" in line:
            key = int(fields["fixture"])
            if fields["model"] == "BELL" and key < 8:
                key += 8
            callback_rows[key, fields["class"]] = fields
        if "[OVERHEAD]" in line:
            key = int(fields["fixture"])
            overheads[key] = fields
        if "[OVERRUN]" in line:
            overruns.append(fields)
        if "[PHASE]" in line:
            phases[int(fields["fixture"]), profile_class(fields), int(fields["phase"])] = float(fields["cycles_per_block"])
        if "[PROFILE_TOTAL]" in line:
            totals[int(fields["fixture"]), profile_class(fields)] = float(fields["cycles_per_block"])
    return rows, callback_rows, overheads, overruns, phases, totals


def summary(path):
    rows, callback_rows, overheads, overruns, phases, totals = read(path)
    print(f"\n### {Path(path).name}\n")
    print("| Model/fixture | Class | N | Avg us | P99 us | Max us | Deadline | Bad voices/BLE lost |")
    print("|---|---|---:|---:|---:|---:|---:|---|")
    for (i, kind), r in sorted(rows.items()):
        print(f'| {r["model"]}/{i} ({r["voices"]} voices) | {kind} | {r["n"]} | {r["avg_us"]} | {r["p99_us"]} | {r["max_us"]} | {r["deadline"]} | {r["bad_voices"]}/{r["ble_lost"]} |')

    # M6.3.9.1 Phase I: Full Callback vs Inner Render Comparison Table
    if callback_rows:
        print("\n### M6.3.9.1 Phase I: Inner Render vs Full Callback Comparison\n")
        fixture_labels = {
            5: "PAN cluster8 (fixture 5, 8 voices)",
            13: "BELL cluster8 (fixture 13, 8 voices)",
            14: "BELL chord4 (fixture 14, 4 voices)",
        }
        for fix_id in [5, 13, 14]:
            if not any((fix_id, c) in rows for c in ["event", "attack_tail", "true_steady", "fixture_transition"]):
                continue
            title = fixture_labels.get(fix_id, f"Fixture {fix_id}")
            print(f"**{title}**\n")
            print("| Class | Inner avg/p99/max us | Callback avg/p99/max us | Overhead avg us | Misses (inner/cb) |")
            print("|:---|:---:|:---:|:---:|:---:|")
            for c in ["event", "attack_tail", "true_steady", "fixture_transition"]:
                in_r = rows.get((fix_id, c))
                cb_r = callback_rows.get((fix_id, c))
                if not in_r and not cb_r:
                    continue
                in_str = f"{float(in_r['avg_us']):.1f} / {in_r['p99_us']} / {in_r['max_us']}" if in_r else "-"
                cb_str = f"{float(cb_r['avg_us']):.1f} / {cb_r['p99_us']} / {cb_r['max_us']}" if cb_r else "-"
                oh_str = f"{float(cb_r['overhead_avg_us']):.2f}" if cb_r and 'overhead_avg_us' in cb_r else "-"
                miss_str = f"{in_r['deadline'] if in_r else 0} / {cb_r['deadline'] if cb_r else 0}"
                print(f"| {c} | {in_str} | {cb_str} | {oh_str} | {miss_str} |")
            print()

    # M6.3.9.1 Phase C: Callback Overhead Breakdown
    if overheads:
        print("\n### M6.3.9.1 Phase C: Callback Overhead Breakdown\n")
        print("| Fixture | Blocks | Model/Reset avg (max) | Queue avg (max) | Render avg | Telem avg (max) | Telem Publishes |")
        print("|---|---:|---:|---:|---:|---:|---:|")
        for fix_id, oh in sorted(overheads.items()):
            print(f"| {fix_id} | {oh['blocks']} | {oh['model_reset_avg_us']} ({oh['model_reset_max_us']}) us | {oh['queue_avg_us']} ({oh['queue_max_us']}) us | {oh['render_avg_us']} us | {oh['telem_avg_us']} ({oh['telem_max_us']}) us | {oh['telem_publishes']} |")

    # M6.3.9.1 Phase B: Overruns Logged
    if overruns:
        print(f"\n### M6.3.9.1 Phase B: Captured Overruns (>= 2667 us): {len(overruns)}\n")
        print("| Seq | Callback us | Inner us | Class | Fixture | Model | Voices | Exciter | MIDI depth/consumed | Telem pub | Model/Reset req |")
        print("|---|---:|---:|---|---|---|---|---|---|---|---|")
        for o in overruns:
            print(f"| {o['seq']} | {o['callback_us']} | {o['inner_us']} | {o['class']} | {o['fixture']} | {o['model']} | {o['voices']} | {o['exciter']} | {o['midi_depth']}/{o['midi_consumed']} | {o['telem']} | {o['model_req']}/{o['reset_req']} |")
    else:
        print("\n### M6.3.9.1 Phase B: Captured Overruns: 0 (clean hard real-time execution)\n")

    for offset, model in [(0, "PAN"), (8, "BELL")]:
        selected = [rows.get((offset + i, "true_steady")) or rows.get((offset + i, "steady")) for i in range(6)]
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
    profile_keys = sorted(totals, key=lambda key: (key[0], key[1] not in ("steady", "true_steady"), key[1]))
    for i, profile_kind in profile_keys:
        total = totals[i, profile_kind]
        p = {phase: cycles for (fixture, kind, phase), cycles in phases.items()
             if fixture == i and kind == profile_kind}
        if not total:
            print(f"\nProfile fixture {i} ({profile_kind}): no sampled blocks.")
            continue

        modal = p.get(1, 0)
        voice = p.get(0, 0) - modal
        body = p.get(5, 0)
        output = p.get(7, 0) + p.get(8, 0)

        if profile_kind in ("steady", "true_steady"):
            other = total - p.get(0, 0) - body - output
            print(f"\nProfile fixture {i} ({profile_kind}): total {total/240:.2f} us; modal {modal/total*100:.1f}%, voice/allocator residual {voice/total*100:.1f}%, body {body/total*100:.1f}%, limiter/PCM {output/total*100:.1f}%, other {other/total*100:.1f}%.")
            row = rows.get((i, "true_steady")) or rows.get((i, "steady"))
            if row and (voices := float(row["voices"])):
                modes = 8 if i < 8 or i in (16, 18) else 9 if i == 17 else 10
                print(f"Modal diagnostic cost: {modal/240/voices:.2f} us/voice/block, {modal/240/voices/modes:.2f} us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.")
            continue

        trigger = sum(p.get(phase, 0) for phase in TRIGGER_PHASES)
        render = p.get(0, 0) + body + output
        other = total - trigger - render
        breakdown = ", ".join(
            f"{label} {p.get(phase, 0)/240:.2f} us"
            for phase, label in TRIGGER_PHASES.items()
        )
        print(f"\nProfile fixture {i} ({profile_kind}): total {total/240:.2f} us; trigger probes {trigger/240:.2f} us ({trigger/total*100:.1f}%); render {render/240:.2f} us; unaccounted/mix {other/240:.2f} us.")
        print(f"Trigger breakdown: {breakdown}.")


if __name__ == "__main__":
    for filename in sys.argv[1:]:
        summary(filename)
