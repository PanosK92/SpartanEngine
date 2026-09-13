"""Apply the island racing widths without changing routes, nodes or scenery.

Dry-run by default; --apply writes only road headers and spline widths. Main
routes (including their former 10 m airport links) become 15 m undivided roads.
Technical routes retain 8 m. Tags make the two groups available for track layouts.
"""
import argparse
from collections import Counter
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
WORLD = ROOT / 'worlds/plan.world'


def tune(source):
    root = ET.fromstring(source)
    roads = next(e for e in root.iter('Entity') if e.get('name') == 'roads')
    edits = []
    counts = Counter()
    for road in roads.findall('Entity'):
        spline = road.find('spline')
        if spline is None:
            continue
        tags = road.get('tags', '').split(',')
        main = 'main' in tags or max(float(spline.get(k, '0')) for k in ('road_width', 'road_width_end')) >= 10
        width = '15' if main else '8'
        tier = 'racing_main' if main else 'racing_technical'
        counts[tier] += 1
        tags = [t for t in tags if t and t not in ('racing_main', 'racing_technical')]
        tags.append(tier)
        header = re.search(r'<Entity\b[^>]*\bid="' + re.escape(road.get('id')) + r'"[^>]*>', source)
        component = re.search(r'<spline\b[^>]*>', source[header.end():])
        start = header.end() + component.start()
        end = header.end() + component.end()
        new_component = re.sub(r'\b(road_width(?:_end)?)="[^"]*"', lambda m: m[1]+'="'+width+'"', component[0])
        new_header = re.sub(r'\btags="[^"]*"', 'tags="'+','.join(tags)+'"', header[0])
        if 'tags=' not in header[0]:
            new_header = header[0][:-1]+' tags="'+','.join(tags)+'">'
        edits.extend(((start, end, new_component), (header.start(), header.end(), new_header)))
    result = source
    for start, end, replacement in sorted(edits, reverse=True):
        result = result[:start] + replacement + result[end:]
    return result, dict(counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    source = WORLD.read_text(encoding='utf-8')
    result, counts = tune(source)
    print(counts, 'changed:', result != source)
    if args.apply and result != source:
        if WORLD.read_text(encoding='utf-8') != source:
            raise RuntimeError('World changed during authoring')
        WORLD.write_text(result, encoding='utf-8', newline='\r\n')


if __name__ == '__main__':
    main()
