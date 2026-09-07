"""Author Exo Chora in Blender, export engine-ready mesh packets and editable source.

blender --background --factory-startup --python tools/map/build_exo_chora.py
Units are meters. Authoring helpers use Spartan's Y-up coordinates.
"""
from pathlib import Path
from collections import defaultdict
import bpy, math, json, random, hashlib, sys
from mathutils import Vector, Matrix

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'binaries/project/exo_chora'
for folder in ['meshes','materials','sources','previews','packets']:(OUT/folder).mkdir(parents=True,exist_ok=True)
rng=random.Random(77321)
BASE=338.6
ORIGIN=(-10435.2,BASE,1065.0)
YAW=27.5
groups={}
placements=[]
features=[]
lights=[]
trees=[]
building_bounds=[]
current=''
transform=None
MATS={
 'lime':((.84,.80,.69),'painted_plaster_wall',3,.88),
 'ochre':((.70,.48,.27),'painted_plaster_wall',3,.88),
 'rose':((.65,.38,.29),'painted_plaster_wall',3,.88),
 'sage_plaster':((.62,.66,.51),'painted_plaster_wall',3,.88),
 'stone':((.83,.79,.67),'plastered_stone_wall',2.3,.92),
 'trim':((.78,.75,.63),'painted_plaster_wall',2,.83),
 'roof':((.91,.76,.60),'clay_roof_tiles_02',2,.88),
 'paving':((1.0,.96,.84),'cobblestone_floor_08',2.2,.94),
 'wood':((.22,.135,.07),None,2,.75),
 'blue':((.07,.19,.22),None,1,.65),
 'green':((.20,.29,.21),None,1,.7),
 'redwood':((.34,.13,.09),None,1,.7),
 'metal':((.045,.05,.048),None,1,.53),
 'glass':((.055,.09,.095),None,1,.16),
 'canvas':((.77,.70,.51),None,1,.93),
 'canvas_red':((.39,.135,.085),None,1,.92),
 'terracotta':((.46,.225,.12),None,1,.89),
 'soil':((.75,.70,.59),'local_dirt',3.5,.99),
 'bark':((.74,.70,.59),'local_bark',1.6,.9),
 'leaves':((.23,.32,.10),None,1,.86),
 'olive_leaf':((.30,.36,.19),None,1,.84),
 'olive_silver':((.40,.44,.28),None,1,.86),
 'flowers':((.66,.08,.29),None,1,.8),
 'amber':((1,.58,.23),None,1,.3),
 'cream':((.93,.86,.68),None,1,.78),
 'asphalt':((.24,.23,.20),None,1,.95),
}

def level(v):
    if v>-12:return (v+12)*.10
    if v>=-70:return 0
    return (v+70)*.060

def start(name,pos=(0,0,0),yaw=0):
    global current,transform
    current=name
    a=math.radians(yaw);c,s=math.cos(a),math.sin(a)
    transform=Matrix(((c,0,s,pos[0]),(0,1,0,pos[1]),(-s,0,c,pos[2]),(0,0,0,1)))

def geom(mat,verts,faces):
    key=(current,mat)
    if key not in groups:groups[key]=[[],[]]
    vv,ff=groups[key];offset=len(vv)
    vv.extend([tuple(transform@Vector(p)) for p in verts])
    ff.extend([tuple(offset+i for i in face) for face in faces])

def box(mat,p,size,angle=0):
    x,y,z=p;hx,hy,hz=[v*.5 for v in size];c,s=math.cos(angle),math.sin(angle)
    vertices=[]
    for a,b,d in [(-hx,-hy,-hz),(hx,-hy,-hz),(hx,hy,-hz),(-hx,hy,-hz),(-hx,-hy,hz),(hx,-hy,hz),(hx,hy,hz),(-hx,hy,hz)]:
        vertices.append((x+c*a+s*d,y+b,z-s*a+c*d))
    geom(mat,vertices,[(0,3,2,1),(4,5,6,7),(0,4,7,3),(1,2,6,5),(3,7,6,2),(0,1,5,4)])

def tube(mat,a,b,r=.035,r2=None,n=10):
    av,bv=Vector(a),Vector(b);axis=(bv-av).normalized();ref=Vector((0,1,0)) if abs(axis.y)<.9 else Vector((1,0,0));u=axis.cross(ref).normalized();v=axis.cross(u).normalized();rr=r if r2 is None else r2
    verts=[tuple(p+(math.cos(i*math.tau/n)*u+math.sin(i*math.tau/n)*v)*rad) for p,rad in [(av,r),(bv,rr)] for i in range(n)]
    faces=[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]+[tuple(reversed(range(n))),tuple(range(n,2*n))]
    geom(mat,verts,faces)

def lathe(mat,p,profile,n=16):
    x,y,z=p;verts=[(x+r*math.cos(i*math.tau/n),y+h,z+r*math.sin(i*math.tau/n)) for r,h in profile for i in range(n)]
    faces=[]
    for j in range(len(profile)-1):
        for i in range(n):faces.append((j*n+i,(j+1)*n+i,(j+1)*n+(i+1)%n,j*n+(i+1)%n))
    geom(mat,verts,faces)

def rail(a,b,height=.95):
    av,bv=Vector(a),Vector(b);length=(bv-av).length
    tube('metal',av+Vector((0,height,0)),bv+Vector((0,height,0)),.025)
    tube('metal',av+Vector((0,.18,0)),bv+Vector((0,.18,0)),.018)
    for i in range(int(length/.18)+1):
        p=av+(bv-av)*i/max(1,int(length/.18));tube('metal',p,p+Vector((0,height,0)),.013,n=6)

