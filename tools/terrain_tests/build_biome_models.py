"""Run with Blender --background --python this_file.py.

Build metre-scaled, grounded prop palettes from CC0 scans and seeded tree
geometry. Sources are retained; game exports have bounded triangle counts.
"""
import bpy
import bmesh
import json
import math
import random
import sys
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'binaries/project/models/island_biomes'
SOURCE=OUT/'sources'
REPORT=[]
ONLY=next((a.split('=',1)[1] for a in sys.argv if a.startswith('--only=')),None)
if ONLY and (OUT/'models.json').exists():
    REPORT=[r for r in json.loads((OUT/'models.json').read_text()) if r['name']!=ONLY and not r['name'].startswith(ONLY+'_')]

def clear():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

def material(name,color,texture=None,alpha=None,normal=None):
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    shader=mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value=(*color,1)
    shader.inputs['Roughness'].default_value=.82
    def image_node(file):
        node=mat.node_tree.nodes.new('ShaderNodeTexImage')
        node.image=bpy.data.images.load(str(file),check_existing=True)
        return node
    if texture:
        node=image_node(texture);mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    if alpha:
        node=image_node(alpha);node.image.colorspace_settings.name='Non-Color'
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Alpha'])
        mat.surface_render_method='DITHERED'
        mat.use_backface_culling=False
    if normal:
        node=image_node(normal);node.image.colorspace_settings.name='Non-Color'
        bump=mat.node_tree.nodes.new('ShaderNodeNormalMap')
        mat.node_tree.links.new(node.outputs['Color'],bump.inputs['Color'])
        mat.node_tree.links.new(bump.outputs['Normal'],shader.inputs['Normal'])
    return mat

class Geometry:
    def __init__(self): self.v=[];self.f=[];self.uv=[]
    def face(self,vertices,uv):
        i=len(self.v);self.v.extend(vertices);self.f.append(tuple(range(i,i+len(vertices))));self.uv.extend(uv)
    def tube(self,start,end,r0,r1,sides=7):
        a,b=Vector(start),Vector(end);axis=(b-a).normalized()
        u=axis.cross(Vector((0,1,0))).normalized()
        if u.length<.1:u=axis.cross(Vector((1,0,0))).normalized()
        v=axis.cross(u).normalized()
        for i in range(sides):
            q0=u*math.cos(i*math.tau/sides)+v*math.sin(i*math.tau/sides)
            q1=u*math.cos((i+1)*math.tau/sides)+v*math.sin((i+1)*math.tau/sides)
            self.face([a+q0*r0,a+q1*r0,b+q1*r1,b+q0*r1],[(i/sides,0),((i+1)/sides,0),((i+1)/sides,(b-a).length), (i/sides,(b-a).length)])
    def needle(self,center,direction,length,roll):
        axis=Vector(direction).normalized();cross=axis.cross(Vector((0,0,1))).normalized()
        if cross.length<.1:cross=Vector((1,0,0))
        other=axis.cross(cross).normalized()
        cross=cross*math.cos(roll)+other*math.sin(roll)
        a=Vector(center);b=a+axis*length;w=length*.25
        # Only the needle twig in the upper-left of the source atlas, no pine cones.
        # Stay inside the twig's cutout island: the adjacent atlas padding is
        # opaque cone/bark data and would become visible triangular scraps.
        self.face([a-cross*w,a+cross*w,b+cross*w,b-cross*w],[(.015,.60),(.195,.60),(.195,.96),(.015,.96)])
    def leaf(self,p,direction,length,width,roll):
        a=Vector(p);axis=Vector(direction).normalized();b=a+axis*length
        u=axis.cross(Vector((0,0,1))).normalized()
        if u.length<.1:u=Vector((1,0,0))
        normal=axis.cross(u);u=u*math.cos(roll)+normal*math.sin(roll)
        mid=(a+b)*.5;raised=mid+axis.cross(u)*width*.2
        for points in [[a,mid+u*width,raised],[mid+u*width,b,raised],[b,mid-u*width,raised],[mid-u*width,a,raised]]:
            self.face(points,[(0,0),(.5,1),(1,0)])
    def object(self,name,mat):
        mesh=bpy.data.meshes.new(name);mesh.from_pydata(self.v,[],self.f);mesh.update()
        uv=mesh.uv_layers.new()
        for loop,coord in zip(uv.data,self.uv):loop.uv=coord
        obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj)
        obj.data.materials.append(mat)
        for face in mesh.polygons:face.use_smooth=True
        return obj

