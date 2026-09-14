"""Build a reusable Ionian town kit and an editable, instanced town scene in Blender."""
from pathlib import Path
import sys,json,math,hashlib,copy,xml.etree.ElementTree as ET
import bpy
from mathutils import Matrix,Quaternion
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(Path(__file__).parent))
import build_exo_chora as g
OUT=ROOT/'binaries/project/zakynthos_town'
for f in ('sources','materials','meshes','packets','previews'):(OUT/f).mkdir(exist_ok=True)
plan=json.loads((OUT/'plan.json').read_text())

def kit():
    g.groups.clear();g.features.clear();g.level=lambda v:0;g.roof=town_roof
    for i,(w,d,floors) in enumerate(plan['prototype_sizes']):
        name=f'block_{i:02}'
        g.building(name,0,0,w,d,floors,['lime','ochre','rose','sage_plaster'][i%4],shop=['CAFE','BAKERY','MARKET','RECORDS','MOTOR CLUB','TAVERNA'][i%6] if i%3!=2 else None,balcony=True)
        g.start(name)
        # Rooftop tanks, aerials and small service details break repeated silhouettes.
        if i%3==0:
            g.lathe('cream',(w*.24,floors*3.2+2,d*.21),[(.8,0),(.85,.15),(.85,1.1),(.7,1.2),(0,1.2)],16)
        g.tube('metal',(-w*.3,floors*3.2,0),(-w*.3,floors*3.2+3.7,0),.023,n=6)
        for y in (floors*3.2+3,floors*3.2+3.4):g.tube('metal',(-w*.3-.65,y,0),(-w*.3+.65,y,0),.016,n=6)
        if i%4==0:
            for x in (-w*.25,w*.25):g.pot(x,0,-d/2-2.2,1,True)
    # Civic square: arcaded museum, church, campanile, fountain and shaded seating.
    g.start('civic');g.box('paving',(0,.05,0),(124,.1,128))
    for x in range(-58,60,8):
        g.box('trim',(x,.108,0),(.16,.016,122))
    g.building('civic',0,49,100,20,2,'lime',shop='ZAKYNTHOS MUSEUM',balcony=False)
    g.start('civic')
    # An open colonnade with curved arch soffits in front of the museum.
    for x in range(-45,46,5):
        g.box('stone',(x,1.7,34),(.7,3.4,1.1));g.box('trim',(x,3.4,34),(1,.2,1.3))
    for i in range(18):arc('stone',-42.5+i*5,3.4,34,2.0,2.4,1.1)
    g.box('trim',(0,5.95,34),(94,.3,2))
    g.building('civic',-23,-43,58,24,3,'ochre',yaw=180,shop=None,balcony=False)
    g.start('civic',(-23,0,-29),180);g.sign('AGIOS DIONYSIOS',0,4,0,10)
    g.start('civic')
    for y in (0,9,18):
        g.box('stone',(45,y+4.5,-43),(9,9,9));g.box('trim',(45,y+.3,-43),(10,.6,10))
    for x in (41.1,48.9):
        for z in (-46.9,-39.1):g.box('stone',(x,31.5,z),(1.2,9,1.2))
    for z in (-46.9,-39.1):arc('stone',45,31.4,z,3.2,3.9,1.2)
    g.box('trim',(45,36,-43),(10,.5,10))
    g.lathe('metal',(45,30.2,-43),[(1.4,0),(1.5,.2),(1,.4),(.55,1.8),(.35,2),(0,2.1)],24)
    g.lathe('roof',(45,36,-43),[(7,0),(5,2),(0,6)],4)
    g.tube('metal',(45,42,-43),(45,45,-43),.1);g.tube('metal',(43.8,44,-43),(46.2,44,-43),.1)
    # Clock faces on both sides of the tower.
    for z in (-47.58,-38.42):
        g.tube('cream',(45,25,z-.05),(45,25,z+.05),1.65,n=32)
        g.tube('metal',(45,25,z-.08),(44.2,26,z-.08),.075,n=6)
        g.tube('metal',(45,25,z-.08),(46.1,25.2,z-.08),.075,n=6)
    fountain('civic',0,-5,6)
    for x in (-46,46):
        for z in (-14,5,23):g.start('civic');g.bench(x,0,z,math.pi/2 if x<0 else -math.pi/2);g.pot(x,0,z+3,1.4,True)
    g.start('market');g.box('paving',(0,.05,0),(124,.1,128))
    fountain('market',0,0,4)
    for x in (-44,44):
        for z in (-42,-21,0,21,42):
            g.start('market',(x,0,z),90 if x<0 else -90)
            g.box('wood',(0,.65,0),(9,1.3,3));g.box('trim',(0,1.35,0),(9.3,.15,3.3))
            for xx in (-4.5,4.5):
                for zz in (-2,2):g.tube('wood',(xx,0,zz),(xx,3.8,zz),.08,n=8)
            for i in range(18):g.box('canvas' if i%2 else 'canvas_red',(-4.25+i*.5,3.75,0),(.5,.12,5))
            for xx in range(-3,4):g.lathe('terracotta',(xx,1.43,0),[(.35,0),(.45,.25),(.3,.4)],12)
    for z in (-48,48):
        for x in (-20,0,20):g.start('market');g.table(x,0,z)
    g.start('lamp')
    g.lathe('metal',(0,0,0),[(.28,0),(.28,.15),(.12,.25),(.09,5.8),(.16,6)],12)
    g.box('metal',(0,6.25,0),(.5,.65,.5));g.box('amber',(0,6.25,0),(.4,.5,.4));g.lathe('metal',(0,6.6,0),[(.43,0),(0,.3)],4)
    g.start('bollard');g.lathe('stone',(0,0,0),[(.17,0),(.17,.8),(.12,.95),(0,.98)],12)

