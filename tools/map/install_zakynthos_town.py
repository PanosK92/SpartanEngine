"""Install the town, sculpt its terrain and connect streets to the island road graph."""
from pathlib import Path
import sys,json,math,copy,struct,re,shutil
import xml.etree.ElementTree as E
import numpy as np
from shapely.geometry import LineString,Point
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(Path(__file__).parent))
from plan_zakynthos_town import weight,BASE,CORE
from install_home_garage import span
OUT=ROOT/'binaries/project/zakynthos_town';WORLD=ROOT/'worlds/plan.world'
counter=9068000000000000000

def vec(v):return ' '.join(f'{x:.6f}' for x in v)
def entity(parent,name,p=(0,0,0),yaw=0):
    global counter;counter+=1;a=math.radians(yaw)/2
    return E.SubElement(parent,'Entity',name=name,id=str(counter),active='true',position=vec(p),rotation=vec((0,math.sin(a),0,math.cos(a))),scale='1 1 1')
def physics(e):E.SubElement(e,'physics',mass='0',is_static='true',is_kinematic='false',friction='.8',restitution='0',body_type='4')
def bind(e,m):
    mat=m['material'];structural=mat in ('lime','ochre','rose','sage_plaster','stone','paving','roof','trim')
    E.SubElement(e,'render',mesh_name=m['name'],mesh_path=m['mesh_path'],sub_mesh_index='0',material_name='town_'+mat,material_path=f'project/zakynthos_town/materials/town_{mat}.xml',material_default='false',flags='13',max_render_distance='1700' if structural else '180',max_shadow_distance='140')
    if mat in ('lime','ochre','rose','sage_plaster','stone','paving','wood'):physics(e)

