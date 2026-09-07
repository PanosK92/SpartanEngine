"""Install the authored village, local terrain terraces and PBR materials into plan.world.

Idempotent and scoped to Exo Chora. Backups and all assets live in binaries/project.
"""
from pathlib import Path
import copy, hashlib, json, math, shutil, struct, xml.etree.ElementTree as ET
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'binaries/project/exo_chora';WORLD=ROOT/'worlds/plan.world'
manifest=json.loads((OUT/'manifest.json').read_text());BASE=manifest['origin'][1];angle=math.radians(manifest['yaw']);cs,sn=math.cos(angle),math.sin(angle)
def vec(v):return ' '.join(f'{x:.6f}' for x in v)
def world(x,y,z):return (manifest['origin'][0]+cs*x+sn*z,BASE+y,manifest['origin'][2]-sn*x+cs*z)
def level(v):return (v+12)*.10 if v>-12 else 0 if v>=-70 else (v+70)*.06
n=0
def entity(parent,name,p=(0,0,0),s=(1,1,1),yaw=0):
    global n;n+=1
    return ET.SubElement(parent,'Entity',name=name,id=str(9046000000000000000+n),active='true',position=vec(p),rotation=vec((0,math.sin(math.radians(yaw)/2),0,math.cos(math.radians(yaw)/2))),scale=vec(s))
def physics(e,kind=4):ET.SubElement(e,'physics',mass='0',is_static='true',is_kinematic='false',friction='0.8',restitution='0',body_type=str(kind))
def bind(e,path,mat,sub=0,distance=1800,shadow=180):
    ET.SubElement(e,'render',mesh_name=Path(path).stem,mesh_path=path,sub_mesh_index=str(sub),material_name='exo_'+mat,material_path='project/exo_chora/materials/exo_'+mat+'.xml',material_default='false',flags='13',max_render_distance=str(distance),max_shadow_distance=str(shadow))

def materials():
    template=ET.parse(ROOT/'binaries/project/zakynthos_landmarks/materials/zante_stone.xml').getroot()
    for name,m in manifest['materials'].items():
        root=copy.deepcopy(template)
        props=dict(gltf=0,terrain_blend=0,normal=.7 if m['texture'] else 0,roughness=m['roughness'],metalness=.75 if name=='metal' else 0,sheen=0,color_variation_from_instance=0,cull_mode=0)
        props.update(zip(['color_r','color_g','color_b'],m['color']))
        if name=='amber':props['emissive_from_albedo']=1
        if name in ['cream','canvas','canvas_red','leaves','olive_leaf','olive_silver','flowers']:props['cull_mode']=2
        if name=='glass':props.update(clearcoat=1,clearcoat_roughness=.08)
        for key,value in props.items():
            e=root.find(key)
            if e is None:e=ET.SubElement(root,key)
            e.text=str(value)
        # Explicit scalar channel images: the engine packs roughness into its G channel.
        if m['texture']:
            for i,channel in [(0,'diff'),(4,'rough'),(12,'nor_gl')]:
                e=root.find(f'textures/texture_{i}');path=OUT/'textures'/f'{m["texture"]}_{channel}.jpg'
                if not path.exists():path=path.with_suffix('.png')
                if not path.exists() and channel=='rough':continue
                assert path.exists(),path
                e.set('texture_name',path.stem);e.set('texture_path',path.relative_to(ROOT/'binaries').as_posix())
            root.find('roughness').text='1'
        ET.ElementTree(root).write(OUT/f'materials/exo_{name}.xml',encoding='utf-8',xml_declaration=True)

