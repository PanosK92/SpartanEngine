"""Install unmodified CC0 Poly Haven road PBR maps in the local project.

Run before opening plan.world on a fresh checkout. Download hashes are checked;
assets stay in binaries/project, like the island's other supporting assets.
"""
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'binaries/project/materials/island_roads'
ASSETS = {'asphalt_track': '4k', 'gravel_road': '2k'}
HEADERS = {'User-Agent': 'SpartanEngine-road-authoring/1.0'}


def read(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=90) as response:
        return response.read()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    receipt = []
    for asset, resolution in ASSETS.items():
        metadata = json.loads(read('https://api.polyhaven.com/files/' + asset))
        channels = {}
        for channel in ('Diffuse', 'nor_gl', 'Rough', 'AO'):
            item = metadata[channel][resolution]['jpg']
            name = item['url'].rsplit('/', 1)[-1]
            path = OUT / name
            if not path.exists() or hashlib.md5(path.read_bytes()).hexdigest() != item['md5']:
                content = read(item['url'])
                if hashlib.md5(content).hexdigest() != item['md5']:
                    raise ValueError('Download checksum mismatch: ' + name)
                path.write_bytes(content)
            channels[channel] = {'file': name, 'md5': item['md5'], 'url': item['url']}
            print(name, flush=True)
        receipt.append({'asset': asset, 'source': 'https://polyhaven.com/a/' + asset,
                        'license': 'CC0-1.0', 'resolution': resolution, 'channels': channels})
    (ROOT / 'tools/map/road_surface_assets.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
