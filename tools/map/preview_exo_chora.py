"""Render the authored village in Blender for geometry/material review."""
from pathlib import Path
import bpy, math, json, sys
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'binaries/project/exo_chora'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'sources/exo_chora.blend'))
manifest=json.loads((OUT/'manifest.json').read_text())
for tree in manifest['trees']:
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(ROOT/f'binaries/project/models/island_biomes/olive_0{tree["variant"]}/olive_0{tree["variant"]}.gltf'))
    added=set(bpy.data.objects)-before
    parent=bpy.data.objects.new('Olive',None);bpy.context.collection.objects.link(parent)
    for ob in added:
        if ob.parent is None:ob.parent=parent
    parent.location=(tree['x'],-tree['z'],tree['y']);parent.scale=(tree['scale'],)*3
scene=bpy.context.scene
scene.render.engine='CYCLES';scene.cycles.samples=24;scene.cycles.use_denoising=True
scene.render.resolution_x=1600;scene.render.resolution_y=1050;scene.render.resolution_percentage=100
scene.world.use_nodes=True;scene.world.node_tree.nodes.get('Background').inputs[0].default_value=(.45,.58,.75,1);scene.world.node_tree.nodes.get('Background').inputs[1].default_value=.55
bpy.ops.object.light_add(type='SUN',rotation=(math.radians(26),math.radians(-32),math.radians(-28)));bpy.context.object.data.energy=3;bpy.context.object.data.angle=.035
scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast'
view=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'overview'
views={'overview':((155,215,135),(0,63,0)), 'square':((-31,75,2.0),(-8,32,3.0)), 'street':((-38,112,2.0),(-40,22,2.5))}
pos,target=views[view]
bpy.ops.object.camera_add(location=pos);cam=bpy.context.object;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.lens=40 if view=='overview' else 24;cam.data.clip_end=1200;scene.camera=cam
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/f'previews/blender-{view}.png')
bpy.ops.render.render(write_still=True)
