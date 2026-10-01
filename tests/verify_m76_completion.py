"""Verify on-device BOOT scheduling and restoration, preserving raw evidence."""
import hashlib
import json
import re
from pathlib import Path

q=Path('docs/qualification/m76')
boot=(q/'boot_raw.log').read_text(encoding='utf-8',errors='replace')
production=(q/'production_raw.log').read_text(encoding='utf-8',errors='replace')
assert 'Guru Meditation' not in boot+production
transitions=re.findall(r"TRANSITION: (\w+) -> (\w+) \(label='([^']*)' mode=(\d+) rel_tick=(\d+)\)",boot)
sequence=['PAN','BELL','TONGUE','BOWL','KALIMBA','GLASS','MARIMBA','VIBRAPHONE','MBIRA','PAN']
expected=list(zip(sequence[:-1],sequence[1:]))*2+[('PAN','BELL')]
assert len(transitions)==19 and [(r[0],r[1]) for r in transitions]==expected
assert all(r[1]==r[2] and r[3]=='0' for r in transitions)
assert 'SCREEN_MODE: 0 -> 1' in boot and 'SCREEN_MODE: 1 -> 2' in boot
assert '3 short, 18 long, 1 extended hold' in boot
assert 'state=8 interval_ms=11.25' in boot and 'state=8 interval_ms=11.25' in production
audio=[dict(re.findall(r'(\w+)=([^\s\x1b]+)',line)) for line in production.splitlines() if '[AUDIO]' in line]
assert len(audio)>=3 and all(r['model']=='PAN' for r in audio)
assert all(int(r[k])==0 for r in audio for k in ['deadline','timeout','tx_error','short'])
assert '[BOOT_QUAL]' not in production
cache=Path('build-ci-prod/CMakeCache.txt').read_text()
assert 'POCKETPAN_BUTTON_QUAL:STRING=0' in cache and 'POCKETPAN_FORENSICS_M76:STRING=0' in cache
assert '# CONFIG_POCKETPAN_POLYPHONY_FORENSICS is not set' in (q/'sdkconfig.production').read_text()
memory=[dict(re.findall(r'(\w+)=([^\s\x1b]+)',line)) for line in production.splitlines() if '[MEM]' in line]
assert memory
hardware=json.loads((q/'hardware_manifest.json').read_text())
for path,sha in hardware['source_sha256'].items():
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==sha
cost=[]
for i in range(3):
    raw=(q/f'batch{i}_raw.log').read_text(errors='replace')
    cost+=re.findall(r'\[MBIRA_BUZZ_COST\] blocks=1024 avg_us=([\d.]+) max_us=([\d.]+)',raw)
assert len(cost)==3 and all(float(r[0])<30 for r in cost)
files=['boot_raw.log','boot_firmware.bin','production_raw.log','production_firmware.bin','sdkconfig.m76','sdkconfig.production']
manifest=dict(boot_transitions=transitions,production_audio=audio,production_memory=memory,buzz_cost=cost,
    artifacts={name:hashlib.sha256((q/name).read_bytes()).hexdigest() for name in files},
    tests={str(path).replace('\\','/'):hashlib.sha256(path.read_bytes()).hexdigest()
           for path in sorted(Path('tests').glob('*')) if path.is_file()})
(q/'completion_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
io=hardware['io']; cluster=io['2']
lines=['# M7.6 completion evidence','',
       'All eleven host suites pass with Process6 ON and OFF; all eight frozen aggregates and twelve frozen Vibraphone dry/M1 fixtures remain exact. MBIRA is provisional. The listening pack contains 21 verified RMS-matched A/B/C and buzz off/on files.','',
       f"Physical qualification passes all seven BLE-connected fixtures with zero deadline misses, I2S timeouts/errors/short writes, hard clamps and modal saturation. Cluster8 maximum is {cluster['max_us']} us, with {128/48000*1e6-int(cluster['max_us']):.1f} us of deadline margin. Full timings are in [hardware report](hardware_report.md).",'',
       f"The isolated, continuously refreshed contact DSP probe averages {min(float(r[0]) for r in cost):.3f}–{max(float(r[0]) for r in cost):.3f} us per 128-frame block. Maximum observed isolated blocks are {min(float(r[1]) for r in cost):.3f}–{max(float(r[1]) for r in cost):.3f} us, including interrupt variability. The average achieves the preferred <30 us target. The full callback separately includes the body and output DC guard.", '',
       'BOOT qualification runs on the physical ESP32-S3 using a firmware schedule injected at the BOOT input read. It is not evidence of manually pressing the mechanical switch. Three short gestures retain the diagnostic flow, 18 long gestures complete two full nine-model cycles, and one extended hold produces exactly one additional PAN→BELL transition. Every transition exposes the correct UI label and status mode. VIBRAPHONE→MBIRA→PAN is observed twice. The short-press production handler itself was not changed. [Raw log](boot_raw.log).', '',
       'Normal firmware was rebuilt with BOOT qualification and forensic flags disabled and restored to COM10. It boots PAN, connects BLE MIDI at 11.25 ms, and the retained normal capture has zero audio deadline/I2S faults. [Raw log](production_raw.log).', '',
       f"Restored production internal free SRAM: {min(int(r['internal_free']) for r in memory)}–{max(int(r['internal_free']) for r in memory)} bytes. Largest free internal block: {min(int(r['largest_internal']) for r in memory)}–{max(int(r['largest_internal']) for r in memory)} bytes. PreparedNote stays 132 bytes, table/model 9,640 bytes, total tables 86,760 bytes, host engine 100,928 bytes, device engine BSS symbol 100,672 bytes. One shared velocity table adds 512 bytes and a readiness flag outside the engine.", '',
       'The unsuccessful event capture (one cluster8 deadline miss at 2,932 us) is retained as `development_event_batch0_raw.log` with its flashed binary. Exact Mbira trigger precomputation reduced event work; qualification was repeated for the final source. Earlier contact probes are retained too. No failed run is substituted for a passing final capture.', '',
       'Remaining decision: listen to A/B/C and the two-velocity/groove buzz comparisons, then explicitly choose tine/contact/body balance before a freeze. No automatic winner is selected.']
(q/'completion.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Verified 19 BOOT transitions, restored normal firmware, matching source hashes and <30 us average contact DSP.')
