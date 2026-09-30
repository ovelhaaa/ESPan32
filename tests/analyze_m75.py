"""Analyze generated M7.5 PCM fixtures (requires NumPy); no candidate scoring."""
import argparse, wave, json
from pathlib import Path
import numpy as np
p = argparse.ArgumentParser()
p.add_argument("directory", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
ratios = np.array([1, 4, 10, 16, 22.4, 29])
def load(path):
    with wave.open(str(path)) as f:
        assert f.getframerate() == 48000 and f.getsampwidth() == 2
        return np.frombuffer(f.readframes(f.getnframes()), dtype="<i2").reshape(-1,2)[:,0].astype(float)/32768
rows=[]
for name,note in [("D3",50),("A3",57),("D4",62),("A4",69),("D5",74)]:
    hz=440*2**((note-69)/12)
    for candidate in "ABC":
        previous=0
        previous_upper=0
        for v in [30,70,110,127]:
            x=load(a.directory/f"vibraphone_{name}_v{v}_{candidate}.wav")
            rms=float(np.sqrt(np.mean(x*x)))
            assert rms>previous, (name,candidate,v)
            previous=rms
            early=x[:4800]; spec=np.abs(np.fft.rfft(early*np.hanning(len(early))))**2
            freqs=np.fft.rfftfreq(len(early),1/48000)
            energies=[float(np.sum(spec[np.abs(freqs-hz*r)<25])) for r in ratios]
            upper=sum(energies[1:])/max(energies[0],1e-30)
            assert upper>previous_upper, (name,candidate,v,"brightness")
            assert max(energies[1:])<energies[0], (name,candidate,v,"fundamental dominance")
            previous_upper=upper
            pitch_freqs=np.fft.rfftfreq(48000,1/48000)
            pitch_spec=np.abs(np.fft.rfft(x[:48000]*np.hanning(48000)))
            band=np.flatnonzero((pitch_freqs>hz*.94)&(pitch_freqs<hz*1.06))
            pitch=float(pitch_freqs[band[np.argmax(pitch_spec[band])]])
            assert abs(1200*np.log2(pitch/hz))<5
            # Fraction of attack spectral power above 2 kHz; no perceptual ranking.
            brightness=float(spec[freqs>2000].sum()/max(spec.sum(),1e-30))
            tail=float(np.sqrt(np.mean(x[24000:72000]**2)))
            rows.append(dict(note=name,velocity=v,candidate=candidate,rms=rms,pitch_hz=pitch,
                cents=float(1200*np.log2(pitch/hz)),attack_above_2k_fraction=brightness,
                mode_energy_relative_to_fundamental=[e/max(energies[0],1e-30) for e in energies],
                tail_0p5_1p5_rms=tail,attack_rms=float(np.sqrt(np.mean(early**2)))))
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(rows,indent=2)+"\n")
print(f"{len(rows)} register/velocity spectra; RMS and upper/fundamental energy strictly increasing; dominant fundamental in every case.")
