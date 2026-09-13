"""Render the authored home at eye level for layout and material review."""
from pathlib import Path
import bpy, math, sys, json
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'binaries/project/home_garage'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'sources/home_garage.blend'))
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=24;scene.cycles.use_denoising=True
scene.render.resolution_x=1440;scene.render.resolution_y=960;scene.render.resolution_percentage=100
scene.world.use_nodes=True;scene.world.node_tree.nodes.get('Background').inputs[0].default_value=(.43,.55,.70,1);scene.world.node_tree.nodes.get('Background').inputs[1].default_value=.45
bpy.ops.object.light_add(type='SUN',rotation=(.45,-.55,-.6));bpy.context.object.data.energy=2.2;bpy.context.object.data.angle=.10
for l in json.loads((OUT/'manifest.json').read_text())['lights']:
    x,y,z=l['position'];bpy.ops.object.light_add(type='POINT',location=(x,-z,y));bpy.context.object.data.energy=l['lumens']/9;bpy.context.object.data.color=(1,.64,.32);bpy.context.object.data.shadow_soft_size=.18
for x,z,s in [(-14,12,.52),(12,12,.55)]:
    before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(ROOT/'binaries/project/models/island_biomes/olive_01/olive_01.gltf'))
    added=set(bpy.data.objects)-before;parent=bpy.data.objects.new('Garden olive',None);scene.collection.objects.link(parent)
    for ob in added:
        if ob.parent is None:ob.parent=parent
    parent.location=(x,-z,.04);parent.scale=(s,)*3
scene.view_settings.view_transform='AgX'
# Match the engine's left-handed view rather than Blender's right-handed view.
# Reflect the complete scene and camera together; the exported glyph correction
# is therefore reviewed from the same screen-right direction as in the game.
review_root=bpy.data.objects.new('Engine handedness preview',None);scene.collection.objects.link(review_root)
for ob in list(scene.objects):
    if ob!=review_root and ob.parent is None:ob.parent=review_root
review_root.scale.x=-1
view=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'entry'
views={'entry':((24,-32,12),(0,-2,1.8),43),'lounge':((6,-5,1.8),(12,6,1.65),22),'workshop':((2,-4,1.8),(-6,8,1.8),24)}
pos,target,lens=views[view]
pos=(-pos[0],pos[1],pos[2]);target=(-target[0],target[1],target[2])
bpy.ops.object.camera_add(location=pos);cam=bpy.context.object;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.lens=lens;scene.camera=cam
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/f'previews/{view}.png');bpy.ops.render.render(write_still=True)
