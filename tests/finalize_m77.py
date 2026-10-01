"""Verify final physical evidence, production restore and source provenance."""
import hashlib
import json
import pathlib
import re

root = pathlib.Path("docs/qualification/m77")
hardware = json.loads((root / "hardware_manifest.json").read_text())
mbira = json.loads((root / "mbira_recheck/hardware_manifest.json").read_text())
boot = json.loads((root / "boot_manifest.json").read_text())
assert hardware["passed"] and mbira["passed"] and boot["passed"]
assert all(row["model"] == "UDU" for row in hardware["io"].values())
assert all(row["model"] == "MBIRA" for row in mbira["io"].values())
for manifest in (hardware, mbira):
    for source, digest in manifest["source_sha256"].items():
        assert hashlib.sha256(pathlib.Path(source).read_bytes()).hexdigest() == digest, source
raw = (root / "production_raw.log").read_bytes()
text = re.sub(r"\x1b\[[0-9;]*m", "", raw.decode(errors="replace"))
assert "[BOOT_QUAL]" not in text and "[FIXTURE_IO]" not in text
assert "[BLE] state=8 interval_ms=11.25" in text
audio = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in text.splitlines() if "[AUDIO]" in line]
assert len(audio) >= 3 and all(row["model"] == "PAN" for row in audio)
for row in audio:
    assert all(int(row[k]) == 0 for k in ("deadline", "timeout", "tx_error", "short"))
memory = [dict(re.findall(r"(\w+)=(\d+)", line)) for line in text.splitlines() if "[MEM]" in line]
assert len(memory) >= 3
free = min(int(row["internal_free"]) for row in memory)
largest = min(int(row["largest_internal"]) for row in memory)
assert free >= 32768 and largest >= 16384
cache = pathlib.Path("build-ci-prod/CMakeCache.txt").read_text()
for key in ("POCKETPAN_BUTTON_QUAL", "POCKETPAN_FORENSICS_M76", "POCKETPAN_FORENSICS_M77"):
    assert re.search(rf"^{key}:STRING=0$", cache, re.MULTILINE), key
sdk = pathlib.Path("build-ci-prod/config/sdkconfig.h").read_text()
assert "#define CONFIG_POCKETPAN_POLYPHONY_FORENSICS 1" not in sdk
firmware = (root / "production_firmware.bin").read_bytes()
assert firmware == pathlib.Path("build-ci-prod/pocket_pan.bin").read_bytes()
summary = {"passed": True, "mbira_frozen": "B + Buzz ON",
           "udu_status": "technically qualified listening candidate; not frozen",
           "production_restored": True, "production_boot_model": "PAN",
           "production_free_internal_min": free, "production_largest_internal_min": largest,
           "synth_engine_device_bytes": 105272, "prepared_note_total_bytes": 86760,
           "udu_cache_bytes": 3576, "production_io_faults": 0,
           "udu_cluster_margin_us": 2666.666667-int(hardware["io"]["2"]["max_us"]),
           "mbira_cluster_margin_us": 2666.666667-int(mbira["io"]["2"]["max_us"]),
           "production_firmware_sha256": hashlib.sha256(firmware).hexdigest(),
           "production_log_sha256": hashlib.sha256(raw).hexdigest()}
(root / "completion.json").write_text(json.dumps(summary, indent=2)+"\n")
print(json.dumps(summary, indent=2))
