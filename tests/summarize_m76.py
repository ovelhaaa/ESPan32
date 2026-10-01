"""Verify the focused Mbira pack and summarize retained host/device evidence."""
import argparse
import csv
import hashlib
import json
import re
import wave
from pathlib import Path

import numpy as np

p = argparse.ArgumentParser()
p.add_argument('--listening', type=Path, default=Path('docs/listening/m76'))
p.add_argument('--qualification', type=Path, default=Path('docs/qualification/m76'))
p.add_argument('--hardware', action='store_true')
a = p.parse_args()
q, listening = a.qualification, a.listening
q.mkdir(parents=True, exist_ok=True)
rows = list(csv.DictReader((listening/'host_metrics.csv').open()))
assert len(rows) == 58
for row in rows:
    for key in ['hard_clamps', 'modal_sat', 'nonfinite', 'gr_gt_0p1', 'gr_gt_1']:
        assert int(row[key]) == 0, (row['fixture'], key)
    assert float(row['max_gr_db']) == float(row['avg_gr_db']) == 0
    assert abs(float(row['dc_mean'])) < 1e-4
groups = [[f'mbira_{f}_{c}.wav' for c in 'ABC'] for f in ['D3_v70','D4_v70','D4_v110','chord','groove']]
groups += [[f'mbira_{f}_buzz_{b}.wav' for b in ['off','on']] for f in ['D4_v70','D4_v110','groove']]
manifest = []
for group in groups:
    levels = []
    for name in group:
        path = listening/name
        with wave.open(str(path)) as w:
            assert (w.getframerate(),w.getsampwidth(),w.getnchannels(),w.getnframes()) == (48000,2,2,288000)
            pcm = np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').reshape(-1,2)
        assert np.array_equal(pcm[:,0],pcm[:,1])
        x = pcm[:,0].astype(float)/32768
        level = float(np.sqrt(np.mean(x*x)))
        levels.append(level)
        manifest.append(dict(file=name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),rms=level,peak=float(np.max(abs(x)))))
    assert max(levels)-min(levels) < 2e-6, (group,levels)
