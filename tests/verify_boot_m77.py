"""Verify two captured physical ten-model BOOT cycles and unchanged screens."""
import hashlib
import json
import pathlib
import re

root = pathlib.Path("docs/qualification/m77")
raw = (root / "boot_raw.log").read_bytes()
text = re.sub(r"\x1b\[[0-9;]*m", "", raw.decode(errors="replace"))
transitions = re.findall(r"TRANSITION: (\w+) -> (\w+) \(label='([^']+)' mode=(\d+)", text)
cycle = ["PAN", "BELL", "TONGUE", "BOWL", "KALIMBA", "GLASS", "MARIMBA", "VIBRAPHONE", "MBIRA", "UDU"]
expected = [(name, cycle[(i+1)%10]) for i, name in enumerate(cycle)] * 2 + [("PAN", "BELL")]
assert [(a, b) for a, b, _, _ in transitions] == expected
assert all(b == label and mode == "0" for _, b, label, mode in transitions)
screens = re.findall(r"SCREEN_MODE: (\d+) -> (\d+) .*rel_tick=(\d+)", text)
assert screens[:2] == [("0", "1", "6"), ("1", "2", "20")]
assert "COMPLETE: schedule finished (3 short, 20 long, 1 extended hold)" in text
audio = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in text.splitlines() if "[AUDIO]" in line]
assert len(audio) >= 8
for row in audio:
    assert all(int(row[k]) == 0 for k in ("deadline", "timeout", "tx_error", "short"))
assert "[BLE] state=8 interval_ms=11.25" in text
report = {"passed": True, "cycles": 2, "long_gestures": 20,
          "extended_hold_transitions": 1, "short_gestures": 3,
          "short_screen_transitions": screens[:2], "label_checks": len(transitions),
          "required_boundary_observations": 2, "io_faults": 0,
          "method": "GPIO input sample injection on physical ESP32-S3",
          "third_short": "Unchanged AudioDiagnostic handler advances PAN to SILENCE; no screen transition expected.",
          "log_sha256": hashlib.sha256(raw).hexdigest(),
          "firmware_sha256": hashlib.sha256((root/"boot_firmware.bin").read_bytes()).hexdigest()}
(root / "boot_manifest.json").write_text(json.dumps(report, indent=2)+"\n")
print(json.dumps(report, indent=2))
