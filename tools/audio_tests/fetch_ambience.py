"""Fetch explicitly CC0 ambience sources; retain provenance alongside derived assets."""
import concurrent.futures
import hashlib
import html
import json
from pathlib import Path
import re
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / 'binaries/audio_tests/ambience_sources'
SOURCES = [
    ('cicadas', 'dethrok', 'https://freesound.org/people/dethrok/sounds/272168/'),
    ('birds', 'vas_mous', 'https://freesound.org/people/vas_mous/sounds/669800/'),
    ('village', 'The_Sound_Side', 'https://freesound.org/people/The_Sound_Side/sounds/331442/'),
    ('harbour', 'ivolipa', 'https://freesound.org/people/ivolipa/sounds/328808/'),
    ('wind', 'Luke.RUSTLTD', 'https://opengameart.org/content/wind1'),
    ('surf', 'jasinski (excerpts by qubodup)', 'https://opengameart.org/content/beach-ocean-waves'),
]
LOCK = ROOT / 'tools/audio_tests/ambience_sources.json'

def fetch(source):
    name, author, page = source
    req = urllib.request.Request(page, headers={'User-Agent': 'SpartanEngine soundscape asset preparation'})
    snapshot = CACHE / (name + '.html')
    content = snapshot.read_text(encoding='utf-8') if snapshot.exists() else urllib.request.urlopen(req, timeout=45).read().decode()
    assert 'creativecommons.org/publicdomain/zero' in content or 'Creative Commons 0' in content, page
    (CACHE / (name + '.html')).write_text(content, encoding='utf-8')
    if 'freesound.org' in page:
        urls = re.findall(r'https?[^\s\"<>]+-hq\.mp3', content)
    else:
        urls = re.findall(r'https?[^\s\"<>]+(?:\.wav|\.flac)', content)
        urls = [u for u in urls if '/files/' in u]
    urls = list(dict.fromkeys(html.unescape(u) for u in urls))
    if name == 'wind': urls = [u for u in urls if u.endswith('/wind2.wav')]
    assert urls, f'No public download link: {page}'
    records = []
    for i, url in enumerate(urls[:4] if name == 'surf' else urls[:1]):
        path = CACHE / (name + str(i) + Path(url).suffix)
        if not path.exists():
            data = urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'}), timeout=90).read()
            path.write_bytes(data)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if LOCK.exists():
            expected = next((s['sha256'] for s in json.loads(LOCK.read_text()) if s['download']==url), None)
            assert expected is None or digest == expected, f'Source changed; review before updating provenance: {url}'
        records.append(dict(name=name, author=author, page=page, download=url, license='CC0-1.0', file=path.relative_to(ROOT).as_posix(), sha256=digest, preview='freesound.org' in page))
    print(name, len(records), flush=True)
    return records

if __name__ == '__main__':
    CACHE.mkdir(parents=True, exist_ok=True)
    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        for result in pool.map(fetch, SOURCES): records.extend(result)
    (CACHE / 'sources.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
