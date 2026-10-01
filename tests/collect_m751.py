"""Collect the focused M7.5.1 pack and verify PCM RMS matching and voicing spectra."""
import argparse, hashlib, json, shutil, wave
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser()
p.add_argument('source',type=Path)
p.add_argument('destination',type=Path)
p.add_argument('manifest',type=Path)
a=p.parse_args()
a.destination.mkdir(parents=True,exist_ok=True)
fixtures=['D3_v70','D4_v70','D4_v110','chord','phrase']
groups=[[f'vibra_{f}_{c}_matched.wav' for c in 'ABC'] for f in fixtures]
groups += [[f'vibra_{f}_motor_{m}.wav' for m in ['off','old_global_tremolo','new_tube_coupling']] for f in ['D4','phrase']]
rows=[]
for group in groups:
    rms=[]
    for name in group:
        source=a.source/name; target=a.destination/name
        shutil.copy2(source,target)
        with wave.open(str(target)) as w:
            assert (w.getframerate(),w.getsampwidth(),w.getnchannels(),w.getnframes())==(48000,2,2,384000)
            pcm=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').reshape(-1,2)
        assert np.array_equal(pcm[:,0],pcm[:,1])
        x=pcm[:,0].astype(float)/32768
        level=float(np.sqrt(np.mean(x*x)));rms.append(level)
        row=dict(file=name,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),rms=level,peak=float(np.max(np.abs(x))))
        if 'D4_v70_' in name:
            # Broad modal-band energy during attack, normalized to fundamental.
            y=x[240:24240]; spectrum=abs(np.fft.rfft(y*np.hanning(len(y))))**2
            hz=np.fft.rfftfreq(len(y),1/48000)
            energies=[float(spectrum[(hz>293.6648*r-20)&(hz<293.6648*r+20)].sum()) for r in [1,4,10]]
            row['mode_energy_db_relative_fundamental']=[float(10*np.log10(e/energies[0])) for e in energies]
        rows.append(row)
    assert max(rms)-min(rms)<2e-6,(group,rms)
a.manifest.parent.mkdir(parents=True,exist_ok=True)
a.manifest.write_text(json.dumps(rows,indent=2)+'\n')
print('21 WAVs; 7 RMS-matched groups verified.')
for row in rows:
    if 'mode_energy_db_relative_fundamental' in row: print(row['file'],row['mode_energy_db_relative_fundamental'])