def tree(kind,index):
    clear();rng=random.Random(191+index*79+(1000 if kind=='olive' else 0))
    wood,leaves=Geometry(),Geometry()
    bark=material(kind+'_bark',(.2,.16,.12),SOURCE/'pine_textures/bark_diff.png',normal=SOURCE/'pine_textures/bark_nor_gl.png')
    foliage=material(kind+'_foliage',(.22,.285,.13),
        SOURCE/'pine_textures/twig_diff.png' if kind=='pine' else None,
        SOURCE/'pine_textures/twig_alpha.png' if kind=='pine' else None)
    height=12+index*1.3 if kind=='pine' else 6+index*.45
    trunk_height=height*(.9 if kind=='pine' else .38)
    def trunk_at(t):return Vector((math.sin(t*3+index)*t*(1.1 if index==2 else .35),math.sin(t*5+index)*t*.3,trunk_height*t))
    for j in range(12):
        t=j/12;wood.tube(trunk_at(t),trunk_at(t+1/12),(.28 if kind=='pine' else .36)*(1-t*.88),(.28 if kind=='pine' else .36)*(1-(t+1/12)*.88),10)
    if kind=='pine':
        # Three genuinely different branch skeletons: broad, narrow, and asymmetric crowns.
        for ring in range(9):
            t=(.42+ring*.058) if index!=1 else (.30+ring*.074)
            for branch in range(5+(ring%2)):
                angle=branch*math.tau/(5+ring%2)+ring*1.71+rng.uniform(-.3,.3)
                axis=Vector((math.cos(angle),math.sin(angle),0))
                shape=(1-(t-.3)*1.05) if index==1 else math.sin((t-.26)/.76*math.pi)**.6
                span=(3.6 if index==0 else 2.8 if index==1 else 4.2)*shape*rng.uniform(.65,1.25)
                start=trunk_at(t);end=start+axis*span+Vector((0,0,rng.uniform(.1,.9)))
                wood.tube(start,end,.09*(1-t),.012,6)
                for twig in range(8):
                    u=.25+twig*.10;origin=start.lerp(end,u)
                    side=Vector((-axis.y,axis.x,0))*(-1 if twig%2 else 1)
                    direction=(axis*.55+side*.65+Vector((0,0,.25))).normalized()
                    tip=origin+direction*(1.3-u*.5)
                    wood.tube(origin,tip,.011,.002,4)
                    for needle in range(9):
                        point=origin.lerp(tip,needle/10)
                        for sign in [-1,1]:
                            d=(direction+side*sign*.75+Vector((0,0,rng.uniform(-.2,.65)))).normalized()
                            leaves.needle(point,d,rng.uniform(.38,.64),rng.uniform(-math.pi,math.pi))
        for j in range(40):leaves.needle(trunk_at(.92), (rng.uniform(-1,1),rng.uniform(-1,1),rng.uniform(.3,1)),rng.uniform(.5,1.2),rng.uniform(0,math.tau))
    else:
        for arm in range(7):
            angle=arm*math.tau/7+index*.4+rng.uniform(-.2,.2)
            start=trunk_at(.68+rng.random()*.25)
            end=Vector((math.cos(angle)*rng.uniform(1.2,2),math.sin(angle)*rng.uniform(1.2,2),height*rng.uniform(.65,.85)))
            wood.tube(start,end,.13,.035,8)
            for branch in range(7):
                a=end.lerp(start,rng.uniform(0,.35));phi=angle+branch*.9
                b=a+Vector((math.cos(phi)*rng.uniform(.6,1.5),math.sin(phi)*rng.uniform(.6,1.5),rng.uniform(.35,1)))
                wood.tube(a,b,.028,.005,5)
                for twig in range(5):
                    root=a.lerp(b,(twig+1)/6);phi+=1.9
                    tip=root+Vector((math.cos(phi)*.55,math.sin(phi)*.55,rng.uniform(-.05,.45)))
                    wood.tube(root,tip,.005,.001,3)
                    for leaf in range(8):
                        p=root.lerp(tip,leaf/8);d=tip-root
                        side=Vector((-d.y,d.x,rng.uniform(-.1,.3))).normalized()
                        for sign in [-1,1]:leaves.leaf(p,d*.4+side*sign,.20,.042,rng.uniform(-1,1))
    wood.object(kind+'_trunk',bark);leaves.object(kind+'_leaves',foliage)
    export(f'{kind}_{index+1:02}',normalize=None)