def window(x,y,z,shutter='green',w=1.05,h=1.45,open=True):
    # Recessed glass, thick reveal, projecting sill, timber crossbars and louvered shutters.
    box('glass',(x,y+h/2,z+.12),(w,h,.055))
    for xx in [x-w/2-.09,x+w/2+.09]:box('trim',(xx,y+h/2,z-.05),(.18,h+.28,.30))
    for yy in [y-.08,y+h+.08]:box('trim',(x,yy,z-.07),(w+.34,.17,.32))
    box('trim',(x,y-.14,z-.18),(w+.46,.13,.53))
    box('wood',(x,y+h/2,z+.06),(.035,h,.05));box('wood',(x,y+h*.56,z+.05),(w,.035,.05))
    for sign in [-1,1]:
        sx=x+sign*(w*.76+.12) if open else x+sign*w*.25
        sw=w*.47;sz=z-.17
        box(shutter,(sx,y+h/2,sz),(sw,h+.04,.065))
        for edge in [-1,1]:box(shutter,(sx+edge*(sw/2-.025),y+h/2,sz-.04),(.04,h+.04,.07))
        for j in range(13):box(shutter,(sx,y+.075+j*(h-.12)/12,sz-.055),(sw-.06,.048,.045))
        for yy in [y+.2,y+h-.2]:box('metal',(sx,yy,sz-.08),(sw,.028,.025))

def wall(mat,width,height,depth,holes):
    xs=sorted(set([-width/2,width/2]+[x+sg*w/2 for x,y,w,h in holes for sg in [-1,1]]));ys=sorted(set([0,height]+[v for x,y,w,h in holes for v in [y,y+h]]))
    for xa,xb in zip(xs,xs[1:]):
        for ya,yb in zip(ys,ys[1:]):
            mx,my=(xa+xb)/2,(ya+yb)/2
            if any(abs(mx-x)<w/2-.001 and y<my<y+h for x,y,w,h in holes):continue
            box(mat,(mx,my,-depth/2),(xb-xa,yb-ya,.34))

def roof(w,d,h):
    a=w/2+.42;b=d/2+.44;rise=min(w,d)*.25
    # Gabled volumes have fully closed ends and real eaves.
    geom('roof',[(-a,h,-b),(a,h,-b),(-a,h,b),(a,h,b),(-a,h+rise,0),(a,h+rise,0)],[(0,4,5,1),(2,3,5,4),(0,2,4),(1,5,3),(0,1,3,2)])
    for z in [-b,b]:box('wood',(0,h-.1,z),(2*a,.18,.13))
    tube('terracotta',(-a-.06,h+rise+.04,0),(a+.06,h+rise+.04,0),.15,n=12)
    # Barrel tiles on eaves and ridge-facing slopes give roof silhouettes physical depth.
    n=int(2*a/.30)
    for i in range(n+1):
        x=-a+i*(2*a/n)
        for sign in [-1,1]:
            for j in range(3):
                z=sign*(b-j*.38);zz=sign*(b-(j+1)*.38)
                y=h+rise*(1-abs(z)/b)+.03;yy=h+rise*(1-abs(zz)/b)+.03
                tube('terracotta',(x,y,z),(x,yy,zz),.075,n=6)

def sign(text,x,y,z,width=3,mat='cream'):
    # Text is converted to actual geometry and exported with the rest of the location.
    p=transform@Vector((x,y,z));q=transform.to_quaternion()
    p=transform@Vector((x,y,z-.028))
    features.append(dict(text=text,position=list(p),rotation=list(q),width=width,material=mat,group=current))
    box('blue',(x,y,z+.05),(width+.22,.70,.10))

