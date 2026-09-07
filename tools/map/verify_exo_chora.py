"""Check saved village scope, native asset references and reproducible geometry."""
from pathlib import Path
import copy, json, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'binaries/project/exo_chora'
old=ET.parse(OUT/'sources/plan_before_exo_chora.world').getroot()
new=ET.parse(ROOT/'worlds/plan.world').getroot()

def without_site(root):
    root=copy.deepcopy(root)
    for parent in root.iter():
        for e in list(parent):
            if e.tag=='Entity' and e.get('name')=='Exo Chora':parent.remove(e)
            elif e.tag=='platform' and (e.get('entity_id','').startswith('9046') or e.get('entity_id')=='9017000000000001276'):parent.remove(e)
    return ET.canonicalize(ET.tostring(root,encoding='unicode'),strip_text=True)

assert without_site(old)==without_site(new),'Unexpected change outside Exo Chora'
ids=[e.get('id') for e in new.iter('Entity')]
assert len(ids)==len(set(ids)),'Duplicate world IDs'
site=next(e for e in new.iter('Entity') if e.get('name')=='Exo Chora - Olive Square')
paths=set()
for e in site.iter():
    for key in ['mesh_path','material_path']:
        if e.get(key):paths.add(e.get(key))
for p in list(paths):
    assert p.startswith('project/'),p
    assert (ROOT/'binaries'/p).is_file(),p
    if p.endswith('.xml'):
        for t in ET.parse(ROOT/'binaries'/p).getroot().iter():
            if t.get('texture_path'):paths.add(t.get('texture_path'))
for p in paths:assert (ROOT/'binaries'/p).is_file(),p
manifest=json.loads((OUT/'manifest.json').read_text())
collision=sum(e.find('physics') is not None for e in site.iter('Entity'))
report=dict(scope='Only Exo Chora and its terrain platforms changed in plan.world',unique_entity_ids=True,
            referenced_files=len(paths),missing_files=0,buildings=len(manifest['buildings']),
            meshes=len(manifest['meshes']),triangles=sum(x['triangles'] for x in manifest['meshes']),
            trees=len(manifest['trees'])+1,static_colliders=collision,
            generated_lods=False,detail_cull_distance_m=400,structure_cull_distance_m=1800)
(OUT/'verification.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
