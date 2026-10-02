"""Verify paired physical cache qualification, exact host suites and target ownership."""
import hashlib
import json
from pathlib import Path
import re

q=Path('docs/qualification/m8')
assert json.loads((q.parent/'m772/completion.json').read_text())['complete']
def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)',re.sub(r'\x1b\[[0-9;]*m','',line)))
def read_capture(directory,qualified=True):
    hw=json.loads((directory/'hardware_manifest.json').read_text())
    if qualified: assert hw['passed']
    if qualified:
        assert all(int(c['deadline'])==0 for c in hw['callback_curves'])
    event={}
    for log in sorted(directory.glob('batch*_raw.log')):
        data=log.read_text(errors='replace')
        assert 'BLE MIDI ready' in data and 'interval_ms=11.25' in data,log
        for line in data.splitlines():
            row=fields(line)
            if '[CURVE]' in line:
                for key in ('deadline','bad_voices','ble_lost','hard','sat'):
                    if key=='deadline' and not qualified: continue
                    assert int(row[key])==0,(log,key,row)
            if '[EVENT_ONLY]' in line: event[int(row['fixture'])]=row
    assert set(event)==set(range(20))
    rows=[]
    for i in range(20):
        io=hw['io'][str(i)]
        curves=[c for c in hw['callback_curves'] if int(c['fixture'])==i]
        n=sum(int(c['n']) for c in curves)
        avg=sum(int(c['n'])*float(c['avg_us']) for c in curves)/n
        e=event[i]
        rows.append(dict(model=io['model'],fixture=('single','chord4','cluster8','rapid','cycle switching')[i%5],
            avg_us=round(avg,2),p95_us=int(io['p95_us']),p99_us=int(io['p99_us']),max_us=int(io['max_us']),
            cpu_pct=round(avg/2666.6667*100,2),event_avg_us=float(e['avg_us']),event_max_us=int(e['max_us']),
            deadline=int(io['deadline']),i2s_faults=0,nonfinite=0,resonator_faults=0,hard_clamps=0))
    return rows
legacy=read_capture(q/'legacy',qualified=False);packed=read_capture(q/'packed')
for log in ('host_p6on.log','host_p6off.log','host_c16.log'):
    assert '100% tests passed, 0 tests failed out of 15' in (q/log).read_text()
assert '1000 exact allocation-free switches PASS' in (q/'layout.log').read_text()
aggregates=dict(re.findall(r'(\w+) 292 exact cases, aggregate FNV 0x([0-9a-f]{16})',
    (q/'aggregate.log').read_text()))
assert len(aggregates)==10 and aggregates['UDU']=='552d8d59691b008e'
before=json.loads((q/'before/memory.json').read_text())
after=json.loads((q/'after/memory.json').read_text())
assert after['types']['UduCache']['bytes']==before['types']['UduCache']['bytes']==5928
tables=[v['bytes'] for k,v in after['types']['SynthEngine']['members'].items() if k.endswith('PreparedNotes_')]
assert sum(tables)==60552 and len(tables)==9
saving=before['types']['SynthEngine']['bytes']-after['types']['SynthEngine']['bytes']
assert saving>=20000
def heap(path):
    data=path.read_text(errors='replace')
    assert 'model=PAN' in data and 'state=8' in data
    assert '[CURVE]' not in data
    for line in data.splitlines():
        if '[AUDIO]' in line:
            r=fields(line)
            for k in ('deadline','timeout','tx_error','short'): assert int(r[k])==0
    samples=re.findall(r'\[MEM\] internal_free=(\d+) largest_internal=(\d+)',data)
    assert samples
    return min(int(a) for a,b in samples),min(int(b) for a,b in samples)
oldfree,oldlarge=heap(q/'before/audit_raw.log')
free,large=heap(q/'after/production_raw.log')
def psram_used(path):
    rows=re.findall(r'\[MEM_STAGE\].*psram_free=(\d+)',path.read_text(errors='replace'))
    assert rows
    return 2*1024*1024-int(rows[-1])
oldpsram=psram_used(q/'before/audit_raw.log');newpsram=psram_used(q/'after/production_raw.log')
binary=(q/'after/production_firmware.bin').read_bytes()
stack=[]
for path in sorted((q/'after').glob('*.su')):
    for line in path.read_text().splitlines():
        cols=line.split('\t')
        if len(cols)==3:
            size=int(cols[1]);assert size<=2048,(path,line)
            stack.append(dict(function=cols[0],frame_bytes=size,kind=cols[2]))
assert stack
report=dict(complete=True,architecture='aligned model-width SoA in internal BSS; boot prepared; bounded note copy',
    prepared_note_before=86760,prepared_note_after=60552,udu_cache_bytes=5928,
    synth_before=before['types']['SynthEngine']['bytes'],synth_after=after['types']['SynthEngine']['bytes'],
    synth_saving_bytes=saving,internal_free_before=oldfree,internal_free_after=free,
    largest_internal_before=oldlarge,largest_internal_after=large,
    psram_heap_used_before=oldpsram,psram_heap_used_after=newpsram,
    rodata_before=before['sections']['.flash.rodata']['bytes'],rodata_after=after['sections']['.flash.rodata']['bytes'],
    bss_before=before['sections']['.dram0.bss']['bytes'],bss_after=after['sections']['.dram0.bss']['bytes'],
    iram_before=before['sections']['.iram0.text']['bytes'],iram_after=after['sections']['.iram0.text']['bytes'],
    net_static_internal_saving=sum(before['sections'][s]['bytes']-after['sections'][s]['bytes']
        for s in ('.dram0.bss','.dram0.data','.iram0.text')),
    firmware_before=(q/'before/audit_firmware.bin').stat().st_size,firmware_after=len(binary),
    firmware_sha256=hashlib.sha256(binary).hexdigest(),aggregate_fnv64=aggregates,
    stack_usage=stack,legacy_timings=legacy,packed_timings=packed)
assert report['net_static_internal_saving']>=20*1024
assert free>55*1024 and large>24*1024
(q/'completion.json').write_text(json.dumps(report,indent=2)+'\n')
doc=Path('docs/m8_cache_memory.md')
body=doc.read_text(encoding='utf-8').split('<!-- M8_COMPLETE -->')[0].rstrip()
lines=['<!-- M8_COMPLETE -->','','## Qualified result','',
    '| Measurement | Before | After | Change bytes |','|---|---:|---:|---:|']
for label,a,b in [('PreparedNote owners (after includes guards)',86760,60552),('UduCache',5928,5928),
    ('SynthEngine BSS',report['synth_before'],report['synth_after']),('Total DRAM BSS',report['bss_before'],report['bss_after']),
    ('IRAM code',report['iram_before'],report['iram_after']),
    ('Static internal BSS + data + IRAM',
        sum(before['sections'][s]['bytes'] for s in ('.dram0.bss','.dram0.data','.iram0.text')),
        sum(after['sections'][s]['bytes'] for s in ('.dram0.bss','.dram0.data','.iram0.text'))),
    ('Connected internal free',oldfree,free),('Largest internal block',oldlarge,large),
    ('PSRAM pool used at UI startup (including pool overhead)',oldpsram,newpsram),
    ('Flash rodata',report['rodata_before'],report['rodata_after']),('Firmware binary',report['firmware_before'],len(binary))]:
    lines.append(f'| {label} | {a} | {b} | {b-a:+d} |')
lines+=['','No cache data moved to PSRAM; external BSS remains zero. The startup PSRAM '
    'heap-stage logs and display pointer capability logs retain actual UI allocation evidence. '
    'The source generates compact coefficients once at boot using the same canonical builder; '
    'there is no generated coefficient file or CI regeneration step.', '',
    '## Paired physical event and callback qualification','',
    'ESP32-S3 / COM10, BLE MIDI ready at 11.25 ms, 240 MHz, live UI and I2S. '
    'Legacy rollback and compact builds use identical fixtures, 4096 blocks each. '
    'Switch events step through all ten models across successive callbacks and '
    'return to the measured model (two selections per timed event). '
    'Rapid restrikes run every eight blocks, alternating v30/v127. '
    'Event timing excludes render and reset; model-cycle timing includes all selection work. '
    'Callback max includes all work; avg is weighted across all classes. '
    'p95 is the 5 µs histogram upper edge; p99 is the AudioStats histogram upper edge. '
    'These sequential physical runs include scheduling variance.', '',
    '| Model / fixture | Legacy event avg / max µs | Packed event avg / max µs | Packed avg µs | p95 | p99 | Max | CPU % | Legacy callback max / misses |',
    '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
for a,b in zip(legacy,packed):
    assert a['model']==b['model'] and a['fixture']==b['fixture']
    lines.append(f"| {b['model']} / {b['fixture']} | {a['event_avg_us']} / {a['event_max_us']} | {b['event_avg_us']} / {b['event_max_us']} | {b['avg_us']} | {b['p95_us']} | {b['p99_us']} | {b['max_us']} | {b['cpu_pct']} | {a['max_us']} / {a['deadline']} |")
lines+=['','All 20 final compact-cache physical trials: **zero deadline misses, I2S faults, nonfinite, '
    'resonator faults, hard clamps, BLE losses and invalid voice counts**. '
    'Normal production is restored with compact caches, PAN boot and BLE connected.', '',
    f"The narrowest measured full-callback margin is {2666.6667-max(r['max_us'] for r in packed):.1f} µs "
    '(PAN cluster8). PAN render headroom remains the main timing limitation; '
    f"UDU cluster8 retains {2666.6667-packed[17]['max_us']:.1f} µs of measured margin.", '',
    f"The legacy diagnostic control recorded {sum(r['deadline'] for r in legacy)} deadline misses; "
    'these are retained explicitly rather than treated as a passing baseline. '
    'The final event path additionally reuses the existing model-independent MIDI-Hz '
    'and exact MIDI-velocity LUTs, and the register value already held in each '
    'PreparedNote. The 36-byte existing headroom table and one shared 524-byte '
    'canonical strike-energy cache avoid repeated libm calls. Small event, copy '
    'and energy helpers use bounded IRAM, and the existing strike-bus renderer '
    'uses O3 without fast-math. These placement/cache changes preserve float results. '
    'Noncanonical velocities, custom configs, damped triggers and out-of-table '
    'notes retain the established fallbacks.', '',
    '## Exactness, CI and limitations','',
    'Both Process6 configurations and the retained candidate-16 CI configuration '
    'pass all **15 suites**. The candidate-16 fast-path counter assertion now '
    'reflects its unavailable Vibraphone attack specialization; PCM checks remain. '
    'All ten aggregate hashes '
    'and every retained Vibraphone, Mbira and UDU special golden fixture remain exact. '
    'See the [ten aggregate results](qualification/m8/aggregate.log) and '
    '[layout/corruption/switch tests](qualification/m8/layout.log). '
    'The compact table test compares all 657 records byte-for-byte with the canonical '
    'full builder, so model definitions cannot drift from stored values. '
    'Readiness, out-of-range MIDI, frequency mismatch, both guards and cold/warm '
    'selections are tested. A thousand cyclic switches allocate no heap and reproduce '
    'cold-selected PCM. Views point only into persistent owners; no pointers to note scratch escape.', '',
    'CI runs the exact host suites with Process6 ON/OFF and verifies target ELF '
    'PreparedNote totals (60552), UduCache (5928) and SynthEngine (<=82000). '
    'Firmware DSP compilation rejects frames above 2048 bytes and emits stack-usage files. '
    'Only one 132-byte unpacked note and the existing small coefficient-preparation '
    'objects are automatic; every table owner remains static/member storage.', '',
    'Target compiler frame evidence is retained as `after/*.su` and in the '
    'completion JSON. These are individual frames, not measured total task high-water marks. '
    'Boot preparation runs on the 8192-byte main stack; NoteOn and model selection '
    'run on the existing 6144-byte audio stack.', '',
    'Intermediate failing PAN captures are preserved under '
    '`qualification/m8/failed` and `qualification/m8/experiments`; they do not '
    'count toward the final gate. The incomplete boot-reset capture is also '
    'retained, with its successful retry in the paired control evidence.', '',
    'The architecture still grows with model count, retains 2336 duplicated frequency '
    'bytes and 876 inactive tuple bytes, and prepares caches at startup. '
    'Flash/XIP and asynchronous active-model caching remain unqualified alternatives. '
    'The diagnostic legacy rollback is disabled in production. No eleventh instrument '
    'was added, and no gain, decay, excitation, modal recurrence, allocation rule, '
    'body, motor, buzz or UDU sound changed.', '',
    '[Completion and timings](qualification/m8/completion.json), '
    '[legacy hardware](qualification/m8/legacy/hardware_manifest.json), '
    '[packed hardware](qualification/m8/packed/hardware_manifest.json), '
    '[target memory after](qualification/m8/after/memory.json).','']
lines+=['## Frozen aggregate hashes','','292 cases per model; notes 24–96, velocities 30/70/110/127.','',
    '| Model | FNV64 |','|---|---|']
lines += [f'| {name} | `{value}` |' for name,value in aggregates.items()]
lines+=['','## Target stack frames','','| Function | Own frame bytes |','|---|---:|']
for row in stack:
    if any(token in row['function'] for token in ('::noteOn(', '::preparePackedNotes(',
        '::prepareNote(', '::configureStrike(', '::renderVibraphoneBlock(', 'SynthEngine::init(',
        'SynthEngine::setInstrumentModel(')):
        name=row['function'].split('pocketpan::dsp::',1)[1].split('(',1)[0]
        lines.append(f"| {name} | {row['frame_bytes']} |")
lines += ['']
doc.write_text(body+'\n\n'+'\n'.join(lines),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if not k.endswith('_timings')},indent=2))