def building(name,x,v,w,d,floors=2,mat='lime',yaw=0,shop=None,balcony=False):
    y=level(v);h=3.2*floors
    start(name,(x,y,v),yaw)
    placements.append(dict(name=name,x=x,z=v,y=y,width=w,depth=d,yaw=yaw,height=h+min(w,d)*.25,kind='building'))
    building_bounds.append((name,x,v,w,d,yaw))
    box('stone',(0,-.32,0),(w+.2,.64,d+.2))
    box('paving',(0,.035,0),(w-.5,.07,d-.5))
    count=max(2,int(w/3.2));xs=[-w/2+(i+1)*w/(count+1) for i in range(count)]
    holes=[(0,0,1.35,2.35)]
    for f in range(floors):
        for xx in xs:
            if f==0 and abs(xx)<1.7:continue
            holes.append((xx,f*3.2+.9,1.05,1.45))
    wall(mat,w,h,d,holes)
    box(mat,(0,h/2,d/2),(w,h,.34))
    for xx in [-w/2,w/2]:box(mat,(xx,h/2,0),(.34,h,d))
    for f in range(1,floors):box('wood',(0,3.2*f,0),(w-.3,.18,d-.3))
    for xx,yy,ww,hh in holes:
        if yy==0:
            for sx in [-1,1]:box('trim',(sx*.77,1.23,-d/2-.05),(.2,2.48,.4))
            box('trim',(0,2.48,-d/2-.05),(1.75,.23,.4))
            # The taverna's doorway remains open; other doors have inset timber panels.
            if shop!='TAVERNA':
                box('wood',(0,1.16,-d/2+.1),(1.32,2.30,.12))
                for px in [-.33,.33]:
                    for py in [.55,1.62]:box('redwood',(px,py,-d/2+.015),(.49,.72,.05))
                lathe('metal',(.44,1.14,-d/2-.03),[(.032,0),(.032,.075)],8)
        else:window(xx,yy,-d/2-.03,shutter=['green','blue','redwood'][rng.randrange(3)],open=rng.random()>.2)
    # Extra side/back facade detail is built on the wall face, not only on the hero front.
    global transform
    original=transform.copy()
    for side in [-1,1]:
        a=math.pi/2*side
        transform=original@Matrix.Rotation(a,4,'Y')
        for f in range(floors):
            for zz in [-d*.24,d*.24]:window(zz,f*3.2+1,-w/2-.20,shutter='green',w=.82,h=1.22)
    transform=original
    box('trim',(0,h-.10,-d/2-.03),(w+.26,.22,.42))
    for xx in [-w/2+.13,w/2-.13]:
        for j in range(int(h/.42)):
            box('trim',(xx,j*.42+.21,-d/2-.06),(.34 if j%2 else .52,.36,.20))
    roof(w,d,h)
    cx=w*.29;cz=d*.24
    box('lime',(cx,h+.70,cz),(.66,1.9,.66));box('trim',(cx,h+1.65,cz),(.83,.13,.83))
    box('metal',(cx,h+1.73,cz),(.47,.025,.47))
    # Downpipe and roof gutter.
    tube('metal',(-w/2-.08,h-.12,-d/2-.36),(w/2+.08,h-.12,-d/2-.36),.047,n=8)
    tube('metal',(w/2-.15,.08,-d/2-.31),(w/2-.15,h-.1,-d/2-.31),.038,n=8)
    if balcony and floors>1:
        box('trim',(0,3.10,-d/2-.86),(min(w-1,4.8),.18,1.8))
        rail((-2.25,3.2,-d/2-1.72),(2.25,3.2,-d/2-1.72))
        rail((-2.25,3.2,-d/2-1.72),(-2.25,3.2,-d/2));rail((2.25,3.2,-d/2-1.72),(2.25,3.2,-d/2))
    if shop:
        sign(shop,0,2.61,-d/2-3.27,min(w-1,5))
        for xx in [-w*.42,w*.42]:tube('wood',(xx,.05,-d/2-3.1),(xx,2.7,-d/2-3.1),.065,n=8)
        # Fabric canopy with individual stripe panels and scalloped valance.
        for i in range(int(w/.45)):
            xa=-w/2+i*.45;xb=min(w/2,xa+.45);sm='canvas' if i%2==0 else 'canvas_red'
            geom(sm,[(xa,3.07,-d/2-.2),(xb,3.07,-d/2-.2),(xb,2.75,-d/2-3.2),(xa,2.75,-d/2-3.2)],[(0,1,2,3),(3,2,1,0)])
            box(sm,((xa+xb)/2,2.61,-d/2-3.2),(xb-xa,.28,.035))
    # Front threshold and lamps.
    box('paving',(0,.05,-d/2-.6),(2.25,.10,1.1))
    for xx in [-1.25,1.25]:
        box('metal',(xx,2.10,-d/2-.27),(.17,.34,.20));box('amber',(xx,2.10,-d/2-.39),(.10,.23,.03))
    if shop=='TAVERNA':
        box('wood',(0,1,-d*.05),(6,.95,.8));box('trim',(0,1.54,-d*.05),(6.2,.12,1))
        for i in range(15):lathe('green',(-2.6+i*.37,1.61,-d*.05),[(.06,0),(.065,.25),(.025,.3),(.025,.38)],8)
    return original

def chair(x,y,z,a=0):
    global transform
    old=transform.copy();transform=old@Matrix.Translation((x,y,z))@Matrix.Rotation(a,4,'Y')
    for xx in [-.23,.23]:
        for zz in [-.22,.22]:tube('blue',(xx,0,zz),(xx,.47 if zz<0 else .93,zz),.024,n=6)
    for i in range(5):box('wood',(0,.47,-.20+i*.10),(.5,.04,.065))
    for yy in [.67,.80,.92]:box('blue',(0,yy,.22),(.5,.065,.04))
    transform=old

def table(x,y,z):
    lathe('wood',(x,y,z),[(.52,.73),(.52,.79),(.0,.79)],24)
    tube('metal',(x,y,z),(x,y+.73,z),.055,n=8)
    for a in [0,math.pi/2]:box('metal',(x,y+.055,z),(.75,.07,.10),a)
    for a in [0,math.pi/2,math.pi,math.pi*1.5]:chair(x+math.sin(a)*.84,y,z+math.cos(a)*.84,a+math.pi)
    lathe('cream',(x+.16,y+.8,z),[(.05,0),(.05,.10),(.035,.10)],12)
    lathe('cream',(x-.17,y+.8,z+.04),[(.10,0),(.10,.01)],16)

def pot(x,y,z,size=1,flower=False):
    lathe('terracotta',(x,y,z),[(.22*size,0),(.34*size,.55*size),(.36*size,.58*size),(.32*size,.62*size),(.28*size,.56*size)],18)
    lathe('soil',(x,y+.55*size,z),[(.29*size,0),(0,0)],18)
    for j in range(14):
        a=rng.random()*math.tau;r=rng.random()*.25*size;hh=(.3+rng.random()*.45)*size
        base=Vector((x+r*math.cos(a),y+.55*size,z+r*math.sin(a)));tip=base+Vector((math.cos(a)*.23*size,hh,math.sin(a)*.23*size))
        tube('leaves',base,tip,.013,n=5)
        for k in range(3):
            c=base+(tip-base)*(.4+k*.2);v=Vector((math.cos(a+1)*.17*size,.05,math.sin(a+1)*.17*size))
            geom('leaves',[tuple(c),tuple(c+v+Vector((.04,.05,.02))),tuple(c+v*1.4),tuple(c+v+Vector((-.03,-.04,-.02)))],[(0,1,2,3),(3,2,1,0)])
        if flower:lathe('flowers',tip,[(.02,0),(.09,.04),(.015,.09)],8)

