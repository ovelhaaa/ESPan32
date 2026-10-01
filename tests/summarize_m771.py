"""Verify focused WAVs, exact legacy reference and unnormalized safety evidence."""
import csv
import hashlib
import json
import math
import struct
import wave
from pathlib import Path

root=Path('docs/listening/m771')
q=Path('docs/qualification/m771')
rows=list(csv.DictReader((root/'host_metrics.csv').open()))
assert len(rows)==61, len(rows)
for row in rows:
    for key in ('hard_clamps','modal_sat','nonfinite','max_gr_db','avg_gr_db'):
        assert float(row[key])==0, row
    assert abs(float(row['dc']))<1e-4, row
singles=rows[:4]
for key in ('opening','hz_multiplier'):
    assert all(float(a[key])<float(b[key]) for a,b in zip(singles,singles[1:]))
assert all(float(a['t60_multiplier'])>float(b['t60_multiplier']) for a,b in zip(singles,singles[1:]))
check=root/'legacy_partial_check.wav'
legacy=Path('docs/listening/m77/udu_B_partial.wav')
assert check.read_bytes()==legacy.read_bytes(), 'M7.7 B/PARTIAL must stay byte-exact'
legacy_hash=hashlib.sha256(check.read_bytes()).hexdigest()
check.unlink()  # temporary regression output, not another listening candidate
groups=[['udu_B_dynamic_v'+str(v)+'.wav' for v in (30,70,110,127)],
        ['udu_B_groove_fixed_partial.wav','udu_B_groove_dynamic_opening.wav'],
        ['udu_B_groove_dynamic_'+c+'.wav' for c in ('linear','smooth','centered')],
        ['udu_B_restrike_R1.wav','udu_B_restrike_R2.wav']]
files={}
for group in groups:
    levels=[]
    for name in group:
        path=root/name
        with wave.open(str(path)) as wav:
            assert (wav.getnchannels(),wav.getsampwidth(),wav.getframerate(),wav.getnframes())==(2,2,48000,288000)
            pcm=struct.unpack('<'+str(576000)+'h',wav.readframes(wav.getnframes()))
        assert pcm[::2]==pcm[1::2]
        samples=pcm[::2]
        rms=math.sqrt(sum(x*x for x in samples)/len(samples))/32768
        levels.append(rms)
        files[name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),rms=rms,
                        peak=max(abs(x) for x in samples)/32768)
    assert max(levels)-min(levels)<2e-6,(group,levels)
assert len(list(root.glob('*.wav')))==11
(root/'manifest.json').write_text(json.dumps(files,indent=2)+'\n')
summary=dict(host_fixtures=len(rows),focused_wavs=9,restrike_probes=2,
             all_required_faults_zero=True,limiter_gr_zero=True,
             legacy_partial_sha256=legacy_hash,fixed_partial_raw_fnv64=rows[4]['pcm_fnv64'],
             peak_max=max(float(r['peak']) for r in rows),dc_abs_max=max(abs(float(r['dc'])) for r in rows),
             single_strikes=singles,udu_cache_bytes=5928,udu_voice_host_bytes=136,
             synth_engine_host_bytes=108232,host_state_increase_bytes=2608,
             listening_selection='pending; centered + R1 is the conservative provisional candidate')
(q/'host_summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
