"""Validate prepared asset contracts and the narrowly scoped world edit."""
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]
ASSETS=ROOT/'binaries/project/models/island_biomes'
models=json.loads((ASSETS/'models.json').read_text())
assert len(models)==12 and len({m['name'] for m in models})==12
for model in models:
    path=ROOT/'binaries'/model['path'];gltf=json.loads(path.read_text())
    for node in gltf.get('nodes',[]):
        assert node.get('translation',[0,0,0])==[0,0,0],(model['name'],node)
        assert node.get('rotation',[0,0,0,1])==[0,0,0,1],(model['name'],node)
        assert node.get('scale',[1,1,1])==[1,1,1],(model['name'],node)
        assert node.get('matrix',[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1])==[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
    for dependency in gltf.get('buffers',[])+gltf.get('images',[]):
        assert (path.parent/dependency['uri']).is_file(),dependency
    triangles=sum(gltf['accessors'][primitive['indices']]['count']//3 for mesh in gltf['meshes'] for primitive in mesh['primitives'])
    assert triangles==model['triangles'],(model['name'],triangles,model['triangles'])
    assert triangles<=19000 if model['name'].startswith(('pine','olive')) else triangles<=5600

root=ET.parse(ROOT/'worlds/plan.world').getroot()
scatter=root.find('.//terrain/scatter')
config=json.loads(Path(__file__).with_name('biome_layers.json').read_text())
for slot,rule in config.items():
    layer=scatter[int(slot)]
    assert layer.get('name')==rule['name'] and int(layer.get('habitat'))==rule['habitat']
    paths=[layer.get('mesh_path')]+layer.get('mesh_variants','').split(';')
    assert len(paths)==len(rule['assets']) and len(set(paths))==len(paths)
    for path in paths:assert (ROOT/'binaries'/path).is_file(),path

backup=ROOT/'binaries/project/backups/plan_before_biomes.world'
if backup.exists():
    old=ET.parse(backup).getroot()
    for slot in [3,4,5]:
        assert old.find('.//terrain/scatter')[slot].attrib==scatter[slot].attrib,slot
    for world in [old,root]:
        terrain=world.find('.//terrain');terrain.remove(terrain.find('scatter'))
    assert ET.tostring(old)==ET.tostring(root),'World data outside scatter changed'
print('PASS: 12 valid bounded models, 5 configured habitats; preserved world data verified when backup exists')
