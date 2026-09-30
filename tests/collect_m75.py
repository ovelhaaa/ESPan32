"""Retain and verify the deterministic M7.5 listening pack; no candidate ranking."""
import argparse
import hashlib
import json
import shutil
import wave
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("destination", type=Path)
p.add_argument("manifest", type=Path)
a = p.parse_args()
a.destination.mkdir(parents=True, exist_ok=True)
rows, groups = [], {}
for source in sorted(a.source.glob("vibraphone_*.wav")):
    target = a.destination / source.name
    shutil.copy2(source, target)
    with wave.open(str(target)) as f:
        assert (f.getframerate(), f.getsampwidth(), f.getnchannels(), f.getnframes()) == (48000, 2, 2, 384000)
        x = np.frombuffer(f.readframes(f.getnframes()), dtype="<i2").reshape(-1, 2).astype(float) / 32768
    assert np.array_equal(x[:, 0], x[:, 1])
    rms = float(np.sqrt(np.mean(x*x)))
    rows.append(dict(file=source.name, bytes=target.stat().st_size,
                     sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                     rms=rms, peak=float(np.max(np.abs(x)))))
    if source.name.endswith("_matched.wav"):
        key = (source.name.split("_motor_")[0]+"_motor") if "_motor_" in source.name else source.name.rsplit("_", 2)[0]+"_abc"
        groups.setdefault(key, []).append(rms)
assert len(rows) == 216, len(rows)
assert len(groups) == 34, len(groups)
error = max(max(v)-min(v) for v in groups.values())
assert error < 2e-6, error
a.manifest.parent.mkdir(parents=True, exist_ok=True)
a.manifest.write_text(json.dumps(rows, indent=2)+"\n")
print(f"{len(rows)} WAVs; {len(groups)} matched groups; max PCM RMS spread {error:.9g}")
