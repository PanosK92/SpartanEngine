"""Organize plan.world into editor layers while preserving every world transform.

No spline control point order, prefab child, component, or entity ID is changed.
Run with --apply; safe to repeat after the map/content generators.
"""
import argparse
import copy
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
WORLD=ROOT/'worlds/plan.world'
BASE=9027000000000000000

def matrix(e):
    p=np.array(list(map(float,e.get('position','0 0 0').split())))
    q=np.array(list(map(float,e.get('rotation','0 0 0 1').split())))
    s=np.array(list(map(float,e.get('scale','1 1 1').split())))
    q=q/np.linalg.norm(q);x,y,z,w=q
    r=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    result=np.eye(4);result[:3,:3]=r@np.diag(s);result[:3,3]=p
    return result

def snapshot(root):
    result={}
    def walk(e,parent):
        transform=parent@matrix(e)
        components=[]
        for c in e:
            if c.tag!='Entity':
                clone=copy.deepcopy(c)
                for v in clone.iter():v.tail=None;v.text=(v.text or '').strip() or None
                components.append(ET.tostring(clone))
        result[e.get('id')]=(dict(e.attrib),transform,components,
            [c.get('id') for c in e.findall('Entity') if c.get('name','').startswith('spline_point_')])
        for c in e.findall('Entity'):walk(c,transform)
    for e in root.find('Entities').findall('Entity'):walk(e,np.eye(4))
    return result

def organize(text):
    root=ET.fromstring(text,parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
    entities=root.find('Entities');before=snapshot(root)
    by_id={e.get('id'):e for e in root.iter('Entity')}
    def parent_of(e):return next(p for p in root.iter() if e in list(p))
    def identity_chain(e):
        while e.tag=='Entity':
            assert np.allclose(matrix(e),np.eye(4),atol=1e-8),f'Cannot reparent from transformed group {e.get("name")}'
            e=parent_of(e)
    def move(e,parent):
        old=parent_of(e)
        if old is parent:return
        identity_chain(old);identity_chain(parent)
        old.remove(e);parent.append(e)
    def group(index,name,parent=entities):
        key=str(BASE+index)
        e=by_id.get(key)
        if e is None:
            e=ET.SubElement(parent,'Entity',name=name,id=key,active='true',position='0 0 0',rotation='0 0 0 1',scale='1 1 1',tags='world_layer')
            by_id[key]=e
        else:move(e,parent)
        return e

    landscape=by_id['9002000000000000001']
    landscape.set('name','Landscape');move(landscape,entities)
    roads=group(1,'Roads');places=group(2,'Places');population=group(3,'Population')
    player=group(4,'Player');lighting=group(5,'Lighting');development=group(6,'Development')
    for e in list(root.iter('Entity')):
        if 'location_root' in e.get('tags','').split(','):
            move(e,player if 'location_player_home' in e.get('tags','').split(',') else places)
    for e in list(entities.findall('Entity')):
        if e.get('name') in ['player_car','physics_body_camera']:move(e,player)
    for e in list(landscape.findall('Entity')):
        name=e.get('name','')
        if name in ['terrain','ocean']:continue
        if name in ['traffic_manager','pedestrian_manager']:move(e,population)
        elif name in ['map_skeleton','Highway','spline_car']:move(e,roads)
        elif name=='light_directional' or e.find('light') is not None:move(e,lighting)
        else:move(e,development)

    sound=next(e for e in root.iter('Entity') if 'island_soundscapes' in e.get('tags','').split(','))
    sound.set('name','Soundscapes');move(sound,entities)
    beds=group(100,'Regional Beds',sound);shore=group(101,'Shoreline',sound);details=group(102,'Local Details',sound)
    district_groups={}
    clips=['highlands','north','east','plain','south','vasilikos','keri','town']
    # Move only acoustic volumes; no functional child subtrees are split apart.
    for e in list(sound.iter('Entity')):
        audio=e.find('audio_source');volume=e.find('volume')
        if audio is None or volume is None:continue
        clip=Path(audio.get('path')).stem
        if volume.get('audio_group')=='island_bed':
            if clip not in district_groups:district_groups[clip]=group(110+clips.index(clip),e.get('name'),beds)
            move(e,district_groups[clip])
        elif volume.get('audio_boundary_only')=='true':move(e,shore)
        else:move(e,details)

    layers=[landscape,roads,places,population,player,lighting,sound,development]
    # Any unclassified root is retained in Development, never silently discarded.
    for e in list(entities.findall('Entity')):
        if e not in layers:move(e,development)
    entities[:]=layers
    places[:]=sorted(places,key=lambda e:e.get('name',''))
    after=snapshot(root)
    assert set(before)<=set(after),'An entity was lost'
    for key,(attributes,transform,components,points) in before.items():
        actual=after[key]
        compare=actual[0].copy()
        if key in ['9002000000000000001',sound.get('id')]:compare['name']=attributes['name']
        assert compare==attributes,f'Entity attributes changed: {attributes.get("name")}'
        assert np.allclose(transform,actual[1],rtol=0,atol=1e-6),f'World transform changed: {attributes.get("name")}'
        assert components==actual[2],f'Component data changed: {attributes.get("name")}'
        assert points==actual[3],f'Spline point order changed: {attributes.get("name")}'
    ids=[e.get('id') for e in root.iter('Entity')];assert len(ids)==len(set(ids))
    ET.indent(root,space=' ')
    output='<?xml version="1.0"?>\n'+ET.tostring(root,encoding='unicode')+'\n'
    report=dict(root_layers=[dict(name=e.get('name'),children=len(e.findall('Entity'))) for e in layers],preserved_entities=len(before),added_groups=len(after)-len(before))
    return output,report

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--apply',action='store_true');args=parser.parse_args()
    text=WORLD.read_text(encoding='utf-8');output,report=organize(text)
    again,_=organize(output);assert again==output,'Hierarchy operation must be idempotent'
    if args.apply:
        backup=ROOT/'binaries/project/backups/plan_before_layers.world'
        if not backup.exists():shutil.copy2(WORLD,backup)
        WORLD.write_text(output,encoding='utf-8',newline='\n')
    print(json.dumps(report,indent=2))
