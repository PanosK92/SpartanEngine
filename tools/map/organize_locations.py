"""Organize plan.world by location without changing any spatial transforms.

Location parents are identity transforms, so existing coordinates and map tools
remain valid. Run with --apply to save; a backup/report goes in binaries/project.
"""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
WORLD = ROOT / 'worlds/plan.world'
OUT = ROOT / 'binaries/project/zakynthos_landmarks'
DESTINATIONS = ('port bochali argassi xirokastello agnadi banana vasilikos porto_roma '
 'gerakas kalamaki laganas agios_sostis lithakia keri_lake keri keri_light machairado '
 'mouzaki tsilivi tragaki alykes katastari xigia makris_gialos agios_nikolaos skinari '
 'volimes anafonitria navagio_view porto_vromi maries exo_chora kampi agios_leon '
 'kiliomeno agalas gyri').split()
BELONGS_TO = dict(agnadi='xirokastello', banana='vasilikos', porto_roma='vasilikos',
 gerakas='vasilikos', keri_lake='keri', keri_light='keri', navagio_view='anafonitria')
SERVICES = dict(dealer_town='port',dealer_exotic='airport',tune_shop='port',
 license_school='airport',tires='laganas',paint='argassi',wash='tsilivi',
 gas_north='katastari',gas_south='lithakia',gas_west='agios_leon',
 gas_volimes='volimes',gas_ferry='agios_nikolaos',ev_harbour='port',
 ev_laganas_drag='laganas',ev_airport_loop='airport',ev_skopos_climb='argassi',
 ev_keri_attack='keri',ev_volimes_pass='volimes',ev_north_run='tsilivi',
 ev_kampi_sunset='kampi',ev_skinari_run='skinari',ev_kiliomeno_rally='kiliomeno')
LABELS = dict(port='Zakynthos Town',airport='Airport',player_home='Player Home',
 makris_gialos='Makris Gialos',xirokastello='Xirokastello')

def parse(text):
 return ET.fromstring(text,parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))

def serialize(root):
 ET.indent(root,space=' ')
 return '<?xml version="1.0"?>\n'+ET.tostring(root,encoding='unicode')+'\n'

def is_location(e):return 'location_root' in e.get('tags','').split(',')

def strip_generated(text):
 """Remove only generated landmark clusters/pads before rebuilding in-place."""
 root=parse(text)
 for parent in list(root.iter()):
  for child in list(parent):
   if child.tag=='Entity' and 'landmark_blockout' in child.get('tags','').split(','):
    parent.remove(child)
   elif child.tag=='platform' and child.get('entity_id','').startswith('9017'):
    parent.remove(child)
   elif child.tag is ET.Comment and 'ZAKYNTHOS LANDMARK' in (child.text or ''):
    parent.remove(child)
 return serialize(root)

def organize(text):
 root=parse(text);entities=root.find('Entities')
 parents={c:p for p in root.iter() for c in p}
 original={e.get('id'):dict(e.attrib) for e in root.iter('Entity')}
 locations={next(t[9:] for t in e.get('tags','').split(',') if t.startswith('location_') and t!='location_root'):e
            for e in entities.findall('Entity') if is_location(e)}
 keys=sorted(set(BELONGS_TO.get(k,k) for k in DESTINATIONS)|{'airport','player_home'})
 for i,key in enumerate(keys):
  if key not in locations:
   locations[key]=ET.SubElement(entities,'Entity',name=LABELS.get(key,key.replace('_',' ').title()),
    id=str(9018000000000000000+i),active='true',position='0 0 0',rotation='0 0 0 1',scale='1 1 1',
    tags=f'location_root,location_{key}')
  e=locations[key]
  assert list(map(float,e.get('position').split()))==[0,0,0], 'Location parent has been moved'
  assert list(map(float,e.get('rotation').split()))==[0,0,0,1]
  assert list(map(float,e.get('scale').split()))==[1,1,1]
 def move(e,destination):
  previous=parents[e]
  if previous is destination:return
  # All old organizational ancestors are identity. Never silently move geometry.
  p=previous
  while p.tag=='Entity':
   assert list(map(float,p.get('position','0 0 0').split()))==[0,0,0],p.get('name')
   assert list(map(float,p.get('rotation','0 0 0 1').split()))==[0,0,0,1],p.get('name')
   assert list(map(float,p.get('scale','1 1 1').split()))==[1,1,1],p.get('name')
   p=parents[p]
  previous.remove(e);destination.append(e);parents[e]=destination
 all_entities=list(root.iter('Entity'))
 systems=next(e for e in all_entities if e.get('id')=='9002000000000000001')
 systems.set('name','World Systems')
 for e in all_entities:
  name=e.get('name','')
  if name.startswith('pin_'):
   key=name[4:];location=BELONGS_TO.get(key,key) if key in DESTINATIONS else SERVICES.get(key)
   assert location in locations,f'Unassigned map marker: {name}'
   move(e,locations[location])
  elif 'landmark_blockout' in e.get('tags','').split(','):
   key=name.removeprefix('landmark_');move(e,locations[BELONGS_TO.get(key,key)])
  elif name=='airport':move(e,locations['airport'])
  elif name=='player_garage_hub':move(e,locations['player_home'])
  elif name.startswith(('city_grid_1km__','gas_station_showcase__')):move(e,locations['port'])
  elif name=='monolith':move(e,locations['xirokastello'])
  elif name in ['light_directional','terrain','ocean','ufo_disc_craft_zeta_reticuli','map_skeleton']:
   move(e,systems)
 # Retire empty catch-all wrappers. Keep the shared road hierarchy and its IDs.
 retired=set()
 for e in all_entities:
  if e.get('name') in ['pins','zakynthos_location_landmarks']:
   assert not e.findall('Entity')
   retired.add(e.get('id'));parents[e].remove(e)
 for parent in list(root.iter()):
  for child in list(parent):
   if child.tag is ET.Comment and 'LANDMARK BLOCKOUT' in (child.text or ''):parent.remove(child)
 # Location roots first, in alphabetical display order, then shared/dynamic entities.
 ordered=sorted(locations.values(),key=lambda e:e.get('name'))
 entities[:]=ordered+[e for e in entities if e not in ordered]
 current={e.get('id'):dict(e.attrib) for e in root.iter('Entity')}
 assert len(current)==len(list(root.iter('Entity'))),'Duplicate entity ID'
 for key,attributes in original.items():
  if key in retired:continue
  actual=current[key].copy()
  if key==systems.get('id'):actual['name']=attributes['name']
  assert actual==attributes,f'Unexpected entity mutation: {attributes.get("name")}'
 # Pin and cluster positions are still world coordinates under identity parents.
 return serialize(root),{e.get('name'):[c.get('name') for c in e.findall('Entity')] for e in ordered}

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--apply',action='store_true');args=parser.parse_args()
 source=WORLD.read_text(encoding='utf-8');result,report=organize(source)
 assert organize(result)[0]==result,'Hierarchy migration must be idempotent'
 if args.apply:
  OUT.mkdir(parents=True,exist_ok=True)
  backup=OUT/f'plan_before_location_hierarchy_{hashlib.sha256(source.encode()).hexdigest()[:12]}.world'
  if not backup.exists():backup.write_bytes(WORLD.read_bytes())
  assert WORLD.read_text(encoding='utf-8')==source,'World changed during migration'
  WORLD.write_text(result,encoding='utf-8')
  (OUT/'hierarchy.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(json.dumps(report,indent=2))

if __name__=='__main__':main()