def terraform():
    # Sculpt only the immediate settlement and feather its boundary into the existing island.
    path=ROOT/'binaries/project/plan_resources/terrain_sculpt.bin';backup=OUT/'sources/terrain_sculpt_before_village.bin'
    if not backup.exists():shutil.copy2(path,backup)
    data=backup.read_bytes();header=list(struct.unpack_from('<II4fII',data));cells,count=header[-2:];offset=32;tiles={}
    for _ in range(count):
        tx,tz=struct.unpack_from('<ii',data,offset);offset+=8
        tiles[(tx,tz)]=np.frombuffer(data,dtype='<f4',count=cells*cells,offset=offset).reshape(cells,cells).copy();offset+=cells*cells*4
    with (ROOT/'binaries/project/plan_resources/terrain_cache.bin').open('rb') as f:
        hdr=struct.unpack('<Q6If',f.read(36));f.seek(36+hdr[3]*4);xyz=np.fromfile(f,dtype='<f4',count=hdr[4]*3).reshape(hdr[6],hdr[5],3)
    changed=[]
    for iz in range(xyz.shape[0]):
        for ix in range(xyz.shape[1]):
            x,y,z=map(float,xyz[iz,ix]);dx=x-manifest['origin'][0];dz=z-manifest['origin'][2];u=dx*cs-dz*sn;v=dx*sn+dz*cs
            # Village behind the existing road. Side boundaries have a 35 m natural blend.
            weight=max(0,min(1,(145-abs(u))/35))*max(0,min(1,(-v-1)/12))*max(0,min(1,(v+185)/35))
            if weight<=0:continue
            key=(ix//cells,iz//cells)
            if key not in tiles:tiles[key]=np.zeros((cells,cells),dtype='<f4')
            old=float(tiles[key][iz%cells,ix%cells]);existing=y+old-5.9;target=BASE+level(v)-.23
            delta=old+(target-existing)*weight
            tiles[key][iz%cells,ix%cells]=delta
            changed.append(dict(x=x,z=z,before=existing,after=y+delta-5.9))
    header[-1]=len(tiles)
    with path.open('wb') as f:
        f.write(struct.pack('<II4fII',*header))
        for (tx,tz),tile in sorted(tiles.items()):f.write(struct.pack('<ii',tx,tz));f.write(tile.astype('<f4').tobytes())
    (OUT/'terrain-change.json').write_text(json.dumps(dict(changed_cells=len(changed),cells=changed),indent=2))

def main():
    source=WORLD.read_text(encoding='utf-8');root=ET.fromstring(source,parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
    old=next(e for e in root.iter('Entity') if e.get('name')=='Exo Chora')
    original=copy.deepcopy(old)
    backup=OUT/'sources/plan_before_exo_chora.world'
    if not backup.exists():backup.write_bytes(WORLD.read_bytes())
    if not (OUT/'sources/original_location.xml').exists():ET.ElementTree(old).write(OUT/'sources/original_location.xml',encoding='utf-8',xml_declaration=True)
    for ch in list(old):
        if ch.get('name')!='pin_exo_chora':old.remove(ch)
        else:
            for label in ch.findall('Entity'):label.set('active','false')
    village=entity(old,'Exo Chora - Olive Square',manifest['origin'],yaw=manifest['yaw']);village.set('tags','authored_village,location_exo_chora')
    grouping={};geometry_by_group={}
    for m in manifest['meshes']:
        group=m['group']
        if group not in grouping:grouping[group]=entity(village,group)
        local_mesh=OUT/'meshes'/(m['name']+'.mesh');source_mesh=ROOT/'binaries'/m['mesh_path']
        assert source_mesh.exists(),source_mesh
        if not local_mesh.exists():shutil.copy2(source_mesh,local_mesh)
        e=entity(grouping[group],m['material']);bind(e,local_mesh.relative_to(ROOT/'binaries').as_posix(),m['material'],distance=1800 if m['material'] in ['roof','lime','ochre','rose','sage_plaster','stone','paving','soil','trim'] else 400,shadow=180)
        geometry_by_group.setdefault(group,[]).append(e)
        # Exact static meshes for walls and walking surfaces, with openings preserved.
        if m['material'] in ['lime','ochre','rose','sage_plaster','stone','paving','soil'] and group!='working_details':physics(e)
    # Per-building terrain pads keep entrances grounded, even on the lower terrace.
    pads=root.find('.//terrain/platforms')
    for pad in list(pads):
        if pad.get('entity_id','').startswith('9046') or pad.get('entity_id')=='9017000000000001276':pads.remove(pad)
    for b in manifest['buildings']:
        group=grouping[b['name']];p=world(b['x'],b['y']-.70,b['z']);yaw=-math.radians(manifest['yaw']+b['yaw']);hx=b['width']/2+.5;hz=b['depth']/2+.5
        # Pads are associated with a renderable part so the engine can retain them.
        part=next(e for e in geometry_by_group[b['name']] if e.find('physics') is not None)
        extx=abs(math.cos(yaw))*hx+abs(math.sin(yaw))*hz;extz=abs(math.sin(yaw))*hx+abs(math.cos(yaw))*hz
        ET.SubElement(pads,'platform',entity_id=part.get('id'),min_x=str(p[0]-extx),max_x=str(p[0]+extx),min_z=str(p[2]-extz),max_z=str(p[2]+extz),center_x=str(p[0]),center_z=str(p[2]),half_x=str(hx),half_z=str(hz),yaw=str(yaw),height=str(p[1]),margin='2.0')
    # Reuse the existing authored olive assets, preserving their PBR bark and foliage.
    tree_group=entity(village,'Olive trees')
    for i,t in enumerate(manifest['trees']):
        name='olive_0'+str(t['variant']);e=entity(tree_group,'Ancient olive' if t['hero'] else 'Olive '+str(i),(t['x'],t['y'],t['z']),(t['scale'],)*3,yaw=i*47)
        for sub,mat in [(0,'foliage'),(1,'bark')]:
            part=entity(e,mat)
            ET.SubElement(part,'render',mesh_name=name,mesh_path=f'project/exo_chora/meshes/{name}.mesh',sub_mesh_index=str(sub),material_name=name+'_olive_'+mat,material_path=f'project/models/island_biomes/{name}/{name}_olive_{mat}.xml',material_default='false',flags='1',max_render_distance='1600',max_shadow_distance='160')
        trunk=entity(e,'Trunk collision',(0,1.8,0),(.65,3.6,.65));physics(trunk,0)
    lamp_group=entity(village,'Warm village lighting')
    for lamp in manifest['lights']:
        e=entity(lamp_group,lamp['name'],lamp['position'])
        ET.SubElement(e,'light',flags='1',light_type='1',color_r='1',color_g='.74',color_b='.43',temperature='3000',intensity='8',intensity_photometric=str(lamp['lumens']),range=str(lamp['range']),angle='.52359879',index='0',preset='0',area_width='1',area_height='1',draw_distance='160',distance_shadows='60',distance_volumetric='25')
    # Reusable visit information; the main player spawn remains at home.
    marker=entity(village,'Visit - Olive Square',(-8,2,-43));marker.set('tags','visit_point')
    # Native road foundations let terrain scatter clear the custom cobbled lanes.
    # They sit below the authored walking surface, without changing terrain height.
    roads=entity(village,'Village road foundations');roads.set('tags','village_roads')
    road_template=next(e.find('spline') for e in root.iter('Entity') if e.get('name')=='r005_machairado_anafonitria')
    paths=[('Olive lane '+str(i+1),[(x,level(z)-.45,z) for z in [-12,-60,-70,-95,-112,-147]]) for i,x in enumerate([-40,43,-78,80])]
    paths += [('Cross lane '+str(i+1),[(x,level(z)-.45,z) for x in [-83,-40,0,43,83]]) for i,z in enumerate([-72,-112])]
    for name,points in paths:
        e=entity(roads,name);s=copy.deepcopy(road_template)
        for ch in list(s):s.remove(ch)
        s.attrib.update(road_width='5.8',road_width_end='5.8',resolution='12',sidewalk_enabled='false',conform_to_terrain='false',terrain_offset='0',grade_limit_enabled='false',embankment_enabled='false',carve_terrain='false',thickness='.03',smoothing_length='0')
        e.append(s)
        for i,p in enumerate(points):entity(e,'spline_point_'+str(i),p)
    prefab=ET.Element('Prefab')
    for ch in roads:prefab.append(copy.deepcopy(ch))
    ET.ElementTree(prefab).write(OUT/'sources/road_foundations.prefab',encoding='utf-8',xml_declaration=True)
    materials();terraform()
    # Only replace the location block and the existing platforms block in the original XML text.
    def serialized(e,indent):
        e=copy.deepcopy(e);ET.indent(e,space=' ',level=indent);return ET.tostring(e,encoding='unicode').rstrip()
    def span_for_name(text,name):
        start=text.index('<Entity name="'+name+'"');pos=start;depth=0
        import re
        for match in re.finditer(r'<Entity\b[^>]*>|</Entity>',text[start:]):
            tag=match.group();depth+=-1 if tag.startswith('</') else 0 if tag.endswith('/>') else 1
            if depth==0:return start,start+match.end()
        raise ValueError(name)
    a,b=span_for_name(source,'Exo Chora');updated=source[:a]+serialized(old,3)+source[b:]
    a=updated.index('<platforms>');b=updated.index('</platforms>',a)+len('</platforms>');updated=updated[:a]+serialized(pads,5)+updated[b:]
    check=ET.fromstring(updated);ids=[e.get('id') for e in check.iter('Entity')];assert len(ids)==len(set(ids))
    assert WORLD.read_text(encoding='utf-8')==source,'World changed during village installation'
    WORLD.write_text(updated,encoding='utf-8')
    # The user's Dropbox backup covers project, so preserve the integrated world
    # there too, alongside the original-world rollback copy.
    (OUT/'sources/plan_with_exo_chora.world').write_text(updated,encoding='utf-8')
    ET.ElementTree(village).write(OUT/'village.xml',encoding='utf-8',xml_declaration=True)
    for name in ['build_exo_chora.py','preview_exo_chora.py','install_exo_chora.py','fetch_village_textures.py','import_village_meshes.mjs','village_bridge.mjs','review_exo_chora.mjs','inspect_village_site.py']:
        src=Path(__file__).resolve().parent/name;dst=OUT/'sources'/name
        if src.resolve()!=dst.resolve():shutil.copy2(src,dst)
    print(json.dumps(dict(buildings=len(manifest['buildings']),render_meshes=len(manifest['meshes']),triangles=sum(m['triangles'] for m in manifest['meshes']),entities=n,visit_position=world(-8,2,-43),changed_world=WORLD.as_posix()),indent=2))

if __name__=='__main__':main()
