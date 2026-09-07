"""Fetch a small CC0 prop/texture palette. Powered by Poly Haven (polyhaven.com).

Downloaded sources stay in ignored binaries; hashes and attribution are retained.
"""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'binaries/project/models/island_biomes/sources'
MODELS = ['coast_land_rocks_02','coast_rocks_01','rock_07','boulder_01','rock_09','shrub_01','shrub_02','shrub_04']

def fetch(url, target, md5=None):
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists():
        request = urllib.request.Request(url,headers={'User-Agent':'SpartanEngine-AssetPreparation/1.0 (Powered by Poly Haven)'})
        with urllib.request.urlopen(request,timeout=90) as response:
            data = response.read()
        if md5: assert hashlib.md5(data).hexdigest()==md5,url
        target.write_bytes(data)
    data=target.read_bytes()
    if md5: assert hashlib.md5(data).hexdigest()==md5,url
    return dict(url=url,file=target.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data))

def main():
    metadata={}
    for asset in MODELS+['pine_tree_01']:
        target=OUT/(asset+'_files.json')
        fetch('https://api.polyhaven.com/files/'+asset,target)
        metadata[asset]=json.loads(target.read_text(encoding='utf-8'))
    jobs=[]
    for asset in MODELS:
        entry=metadata[asset]['gltf']['1k']['gltf']
        jobs.append((asset,entry,OUT/asset/(asset+'.gltf')))
        for name,item in entry['include'].items():jobs.append((asset,item,OUT/asset/name))
    for key in ['bark_diff','bark_nor_gl','bark_rough','twig_diff','twig_alpha','twig_nor_gl','twig_rough']:
        entry=metadata['pine_tree_01'][key]['1k']['png']
        jobs.append(('pine_tree_01',entry,OUT/'pine_textures'/(key+'.png')))
    def work(job):
        asset,entry,target=job
        result=fetch(entry['url'],target,entry.get('md5'))
        return dict(asset=asset,page='https://polyhaven.com/a/'+asset,license='CC0-1.0',**result)
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:records=list(pool.map(work,jobs))
    (OUT.parent/'sources.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
    (Path(__file__).parent/'biome_sources.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
    print(f'Fetched/verified {len(records)} files, {sum(r["bytes"] for r in records)/1e6:.1f} MB')

if __name__=='__main__':main()
