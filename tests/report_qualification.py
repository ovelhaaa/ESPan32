"""Verify captured fixture safety and retain timing/provenance as JSON."""
import argparse
import hashlib
import json
import pathlib
import re

p = argparse.ArgumentParser()
p.add_argument("directory", type=pathlib.Path)
p.add_argument("--fixtures", type=int, required=True)
args = p.parse_args()
io, safety, curves, hist = {}, {}, [], {}
evidence = []
for log in sorted(args.directory.glob("batch*_raw.log")):
    raw = log.read_bytes()
    evidence.append({"log": log.name, "sha256": hashlib.sha256(raw).hexdigest()})
    for line in raw.decode(errors="replace").splitlines():
        line = re.sub(r"\x1b\[[0-9;]*m", "", line)
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if "[FIXTURE_IO]" in line:
            io[int(fields["fixture"])] = fields
        if "[SAFETY]" in line:
            safety[int(fields["fixture"])] = fields
        if "[CALLBACK_CURVE]" in line:
            curves.append(fields)
        if "[CALLBACK_HIST]" in line:
            bins = hist.setdefault(int(fields["fixture"]), {})
            lower = int(fields["lower_us"])
            bins[lower] = bins.get(lower, 0) + int(fields["n"])
assert set(io) == set(range(args.fixtures)), "Missing physical fixtures"
assert set(safety) == set(io), "Missing explicit safety evidence"
for fixture, fields in io.items():
    for key in ("deadline", "timeouts", "tx_errors", "short_writes"):
        assert int(fields[key]) == 0, (fixture, key, fields[key])
    for key in ("hard", "sat", "nonfinite", "ble_lost"):
        assert int(safety[fixture][key]) == 0, (fixture, key, safety[fixture][key])
    bins = hist[fixture]
    rank = (sum(bins.values()) * 95 + 99) // 100
    total = 0
    for lower, count in sorted(bins.items()):
        total += count
        if total >= rank:
            fields["p95_us"] = lower + 5
            break
for binary in sorted(args.directory.glob("*_firmware.bin")):
    evidence.append({"firmware": binary.name,
                     "sha256": hashlib.sha256(binary.read_bytes()).hexdigest()})
sources = {}
for source in pathlib.Path("main").rglob("*"):
    if source.is_file():
        sources[source.as_posix()] = hashlib.sha256(source.read_bytes()).hexdigest()
report = {"passed": True, "io": io, "safety": safety, "callback_curves": curves,
          "evidence": evidence, "source_sha256": sources}
(args.directory / "hardware_manifest.json").write_text(json.dumps(report, indent=2) + "\n")
for fixture, row in sorted(io.items()):
    print(fixture, row)
