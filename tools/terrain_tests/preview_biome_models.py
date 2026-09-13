"""Blender batch render of the game exports, for silhouette/material inspection."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[2]
directory=root/'binaries/project/models/island_biomes'
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
models=json.loads((directory/'models.json').read_text())
order=['pine_01','pine_02','pine_03','pine_04','olive_01','olive_02','olive_03','olive_04','limestone_slab','limestone_boulder','weathered_stone','angular_stone','scrub_01','scrub_02','scrub_03','scrub_04','scrub_05']
models.sort(key=lambda m:order.index(m['name']))
for i,r in enumerate(models):
    before=set(bpy.context.scene.objects)
    bpy.ops.import_scene.gltf(filepath=str(root/'binaries'/r['path']))
    imported=set(bpy.context.scene.objects)-before
    position=Vector(((i%8)*11,-(i//8)*12,0))
    for o in imported:
        if o.parent is None:
            o.location+=position
            if i>=8:o.scale*=4
bpy.ops.mesh.primitive_plane_add(size=200,location=(38,0,-.02))
mat=bpy.data.materials.new('ground');mat.diffuse_color=(.20,.19,.15,1);bpy.context.object.data.materials.append(mat)
bpy.ops.object.light_add(type='SUN',location=(0,-20,25));sun=bpy.context.object;sun.rotation_euler=(.45,-.4,-.4);sun.data.energy=3;sun.data.angle=.15
bpy.ops.object.camera_add(location=(65,-65,34));cam=bpy.context.object;cam.rotation_euler=(Vector((38,-6,5))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=95
scene=bpy.context.scene;scene.camera=cam;scene.render.engine='CYCLES';scene.cycles.samples=24
scene.world.color=(.3,.3,.3);scene.render.resolution_x=1600;scene.render.resolution_y=850;scene.render.resolution_percentage=100
scene.render.filepath=str(root/'binaries/terrain_tests/biome_palette.png');bpy.ops.render.render(write_still=True)
