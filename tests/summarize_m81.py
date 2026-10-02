"""Summarize measured M8.1 captures; profile times are diagnostic, not qualification."""
import argparse
import json
import re
from pathlib import Path

PHASES = ('allocator', 'modal', 'exciter', 'damping', 'energy', 'body', 'mix',
          'limiter', 'pcm', 'midi', 'allocation', 'register', 'coefficients',
          'coupling', 'trigger_exciter', 'lookup', 'trigger_other', 'sympathetic')
parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
args = parser.parse_args()
report = {}
for path in sorted(args.directory.glob('batch*_raw.log')):
    for line in path.read_text(errors='replace').splitlines():
        line = re.sub(r'\x1b\[[0-9;]*m', '', line)
        row = dict(re.findall(r'(\w+)=([^\s]+)', line))
        if 'fixture' not in row:
            continue
        fixture = report.setdefault(row['fixture'], {})
        if '[PHASE]' in line:
            fixture.setdefault('profile_us', {}).setdefault(row['class'], {})[
                PHASES[int(row['phase'])]] = round(float(row['cycles_per_block']) / 240, 3)
        elif '[PROFILE_MAX]' in line:
            fixture.setdefault('profile_max', {}).setdefault(row['class'], {})[
                PHASES[int(row['phase'])]] = round(int(row['cycles']) / 240, 3)
        elif '[KERNEL]' in line:
            fixture.setdefault('kernels', {}).setdefault(row['class'], {})[
                ('scalar', 'six', 'eight', 'ten')[int(row['kernel'])]] = int(row['calls'])
        elif '[FASTPATH]' in line:
            fixture['fastpaths'] = {k: int(row[k]) for k in ('stable8', 'attack', 'sustain', 'shared_noise') if k in row}
        elif '[CURVE]' in line or '[CALLBACK_CURVE]' in line:
            key = 'callback' if '[CALLBACK_CURVE]' in line else 'render'
            fixture.setdefault(key, {})[row['class']] = row
        elif '[EVENT_ONLY]' in line:
            fixture['event'] = row
        elif '[FIXTURE_IO]' in line:
            fixture['io'] = row
data = json.dumps(report, indent=2)
(args.directory / 'summary.json').write_text(data + '\n')
print(data)