def bench(x,y,z,a=0):
    global transform
    old=transform.copy();transform=old@Matrix.Translation((x,y,z))@Matrix.Rotation(a,4,'Y')
    for xx in [-.72,.72]:
        for zz in [-.22,.22]:tube('metal',(xx,0,zz),(xx,.47,zz),.035,n=8)
        tube('metal',(xx,.45,.22),(xx,.95,.36),.03,n=8)
    for i in range(5):box('wood',(0,.47,-.23+i*.105),(1.85,.055,.075))
    for yy in [.69,.82,.95]:box('wood',(0,yy,.29+(yy-.69)*.28),(1.85,.085,.04))
    transform=old

def leaf_cloud(center,radius,count,flower=False):
    c=Vector(center)
    for _ in range(count):
        a=rng.uniform(0,math.tau);b=rng.uniform(-1,1);r=radius*rng.random()**.333
        p=c+Vector((math.cos(a)*math.sqrt(1-b*b)*r,b*r*.48,math.sin(a)*math.sqrt(1-b*b)*r))
        axis=Vector((rng.uniform(-1,1),rng.uniform(-.6,.6),rng.uniform(-1,1))).normalized()
        side=axis.cross(Vector((0,1,0))).normalized();length=rng.uniform(.19,.30);width=length*.24
        verts=[tuple(p-axis*length/2),tuple(p+side*width),tuple(p+axis*length/2),tuple(p-side*width),tuple(p+Vector((0,.035,0)))]
        mat='flowers' if flower and rng.random()<.33 else rng.choice(['olive_leaf','olive_silver','leaves'])
        geom(mat,verts,[(0,1,4),(1,2,4),(2,3,4),(3,0,4),(4,1,0),(4,2,1),(4,3,2),(4,0,3)])

def ancient_olive():
    start('Ancient_olive_sculpt',(-8,0,-44))
    # Lobed, fluted trunk with a dark hollow and exposed buttress roots.
    verts=[];rings=18;segments=28
    for j in range(rings):
        h=j*.21;radius=1.42-.095*j+.15*math.sin(j*.7)
        radius=max(.60,radius)
        for i in range(segments):
            a=i*math.tau/segments+.018*j
            rr=radius*(1+.23*math.sin(a*7+j*.19)+.09*math.sin(a*11-j*.13))
            verts.append((math.cos(a)*rr+.16*math.sin(j*.28),h,math.sin(a)*rr))
    faces=[]
    for j in range(rings-1):
        for i in range(segments):
            if j<8 and 6<=i<=8:continue
            faces.append((j*segments+i,(j+1)*segments+i,(j+1)*segments+(i+1)%segments,j*segments+(i+1)%segments))
    geom('bark',verts,faces)
    tube('bark',(0,0,0),(.18,3.55,0),.63,.43,16)
    for i in range(9):
        a=i*math.tau/9;prev=Vector((math.cos(a)*.4,.4,math.sin(a)*.4))
        for j in range(1,7):
            p=Vector((math.cos(a+j*.05)*j*.42,max(.07,.5-j*.07),math.sin(a+j*.05)*j*.42))
            tube('bark',prev,p,.38-j*.035,.34-j*.035,8);prev=p
    for i in range(10):
        a=i*math.tau/10;prev=Vector((.1,2.4,0));points=[]
        for j in range(1,10):
            r=j*.51;p=Vector((math.cos(a+j*.065)*r,2.6+j*.34+math.sin(j*.45)*.25,math.sin(a+j*.065)*r))
            tube('bark',prev,p,max(.06,.40-j*.034),max(.04,.37-j*.034),9);prev=p;points.append(p)
        for j in [4,6,8]:
            p=points[j]
            end=p+Vector((math.cos(a+.6)*1.35,.8,math.sin(a+.6)*1.35))
            tube('bark',p,end,.07,.015,7);leaf_cloud(end,1.2,235)
        leaf_cloud(points[-1]+Vector((0,.4,0)),1.5,380)

def courtyard_details():
    # Uneven, planted domestic plots break up the larger street grid.
    for k,(x,z) in enumerate([(-56,-26),(59,-26),(-96,-26),(97,-26),(-56,-52),(59,-52),(-96,-52),(97,-52),(-56,-91),(59,-91)]):
        start('courtyard_'+str(k))
        y=level(z);side=-1 if x<0 else 1
        # Low enclosure with coping, a gate opening and a short worn path.
        for dz in [-8,8]:
            box('stone',(x,y+.48,z+dz),(17,.96,.5));box('trim',(x,y+.99,z+dz),(17.2,.10,.66))
        box('paving',(x-side*9,y-.02,z),(6,.10,1.25))
        for dx,dz in [(6,7),(-6,7),(7,-6)]:
            px,pz=x+dx,z+dz
            pot(px,y,pz,.85,k%2==0)
            leaf_cloud((px,y+.65,pz),.7,130,k%3==0)
        trees.append(dict(x=x+side*6,z=z+7,y=y,scale=.85+rng.random()*.3,variant=1+rng.randrange(3),hero=False))
        # Garden sheds and covered cooking/work areas, at a different orientation from the house.
        global transform
        old=transform.copy();transform=old@Matrix.Translation((x+side*8,y,z-7))@Matrix.Rotation(math.radians(8 if k%2 else -7),4,'Y')
        box('stone',(0,1.15,0),(3.8,2.3,3.0));roof(3.8,3,2.3)
        box('wood',(0,1,-1.55),(.85,2,.09))
        transform=old
    # Vines over the terrace and climbing selected house fronts.
    start('taverna_vines')
    for x in [-28,-21,-14,-7]:
        tube('wood',(x,0,-24),(x+.2,3.2,-24),.055,.025,8)
        for z in [-24,-27,-30]:leaf_cloud((x,3.3,z),1.8,230,True)
    # Soft planted edges: enough variation to avoid a row of identical trees.
    for i in range(32):
        x=rng.uniform(-118,118);z=rng.choice([-152,-156,-159])+rng.uniform(-3,3)
        trees.append(dict(x=x,z=z,y=level(z),scale=rng.uniform(.75,1.35),variant=1+rng.randrange(3),hero=False))
    for i in range(16):
        x=rng.choice([-115,115])+rng.uniform(-3,3);z=rng.uniform(-140,-13)
        trees.append(dict(x=x,z=z,y=level(z),scale=rng.uniform(.85,1.3),variant=1+rng.randrange(3),hero=False))

