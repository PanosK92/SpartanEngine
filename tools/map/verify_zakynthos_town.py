"""Check town road clearance, terrain scope, graph connections and asset references."""
from pathlib import Path
import sys,json,struct,re,xml.etree.ElementTree as E
import numpy as np
from shapely.geometry import LineString
from shapely.ops import unary_union
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(Path(__file__).parent))
from plan_zakynthos_town import footprint,weight,BASE
from install_home_garage import span
OUT=ROOT/'binaries/project/zakynthos_town'

def main():
    text=(ROOT/'worlds/plan.world').read_text();root=E.fromstring(text);plan=json.loads((OUT/'plan.json').read_text());manifest=json.loads((OUT/'manifest.json').read_text())
    ids=[e.get('id') for e in root.iter('Entity')];assert len(ids)==len(set(ids))
    town=next(e for e in root.iter('Entity') if e.get('name')=='Zakynthos Town - city')
    materials=set()
    for e in town.iter('Entity'):
        for c in e:
            for key in ('mesh_path','material_path'):
                p=c.get(key)
                if p:assert (ROOT/'binaries'/p).exists(),p
                if p and key=='material_path':materials.add(p)
    for p in materials:
        r=E.parse(ROOT/'binaries'/p).getroot()
        for t in r.find('textures'):
            path=t.get('texture_path');assert not path or (ROOT/'binaries'/path).exists(),path
        if Path(p).name.startswith('town_') and not Path(p).stem.endswith('_metal'):assert float(r.findtext('metalness','0'))==0,p
    original=json.loads((OUT/'sources/site_roads.json').read_text())
    corridors=unary_union([LineString(r['xy']).buffer(r['width']/2+3) for r in original+plan['roads']])
    for b in plan['buildings']:
        assert not footprint(b['x'],b['z'],b['w'],b['d'],b['yaw'],3.3).intersects(corridors),b['name']
    tags={}
    parents={c:p for p in root.iter() for c in p}
    for e in root.iter('Entity'):
        for tag in e.get('tags','').split(','):
            if not tag.startswith('road_node_town_'):continue
            p=parents[e];pos=[float(a)+float(b) for a,b in zip(e.get('position').split(),p.get('position').split())];tags.setdefault(tag,[]).append(pos)
    for tag,points in tags.items():
        assert len(points)>=2,(tag,'disconnected')
        assert max(np.linalg.norm(np.array(p)-points[0]) for p in points)<.02,(tag,points)
    def tiles(path):
        data=path.read_bytes();h=struct.unpack_from('<II4fII',data);offset=32;result={}
        for _ in range(h[-1]):
            key=struct.unpack_from('<ii',data,offset);offset+=8;result[key]=np.frombuffer(data,dtype='<f4',count=h[-2]**2,offset=offset).reshape(h[-2],h[-2]);offset+=h[-2]**2*4
        return h,result
    header,current=tiles(ROOT/'binaries/project/plan_resources/terrain_sculpt.bin');records=json.loads((OUT/'sources/terrain_base_patch.json').read_text());cell=header[-2]
    for ix,iz,y,old,w in records:
        actual=y+float(current[ix//cell,iz//cell][iz%cell,ix%cell])-5.9;expected=y+old-5.9+(BASE-(y+old-5.9))*w
        assert abs(actual-expected)<.0001,(ix,iz,actual,expected)
    backup=OUT/'sources/before_town_sculpt.bin'
    if backup.exists():
        _,previous=tiles(backup);allowed={(ix,iz) for ix,iz,*_ in records}
        for key in set(previous)|set(current):
            before=previous.get(key,np.zeros((cell,cell)));after=current.get(key,np.zeros((cell,cell)))
            for iz,ix in zip(*np.nonzero(before!=after)):assert (key[0]*cell+int(ix),key[1]*cell+int(iz)) in allowed
    backup=OUT/'sources/before_town.world'
    if backup.exists():
        def strip(s):
            edits=[span(s,'Zakynthos Town')]+[span(s,r['name']) for r in original]
            for a,b in sorted(edits,reverse=True):s=s[:a]+s[b:]
            return re.sub(r'<platforms>.*?</platforms>','',s,flags=re.S)
        assert strip(backup.read_text())==strip(text),'Unrelated world content changed'
    print(f'PASS: {len(plan["buildings"])} clear building lots, {len(tags)} aligned town junctions, {len(materials)} materials, {len(records)} scoped terrain samples, unique IDs and asset references.')

if __name__=='__main__':main()
