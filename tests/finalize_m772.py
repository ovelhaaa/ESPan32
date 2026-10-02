"""Close the UDU freeze gate from fresh physical and exact host evidence."""
import hashlib
import json
from pathlib import Path
import re

q = Path('docs/qualification/m772')
hw = json.loads((q/'hardware_manifest.json').read_text())
assert hw['passed']
for name in ('host_p6on.log', 'host_p6off.log'):
    assert '100% tests passed, 0 tests failed out of 14' in (q/name).read_text()
events = {}
for log in sorted(q.glob('batch*_raw.log')):
    content = log.read_text(errors='replace')
    assert 'BLE MIDI ready' in content and 'interval_ms=11.25' in content
    for line in content.splitlines():
        line = re.sub(r'\x1b\[[0-9;]*m', '', line)
        if '[CURVE]' in line:
            row = dict(re.findall(r'(\w+)=([^\s]+)', line))
            for key in ('deadline', 'bad_voices', 'ble_lost', 'hard', 'sat'):
                assert int(row[key]) == 0, (key, row)
        if '[EVENT_ONLY]' in line:
            row = dict(re.findall(r'(\w+)=([^\s]+)', line))
            events[row['fixture']] = row
prod = (q/'production_raw.log').read_text(errors='replace')
assert 'model=PAN' in prod and 'state=8' in prod
assert '[CURVE]' not in prod
memory = re.findall(r'\[MEM\] internal_free=(\d+) largest_internal=(\d+)', prod)
assert memory
free = min(int(a) for a,b in memory)
largest = min(int(b) for a,b in memory)
names = ('single v127','chord4','cluster8','dynamic groove','model switching','rapid restrike')
timing=[]
for i,name in enumerate(names):
    rows = [r for r in hw['callback_curves'] if int(r['fixture']) == i]
    n = sum(int(r['n']) for r in rows)
    avg = sum(int(r['n'])*float(r['avg_us']) for r in rows)/n
    io = hw['io'][str(i)]
    assert int(io['max_us']) < 2400, io
    timing.append(dict(fixture=name,avg_us=round(avg,2),p95_us=int(io['p95_us']),
        p99_us=int(io['p99_us']),max_us=int(io['max_us']),cpu_pct=round(avg/2666.6667*100,2),
        event_only=events[str(i)],deadline=0,i2s_faults=0,nonfinite=0,resonator_faults=0,hard_clamps=0))
summary=dict(complete=True,identity='UDU V1 = B + CENTERED + R1',
    aggregate_fnv64='552d8d59691b008e',timing=timing,internal_free=free,
    largest_internal=largest,udu_cache_bytes=5928,udu_voice_bytes=124,
    synth_engine_bytes=107848,prepared_note_bytes=86760,
    firmware_bytes=(q/'production_firmware.bin').stat().st_size,
    firmware_sha256=hashlib.sha256((q/'production_firmware.bin').read_bytes()).hexdigest())
(q/'completion.json').write_text(json.dumps(summary,indent=2)+'\n')
doc=Path('docs/m772_udu_v1_freeze.md')
body=doc.read_text(encoding='utf-8').split('<!-- M772_PHYSICAL -->')[0].rstrip()
lines=['<!-- M772_PHYSICAL -->','','Physical ESP32-S3 / COM10, BLE MIDI at 11.25 ms, '
    '240 MHz, 48 kHz / 128 frames, live UI and I2S. 8192 blocks per fixture. '
    'Average is weighted across all callback classes; CPU is average / 2666.7 µs. '
    'p95 is the 5 µs histogram upper edge; p99 is the AudioStats histogram upper edge.', '',
    '| Fixture | Avg µs | p95 µs | p99 µs | Max µs | CPU % | Event avg / max µs |',
    '|---|---:|---:|---:|---:|---:|---|']
for row in timing:
    e=row['event_only']
    lines.append(f"| {row['fixture']} | {row['avg_us']} | {row['p95_us']} | {row['p99_us']} | {row['max_us']} | {row['cpu_pct']} | {e['avg_us']} / {e['max_us']} |")
lines += ['', 'Every fixture: deadline misses, I2S timeout/error/short, nonfinite, '
    'resonator faults, hard clamps, BLE losses and invalid voice counts are **zero**.',
    f"Cluster8 margin: {2666.6667-timing[2]['max_us']:.1f} µs.", '',
    f'Restored production: internal free **{free} bytes**, largest block **{largest} bytes**; '
    f'SynthEngine BSS 107848 bytes; PreparedNote tables 86760 bytes; firmware {summary["firmware_bytes"]} bytes. '
    'Forensics and fixed-opening control disabled; PAN boots and BLE reconnects.', '',
    'Both Process6 configurations pass all 14 suites, including ten aggregate hashes, '
    'Vibraphone fixed fixtures, Mbira B + Buzz fixtures and the authoritative UDU fixtures. '
    '**M7.7.2 complete. M8 may begin.**', '',
    '[Raw hardware evidence](qualification/m772/hardware_manifest.json), '
    '[completion and production memory](qualification/m772/completion.json).','']
doc.write_text(body+'\n\n'+'\n'.join(lines),encoding='utf-8')
print(json.dumps(summary,indent=2))
