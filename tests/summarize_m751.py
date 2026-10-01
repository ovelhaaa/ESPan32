"""Validate focused real-board evidence and write the M7.5.1 hardware report."""
import argparse, hashlib, json, re
from collections import defaultdict
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args()
rows=[];io={};hist=defaultdict(lambda:defaultdict(int));logs=[]
for name in ['batch0_fused_raw.log','batch1_raw.log']:
    path=a.directory/name;text=path.read_text(errors='replace')
    assert 'Guru Meditation' not in text and '[OVERRUN]' not in text
    assert 'interval_ms=11.25' in text
    logs.append(dict(file=name,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    for line in text.splitlines():
        f=dict(re.findall(r'(\w+)=([^\s\x1b]+)',line))
        if '[CURVE]' in line or '[CALLBACK_CURVE]' in line:
            f['type']='inner' if '[CURVE]' in line else 'callback'; rows.append(f)
            assert f['deadline']=='0'
            if f['type']=='inner':
                for key in ['hard','sat','bad_voices','ble_lost']: assert f[key]=='0'
        if '[FIXTURE_IO]' in line:
            io[int(f['fixture'])]=f
            for key in ['deadline','timeouts','tx_errors','short_writes']: assert f[key]=='0'
        if '[CALLBACK_HIST]' in line: hist[int(f['fixture'])][int(f['lower_us'])]+=int(f['n'])
assert len(rows)==48 and set(io)=={0,1,2,4,5,6}
names={0:'single motor OFF',1:'chord4 motor ON',2:'cluster8 motor ON',4:'phrase motor ON',5:'model switching',6:'single motor ON'}
def q95(h):
    total=sum(h.values()); cumulative=0
    for lower,n in sorted(h.items()):
        cumulative+=n
        if cumulative*100>=total*95:return lower+5
lines=['# M7.5.1 hardware evidence','', 'ESP32-S3 on COM10, 240 MHz, 48 kHz, 128-frame blocks, candidate 25, Process6 ON, phase profiling OFF. UI/I2S active; BLE connected at 11.25 ms. Each fixture spans 8192 blocks. All six final fixtures have zero deadline misses, I2S timeouts/errors/short writes, PCM clamps, modal saturation, bad voice counts, and BLE event loss.','', '| Fixture | Callback avg us | p95 us | I2S p99 us | Callback max us | CPU % | Misses | I2S faults | Clamps / sat |','|---|---:|---:|---:|---:|---:|---:|---|---|']
for i in [0,6,1,4,2,5]:
    f=io[i];lines.append(f"| {names[i]} | {f['avg_us']} | {q95(hist[i])} | {f['p99_us']} | {f['max_us']} | {f['cpu_pct']} | 0 | 0 / 0 / 0 | 0 / 0 |")
lines+=['','p95 is reconstructed from the combined callback histograms (5 us bin upper edges); p99 is the existing AudioStats value (75 us bins). The final callback is outside the class snapshot, which covers 8191 blocks. Maxima come from full AudioStats.','', '| Fixture | Class | Timing | N | Avg us | p95 us | p99 us | Max us |','|---|---|---|---:|---:|---:|---:|---:|']
for f in rows: lines.append(f"| {names[int(f['fixture'])]} | {f['class']} | {f['type']} | {f['n']} | {f['avg_us']} | {f['p95_us']} | {f['p99_us']} | {f['max_us']} |")
steady={int(f['fixture']):float(f['avg_us']) for f in rows if f['type']=='inner' and f['class']=='true_steady'}
delta=steady[6]-steady[0]
lines+=['', f"Single steady inner render: OFF {steady[0]:.2f} us, ON {steady[6]:.2f} us, added tube/motor path **{delta:.2f} us/block** ({delta/2666.6667*100:.2f}% of a block). These are separate fixtures and include scheduling variation. M7.5 connected old-motor delta was 11.69 us/block; the historical incremental comparison is approximately {delta-11.69:.2f} us/block. This is not an isolated paired measurement against the old firmware.",'', 'Polyphonic cost remains higher: chord4 steady 687.55 us versus historical M7.5 dry 604.55 us (+83.00 us); cluster8 1174.36 versus historical dry 1067.11 us (+107.25 us). The preference for less than 50 us additional cost is not established for polyphony. All final measured fixtures meet the deadline, but cluster8 maximum 2658 us leaves only about 9 us beneath 2666.7 us.','', 'Repeated MARIMBA -> VIBRAPHONE paired switches remain inside the measured event callback; no tables or coefficients are regenerated on selection. Use the event rows above for the exact switch maxima.','', 'The initial sample-call tube renderer in batch0_raw.log produced one cluster event deadline miss (3094 us callback). It was superseded by the fused sustain/attack block renderer. That failed capture is preserved and is excluded from the final qualification totals.']
(a.directory/'hardware_report.md').write_text('\n'.join(lines)+'\n')
(a.directory/'hardware_manifest.json').write_text(json.dumps(dict(logs=logs,io=list(io.values()),timings=rows,tube_single_delta_us=delta,initial_failed_capture='batch0_raw.log'),indent=2)+'\n')
print('\n'.join(lines[:13]));print('Tube path delta',delta)
