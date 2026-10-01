"""Close M7.7.1 technical evidence without making a listening selection/freeze."""
import hashlib
import json
import re
from pathlib import Path

q=Path('docs/qualification/m771')
hw=json.loads((q/'hardware_manifest.json').read_text())
control=json.loads((q/'fixed_control/hardware_manifest.json').read_text())
host=json.loads((q/'host_summary.json').read_text())
assert hw['passed'] and control['passed'] and host['all_required_faults_zero']
def clean(path):
    return re.sub(r'\x1b\[[0-9;]*m','',path.read_bytes().decode(errors='replace'))
def fields(text,tag):
    return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if tag in line]
events={}
for batch in (0,1):
    text=clean(q/f'batch{batch}_raw.log')
    assert 'state=8 interval_ms=11.25' in text and 'Guru Meditation' not in text
    assert fields(text,'[UDU_STATE]')[0]['fixed_reference']=='0'
    for row in fields(text,'[EVENT_ONLY]'): events[row['fixture']]=row
    for row in fields(text,'[CURVE]'):
        assert all(int(row[k])==0 for k in ('bad_voices','deadline','ble_lost','hard','sat'))
    assert len(fields(text,'[SAFETY]'))==3
    text=clean(q/'fixed_control'/f'batch{batch}_raw.log')
    assert 'state=8 interval_ms=11.25' in text and 'Guru Meditation' not in text
    assert fields(text,'[UDU_STATE]')[0]['fixed_reference']=='1'
assert len(events)==6
for manifest in (hw,control):
    for source,digest in manifest['source_sha256'].items():
        assert hashlib.sha256(Path(source).read_bytes()).hexdigest()==digest,source
production=clean(q/'production_raw.log')
assert '[FIXTURE_IO]' not in production and '[BOOT_QUAL]' not in production
assert 'state=8 interval_ms=11.25' in production
audio=fields(production,'[AUDIO]')
memory=fields(production,'[MEM]')
assert len(audio)>=3 and len(memory)>=3
assert all(row['model']=='PAN' for row in audio)
for row in audio:
    assert all(int(row[k])==0 for k in ('deadline','timeout','tx_error','short'))
free=min(int(row['internal_free']) for row in memory)
largest=min(int(row['largest_internal']) for row in memory)
assert free>32768 and largest>16384
cache=Path('build-ci-prod/CMakeCache.txt').read_text()
for key in ('POCKETPAN_FORENSICS_M77','POCKETPAN_FORENSICS_M76','POCKETPAN_BUTTON_QUAL','POCKETPAN_UDU_FIXED_REFERENCE'):
    assert re.search(rf'^{key}:STRING=0$',cache,re.MULTILINE),key
assert '#define CONFIG_POCKETPAN_POLYPHONY_FORENSICS 1' not in Path('build-ci-prod/config/sdkconfig.h').read_text()
binary=(q/'production_firmware.bin').read_bytes()
assert binary==Path('build-ci-prod/pocket_pan.bin').read_bytes()
device_memory=fields(clean(q/'batch0_raw.log'),'[M75_MEMORY]')[0]
voice=int(fields(clean(q/'batch0_raw.log'),'[UDU_STATE]')[0]['voice_bytes'])
baseline=json.loads(Path('docs/qualification/m77/completion.json').read_text())
names=['single v127','chord4','cluster8','dynamic groove','model switching','rapid retrigger / restrike']
def callback_mean(manifest,index):
    curves=[row for row in manifest['callback_curves'] if int(row['fixture'])==index]
    count=sum(int(row['n']) for row in curves)
    assert count in (8191,8192) # boundary callback may be credited to the next fixture
    return sum(float(row['avg_us'])*int(row['n']) for row in curves)/count
timing=[]
for i,name in enumerate(names):
    row=dict(hw['io'][str(i)])
    row['io_window_avg_us']=row['avg_us'];row['io_window_cpu_pct']=row['cpu_pct']
    row['avg_us']=round(callback_mean(hw,i),2)
    row['cpu_pct']=round(callback_mean(hw,i)/(128/48000*1e6)*100,2)
    fixed_mean=callback_mean(control,i)
    timing.append(dict(row,fixture=name,event_only=events[str(i)],
        fixed_partial_avg_us=round(fixed_mean,2),
        fixed_partial_io_window_avg_us=control['io'][str(i)]['avg_us'],
        avg_delta_fixed_us=round(callback_mean(hw,i)-fixed_mean,2)))
summary=dict(technical_passed=True,listening_decision='pending; centered/R1 provisional; UDU not frozen',
    production_restored=True,production_boot_model='PAN',udu_cache_bytes=5928,udu_voice_device_bytes=voice,
    synth_engine_device_bytes=int(device_memory['synth_bytes']),
    device_state_increase_bytes=int(device_memory['synth_bytes'])-baseline['synth_engine_device_bytes'],
    internal_free=free,largest_internal=largest,free_delta_m77=free-baseline['production_free_internal_min'],
    largest_delta_m77=largest-baseline['production_largest_internal_min'],timing=timing,
    aggregate_avg_delta_under_10_us=all(row['avg_delta_fixed_us']<10 for row in timing),
    render_loop_added_arithmetic=0,
    production_firmware_sha256=hashlib.sha256(binary).hexdigest(),
    production_log_sha256=hashlib.sha256((q/'production_raw.log').read_bytes()).hexdigest())