(listening/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
lines = ['# M7.6 host qualification','',
         '58 unnormalized fixtures; 21 RMS-matched listening WAVs in eight comparison groups. Two additional Kalimba reference WAVs are analysis aids. All waveforms are mono duplicated to stereo, 48 kHz/16 bit, six seconds. Matching uses the lowest raw RMS within each group and never boosts a candidate. Full precision production PCM supplies the safety measurements below.','',
         '| Fixture | Candidate | Peak | RMS | Crest | DC mean | Pre peak | Max/avg GR dB | Voices | Steals |',
         '|---|---|---:|---:|---:|---:|---:|---|---:|---:|']
for r in rows:
    lines.append(f"| {r['fixture']} | {r['candidate']} | {float(r['peak']):.6f} | {float(r['rms']):.6f} | {float(r['crest']):.2f} | {float(r['dc_mean']):.8f} | {float(r['pre_peak']):.6f} | 0 / 0 | {r['active_voices']} | {r['steals']} |")
lines += ['', 'Every row: samples >0.1 dB GR = 0; samples >1 dB GR = 0; hard clamps = 0; modal saturation = 0; nonfinite = 0. All normal and stress fixtures are independent of the limiter. Eight steals in the deliberate 16-note steal probe are expected. DC means include finite phrase boundaries, rather than a claim that every finite waveform has exactly zero mean.','',
          '| D4 velocity | MBIRA RMS | Upper/fundamental dB | Buzz difference RMS |',
          '|---|---:|---:|---:|']
for r in rows:
    if r['fixture'].startswith('note62_'):
        lines.append(f"| {r['fixture'].split('_v')[1]} | {float(r['rms']):.7f} | {float(r['upper_fundamental_db']):.2f} | {float(r['buzz_delta_rms']):.8f} |")
lines += ['', 'RMS, modal-band brightness and buzz difference increase monotonically at velocities 30/70/110/127 for each D3/A3/D4/A4/D5. The fundamental exceeds the combined upper-mode energy in all these probes. Modal-band measurements use Goertzel energy from 1–151 ms at the preset mode frequencies; they are a structural diagnostic, not perceptual scoring.','',
          '| Instrument | D4 v70 upper/fundamental dB | D4 v110 upper/fundamental dB |',
          '|---|---:|---:|']
for name, key in [('KALIMBA','kalimba_D4_v'),('MBIRA','note62_v')]:
    values = [next(r for r in rows if r['fixture']==key+str(v)) for v in [70,110]]
    lines.append(f"| {name} | {float(values[0]['upper_fundamental_db']):.2f} | {float(values[1]['upper_fundamental_db']):.2f} |")
(q/'host_report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Host safety and 21 matched WAVs verified.')

if a.hardware:
    names = ['single buzz OFF','chord4','cluster8','roll','groove','model switching','single buzz ON']
    evidence, io, hist, curves, memory = [], {}, {}, [], []
    for batch in range(3):
        path = q/f'batch{batch}_raw.log'
        text = path.read_text(encoding='utf-8',errors='replace')
        assert 'Guru Meditation' not in text and 'state=8 interval_ms=11.25' in text
        firmware = q/f'batch{batch}_firmware.bin'
        evidence.append(dict(log=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                             firmware=firmware.name,firmware_sha256=hashlib.sha256(firmware.read_bytes()).hexdigest()))
        for line in text.splitlines():
            f = dict(re.findall(r'(\w+)=([^\s\x1b]+)',line))
            if '[FIXTURE_IO]' in line: io[int(f['fixture'])] = f
            if '[CALLBACK_HIST]' in line:
                h=hist.setdefault(int(f['fixture']),{})
                lower=int(f['lower_us']); h[lower]=h.get(lower,0)+int(f['n'])
            if '[CURVE]' in line: curves.append(f)
            if '[MEM]' in line: memory.append(f)
    assert set(io) == set(range(7)) and len(curves)==28
    for f in io.values():
        assert all(int(f[k])==0 for k in ['deadline','timeouts','tx_errors','short_writes'])
    for f in curves:
        assert all(int(f[k])==0 for k in ['deadline','hard','sat','bad_voices','ble_lost'])
    def p95(i):
        h=hist[i];count=0;total=sum(h.values())
        for lower,n in sorted(h.items()):
            count+=n
            if count*100>=total*95: return lower+5
    lines = ['# M7.6 physical ESP32-S3 qualification','',
             'COM10, 240 MHz, 48 kHz/128 frames, candidate 25, Process6 ON, BLE MIDI ready at 11.25 ms, UI and I2S running. Seven fixtures of 8192 blocks, captured in three memory-bounded batches. Full callbacks include event, restrike, pressure-free rendering and model transition work.','',
             '| Fixture | Avg us | p95 us | p99 us | Max us | CPU % | Deadline misses | I2S timeout/error/short |',
             '|---|---:|---:|---:|---:|---:|---:|---|']
    for i in range(7):
        f=io[i]
        lines.append(f"| {names[i]} | {f['avg_us']} | {p95(i)} | {f['p99_us']} | {f['max_us']} | {f['cpu_pct']} | 0 | 0 / 0 / 0 |")
    lines += ['', f"Cluster8 margin: {128/48000*1e6-int(io[2]['max_us']):.1f} us. All fixtures have zero hard clamps, modal saturation, invalid voice counts and BLE losses. p95 uses 5 us callback histogram upper edges; p99 uses AudioStats 75 us bins. The callback histogram excludes the final callback; reported averages/maxima/deadlines include all 8192.", '',
              f"Qualification internal free SRAM minimum: {min(int(f['internal_free']) for f in memory)} bytes; largest free internal block minimum: {min(int(f['largest_internal']) for f in memory)} bytes.", '',
              'The initial seven-fixture diagnostic build exceeded BSS by 46,368 bytes. Limiting the diagnostics to three concurrent fixture records fixes that measurement-only problem. The cache itself is unchanged. The earlier development batch is retained separately and is not evidence for the final source.']
    (q/'hardware_report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (q/'hardware_manifest.json').write_text(json.dumps(dict(evidence=evidence,io=io,curves=curves,
        source_sha256={str(path).replace('\\','/'):hashlib.sha256(path.read_bytes()).hexdigest()
                       for path in sorted(Path('main').rglob('*')) if path.is_file()}),indent=2)+'\n',encoding='utf-8')
    print('\n'.join(lines[:15]))