def terraform():
    path=ROOT/'binaries/project/plan_resources/terrain_sculpt.bin';data=path.read_bytes();header=list(struct.unpack_from('<II4fII',data));cells,count=header[-2:];offset=32;tiles={}
    for _ in range(count):
        tx,tz=struct.unpack_from('<ii',data,offset);offset+=8;tiles[tx,tz]=np.frombuffer(data,dtype='<f4',count=cells*cells,offset=offset).reshape(cells,cells).copy();offset+=cells*cells*4
    with (ROOT/'binaries/project/plan_resources/terrain_cache.bin').open('rb') as f:
        h=struct.unpack('<Q6If',f.read(36));f.seek(36+h[3]*4);xyz=np.fromfile(f,dtype='<f4',count=h[4]*3).reshape(h[6],h[5],3)
    patch=OUT/'sources/terrain_base_patch.json'
    if patch.exists():records=json.loads(patch.read_text())
    else:
        records=[]
        for iz in range(xyz.shape[0]):
            for ix in range(xyz.shape[1]):
                x,y,z=map(float,xyz[iz,ix]);w=weight(x,z)
                if w<=0:continue
                old=float(tiles[ix//cells,iz//cells][iz%cells,ix%cells]) if (ix//cells,iz//cells) in tiles else 0
                records.append([ix,iz,y,old,w])
        patch.write_text(json.dumps(records,separators=(',',':')))
    changed=[]
    for ix,iz,y,old,w in records:
        key=(ix//cells,iz//cells)
        if key not in tiles:tiles[key]=np.zeros((cells,cells),dtype='<f4')
        before=y+old-5.9;after=before+(BASE-before)*w;tiles[key][iz%cells,ix%cells]=after-y+5.9;changed.append((before,after))
    header[-1]=len(tiles)
    with path.open('wb') as f:
        f.write(struct.pack('<II4fII',*header))
        for (tx,tz),tile in sorted(tiles.items()):f.write(struct.pack('<ii',tx,tz));f.write(tile.astype('<f4').tobytes())
    print('Terrain sculpted:',len(records),'samples; core elevation',BASE,'m; 160 m feather')

def main():
    source=WORLD.read_text();root=E.fromstring(source);manifest=json.loads((OUT/'manifest.json').read_text());plan=manifest['plan']
    for m in manifest['meshes']:assert (ROOT/'binaries'/m['mesh_path']).exists(),m['mesh_path']
    location=next(e for e in root.iter('Entity') if e.get('name')=='Zakynthos Town');oldids=set();label=None
    for child in list(location):
        if child.get('name','').startswith('city_grid_1km') or child.get('name') in ('landmark_port','Zakynthos Town - city'):
            oldids.update(e.get('id') for e in child.iter('Entity'))
            if child.get('name','').startswith('city_grid_1km'):
                label=copy.deepcopy(child.find("Entity[@name='map_label']"));p=list(map(float,child.get('position').split()));q=list(map(float,label.get('position').split()));label.set('position',vec([p[i]+q[i] for i in range(3)]))
            elif child.get('name')=='Zakynthos Town - city':label=copy.deepcopy(child.find("Entity[@name='map_label']"))
            location.remove(child)
    town=entity(location,'Zakynthos Town - city');town.set('tags','authored_city,location_port,landmark')
    if label is not None:town.append(label)
    bygroup={}
    for m in manifest['meshes']:bygroup.setdefault(m['group'],[]).append(m)
    pads=root.find('.//terrain/platforms')
    for p in list(pads):
        if p.get('entity_id') in oldids:pads.remove(p)
    placements=plan['buildings']+[dict(name=p['name'],prototype='civic' if i==0 else 'market',x=p['x'],z=p['z'],w=p['w'],d=p['d'],yaw=0) for i,p in enumerate(plan['plazas'])]
    for b in placements:
        e=entity(town,b['name'],(b['x'],BASE+.35,b['z']),b['yaw']);anchor=None
        for m in bygroup[b['prototype']]:
            child=entity(e,m['material']);bind(child,m)
            if m['material']=='stone':anchor=child
        # The town is already sculpted flat; per-building refinement pads would
        # redundantly tessellate hundreds of flat lots and exhaust the GPU meshlet budget.
    landscaping=entity(town,'Street trees and lamps')
    for i,t in enumerate(plan['trees']):
        tree=entity(landscaping,'Street olive',(t['x'],BASE+.05,t['z']),i*137.5);tree.set('scale',vec([t['scale']]*3))
        for sub,mat in ((0,'foliage'),(1,'bark')):
            c=entity(tree,mat);E.SubElement(c,'render',mesh_name='olive_01',mesh_path='project/exo_chora/meshes/olive_01.mesh',sub_mesh_index=str(sub),material_name='olive_01_olive_'+mat,material_path='project/models/island_biomes/olive_01/olive_01_olive_'+mat+'.xml',material_default='false',flags='13',max_render_distance='700',max_shadow_distance='100')
        if i%3==0:
            lamp=entity(landscaping,'Town lantern',(t['x']+1,BASE+.15,t['z']))
            for m in bygroup['lamp']:bind(entity(lamp,m['material']),m)
            l=entity(lamp,'Warm street light',(0,6.15,0));E.SubElement(l,'light',flags='1',light_type='1',color_r='1',color_g='.78',color_b='.54',temperature='3200',intensity='8',intensity_photometric='850',range='14',angle='.523599',index='0',preset='0',area_width='1',area_height='1',draw_distance='95',distance_shadows='25',distance_volumetric='15')
    # Save original road entities as editable source, avoiding cumulative grade changes on rebuild.
    surveyed=json.loads((OUT/'sources/site_roads.json').read_text());original=OUT/'sources/original_roads.xml'
    if not original.exists():
        holder=E.Element('Roads');ids={r['id'] for r in surveyed}
        for e in root.iter('Entity'):
            if e.get('id') in ids:holder.append(copy.deepcopy(e))
        E.ElementTree(holder).write(original,encoding='utf-8')
    originals={e.get('id'):e for e in E.parse(original).getroot()};roads=[]
    for r in surveyed:roads.append(dict(r,entity=copy.deepcopy(originals[r['id']]),new=False,line=LineString(r['xy']),nodes=[]))
    template=copy.deepcopy(next(iter(originals.values())))
    streets=entity(town,'Connected town streets')
    for r in plan['roads']:
        e=entity(streets,r['name'],(r['xy'][0][0],BASE+.25,r['xy'][0][1]));e.set('tags','road,map_road,town_street');physics(e)
        render=copy.deepcopy(template.find('render'));e.append(render)
        s=copy.deepcopy(template.find('spline'));e.append(s)
        for c in list(s):s.remove(c)
        s.attrib.update(road_width=str(r['width']),road_width_end=str(r['width']),sidewalk_enabled='true',sidewalk_width='2.5',conform_to_terrain='false',grade_limit_enabled='false',carve_terrain='false',embankment_enabled='false',resolution='16')
        roads.append(dict(r,entity=e,new=True,line=LineString(r['xy']),nodes=[]))
    intersections={}
    for i,a in enumerate(roads):
        for b in roads[i+1:]:
            if not(a['new'] or b['new']):continue
            hit=a['line'].intersection(b['line']);points=[hit] if hit.geom_type=='Point' else list(hit.geoms) if hit.geom_type=='MultiPoint' else []
            for p in points:
                key=(round(p.x,2),round(p.y,2));tag=intersections.setdefault(key,'road_node_town_'+str(len(intersections)))
                for r in (a,b):r['nodes'].append((r['line'].project(p),p.x,p.y,tag))
    replacements=[]
    for r in roads:
        e=r['entity'];origin=list(map(float,e.get('position').split()));line=r['line'];points=[]
        if r['new']:
            for x,z in r['xy']:points.append((line.project(Point(x,z)),x,z,BASE+.25,None))
        else:
            for c in e.findall('Entity'):
                if not c.get('name','').startswith('spline_point_'):continue
                p=[origin[i]+float(v) for i,v in enumerate(c.get('position').split())];w=weight(p[0],p[2]);y=p[1]+(BASE+.25-p[1])*w;points.append((line.project(Point(p[0],p[2])),p[0],p[2],y,c))
                e.remove(c)
        for distance,x,z,tag in r['nodes']:
            # Reuse a nearby node, otherwise insert a junction into the curve.
            nearest=min(points,key=lambda p:abs(p[0]-distance))
            if abs(nearest[0]-distance)<.2:
                points.remove(nearest);child=nearest[4]
            else:child=None
            if child is None:child=entity(E.Element('tmp'),'spline_point_new')
            tags=[v for v in child.get('tags','').split(',') if v]
            # Existing graph membership wins when a new street reaches an existing junction.
            oldtag=next((t for t in tags if t.startswith('road_node_')),None)
            if oldtag and oldtag!=tag:
                for road in roads:
                    road['nodes']=[(d,xx,zz,oldtag if t==tag else t) for d,xx,zz,t in road['nodes']]
            else:tags.insert(0,tag)
            child.set('tags',','.join(dict.fromkeys(tags)));points.append((distance,x,z,BASE+.25,child))
        for i,(_,x,z,y,c) in enumerate(sorted(points,key=lambda p:p[0])):
            if c is None:c=entity(E.Element('tmp'),'spline_point_'+str(i))
            c.set('name','spline_point_'+str(i));c.set('position',vec((x-origin[0],y-origin[1],z-origin[2])));e.append(c)
        if not r['new']:
            oldname=e.get('name');a,b=span(source,oldname);E.indent(e,space=' ',level=5);replacements.append((a,b,E.tostring(e,encoding='unicode').rstrip()))
    a,b=span(source,'Zakynthos Town');E.indent(location,space=' ',level=3);replacements.append((a,b,E.tostring(location,encoding='unicode').rstrip()))
    a=source.index('<platforms>');b=source.index('</platforms>',a)+len('</platforms>');E.indent(pads,space=' ',level=5);replacements.append((a,b,E.tostring(pads,encoding='unicode').rstrip()))
    # Roads live outside the town hierarchy. No overlapping text replacements are permitted.
    replacements.sort(reverse=True)
    for i,(a,b,_) in enumerate(replacements):
        if i:assert b<=replacements[i-1][0]
    updated=source
    for a,b,text in replacements:updated=updated[:a]+text+updated[b:]
    check=E.fromstring(updated);ids=[e.get('id') for e in check.iter('Entity')];assert len(ids)==len(set(ids))
    assert WORLD.read_text()==source,'World changed during installation'
    terraform();WORLD.write_text(updated)
    for name in ('plan_zakynthos_town.py','build_zakynthos_town.py','install_zakynthos_town.py','import_zakynthos_town.mjs'):
        p=ROOT/'tools/map'/name
        if p.exists():shutil.copyfile(p,OUT/'sources'/name)
    (OUT/'sources/town.xml').write_text(E.tostring(location,encoding='unicode'))
    print('TOWN INSTALLED:',len(placements),'buildings/civic spaces;',len(intersections),'connected junctions;',len(list(town.iter('Entity'))),'entities')

if __name__=='__main__':main()