def chapel(x,z):
    # A single nave, high arched windows and a semicircular apse give the church
    # a different silhouette and rhythm from the surrounding domestic buildings.
    start('06_Agios_Nikolaos_Chapel',(x,0,z),90)
    placements.append(dict(name=current,x=x,z=z,y=0,width=10,depth=17,yaw=90,height=8.4,kind='building'))
    box('stone',(0,-.22,0),(10.4,.44,17.4))
    wall('lime',10,5.8,17,[(0,0,2.05,3.38)])
    box('lime',(0,2.9,8.5),(10,5.8,.4))
    for side in [-1,1]:
        box('lime',(side*5,2.9,0),(.4,5.8,17))
        box('trim',(side*5,5.65,0),(.65,.30,17.4))
        for pz in [-5,0,5]:
            box('trim',(side*5.15,1.6,pz),(.5,3.2,.65))
            box('glass',(side*5.23,3.95,pz),(.06,1.55,.78))
            for dz in [-.51,.51]:box('trim',(side*5.28,3.95,pz+dz),(.12,1.75,.18))
            box('trim',(side*5.29,3.06,pz),(.13,.18,1.2))
            for k in range(14):
                a=k*math.pi/14;b=(k+1)*math.pi/14
                geom('trim',[(side*5.29,4.71+math.sin(aa)*r,pz+math.cos(aa)*r) for r in [.4,.6] for aa in [a,b]],[(0,1,3,2),(2,3,1,0)])
    box('wood',(0,1.62,-8.48),(2,3.24,.16))
    for xx in [-.5,.5]:
        for yy in [.75,2.1]:box('redwood',(xx,yy,-8.59),(.75,1.1,.055))
    for side in [-1,1]:box('trim',(side*1.15,1.62,-8.63),(.25,3.25,.4))
    roof(10,17,5.9)
    # Polygonal apse with a low tiled conical roof.
    for k in range(12):
        a=k*math.pi/12;b=(k+1)*math.pi/12
        verts=[(math.cos(t)*3.8,y,8.3+math.sin(t)*3.8) for y in [0,4.6] for t in [a,b]]
        geom('lime',verts,[(0,1,3,2)])
        geom('roof',[(math.cos(a)*4,4.7,8.3+math.sin(a)*4),(math.cos(b)*4,4.7,8.3+math.sin(b)*4),(0,6.5,8.3)],[(0,1,2)])
    box('paving',(0,.04,-9.15),(3.4,.08,1.3))

