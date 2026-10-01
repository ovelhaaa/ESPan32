"""Summarize full callback evidence, including failed development runs."""
import argparse
import hashlib
import json
import re
from collections import defaultdict
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
p.add_argument('--batch0', default='batch0_six_raw.log')
p.add_argument('--batch1', default='batch1_raw.log')
a = p.parse_args()
names = {0:'single OFF', 1:'chord4 ON', 2:'cluster8 ON', 4:'phrase ON', 5:'model switching', 6:'single ON'}
io, rows, hist = {}, [], defaultdict(lambda: defaultdict(int))
logs = []
for name in [a.batch0, a.batch1]:
    path = a.directory / name
    text = path.read_text(errors='replace')
    assert 'Guru Meditation' not in text
    assert 'state=8 interval_ms=11.25' in text, 'connected BLE evidence required'
    logs.append(dict(file=name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    for line in text.splitlines():
        f = dict(re.findall(r'(\w+)=([^\s\x1b]+)', line))
        if '[FIXTURE_IO]' in line:
            io[int(f['fixture'])] = f
        if '[CALLBACK_HIST]' in line:
            hist[int(f['fixture'])][int(f['lower_us'])] += int(f['n'])
        if '[CURVE]' in line or '[CALLBACK_CURVE]' in line:
            f['type'] = 'inner' if '[CURVE]' in line else 'callback'
            rows.append(f)
assert set(io) == set(names) and len(rows) == 48

def percentile(h, percent):
    total, count = sum(h.values()), 0
    for lower, n in sorted(h.items()):
        count += n
        if count*100 >= total*percent:
            return lower+5

faults = any(int(f[k]) for f in io.values() for k in ['deadline','timeouts','tx_errors','short_writes'])
faults |= any(int(f[k]) for f in rows if f['type']=='inner' for k in ['hard','sat','bad_voices','ble_lost'])
margin = 128/48000*1e6-int(io[2]['max_us'])
lines = ['# M7.5.2 hardware evidence', '',
         'ESP32-S3 on COM10, 240 MHz, 48 kHz/128 frames, candidate 25, Process6 ON, profiling OFF, UI/I2S active, BLE MIDI connected at 11.25 ms. Six fixtures of 8192 blocks each. Full callback includes event and transition blocks.', '',
         '| Fixture | Avg us | p95 us | p99 us | Max us | CPU % | Misses | I2S timeouts/errors/short writes |',
         '|---|---:|---:|---:|---:|---:|---:|---|']
for i in [0,6,1,2,4,5]:
    f=io[i]
    lines.append(f"| {names[i]} | {f['avg_us']} | {percentile(hist[i],95)} | {f['p99_us']} | {f['max_us']} | {f['cpu_pct']} | {f['deadline']} | {f['timeouts']}/{f['tx_errors']}/{f['short_writes']} |")
lines += ['', f"Fault-free qualification: **{not faults}**. Cluster8 worst-case margin: **{margin:.1f} us**. Preferred >250 us margin achieved: **{margin>250}**.",
          f"Historical M7.5.1 cluster8: avg 1210 us, p95 1250 us, p99 1875 us, max 2658 us, CPU 45.37%, no misses, margin 8.7 us. New worst-case margin change: **{2658-int(io[2]['max_us'])} us**. These are separate captures, not a paired isolated benchmark.", '',
          'p95 uses combined callback histograms with 5 us upper edges; p99 uses AudioStats 75 us bins. Histograms contain 8191 callbacks; full averages/maxima/misses cover 8192, including the final callback.', '',
          '| Fixture | Class | Timing | N | Avg us | p95 us | p99 us | Max us | Misses |',
          '|---|---|---|---:|---:|---:|---:|---:|---:|']
for f in rows:
    lines.append(f"| {names[int(f['fixture'])]} | {f['class']} | {f['type']} | {f['n']} | {f['avg_us']} | {f['p95_us']} | {f['p99_us']} | {f['max_us']} | {f['deadline']} |")
lines += ['', 'All development captures are retained below; unsuccessful versions are not qualification evidence for the final source.', '', '| Capture | Cluster avg us | Max us | Misses |', '|---|---:|---:|---:|']
development=[]
for path in sorted(a.directory.glob('*raw.log')):
    if path.name in [a.batch0,a.batch1]: continue
    text=path.read_text(errors='replace')
    for line in text.splitlines():
        if '[FIXTURE_IO]' in line and 'fixture=2 ' in line:
            f=dict(re.findall(r'(\w+)=([^\s\x1b]+)',line))
            lines.append(f"| {path.name} | {f['avg_us']} | {f['max_us']} | {f['deadline']} |")
            development.append(dict(file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(), cluster=f))
(a.directory/'hardware_report.md').write_text('\n'.join(lines)+'\n')
(a.directory/'hardware_manifest.json').write_text(json.dumps(dict(logs=logs, io=list(io.values()), timings=rows,
    development=development, fault_free=not faults, cluster_margin_us=margin),indent=2)+'\n')
print('\n'.join(lines[:17]))
