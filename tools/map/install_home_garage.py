"""Install imported home assets, preserving the rest of the island byte for byte."""
from pathlib import Path
import copy, json, math, re, shutil
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'binaries/project/home_garage'
WORLD=ROOT/'worlds/plan.world'

def span(text,name):
    begin=text.index('<Entity name="'+name+'"');depth=0
    for match in re.finditer(r'<Entity\b[^>]*>|</Entity>',text[begin:]):
        tag=match.group();depth+=-1 if tag.startswith('</') else 0 if tag.endswith('/>') else 1
        if depth==0:return begin,begin+match.end()
    raise ValueError(name)

def main():
    source=WORLD.read_text(encoding='utf-8');root=ET.fromstring(source)
    old=next(e for e in root.iter('Entity') if e.get('name')=='player_garage_hub')
    old_ids={e.get('id') for e in old.iter('Entity')}
    manifest=json.loads((OUT/'manifest.json').read_text())
    for mesh in manifest['meshes']:assert (ROOT/'binaries'/mesh['mesh_path']).exists(),mesh['mesh_path']
    backup=OUT/'sources/home_before_rebuild.world'
    if not backup.exists():backup.write_text(source,encoding='utf-8')
    home=ET.Element('Entity',old.attrib)
    home.set('tags','location_player_home,landmark,home_garage')
    # IDs referenced by the vehicle reset and map marker stay stable.
    for e in old:
        if e.get('name') in ('player_car_spawn','map_label'):home.append(copy.deepcopy(e))
    next_id=9057000000000000000
    def entity(parent,name,p=(0,0,0)):
        nonlocal next_id
        next_id+=1
        return ET.SubElement(parent,'Entity',name=name,id=str(next_id),active='true',position=' '.join(map(str,p)),rotation='0 0 0 1',scale='1 1 1')
    groups={}
    for m in manifest['meshes']:
        group=m['group']
        if group not in groups:groups[group]=entity(home,group.replace('_',' ').title(),(9,2.93,-2) if group=='fan' else (0,0,0))
        e=entity(groups[group],m['name'])
        ET.SubElement(e,'render',mesh_name=m['name'],mesh_path=m['mesh_path'],sub_mesh_index='0',material_name='home_'+m['material'],material_path='project/home_garage/materials/home_'+m['material']+'.xml',material_default='false',flags='13',max_render_distance='650' if group in ('foundation','workshop','roof','lounge','garden') else '110',max_shadow_distance='90')
        if group not in ('fan','fan_mount','lighting','lettering') and m['material'] not in ('leaf','flower','amber','mint_glow'):
            ET.SubElement(e,'physics',mass='0',is_static='true',is_kinematic='false',friction='.85',restitution='0',body_type='4')
    fan_script=OUT/'sources/ceiling_fan.lua'
    fan_script.write_text('local fan = {}\nfunction fan.Tick(self, entity)\n    local a = Timer.GetDeltaTimeSec() * 0.55\n    local q = Quaternion()\n    q.x = 0; q.y = math.sin(a); q.z = 0; q.w = math.cos(a)\n    entity:Rotate(q)\nend\nreturn fan\n')
    ET.SubElement(groups['fan'],'script',file_path='project/home_garage/sources/ceiling_fan.lua')
    practicals=entity(home,'Warm practical lighting')
    for light in manifest['lights']:
        e=entity(practicals,light['name'],light['position'])
        ET.SubElement(e,'light',flags='1',light_type='1',color_r='1',color_g='.74',color_b='.43',temperature='3000',intensity='8',intensity_photometric=str(light['lumens']),range=str(light['range']),angle='.52359879',index='0',preset='0',area_width='1',area_height='1',draw_distance='70',distance_shadows='25',distance_volumetric='18')
    e=entity(home,'Jukebox - Slow Roads',(14.6,1.2,-1.5))
    ET.SubElement(e,'audio_source',path='project/home_garage/audio/slow_roads.wav',ambient='false',is_3d='true',loop='true',play_on_start='true',volume='0.55',pitch='1',reverb_enabled='true')
    # Small established olive trees frame the garden without blocking the drive.
    for i,(x,z,scale) in enumerate([(-14,12,.52),(12,12,.55)]):
        e=entity(home,'Garden olive '+str(i+1),(x,.04,z));e.set('scale',f'{scale} {scale} {scale}')
        for sub,mat in [(0,'foliage'),(1,'bark')]:
            part=entity(e,mat)
            ET.SubElement(part,'render',mesh_name='olive_01',mesh_path='project/exo_chora/meshes/olive_01.mesh',sub_mesh_index=str(sub),material_name='olive_01_olive_'+mat,material_path='project/models/island_biomes/olive_01/olive_01_olive_'+mat+'.xml',material_default='false',flags='1',max_render_distance='450',max_shadow_distance='100')
    # Replace stale overlapping blockout pads with one matching the new terrace.
    pads=root.find('.//terrain/platforms')
    for p in list(pads):
        if p.get('entity_id') in old_ids:pads.remove(p)
    anchor=next(e for e in groups['foundation'] if e.find('render').get('material_name')=='home_stone')
    ox,oy,oz=map(float,home.get('position').split());a=math.radians(140);cx=ox+math.sin(a)*2;cz=oz+math.cos(a)*2
    hx,hz=17.5,14;ex=abs(math.cos(a))*hx+abs(math.sin(a))*hz;ez=abs(math.sin(a))*hx+abs(math.cos(a))*hz
    ET.SubElement(pads,'platform',entity_id=anchor.get('id'),min_x=str(cx-ex),max_x=str(cx+ex),min_z=str(cz-ez),max_z=str(cz+ez),center_x=str(cx),center_z=str(cz),half_x=str(hx),half_z=str(hz),yaw=str(-a),height=str(oy-.32),margin='2.07')
    def serial(e,level):ET.indent(e,space=' ',level=level);return ET.tostring(e,encoding='unicode').rstrip()
    a,b=span(source,'player_garage_hub');updated=source[:a]+serial(home,4)+source[b:]
    a=updated.index('<platforms>');b=updated.index('</platforms>',a)+len('</platforms>');updated=updated[:a]+serial(pads,5)+updated[b:]
    check=ET.fromstring(updated);ids=[e.get('id') for e in check.iter('Entity')];assert len(ids)==len(set(ids))
    for e in home.iter('Entity'):
        for component in e:
            for key in ('mesh_path','material_path','file_path','path'):
                path=component.get(key)
                if path and path.startswith('project/'):assert (ROOT/'binaries'/path).exists(),path
    assert WORLD.read_text(encoding='utf-8')==source,'World changed during installation'
    WORLD.write_text(updated,encoding='utf-8')
    (OUT/'sources/home.xml').write_text(serial(home,0),encoding='utf-8')
    for name in ('build_home_garage.py','install_home_garage.py','import_home_garage.mjs','preview_home_garage.py'):
        src=ROOT/'tools/map'/name
        if src.exists():shutil.copy2(src,OUT/'sources'/name)
    print('HOME INSTALLED:',len(list(home.iter('Entity'))),'entities; spawn preserved; unique IDs and asset references verified')

if __name__=='__main__':main()