def village():
    # Continuous, drivable public realm. The compact footprint preserves the through-road.
    start('public_ground')
    for ix in range(-22,22):
        for iz in range(-30,-2):
            x,z=ix*5,iz*5
            geom('soil',[(x,level(z)-.12,z),(x,level(z+5)-.12,z+5),(x+5,level(z+5)-.12,z+5),(x+5,level(z)-.12,z)],[(0,1,2,3)])
    start('olive_square')
    box('paving',(0,-.10,-43),(62,.20,43))
    for z in [-64.5,-21.5]:box('trim',(0,.015,z),(62,.04,.42))
    for x in [-31,31]:box('trim',(x,.015,-43),(.42,.04,43))
    # Inner square border and banded paving give the place a human scale.
    for z in [-59,-27]:box('trim',(-3,.016,z),(47,.035,.20))
    for x in [-26.5,20.5]:box('trim',(x,.016,-43),(.20,.035,32))
    # Two connected driving lanes and a lower cross street.
    for i,x in enumerate([-40,43,-78,80]):
        start('lane_'+str(i))
        for k in range(27):
            z=-12-k*5
            geom('paving',[(x-3,level(z)+.015,z),(x-3,level(z-5)+.015,z-5),(x+3,level(z-5)+.015,z-5),(x+3,level(z)+.015,z)],[(3,2,1,0)])
        for side in [-1,1]:
            for k in range(0,27):
                z=-14.5-k*5
                if abs(z+72)>6 and abs(z+112)>6:
                    cx=x+side*3.2
                    verts=[(cx+dx,level(z+dz)+.025+dy,z+dz) for dx,dy,dz in [(-.12,-.08,-2.45),(.12,-.08,-2.45),(.12,.08,-2.45),(-.12,.08,-2.45),(-.12,-.08,2.45),(.12,-.08,2.45),(.12,.08,2.45),(-.12,.08,2.45)]]
                    geom('trim',verts,[(0,3,2,1),(4,5,6,7),(0,4,7,3),(1,2,6,5),(3,7,6,2),(0,1,5,4)])
    # Surveyed connections to the existing curved road. The long approach keeps
    # grades comfortable, and the last section rolls over the sidewalk curb.
    for i,(x,edge,road,roll,bank) in enumerate([(-78,-3,340.142,-1,.020),(-40,-7,339.762,-5,-.007),(43,-5,340.401,-3,.024),(80,-5,341.201,-3,.016)]):
        start('entrance_'+str(i))
        def approach(z,dx=0):
            if z<=edge:
                t=max(0,min(1,(z+32)/(edge+32)))
                h=.015+(road-BASE+.17-.015)*(t*t*(3-2*t))
            else:
                t=min(1,(z-edge)/(roll-edge));h=road-BASE+.17*(1-t)+.012
            return h+bank*dx*max(0,min(1,(z+32)/(edge+32)))
        steps=32
        for k in range(steps):
            z0=-32+(roll+32)*k/steps;z1=-32+(roll+32)*(k+1)/steps
            w0=3+max(0,(z0+14)/(roll+14))*1.15;w1=3+max(0,(z1+14)/(roll+14))*1.15
            geom('paving',[(x-w0,approach(z0,-w0),z0),(x-w1,approach(z1,-w1),z1),(x+w1,approach(z1,w1),z1),(x+w0,approach(z0,w0),z0)],[(0,1,2,3)])
    for j,z in enumerate([-72,-112]):
        start('cross_lane_'+str(j))
        # Follow the terrace slope through each junction without a raised slab edge.
        for dz in [-3,-2,-1,0,1,2]:
            a,b=z+dz,z+dz+1
            geom('paving',[(-83,level(a)+.017,a),(-83,level(b)+.017,b),(83,level(b)+.017,b),(83,level(a)+.017,a)],[(0,1,2,3)])
    # Primary square architecture.
    building('01_Olive_Tree_Taverna',-17,-14,19,10,2,'lime',shop='TAVERNA',balcony=True)
    building('02_Village_Bakery',12,-13,11,10,1,'ochre',shop='BAKERY')
    building('03_Olive_Oil_Cooperative',-20,-85,16,10,2,'stone',180,'OLIVE OIL',True)
    building('04_Local_Provisions',0,-83,12,11,2,'rose',180,'PANTOPOLIO',True)
    building('05_Guesthouse',18,-85,11,11,2,'lime',180,None,True)
    # Chapel: a deep apse-like body, arched portal and Venetian bell tower.
    chapel(28,-47)
    start('chapel_details',(28,0,-47),90)
    # Layered voussoirs around a semicircular church portal.
    for i in range(15):
        a=i*math.pi/15;b=(i+1)*math.pi/15
        geom('trim',[(math.cos(aa)*r,2.38+math.sin(aa)*r,z) for z in [-8.78,-8.45] for r in [1.04,1.28] for aa in [a,b]],[(0,1,3,2),(4,6,7,5),(0,4,5,1),(2,3,7,6)])
    tube('metal',(0,8.15,0),(0,9.75,0),.06);tube('metal',(-.55,9.2,0),(.55,9.2,0),.06)
    start('07_Bell_Tower',(32,0,-29))
    box('stone',(0,.35,0),(4.6,.7,4.6));box('lime',(0,4.4,0),(3.8,8.2,3.8))
    for h in [1.2,6.5,8.35,11.5]:box('trim',(0,h,0),(4.2,.25,4.2))
    for x in [-1.5,1.5]:
        for z in [-1.5,1.5]:box('lime',(x,9.95,z),(.55,3.05,.55))
    for z in [-1.5,1.5]:box('lime',(0,11.35,z),(3.8,.4,.55))
    for x in [-1.5,1.5]:box('lime',(x,11.35,0),(.55,.4,3.8))
    lathe('metal',(0,8.8,0),[(.73,0),(.7,.12),(.42,.35),(.3,.8),(.13,1)],24)
    tube('wood',(-1.6,10.55,0),(1.6,10.55,0),.15)
    roof(4.3,4.3,11.65);tube('metal',(0,12.7,0),(0,14.05,0),.04);tube('metal',(-.35,13.6,0),(.35,13.6,0),.04)
    placements.append(dict(name='07_Bell_Tower',x=32,z=-29,y=0,width=4.6,depth=4.6,yaw=0,height=14.1,kind='building'))
    # Inhabited lanes, with varied footprints and small setbacks.
    idx=8
    for x,face in [(-56,-90),(59,90),(-96,-90),(97,90)]:
        for j,z in enumerate([-26,-52,-91]):
            idx+=1;w=[10,12,9][j];d=[10,11,9][j]
            building(f'{idx:02d}_Lane_House',x,z,w,d,1 if (idx%4==0) else 2,['lime','ochre','sage_plaster','stone'][idx%4],face,balcony=idx%3==0)
    for j,x in enumerate([-85,-60,-33,-6,23,53,83]):
        idx+=1
        building(f'{idx:02d}_Lower_Lane_'+('Workshop' if j==1 else 'House'),x,-129+(j%2)*2,14 if j==1 else 10+(j%3),10,1 if j%3==0 else 2,['stone','lime','rose'][j%3],180,'MOTOR WORKS' if j==1 else None,balcony=j%2==0)
    # Garden walls, gates and terraced plots.
    start('gardens')
    for x in [-110,110]:
        for z in range(-20,-146,-6):box('stone',(x,level(z)+.47,z),(.6,.95,5.8))
    for x in [-95,-58,59,96]:
        for z in [-39,-68,-105]:
            y=level(z);box('stone',(x,y+.45,z),(20,.9,.48));box('trim',(x,y+.93,z),(20.15,.10,.62))
    for x in [-23,5,34]:
        box('stone',(x,level(-145)+.45,-145),(23,.9,.5));box('trim',(x,level(-145)+.93,-145),(23.2,.1,.65))
    # Old olive tree ring, drinking fountain and comfortable edges.
    start('square_furniture')
    lathe('stone',(-8,.0,-44),[(3.5,0),(3.5,.42),(3.1,.42),(3.1,0)],48)
    lathe('trim',(-8,.42,-44),[(3.57,0),(3.57,.12),(3.06,.12),(3.06,0)],48)
    # The square's hero tree is modeled separately, with an old fluted trunk.
    for x,z,a in [(-22,-54,0),(-22,-33,0),(10,-55,0),(7,-29,math.pi)]:bench(x,0,z,a)
    box('stone',(15,1,-61),(2.2,2,.60));box('trim',(15,2.06,-61),(2.4,.18,.82))
    lathe('trim',(15,.35,-60.4),[(.76,0),(.80,.20),(.67,.25),(.56,.11)],24)
    tube('metal',(15,1.15,-60.7),(15,1.15,-60.35),.035)
    sign('EXO CHORA',15,1.65,-61.35,1.8)
    # Taverna and bakery terraces with individually modeled tables, chairs and ceramics.
    for x,z in [(-24,-26),(-19,-28),(-13,-26),(-7,-28),(9,-25),(15,-25)]:table(x,0,z)
    for x,z in [(-30,-24),(-4,-22),(5,-23),(21,-23),(-27,-59),(19,-59),(22,-40),(-47,-34),(-47,-58),(51,-25),(51,-60),(-10,-78),(11,-77)]:pot(x,level(z),z,rng.uniform(.8,1.3),True)
    # Pergola between the taverna and tree.
    for x in [-29,-4]:
        for z in [-23,-31]:tube('wood',(x,0,z),(x,3.0,z),.085,n=8)
    for z in [-23,-31]:box('wood',(-16.5,3,z),(25.5,.16,.18))
    for x in range(-29,-3):box('wood',(x,3.10,-27),(.1,.13,8.3))
    # Arrival parking: six 2.6 x 5.2 m bays, a generous maneuvering aisle and entry sign.
    start('arrival_parking')
    box('paving',(65,level(-13)-.01,-13),(29,.18,12))
    for i in range(8):box('cream',(53+i*2.65,level(-14)+.085,-14),(.10,.014,5.1))
    box('cream',(62.3,level(-16.55)+.09,-16.55),(19,.014,.10))
    for x in [49,81]:
        tube('metal',(x,level(-9),-9),(x,level(-9)+2.5,-9),.045)
    sign('EXO CHORA',64,2.5,-8,4.6)
    # A little workshop life: crates, barrels and olive-oil jars.
    start('working_details')
    for i in range(9):
        x=-26+(i%3)*.72;z=-76-(i//3)*.68;y=level(z)
        box('wood',(x,y+.29,z),(.62,.58,.54))
        for k in range(4):box('trim',(x,y+.10+k*.13,z-.28),(.56,.045,.02))
    for x in [-63,-61,-59]:lathe('terracotta',(x,level(-121),-121),[(.35,0),(.48,.4),(.43,.9),(.23,1.15),(.26,1.23)],20)
    # Evening practicals and warm windows.
    for i,(x,z) in enumerate([(-32,-23),(-32,-64),(34,-65),(46,-105),(-43,-108),(84,-25),(-78,-18)]):
        start('lamp_'+str(i));y=level(z)
        lathe('metal',(x,y,z),[(.17,0),(.14,.22),(.065,.26),(.055,3.5),(.1,3.6)],12)
        box('metal',(x,y+3.77,z),(.38,.5,.38));box('amber',(x,y+3.77,z),(.28,.35,.28))
        lathe('metal',(x,y+4.02,z),[(.31,0),(.06,.20)],12)
        lights.append(dict(name='Village lantern '+str(i+1),position=[x,y+3.77,z],lumens=1600,range=12))
    # Trees are existing local olive assets, plus bushes and planted flowers.
    for x,z,sc in [(-103,-14,1.1),(-106,-60,1.3),(-105,-121,1.1),(108,-32,1.15),(108,-95,1.2),(75,-144,1.4),(-42,-146,1.2),(37,-99,.9),(-25,-100,1.0),(9,-102,.95)]:trees.append(dict(x=x,z=z,y=level(z),scale=sc,variant=1+rng.randrange(3),hero=False))

def make_materials():
    result={}
    for name,(rgb,texture,scale,rough) in MATS.items():
        m=bpy.data.materials.new('exo_'+name);m.use_nodes=True;m.diffuse_color=(*rgb,1)
        nodes=m.node_tree.nodes;links=m.node_tree.links;bsdf=nodes.get('Principled BSDF');bsdf.inputs['Base Color'].default_value=(*rgb,1);bsdf.inputs['Roughness'].default_value=rough
        if name=='metal':bsdf.inputs['Metallic'].default_value=.75
        if name=='amber':bsdf.inputs['Emission Color'].default_value=(*rgb,1);bsdf.inputs['Emission Strength'].default_value=1
        if texture:
            for channel,target in [('diff','Base Color'),('rough','Roughness'),('nor_gl','Normal')]:
                path=OUT/'textures'/f'{texture}_{channel}.jpg'
                if not path.exists():path=path.with_suffix('.png')
                if not path.exists():continue
                tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(path),check_existing=True)
                if channel!='diff':tex.image.colorspace_settings.name='Non-Color'
                if channel=='diff':
                    mix=nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;mix.inputs[2].default_value=(*rgb,1);links.new(tex.outputs['Color'],mix.inputs[1]);links.new(mix.outputs[0],bsdf.inputs[target])
                elif channel=='nor_gl':
                    n=nodes.new('ShaderNodeNormalMap');n.inputs['Strength'].default_value=.7;links.new(tex.outputs['Color'],n.inputs['Color']);links.new(n.outputs[0],bsdf.inputs[target])
                else:links.new(tex.outputs[0],bsdf.inputs[target])
        result[name]=m
    return result