def town_roof(w,d,h):
    # One physical eave row preserves the tile silhouette; PBR maps carry the field.
    a=w/2+.42;b=d/2+.44;rise=min(w,d)*.25
    g.geom('roof',[(-a,h,-b),(a,h,-b),(-a,h,b),(a,h,b),(-a,h+rise,0),(a,h+rise,0)],[(0,4,5,1),(2,3,5,4),(0,2,4),(1,5,3),(0,1,3,2)])
    for z in (-b,b):g.box('wood',(0,h-.1,z),(2*a,.18,.13))
    g.tube('terracotta',(-a-.06,h+rise+.04,0),(a+.06,h+rise+.04,0),.15,n=8)
    count=max(1,int(2*a/.5))
    for i in range(count+1):
        x=-a+i*2*a/count
        for side in (-1,1):g.tube('terracotta',(x,h+.03,side*b),(x,h+rise*.38/b+.03,side*(b-.38)),.075,n=6)

def arc(mat,x,y,z,inner,outer,depth):
    vertices=[(x+r*math.cos(i*math.pi/20),y+r*math.sin(i*math.pi/20),z+side*depth/2) for side in (-1,1) for r in (inner,outer) for i in range(21)]
    faces=[]
    for i in range(20):
        faces.extend([(i,i+1,22+i,21+i),(42+i,63+i,64+i,43+i),(i,42+i,43+i,i+1),(21+i,22+i,64+i,63+i)])
    faces.extend([(0,21,63,42),(20,62,83,41)]);g.geom(mat,vertices,faces)

def fountain(group,x,z,r):
    g.start(group);g.lathe('stone',(x,0,z),[(0,0),(r,0),(r,.7),(r-.5,.7),(r-.5,.2),(0,.2)],48)
    g.lathe('glass',(x,.22,z),[(0,0),(r-.55,0)],48)
    g.lathe('stone',(x,.25,z),[(1.3,0),(1.1,.25),(.35,2.5),(1.8,2.6),(1.8,2.85),(.45,2.9),(.22,4),(0,4.3)],32)

