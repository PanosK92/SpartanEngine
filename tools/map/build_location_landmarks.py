"""Build modest, editable Zakynthos landmark clusters in plan.world.

Run from any directory: python tools/map/build_location_landmarks.py [--apply].
Materials, backup, placement manifest and reusable XML live in binaries/project.
Generated clusters and pads are replaced by tag; location grouping is retained.
Requires numpy (also used by the map tools).
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path
import struct
import xml.etree.ElementTree as ET

import numpy as np
from organize_locations import organize, strip_generated

ROOT = Path(__file__).resolve().parents[2]
WORLD = ROOT / 'worlds/plan.world'
OUT = ROOT / 'binaries/project/zakynthos_landmarks'
BEGIN = '  <!-- ZAKYNTHOS LANDMARK BLOCKOUT BEGIN -->'
END = '  <!-- ZAKYNTHOS LANDMARK BLOCKOUT END -->'
PAD_BEGIN = '<!-- ZAKYNTHOS LANDMARK PADS BEGIN -->'
PAD_END = '<!-- ZAKYNTHOS LANDMARK PADS END -->'
# These are deliberately evocative blockouts, not surveyed replicas.
SITES = {
 'port': ('church', 'Harbour church and bell tower'),
 'bochali': ('castle', 'Venetian castle gate and lookout cafe'),
 'argassi': ('resort', 'Seaside taverna and guesthouse'),
 'xirokastello': ('village', 'Stone village houses'),
 'agnadi': ('lookout', 'View terrace and taverna'),
 'banana': ('beach', 'Beach shelter and refreshment kiosk'),
 'vasilikos': ('resort', 'Low village guesthouse and taverna'),
 'porto_roma': ('harbour', 'Small harbour taverna and boat shed'),
 'gerakas': ('beach', 'Turtle beach information shelter'),
 'kalamaki': ('beach', 'Turtle beach shelter and low taverna'),
 'laganas': ('resort', 'Resort guesthouse and cafe'),
 'agios_sostis': ('harbour', 'Coastal chapel and boat shed'),
 'lithakia': ('village', 'Traditional stone house and olive workshop'),
 'keri_lake': ('harbour', 'Lakeside fishing village taverna'),
 'keri': ('church', 'Keriotissa-inspired village church'),
 'keri_light': ('lighthouse', 'Keri lighthouse and keeper cottage'),
 'machairado': ('church', 'Agia Mavra-inspired church and campanile'),
 'mouzaki': ('village', 'Village houses and workshop'),
 'tsilivi': ('resort', 'Guesthouse and seaside cafe'),
 'tragaki': ('church', 'Village chapel and stone house'),
 'alykes': ('resort', 'Low salt-flat resort buildings'),
 'katastari': ('village', 'Village square houses'),
 'xigia': ('beach', 'Cove shelter and small kiosk'),
 'makris_gialos': ('beach', 'Beach taverna and shade shelter'),
 'agios_nikolaos': ('harbour', 'Fishing port chapel and boat shed'),
 'skinari': ('windmill', 'Skinari windmill and keeper cottage'),
 'volimes': ('market', 'Mountain craft shop and stone houses'),
 'anafonitria': ('monastery', 'Anafonitria-inspired monastery and gate tower'),
 'navagio_view': ('lookout', 'Clifftop viewing shelter'),
 'porto_vromi': ('harbour', 'Boat excursion kiosk and fishing shed'),
 'maries': ('church', 'Village church and stone house'),
 'exo_chora': ('olive', 'Old olive tree square and stone cafe'),
 'kampi': ('cross', 'Sunset cross and lookout taverna'),
 'agios_leon': ('church', 'Village church with round bell tower'),
 'kiliomeno': ('church', 'Stone church and detached bell tower'),
 'agalas': ('village', 'Stone village house and taverna'),
 'gyri': ('village', 'Mountain stone cottages'),
}
COLORS = {'plaster':(.82,.77,.65), 'white':(.92,.90,.81),
 'stone':(.48,.40,.29), 'terracotta':(.45,.15,.07), 'blue':(.045,.19,.28),
 'wood':(.20,.105,.045), 'glass':(.065,.14,.17), 'metal':(.075,.085,.085),
 'leaf':(.20,.27,.115), 'ochre':(.66,.40,.16)}

def vec(v): return ' '.join(f'{float(x):.5f}' for x in v)

class Builder:
 def __init__(self): self.n = 0
 def entity(self, parent, name, p=(0,0,0), s=(1,1,1), angle=0, axis='y'):
  self.n += 1
  q=[0,0,0,math.cos(angle/2)]; q['xyz'.index(axis)]=math.sin(angle/2)
  return ET.SubElement(parent,'Entity',name=name,id=str(9017000000000000000+self.n),
    active='true',position=vec(p),rotation=vec(q),scale=vec(s))
 def part(self,parent,name,p,s,mat='plaster',mesh='cube',angle=0,axis='y',solid=False):
  e=self.entity(parent,name,p,s,angle,axis)
  ET.SubElement(e,'render',mesh_name='standard_'+mesh,mesh_path='',sub_mesh_index='0',
    material_name='zante_'+mat,material_path=f'project/zakynthos_landmarks/materials/zante_{mat}.xml',
    material_default='false',flags='1',max_render_distance='1600',max_shadow_distance='250')
  if solid: ET.SubElement(e,'physics',mass='0',is_static='true',is_kinematic='false',friction='0.7',restitution='0',body_type='0')
  return e
 def roof(self,e,w,d,h):
  # Two sloping solid slabs, with a ridge cap. Native cubes keep everything editable.
  pitch=math.radians(24); length=(w/2+.45)/math.cos(pitch)
  for side in [-1,1]:
   self.part(e,'clay_roof', (side*w/4,h+.65,d*0), (length,.26,d+.9),'terracotta',angle=-side*pitch,axis='z')
  self.part(e,'ridge',(0,h+.65+w/4*math.tan(pitch),0),(.32,.30,d+1),'terracotta')
 def house(self,e,name,x,z,w=9,d=7,h=4.6,mat='plaster',awning=False):
  b=self.entity(e,name,(x,0,z))
  self.part(b,'walls',(0,h/2,0),(w,h,d),mat,solid=True)
  self.part(b,'stone_base',(0,.25,0),(w+.2,.5,d+.2),'stone')
  self.roof(b,w,d,h)
  self.part(b,'door',(0,1.25,-d/2-.07),(1.25,2.5,.16),'wood')
  for side in [-1,1]:
   for yy in ([2] if h<6 else [2,5.1]):
    xx=side*w*.30
    self.part(b,'window_surround',(xx,yy,-d/2-.09),(1.7,1.7,.18),'white')
    self.part(b,'window',(xx,yy,-d/2-.20),(1.2,1.3,.08),'glass')
    for k in [-1,1]: self.part(b,'blue_shutter',(xx+k*.76,yy,-d/2-.23),(.34,1.45,.10),'blue')
  self.part(b,'chimney',(-w*.3,h+1.3,d*.22),(.7,1.8,.8),'stone')
  if awning:
   self.part(b,'canvas_awning',(0,3,-d/2-1.8),(w+.6,.18,3.6),'ochre')
   for xx in [-w/2,w/2]: self.part(b,'awning_post',(xx,1.5,-d/2-3.4),(.16,3,.16),'wood')
  return b
 def tower(self,e,x,z,round=False):
  self.part(e,'bell_tower',(x,4.3,z),(2 if round else 3.6,8.6,2 if round else 3.6),'stone',mesh='cylinder' if round else 'cube',solid=True)
  for dx in [-1.4,1.4]:
   for dz in [-1.4,1.4]:self.part(e,'belfry_pier',(x+dx,10,z+dz),(.55,2.8,.55),'white')
  self.part(e,'bell',(x,10,z),(.6,.9,.6),'metal',mesh='sphere')
  self.part(e,'belfry_roof',(x,11.6,z),(4.2,.5,4.2),'terracotta')
  self.cross(e,x,13,z,1.5)
 def cross(self,e,x,y,z,size=4):
  self.part(e,'cross_upright',(x,y,z),(.33,size,.36),'white')
  self.part(e,'cross_arms',(x,y+size*.18,z),(size*.6,.33,.36),'white')
 def shelter(self,e,x,z):
  for dx in [-3,3]:
   for dz in [-2,2]:self.part(e,'timber_post',(x+dx,1.6,z+dz),(.22,3.2,.22),'wood')
  for dz in np.linspace(-2.4,2.4,8): self.part(e,'shade_beam',(x,3.2,z+dz),(7,.20,.32),'wood')
  self.part(e,'bench',(x,.6,z+1),(4,.23,.6),'blue')
  self.part(e,'information_board',(x,1.7,z+1.8),(2,1.1,.13),'blue')
 def cluster(self,e,kind,key):
  if kind in ['lighthouse','windmill']:
   self.part(e,'white_tower',(0,6,0),(2.8,12,2.8),'white',mesh='cylinder',solid=True)
   if kind=='lighthouse':
    self.part(e,'lantern_balcony',(0,11.8,0),(3.6,.4,3.6),'stone',mesh='cylinder')
    self.part(e,'lantern_glazing',(0,13,0),(1.65,2.2,1.65),'glass',mesh='cylinder')
    for a in np.linspace(0,math.tau,12,endpoint=False):
     self.part(e,'balcony_railing',(3.2*math.cos(a),12.5,3.2*math.sin(a)),(.10,1.2,.10),'metal')
     self.part(e,'lantern_frame',(1.6*math.cos(a),13,1.6*math.sin(a)),(.10,2.2,.10),'white')
    self.part(e,'lantern_cap',(0,14.6,0),(2.1,.65,2.1),'terracotta',mesh='cone')
   else:
    self.part(e,'mill_cap',(0,12.8,0),(3.2,1.5,3.2),'terracotta',mesh='cone')
    for a in [0,math.pi/3,2*math.pi/3]: self.part(e,'windmill_sail',(0,9,-3),(10,.35,.20),'white',angle=a,axis='z')
   self.part(e,'tower_door',(0,1.3,-2.8),(1.3,2.6,.18),'blue')
   self.house(e,'keeper_cottage',12,3,8,6,4)
  elif kind in ['church','monastery']:
   self.house(e,'church_nave',-3,2,10,14,6,'stone' if key=='kiliomeno' else 'white')
   self.cross(e,-3,9,-5,2)
   self.tower(e,9,0,round=key=='agios_leon')
   self.house(e,'monastery_wing' if kind=='monastery' else 'village_house',-4,18,14 if kind=='monastery' else 8,6,4,'stone')
  elif kind=='castle':
   for x in [-8,8]:
    self.part(e,'gate_tower',(x,4,2),(6,8,7),'stone',solid=True)
    for dx in [-2,0,2]: self.part(e,'battlement',(x+dx,8.4,-1),(1,1,1),'stone')
   self.part(e,'gate_lintel',(0,6.5,2),(10,2,5),'stone')
   self.house(e,'lookout_cafe',0,15,10,7,4,awning=True)
  elif kind in ['lookout','cross']:
   self.shelter(e,-7,0)
   self.house(e,'view_taverna',7,8,9,7,4,awning=True)
   if kind=='cross':self.cross(e,-7,6,9,9)
  elif kind=='beach':
   self.shelter(e,-7,0)
   self.house(e,'beach_kiosk',6,5,7,5,3.5,awning=True)
  elif kind=='olive':
   self.part(e,'old_olive_trunk',(-8,2.5,0),(1.2,5,1.1),'wood',mesh='cylinder')
   for x,y,z in [(-8,6,0),(-11,5,1),(-5,5,-1)]:self.part(e,'olive_crown',(x,y,z),(3.2,2.2,2.8),'leaf',mesh='sphere')
   self.house(e,'square_cafe',6,8,11,7,4,'stone',True)
  else:
   self.house(e,'taverna' if kind!='village' else 'stone_house',-7,3,10,8,6.8 if kind=='resort' else 4.5,'plaster' if kind=='resort' else 'stone',True)
   self.house(e,'boat_shed' if kind=='harbour' else 'craft_shop' if kind=='market' else 'village_cottage',8,10,8,6,4,'ochre')
   if kind=='market':self.shelter(e,7,-3)

def main(apply=False):
 source=WORLD.read_text(encoding='utf-8'); clean=source
 organized='location_root' in source
 if organized: clean=strip_generated(source)
 if BEGIN in clean: clean=clean[:clean.index(BEGIN)]+clean[clean.index(END)+len(END)+1:]
 clean=re.sub(re.escape(PAD_BEGIN)+r'.*?'+re.escape(PAD_END),'',clean,flags=re.S)
 root=ET.fromstring(clean)
 pins={e.get('name','')[4:]:e for e in root.iter('Entity') if e.get('name','').startswith('pin_')}
 assert set(SITES)<=pins.keys()
 # Cached geometry is the eroded ground, before local road carving; use it when available.
 cache=ROOT/'binaries/project/plan_resources/terrain_cache.bin'
 with cache.open('rb') as f:
  hdr=struct.unpack('<Q6If',f.read(36)); f.seek(36+hdr[3]*4)
  xyz=np.fromfile(f,dtype='<f4',count=hdr[4]*3).reshape(hdr[6],hdr[5],3)
 heights=xyz[:,:,1]; H,W=heights.shape
 sculpt=ROOT/'binaries/project/plan_resources/terrain_sculpt.bin'
 if sculpt.exists():
  with sculpt.open('rb') as f:
   magic,version,ox,oz,cx,cz,cells,count=struct.unpack('<II4fII',f.read(32))
   assert magic==0x4c435053 and version==1 and cells==64
   assert abs(cx-25)<.01 and abs(cz-25)<.01
   for _ in range(count):
    tx,tz=struct.unpack('<ii',f.read(8)); tile=np.frombuffer(f.read(cells*cells*4),dtype='<f4').reshape(cells,cells)
    x0,z0=tx*cells,tz*cells
    if x0>=0 and z0>=0 and x0<W and z0<H:
     heights[z0:min(z0+cells,H),x0:min(x0+cells,W)]+=tile[:min(cells,H-z0),:min(cells,W-x0)]
 platforms=list(root.iter('platform'))
 def ground(x,z):
  for pad in platforms:
   dx=x-float(pad.get('center_x','0')); dz=z-float(pad.get('center_z','0')); yaw=float(pad.get('yaw','0'))
   if abs(dx*math.cos(yaw)+dz*math.sin(yaw))<=float(pad.get('half_x','0')) and abs(-dx*math.sin(yaw)+dz*math.cos(yaw))<=float(pad.get('half_z','0')):
    return float(pad.get('height'))
  px=np.clip((x-float(xyz[0,0,0]))/25,0,W-1.001); pz=np.clip((z-float(xyz[0,0,2]))/25,0,H-1.001)
  ix,iz=int(px),int(pz); u,v=px-ix,pz-iz
  return float((heights[iz,ix]*(1-u)+heights[iz,ix+1]*u)*(1-v)+(heights[iz+1,ix]*(1-u)+heights[iz+1,ix+1]*u)*v)-5.9
 starts=[]; ends=[]; widths=[]
 for e in root.iter('Entity'):
  s=e.find('spline')
  if s is None or s.get('profile')!='0':continue
  assert e.get('rotation')=='0 0 0 1' and e.get('scale')=='1 1 1'
  p=np.array([float(v) for v in e.get('position').split()])
  pts=[p+np.array([float(v) for v in c.get('position').split()]) for c in e.findall('Entity') if c.get('name','').startswith('spline_point_')]
  for a,b in zip(pts,pts[1:]):
   starts.append(a[[0,2]]);ends.append(b[[0,2]]);widths.append(max(float(s.get('road_width','10')),float(s.get('road_width_end','10')))/2)
 a=np.array(starts); delta=np.array(ends)-a; lens=np.maximum(np.sum(delta*delta,axis=1),.0001); widths=np.array(widths)
 def clearance(x,z):
  p=np.array([x,z]); t=np.clip(np.sum((p-a)*delta,axis=1)/lens,0,1)
  return float(np.min(np.linalg.norm(p-a-t[:,None]*delta,axis=1)-widths))
 b=Builder(); holder=ET.Element('Entities'); group=b.entity(holder,'zakynthos_location_landmarks')
 group.set('tags','zakynthos_landmarks,blockout')
 manifest=[]; authored_pads=[]
 for key,(kind,description) in SITES.items():
  px,py,pz=map(float,pins[key].get('position').split()); choices=[]
  for radius in [45,70,100,140,190,260,350,470,600,800]:
   for angle in np.linspace(0,math.tau,40,endpoint=False):
    x,z=px+radius*math.cos(angle),pz+radius*math.sin(angle)
    gap=clearance(x,z)
    if gap<65:continue # room for footprint, pavement, splines' curvature and shoulders
    if any(math.hypot(x-float(pad.get('center_x','0')),z-float(pad.get('center_z','0'))) < math.hypot(float(pad.get('half_x','0')),float(pad.get('half_z','0')))+35 for pad in platforms):continue
    levels=[ground(x+dx,z+dz) for dx in [-19,0,19] for dz in [-19,0,23]]
    if min(levels)<1.0 or (key=='navagio_view' and min(levels)<40):continue
    relief=max(levels)-min(levels)
    if relief>9:continue
    if any(math.hypot(x-m['position'][0],z-m['position'][2])<65 for m in manifest):continue
    choices.append((radius+relief*40+max(0,gap-100)*.3,x,z,levels,gap))
   if choices and radius>=190:break
  if not choices:raise RuntimeError(f'No suitable dry, road-clear site for {key}')
  _,x,z,levels,gap=min(choices,key=lambda c:c[0]); y=float(np.mean(levels))+.15
  e=b.entity(group,'landmark_'+key,(x,y,z));e.set('tags','landmark_blockout,location_'+key)
  depth=.7
  floor=b.part(e,'stone_terrace',(0,-depth/2,2),(38,depth,42),'stone',solid=True)
  authored_pads.append(ET.Element('platform',entity_id=floor.get('id'),min_x=str(x-19),max_x=str(x+19),min_z=str(z-19),max_z=str(z+23),center_x=str(x),center_z=str(z+2),half_x='19',half_z='21',yaw='0',height=str(y-.08),margin='6'))
  b.cluster(e,kind,key)
  manifest.append(dict(location=key,concept=description,kind=kind,position=[round(x,4),round(y,4),round(z,4)],marker_position=[px,py,pz],marker_offset=round(math.hypot(x-px,z-pz),2),road_edge_clearance=round(gap,2),terrain_relief=round(max(levels)-min(levels),3)))
 ET.indent(holder,space=' '); block=ET.tostring(group,encoding='unicode')
 updated=clean.replace(' </Entities>',BEGIN+'\n'+block+'\n'+END+'\n </Entities>')
 pad_text=PAD_BEGIN+''.join(ET.tostring(p,encoding='unicode') for p in authored_pads)+PAD_END
 if '</platforms>' in updated: updated=updated.replace('</platforms>',pad_text+'</platforms>',1)
 else: updated=re.sub(r'<platforms\s*/>',lambda m:'<platforms>'+pad_text+'</platforms>',updated,count=1)
 assert PAD_BEGIN in updated
 assert updated!=clean
 updated,_=organize(updated)
 result=ET.fromstring(updated); ids=[e.get('id') for e in result.iter('Entity')]
 assert len(ids)==len(set(ids)), 'Entity ID collision'
 assert len(group.findall('Entity'))==len(SITES)
 if apply:
  OUT.mkdir(parents=True,exist_ok=True);(OUT/'materials').mkdir(exist_ok=True)
  backup=OUT/('plan_before_landmarks_'+hashlib.sha256(source.encode()).hexdigest()[:12]+'.world')
  if not organized and BEGIN not in source and not backup.exists():backup.write_bytes(WORLD.read_bytes())
  template=ET.parse(ROOT/'binaries/project/plan_resources/gs_road_paint.xml').getroot()
  for name,rgb in COLORS.items():
   m=ET.fromstring(ET.tostring(template))
   for tag,value in zip(['color_r','color_g','color_b'],rgb):m.find(tag).text=str(value)
   m.find('roughness').text='0.28' if name=='glass' else '0.85'
   m.find('metalness').text='0.6' if name=='metal' else '0'
   ET.ElementTree(m).write(OUT/f'materials/zante_{name}.xml',encoding='utf-8',xml_declaration=True)
  (OUT/'landmarks.xml').write_text(block,encoding='utf-8')
  (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
  # Refuse to overwrite an edit made while the placement search ran.
  assert WORLD.read_text(encoding='utf-8')==source,'World changed during generation; run again'
  WORLD.write_text(updated,encoding='utf-8')
 print(json.dumps(dict(applied=apply,locations=len(manifest),entities=b.n,largest_marker_offset=max(m['marker_offset'] for m in manifest),sites=manifest),indent=2))

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--apply',action='store_true')
 main(parser.parse_args().apply)