def build():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    village();ancient_olive();courtyard_details();materials=make_materials();objects=[]
    for (name,mat),(vertices,faces) in groups.items():
        mesh=bpy.data.meshes.new(name+'_'+mat);mesh.from_pydata([(x,-z,y) for x,y,z in vertices],[],faces);mesh.update()
        ob=bpy.data.objects.new(name+'_'+mat,mesh);bpy.context.collection.objects.link(ob);mesh.materials.append(materials[mat]);ob['spartan_material']=mat;ob['spartan_group']=name
        # Real-world texture scale with planar projection per face; roofs use slope-length UVs.
        uv=mesh.uv_layers.new(name='UVMap');scale=MATS[mat][2]
        for poly in mesh.polygons:
            norm=poly.normal;axis=max(range(3),key=lambda j:abs(norm[j]));axes=[j for j in range(3) if j!=axis]
            for li in poly.loop_indices:
                p=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(p[axes[0]]/scale,p[axes[1]]/scale)
        # Modest bevels give masonry and wood real highlights at pedestrian scale.
        if mat in ['lime','ochre','rose','sage_plaster','trim','stone'] and len(mesh.polygons)<12000:
            bevel=ob.modifiers.new('Worn edges','BEVEL');bevel.width=.025;bevel.segments=1
            bevel.limit_method='ANGLE';bevel.angle_limit=.6
        objects.append(ob)
    # Convert sign lettering from Blender's curves to exportable mesh geometry.
    for i,f in enumerate(features):
        curve=bpy.data.curves.new('sign_text','FONT');curve.body=f['text'];curve.align_x='CENTER';curve.align_y='CENTER';curve.size=.45;curve.extrude=.002;curve.resolution_u=3
        ob=bpy.data.objects.new('lettering_'+str(i),curve);bpy.context.collection.objects.link(ob)
        x,y,z=f['position'];ob.location=(x,-z,y)
        q=Matrix(((1,0,0,0),(0,0,-1,0),(0,1,0,0),(0,0,0,1)))
        from mathutils import Quaternion
        # Text lies in XY in Blender; face it toward local -Z in Spartan.
        local=Quaternion(f['rotation']).to_matrix().to_4x4()
        ob.rotation_euler=(q@local).to_euler()
        bpy.context.view_layer.update()
        if ob.dimensions.x>0:ob.scale*=min(1,f['width']*.9/max(ob.dimensions.x,.1))
        ob.data.materials.append(materials[f['material']]);ob['spartan_material']=f['material'];ob['spartan_group']='signs';objects.append(ob)
    # Save the exact authored scene. Rendering is performed separately to allow iteration.
    scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    for img in bpy.data.images:
        if img.source=='FILE' and img.filepath:
            img.filepath=bpy.path.relpath(img.filepath,start=str(OUT/'sources'))
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'sources/exo_chora.blend'))
    # Export evaluated meshes using split corner normals and explicit UVs.
    deps=bpy.context.evaluated_depsgraph_get();packets=[]
    for i,ob in enumerate(objects):
        ev=ob.evaluated_get(deps);mesh=ev.to_mesh();mesh.calc_loop_triangles()
        if not mesh.loop_triangles:ev.to_mesh_clear();continue
        positions=[];normals=[];uvs=[];indices=[];lookup={};uv=mesh.uv_layers.active
        for tri in mesh.loop_triangles:
            for li in tri.loops:
                loop=mesh.loops[li];p=ob.matrix_world@mesh.vertices[loop.vertex_index].co
                n=ob.matrix_world.to_3x3()@mesh.corner_normals[li].vector
                tx=uv.data[li].uv[:] if uv else (0,0)
                pp=(round(p.x,5),round(p.z,5),round(-p.y,5));nn=(round(n.x,5),round(n.z,5),round(-n.y,5));tt=(round(tx[0],5),round(tx[1],5))
                key=pp+nn+tt
                if key not in lookup:
                    lookup[key]=len(positions)//3;positions.extend(pp);normals.extend(nn);uvs.extend(tt)
                indices.append(lookup[key])
        ev.to_mesh_clear()
        assert len(positions)//3<=100000 and len(indices)<=300000,ob.name
        data=dict(positions=positions,normals=normals,uv0=uvs,indices=indices)
        digest=hashlib.sha256(json.dumps(data,separators=(',',':')).encode()).hexdigest()[:12]
        name='exo_'+ob.name+'_'+digest
        data['path']='project/mcp/blockout/meshes/'+name+'.mesh'
        packet=OUT/'packets'/(name+'.json');packet.write_text(json.dumps(data,separators=(',',':')))
        packets.append(dict(name=name,file=str(packet),mesh_path=data['path'],material=ob['spartan_material'],group=ob['spartan_group'],vertices=len(positions)//3,triangles=len(indices)//3))
    manifest=dict(origin=ORIGIN,yaw=YAW,buildings=placements,meshes=packets,materials={k:dict(color=v[0],texture=v[1],uv_meters=v[2],roughness=v[3]) for k,v in MATS.items()},trees=trees,lights=lights,features=features)
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print('VILLAGE_AUTHORING_COMPLETE',json.dumps(dict(buildings=len(placements),meshes=len(packets),triangles=sum(p['triangles'] for p in packets))))

if __name__=='__main__':build()