def main():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False);kit()
    materials=g.make_materials();objects=[]
    chunks=[(group,mat,verts,faces[start:start+8000]) for (group,mat),(verts,faces) in g.groups.items() for start in range(0,len(faces),8000)]
    for group,mat,verts,faces in chunks:
        mesh=bpy.data.meshes.new(group+'_'+mat);mesh.from_pydata([(x,-z,y) for x,y,z in verts],[],faces);mesh.update()
        ob=bpy.data.objects.new(group+'_'+mat,mesh);bpy.context.collection.objects.link(ob);mesh.materials.append(materials[mat]);ob['group']=group;ob['material']=mat
        uv=mesh.uv_layers.new();scale=g.MATS[mat][2]
        for poly in mesh.polygons:
            axis=max(range(3),key=lambda i:abs(poly.normal[i]));axes=[i for i in range(3) if i!=axis]
            for li in poly.loop_indices:
                p=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(p[axes[0]]/scale,p[axes[1]]/scale)
        objects.append(ob)
    for f in g.features:
        curve=bpy.data.curves.new('town_sign','FONT');curve.body=f['text'];curve.align_x='CENTER';curve.align_y='CENTER';curve.size=.48;curve.extrude=.002;curve.resolution_u=2
        ob=bpy.data.objects.new('Sign '+f['text'],curve);bpy.context.collection.objects.link(ob);x,y,z=f['position'];ob.location=(x,-z,y)
        basis=Matrix(((1,0,0),(0,0,-1),(0,1,0)));ob.rotation_euler=(basis@Quaternion(f['rotation']).to_matrix()).to_euler()
        bpy.context.view_layer.update();ob.scale*=min(1,f['width']*.9/max(ob.dimensions.x,.1));ob.data.materials.append(materials[f['material']]);ob['material']=f['material'];ob['group']=f['group'];objects.append(ob)
    bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get();meshes=[]
    for ob in objects:
        ev=ob.evaluated_get(deps);mesh=ev.to_mesh();mesh.calc_loop_triangles();p=[];n=[];uv=[];idx=[];lookup={}
        for tri in mesh.loop_triangles:
            for li in tri.loops:
                v=ob.matrix_world@mesh.vertices[mesh.loops[li].vertex_index].co;normal=ob.matrix_world.to_3x3()@mesh.corner_normals[li].vector;t=mesh.uv_layers.active.data[li].uv[:] if mesh.uv_layers.active else (0,0)
                key=tuple(round(v,5) for v in (v.x,v.z,-v.y,normal.x,normal.z,-normal.y,*t))
                if key not in lookup:lookup[key]=len(p)//3;p.extend(key[:3]);n.extend(key[3:6]);uv.extend(key[6:])
                idx.append(lookup[key])
        ev.to_mesh_clear();assert len(p)//3<100000 and len(idx)<300000,ob.name
        data=dict(positions=p,normals=n,uv0=uv,indices=idx);digest=hashlib.sha256(json.dumps(data).encode()).hexdigest()[:12];name='town_'+ob.name.replace(' ','_')+'_'+digest
        data['path']='project/zakynthos_town/meshes/'+name+'.mesh';packet=OUT/'packets'/f'{name}.json';packet.write_text(json.dumps(data,separators=(',',':')))
        meshes.append(dict(name=name,group=ob['group'],material=ob['material'],file=str(packet),mesh_path=data['path'],triangles=len(idx)//3))
    template=ET.parse(ROOT/'binaries/project/home_garage/materials/home_plaster.xml').getroot()
    for name,(rgb,tex,scale,rough) in g.MATS.items():
        root=copy.deepcopy(template)
        for t in root.find('textures'):t.set('texture_name','');t.set('texture_path','')
        props=dict(zip(('color_r','color_g','color_b'),rgb),roughness=rough,metalness=.75 if name=='metal' else 0,normal=.25 if tex else 0,clearcoat=0,sheen=0,paint_preset=0,surface_preset=0,emissive_from_albedo=.8 if name=='amber' else 0,terrain_blend=0,cull_mode=0)
        for k,v in props.items():root.find(k).text=str(v)
        if tex:
            for slot,channel in ((0,'diff'),(4,'rough'),(12,'nor_gl')):
                path=ROOT/'binaries/project/exo_chora/textures'/f'{tex}_{channel}.jpg'
                if not path.exists():path=path.with_suffix('.png')
                if not path.exists():continue
                t=root.find(f'textures/texture_{slot}');t.set('texture_name',path.stem);t.set('texture_path',path.relative_to(ROOT/'binaries').as_posix())
        ET.ElementTree(root).write(OUT/'materials'/f'town_{name}.xml',encoding='utf-8',xml_declaration=True)
    manifest=dict(meshes=meshes,plan=plan);(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    # Keep the shared source kit hidden, with linked objects forming the editable town.
    library=bpy.data.collections.new('Source architecture kit');bpy.context.scene.collection.children.link(library)
    for ob in objects:
        for col in list(ob.users_collection):col.objects.unlink(ob)
        library.objects.link(ob)
    library.hide_render=True;library.hide_viewport=True
    placements=plan['buildings']+[dict(name=p['name'],prototype='civic' if i==0 else 'market',x=p['x'],z=p['z'],yaw=0) for i,p in enumerate(plan['plazas'])]
    for b in placements:
        parent=bpy.data.objects.new(b['name'],None);bpy.context.collection.objects.link(parent);parent.location=(b['x']-8300,-(b['z']+400),0);parent.rotation_euler.z=-math.radians(b['yaw'])
        for ob in objects:
            if ob['group']!=b['prototype']:continue
            clone=ob.copy();clone.data=ob.data;bpy.context.collection.objects.link(clone);clone.parent=parent
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'sources/zakynthos_town.blend'))
    print(f'TOWN KIT: {len(meshes)} meshes, {sum(m["triangles"] for m in meshes)} unique triangles; {len(plan["buildings"])} linked buildings')

if __name__=='__main__':main()
