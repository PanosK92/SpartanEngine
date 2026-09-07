"""Blender background preview/export of the exact authored landmark primitives.
blender --background --factory-startup --python tools/map/preview_location_landmarks.py
"""
from pathlib import Path
import math
import xml.etree.ElementTree as ET
import bpy
from mathutils import Quaternion, Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'binaries/project/zakynthos_landmarks'
tree=ET.parse(OUT/'landmarks.xml').getroot()
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
materials={}
for path in (OUT/'materials').glob('*.xml'):
 xml=ET.parse(path).getroot();m=bpy.data.materials.new(path.stem);m.use_nodes=True
 rgb=tuple(float(xml.find(k).text) for k in ['color_r','color_g','color_b'])
 m.diffuse_color=(*rgb,1);node=m.node_tree.nodes.get('Principled BSDF')
 node.inputs['Base Color'].default_value=(*rgb,1)
 node.inputs['Roughness'].default_value=float(xml.find('roughness').text)
 node.inputs['Metallic'].default_value=float(xml.find('metalness').text)
 materials[path.stem]=m

def load(e,parent=None):
 render=e.find('render');name=e.get('name')
 if render is None:
  o=bpy.data.objects.new(name,None);bpy.context.collection.objects.link(o)
 else:
  kind=render.get('mesh_name')
  if kind=='standard_cube':bpy.ops.mesh.primitive_cube_add(size=1)
  elif kind=='standard_cylinder':bpy.ops.mesh.primitive_cylinder_add(vertices=32,radius=1,depth=1,rotation=(math.pi/2,0,0))
  elif kind=='standard_cone':bpy.ops.mesh.primitive_cone_add(vertices=32,radius1=1,depth=2,rotation=(-math.pi/2,0,0))
  else:bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,radius=1)
  o=bpy.context.object;o.name=name
  if kind in ['standard_cylinder','standard_cone']:
   bpy.ops.object.transform_apply(location=False,rotation=True,scale=False)
  o.data.materials.append(materials[render.get('material_name')])
 o.parent=parent;o.location=tuple(map(float,e.get('position').split()))
 q=list(map(float,e.get('rotation').split()));o.rotation_mode='QUATERNION';o.rotation_quaternion=Quaternion((q[3],*q[:3]))
 o.scale=tuple(map(float,e.get('scale').split()))
 for child in e.findall('Entity'):load(child,o)
 return o

basis=bpy.data.objects.new('Spartan_Y_up_to_Blender_Z_up',None);bpy.context.collection.objects.link(basis)
basis.rotation_euler=(math.pi/2,0,0)
sites={e.get('name')[9:]:load(e,basis) for e in tree.findall('Entity')}
bpy.context.view_layer.update()
(OUT/'models').mkdir(exist_ok=True)
for key,o in sites.items():
 # GLB export at the local origin, with standard glTF Y-up conversion.
 old=o.location.copy();o.location=(0,0,0)
 bpy.ops.object.select_all(action='DESELECT')
 o.select_set(True)
 for ch in o.children_recursive:ch.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(OUT/'models'/f'{key}.glb'),use_selection=True,export_format='GLB',export_yup=True)
 o.location=old
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'landmark_library.blend'))

# A studio contact sheet, explicitly separate from an in-game screenshot.
featured=['keri_light','anafonitria','kalamaki','bochali','skinari','exo_chora']
for key,o in sites.items():
 if key not in featured:
  for ob in [o,*o.children_recursive]:ob.hide_render=True
for i,key in enumerate(featured):
 o=sites[key];x=(i%3)*64-64;z=(i//3)*65-30;o.location=(x,0,z)
 bpy.ops.object.text_add(location=(x,-z+21,.2))
 txt=bpy.context.object;txt.data.body=key.replace('_',' ').upper();txt.data.align_x='CENTER';txt.data.size=2
 txt.data.extrude=.01
 txt.data.materials.append(materials['zante_blue'])

bpy.ops.mesh.primitive_plane_add(size=400,location=(0,0,-.12));floor=bpy.context.object
floor.data.materials.append(materials['zante_plaster'])
bpy.ops.object.light_add(type='AREA',location=(-55,-55,100));bpy.context.object.data.energy=120000;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=85
bpy.ops.object.light_add(type='SUN',rotation=(.3,-.5,-.4));bpy.context.object.data.energy=2
bpy.ops.object.camera_add(location=(105,-155,145));cam=bpy.context.object
cam.rotation_euler=(Vector((0,-5,0))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=225
scene=bpy.context.scene;scene.camera=cam;scene.render.engine='CYCLES';scene.cycles.samples=32
scene.world.color=(.3,.3,.3);scene.render.resolution_x=1800;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/'landmark_preview.png')
bpy.ops.render.render(write_still=True)
