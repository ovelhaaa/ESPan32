"""Verify the focused UDU pack and its unnormalized host safety measurements."""
import csv
import hashlib
import json
import pathlib
import wave
import numpy as np

root = pathlib.Path("docs/listening/m77")
rows = list(csv.DictReader((root / "host_metrics.csv").open()))
assert len(rows) == 120
for row in rows:
    for key in ("hard_clamps", "modal_sat", "nonfinite", "gr_gt_0p1", "gr_gt_1"):
        assert int(row[key]) == 0, row
    assert float(row["max_gr_db"]) == float(row["avg_gr_db"]) == 0
    assert abs(float(row["dc"])) < 1e-4
files = {}
for path in sorted(root.glob("*.wav")):
    with wave.open(str(path)) as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) == (2, 2, 48000, 288000)
        pcm = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").reshape(-1, 2)
    assert np.array_equal(pcm[:, 0], pcm[:, 1])
    audio = pcm[:, 0].astype(float) / 32768
    files[path.name] = dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                           rms=float(np.sqrt(np.mean(audio**2))), peak=float(np.max(np.abs(audio))))
for name in ("low_v70", "mid_v70", "mid_v110", "high_v70", "groove"):
    rms = [files[f"udu_{name}_{c}.wav"]["rms"] for c in "ABC"]
    assert max(rms) / min(rms) < 1.005, (name, rms)
rms = [files[f"udu_B_{hole}.wav"]["rms"] for hole in ("open", "partial", "closed")]
assert max(rms) / min(rms) < 1.005
assert len(files) == 21
spectral = {}
for candidate in "ABC":
    path = root / f"udu_mid_v110_{candidate}.wav"
    with wave.open(str(path)) as wav:
        x = np.frombuffer(wav.readframes(2400), dtype="<i2")[::2].astype(float)
    power = abs(np.fft.rfft(x * np.hanning(len(x))))**2
    frequencies = np.fft.rfftfreq(len(x), 1 / 48000)
    db = float(10*np.log10(power[frequencies > 400].sum() / power[frequencies < 350].sum()))
    spectral[candidate] = db
    assert db < 0, (candidate, "air band should dominate this mid-register attack", db)
(root / "manifest.json").write_text(json.dumps(files, indent=2) + "\n")
summary = {"host_fixtures": len(rows), "wav_count": len(files),
           "peak_max": max(float(r["peak"]) for r in rows),
           "pre_limiter_max": max(float(r["pre_peak"]) for r in rows),
           "dc_abs_max": max(abs(float(r["dc"])) for r in rows),
           "max_voices": max(int(r["active_voices"]) for r in rows),
           "max_steals": max(int(r["steals"]) for r in rows),
           "all_required_faults_zero": True, "limiter_gr_zero": True}
summary["mid_v110_first_50ms_high_low_band_db"] = spectral
q = pathlib.Path("docs/qualification/m77")
q.mkdir(parents=True, exist_ok=True)
(q / "host_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))
