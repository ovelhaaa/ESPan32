"""Apply the preferred M8.1 gate to actual callback captures, never host timings."""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
a = p.parse_args()
manifest = json.loads((a.directory / 'hardware_manifest.json').read_text())
row = manifest['io']['2']
assert row['model'] == 'PAN'
deadline = 128 * 1_000_000 / 48000
maximum = int(row['max_us'])
margin = deadline - maximum
assert manifest['passed'] and int(row['deadline']) == 0
assert maximum <= 2400 and margin >= 250, (maximum, margin)
for curve in manifest['callback_curves']:
    if curve['fixture'] == '2':
        assert int(curve['deadline']) == 0
result = {'passed': True, 'gate': 'preferred', 'deadline_us': deadline,
          'maximum_us': maximum, 'margin_us': margin, 'p99_us': int(row['p99_us'])}
(a.directory / 'gate.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
