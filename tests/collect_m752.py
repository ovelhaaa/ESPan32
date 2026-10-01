"""Collect exactly 16 C modulation comparisons; no subjective ranking."""
import argparse
import hashlib
import json
import shutil
import wave
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('source', type=Path)
p.add_argument('destination', type=Path)
p.add_argument('manifest', type=Path)
a = p.parse_args()
a.destination.mkdir(parents=True, exist_ok=True)
rows = []
for fixture in ['D4_v70', 'D4_v110', 'chord', 'phrase']:
    levels = []
    for state in range(4):
        name = f'vibra_C_{fixture}_M{state}_matched.wav'
        target = a.destination / name
        shutil.copy2(a.source / name, target)
        with wave.open(str(target)) as w:
            assert (w.getframerate(), w.getsampwidth(), w.getnchannels(), w.getnframes()) == (48000, 2, 2, 384000)
            pcm = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, 2)
        assert np.array_equal(pcm[:, 0], pcm[:, 1])
        x = pcm[:, 0].astype(float) / 32768
        rms = float(np.sqrt(np.mean(x*x)))
        levels.append(rms)
        rows.append(dict(file=name, sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                         rms=rms, peak=float(np.max(np.abs(x)))))
    assert max(levels)-min(levels) < 2e-6, levels
a.manifest.parent.mkdir(parents=True, exist_ok=True)
a.manifest.write_text(json.dumps(rows, indent=2)+'\n')
print('16 WAVs; four RMS-matched groups; no automatic winner.')
