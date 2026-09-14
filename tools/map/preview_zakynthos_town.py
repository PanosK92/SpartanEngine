"""Render an authoring view of the instanced town, independent of the engine window."""
from pathlib import Path
import bpy,sys,json,math
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'binaries/project/zakynthos_town'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'sources/zakynthos_town.blend'))
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=16;scene.cycles.use_denoising=True
scene.render.resolution_x=1440;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.55,.66,.8,1);scene.world.node_tree.nodes['Background'].inputs[1].default_value=.5
bpy.ops.object.light_add(type='SUN',rotation=(.5,-.55,-.5));bpy.context.object.data.energy=2;bpy.context.object.data.angle=.08
def mat(name,color):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True;m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(*color,1);m.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.9;return m
ground=mat('Town courtyards',(.29,.32,.23));roadmat=mat('Preview asphalt',(.10,.11,.105));walkmat=mat('Preview sidewalks',(.62,.59,.49))
bpy.ops.mesh.primitive_plane_add(size=2,location=(15,10,-.35));bpy.context.object.scale=(800,920,1);bpy.context.object.data.materials.append(ground)
plan=json.loads((OUT/'plan.json').read_text());roads=json.loads((OUT/'sources/site_roads.json').read_text())+plan['roads']
for ri,r in enumerate(roads):
    for width,y,material in ((r['width']+5,-.12,walkmat),(r['width'],-.10,roadmat)):
        vv=[];ff=[]
        for a,b in zip(r['xy'],r['xy'][1:]):
            if not(7500<a[0]<9150 and -1400<a[1]<550):continue
            dx,dz=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dz)
            if length<.01:continue
            nx,nz=-dz/length*width/2,dx/length*width/2;k=len(vv)
            vv.extend([(x-8300,-(z+400),y) for x,z in ((a[0]+nx,a[1]+nz),(a[0]-nx,a[1]-nz),(b[0]-nx,b[1]-nz),(b[0]+nx,b[1]+nz))]);ff.append((k,k+1,k+2,k+3))
        mesh=bpy.data.meshes.new('Street');mesh.from_pydata(vv,[],ff);ob=bpy.data.objects.new('Street '+str(ri),mesh);scene.collection.objects.link(ob);mesh.materials.append(material)
before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(ROOT/'binaries/project/models/island_biomes/olive_01/olive_01.gltf'));templates=list(set(bpy.data.objects)-before)
for t in plan['trees']:
    parent=bpy.data.objects.new('Olive',None);scene.collection.objects.link(parent);parent.location=(t['x']-8300,-(t['z']+400),-.3);parent.scale=(t['scale'],)*3
    for ob in templates:
        if ob.type!='MESH':continue
        clone=ob.copy();clone.data=ob.data;scene.collection.objects.link(clone);clone.parent=parent
for ob in templates:bpy.data.objects.remove(ob,do_unlink=True)
view=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'overview'
views={'overview':((9200,650,-1200),(8300,8,-400),42),'street':((8200,10.1,-625),(8200,12,-850),25),'square':((8475,12,-367),(8480,15,-301),23)}
pos,target,lens=views[view]
def local(p):return (p[0]-8300,-(p[2]+400),p[1]-8.35)
root=bpy.data.objects.new('Engine handedness',None);scene.collection.objects.link(root)
for ob in list(scene.objects):
    if ob!=root and ob.parent is None and not any(c.name=='Source architecture kit' for c in ob.users_collection):ob.parent=root
root.scale.x=-1
pos=local(pos);target=local(target);pos=(-pos[0],pos[1],pos[2]);target=(-target[0],target[1],target[2])
bpy.ops.object.camera_add(location=pos);cam=bpy.context.object;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.lens=lens;cam.data.clip_end=5000;scene.camera=cam
scene.view_settings.view_transform='AgX';scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/'previews'/f'{view}.png');bpy.ops.render.render(write_still=True)