def export(name,normalize=None,budget=None):
    objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    points=[o.matrix_world@Vector(c) for o in objects for c in o.bound_box]
    lo=Vector(tuple(min(p[i] for p in points) for i in range(3)))
    hi=Vector(tuple(max(p[i] for p in points) for i in range(3)))
    # Apply all node transforms to vertices; Spartan's instancer uses raw mesh space.
    factor=normalize/max(hi-lo) if normalize else 1
    center=Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z))
    for o in objects:o.data.calc_loop_triangles()
    total=sum(len(o.data.loop_triangles) for o in objects)
    for o in objects:
        transform=o.matrix_world.copy()
        for vertex in o.data.vertices:vertex.co=(transform@vertex.co-center)*factor
        o.location=(0,0,0);o.rotation_euler=(0,0,0);o.scale=(1,1,1)
        if budget and total>budget:
            bpy.context.view_layer.objects.active=o
            # glTF splits vertices at UV/normal seams. Restore connected topology
            # so the decimator can simplify scans with individually split faces.
            bm=bmesh.new();bm.from_mesh(o.data)
            bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=0.00001)
            bm.to_mesh(o.data);bm.free();o.data.update()
            dec=o.modifiers.new('game_budget','DECIMATE');dec.ratio=budget/total
            bpy.ops.object.modifier_apply(modifier=dec.name)
        for m in o.data.materials:
            if m:
                m.name=name+'_'+m.name.split('.')[-1] if m.name.startswith('Material.') else name+'_'+m.name
                if name.startswith('scrub'):m.name=name+'_foliage'
        tri=o.modifiers.new('triangles','TRIANGULATE');bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=tri.name)
    triangles=sum(len(o.data.polygons) for o in objects)
    assert not budget or triangles<=budget*1.05,(name,triangles,budget)
    directory=OUT/name;directory.mkdir(parents=True,exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(directory/(name+'.gltf')),export_format='GLTF_SEPARATE',use_selection=True,export_yup=True,export_image_format='AUTO')
    p=directory/(name+'.gltf');data=json.loads(p.read_text())
    for m in data.get('materials',[]):
        if m.get('alphaMode')=='BLEND':m['alphaMode']='MASK';m['alphaCutoff']=.45;m['doubleSided']=True
    p.write_text(json.dumps(data,separators=(',',':')))
    REPORT.append(dict(name=name,path=p.relative_to(ROOT/'binaries').as_posix(),triangles=triangles,source_dimensions=list(hi-lo),scale=factor))
    print('BIOME_ASSET',REPORT[-1],flush=True)

for kind in ['pine','olive']:
    for i in range(3):
        if not ONLY or ONLY in [kind,f'{kind}_{i+1:02}']:tree(kind,i)
for source,name,budget in [('coast_land_rocks_02','limestone_slab',4500),('boulder_01','limestone_boulder',3200),('rock_07','weathered_stone',1400),('rock_09','angular_stone',1600),('shrub_04','scrub_01',5500),('shrub_04','scrub_02',3500)]:
    if ONLY and ONLY!=name:continue
    clear();bpy.ops.import_scene.gltf(filepath=str(SOURCE/source/(source+'.gltf')))
    if 'scrub' in name:
        # These source files are branch libraries laid out in a row. Assemble
        # the branches around a shared base before using them as whole bushes.
        objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
        for o in objects:
            matrix=o.matrix_world.copy()
            for v in o.data.vertices:v.co=matrix@v.co
            o.matrix_world.identity()
        lo=min(v.co.x for o in objects for v in o.data.vertices)
        hi=max(v.co.x for o in objects for v in o.data.vertices)
        groups=4 if source=='shrub_04' else 8
        width=(hi-lo)/groups
        for o in objects:
            for v in o.data.vertices:
                group=min(groups-1,int((v.co.x-lo)/width))
                spreading=name=='scrub_02'
                a=group*(2.1 if spreading else 2.39996)+(.73 if spreading else 0)
                x=v.co.x-(lo+(group+.5)*width);y=v.co.y
                radius=width*(.28 if spreading else .15)
                v.co.x=x*math.cos(a)-y*math.sin(a)+math.cos(a)*radius
                v.co.y=x*math.sin(a)+y*math.cos(a)+math.sin(a)*radius
                if spreading:v.co.z*=.65
    export(name,normalize=1 if 'scrub' not in name else 1.7,budget=budget)
(OUT/'models.json').write_text(json.dumps(REPORT,indent=2))
print('BUILT',len(REPORT),'MODELS',flush=True)