(q/'completion.json').write_text(json.dumps(summary,indent=2)+'\n')
lines=['<!-- M771_PHYSICAL_REPORT -->','',
    'Physical evidence: ESP32-S3 on COM10, BLE MIDI ready at 11.25 ms throughout, '
    '240 MHz, 48 kHz / 128 frames, live UI/I2S. Each fixture has 8192 blocks. '
    'Event-only timing includes dispatch/allocator/strike and model switching, '
    'and excludes render; full callbacks include all audio callback work. '
    'Means combine all four recorded callback classes (8191–8192 samples per '
    'fixture); the existing recorder may credit a boundary callback to the '
    'following fixture. Max/deadline/I2S counters cover all 8192. '
    'CPU % is this whole-fixture mean divided by the 2666.7 µs budget. '
    'The original FIXTURE_IO avg/cpu fields are the final 100-block window, '
    'preserved separately in completion.json and the raw manifest.','',
    '| Fixture | Avg µs | p95 µs | p99 µs | Max µs | CPU % | Event avg / max µs |',
    '|---|---:|---:|---:|---:|---:|---|']
for row in timing:
    event=row['event_only']
    lines.append(f"| {row['fixture']} | {row['avg_us']} | {row['p95_us']} | {row['p99_us']} | {row['max_us']} | {row['cpu_pct']} | {event['avg_us']} / {event['max_us']} |")
lines+=['','All six dynamic fixtures: deadline misses = 0; I2S timeout/error/short = '
    '0/0/0; hard clamps = 0; nonfinite = 0; resonator faults = 0; BLE losses = 0; '
    'invalid event/attack voice counts = 0. p95 uses the upper edge of the 5 µs '
    'full-callback histogram; p99 uses the existing 75 µs AudioStats histogram.',
    f"Cluster8 margin is {128/48000*1e6-int(hw['io']['2']['max_us']):.1f} µs. "
    'Rapid retrigger repeats MIDI 60 every eight blocks (21.33 ms), alternating '
    'v30/v127, exercising retained cavity state and new excitation.', '',
    '| Fixture | Fixed PARTIAL avg µs | Dynamic avg µs | Delta µs |',
    '|---|---:|---:|---:|']
for row in timing:
    lines.append(f"| {row['fixture']} | {row['fixed_partial_avg_us']:.2f} | {row['avg_us']:.2f} | {row['avg_delta_fixed_us']:+.2f} |")
lines+=['','This fresh control uses the same implementation with opening fixed at '
    'PARTIAL during startup. The render loop adds zero arithmetic and no '
    'interpolation, coefficient motion, trig or exp. Aggregate fixture cost also '
    'depends on the chosen cavity decay: CLOSED tails can keep voices active '
    'longer than fixed PARTIAL. The host mirror of the repeating device groove '
    'counts 2,542,080 dynamic voice-frames versus 2,501,376 fixed (+1.63%); '
    'this is host occupancy evidence, not simulated hardware timing. The table '
    'also includes physical scheduling variance. A structural zero sample-loop cost '
    'does not imply identical aggregate groove CPU.',
    f"The literal <10 µs average delta across all six fixtures is "
    f"{'met' if summary['aggregate_avg_delta_under_10_us'] else 'not met in this capture'}; "
    'this target is reported separately from the required zero-fault/deadline '
    'gates. The comparative model/groove results and remaining listening choice '
    'must be considered before declaring the entire musical milestone accepted. '
    'In particular, the final 100-block groove window reads 467 versus 430 µs, '
    'while the complete callback mean is 443.66 versus 442.32 µs. The window '
    'difference is not a whole-fixture average regression. The normal-render '
    'inner class means also differ only +3.00 µs steady / -1.12 µs attack for '
    'this groove; event-only costs are reported independently above.', '',
    f"Restored normal production boots PAN and reconnects BLE MIDI. Minimum "
    f"internal free SRAM is **{free} bytes** (M7.7: 40303, delta {free-40303:+d}); "
    f"largest free internal block is **{largest} bytes** (M7.7: 23552, delta {largest-23552:+d}). "
    'Both clear the strict 32 KiB / 16 KiB guardrails. Forensics, BOOT injection '
    'and the fixed timing control are disabled. The retained production binary '
    'matches the flashed build by SHA256; the final connected production '
    'samples have zero deadline and I2S faults.', '',
    '[Dynamic hardware manifest](qualification/m771/hardware_manifest.json), '
    '[fixed control](qualification/m771/fixed_control/hardware_manifest.json), '
    '[production log](qualification/m771/production_raw.log), '
    '[completion and memory](qualification/m771/completion.json).','']
document=Path('docs/m771_udu_dynamic_hole.md')
body=document.read_bytes().split(b'<!-- M771_PHYSICAL_REPORT -->')[0].decode('utf-8')
body=body.replace('\r\n','\n').replace('\r','').rstrip()
document.write_text(body+'\n\n'+'\n'.join(lines),encoding='utf-8',newline='\n')
print(json.dumps(summary,indent=2))
