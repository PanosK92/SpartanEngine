"""Fetch a small set of CC0 PBR textures from Poly Haven's public API."""
import json, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'binaries/project/exo_chora/textures'
OUT.mkdir(parents=True,exist_ok=True)
assets=['painted_plaster_wall','plastered_stone_wall','clay_roof_tiles_02','stone_tiles','cobblestone_floor_08']
receipts=[]
for asset in assets:
    cache=OUT/(asset+'.json')
    if cache.exists():meta=json.loads(cache.read_text())
    else:
        req=urllib.request.Request('https://api.polyhaven.com/files/'+asset,headers={'User-Agent':'SpartanEngine-village-authoring/1.0'})
        with urllib.request.urlopen(req,timeout=30) as response:meta=json.load(response)
    (OUT/(asset+'.json')).write_text(json.dumps(meta,indent=2))
    channels={}
    for channel,key in [('diff','Diffuse'),('nor_gl','nor_gl'),('rough','Rough')]:
        entry=meta.get(key,{}).get('2k',{})
        fmt='jpg' if 'jpg' in entry else 'png'
        if fmt not in entry:continue
        info=entry[fmt];target=OUT/(asset+'_'+channel+'.'+fmt)
        if not target.exists():
            with urllib.request.urlopen(urllib.request.Request(info['url'],headers={'User-Agent':'SpartanEngine-village-authoring/1.0'}),timeout=60) as response:target.write_bytes(response.read())
        channels[channel]=target.name
    receipts.append({'asset':asset,'license':'CC0','source':'https://polyhaven.com/a/'+asset,'channels':channels})
    print(asset,channels,flush=True)
(OUT/'sources.json').write_text(json.dumps(receipts,indent=2))
