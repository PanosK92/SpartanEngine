"""Author the player's home. Run with Blender --background --python this_file.

Runtime assets and editable Blender source live in binaries/project/home_garage.
The separate installer replaces only the home subtree in the island world.
"""
from pathlib import Path
import sys, math, json, hashlib, random, copy, wave
import xml.etree.ElementTree as ET
import bpy
import numpy as np
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import build_exo_chora as g
OUT = ROOT / 'binaries/project/home_garage'
for folder in ('meshes', 'materials', 'sources', 'packets', 'previews', 'audio'):
    (OUT / folder).mkdir(parents=True, exist_ok=True)
g.groups.clear()
box, tube, lathe, start = g.box, g.tube, g.lathe, g.start
rng = random.Random(2002)
# Muted pigments, warm wood, worn enamel and restrained brass.
MATS = {
    'plaster': ((.77,.71,.58),'painted_plaster_wall',3,.88),
    'stone': ((.64,.60,.49),'plastered_stone_wall',2,.92),
    'paving': ((.73,.69,.59),'cobblestone_floor_08',2.5,.88),
    'roof': ((.55,.26,.14),'clay_roof_tiles_02',2,.83),
    'floor': ((.36,.37,.34),None,2,.83),
    'wood': ((.22,.105,.047),None,1,.65),
    'oak': ((.46,.28,.13),None,1,.66),
    'cream': ((.87,.79,.60),None,1,.65),
    'sage': ((.19,.29,.25),None,1,.6),
    'oxblood': ((.26,.055,.035),None,1,.49),
    'metal': ((.045,.053,.05),None,1,.48),
    'chrome': ((.48,.52,.51),None,1,.25),
    'brass': ((.61,.37,.10),None,1,.3),
    'rubber': ((.019,.023,.024),None,1,.9),
    'amber': ((1,.52,.16),None,1,.35),
    'mint_glow': ((.22,.75,.58),None,1,.4),
    'soil': ((.18,.12,.06),None,1,1),
    'leaf': ((.17,.26,.065),None,1,.87),
    'flower': ((.63,.24,.36),None,1,.85),
    'paper': ((.74,.67,.48),None,1,.9),
    'lawn': ((.24,.30,.10),None,1,.98),
    'glass': ((.075,.13,.14),None,1,.13),
    'rug': ((.35,.15,.09),None,1,.97),
    'linen': ((.66,.60,.43),None,1,.94),
    'cork': ((.40,.25,.12),None,1,.95),
    'ceramic': ((.66,.72,.62),None,1,.30),
}
lights=[]

def foliage(p,s=1,count=38):
    x,y,z=p
    for i in range(count):
        a=rng.uniform(0,math.tau);r=rng.uniform(.06,.31)*s;h=rng.uniform(.10,.43)*s
        tip=(x+math.cos(a)*r,y+h,z+math.sin(a)*r)
        tube('leaf',(x,y,z),tip,.006*s,n=5)
        for j in range(3):
            t=.4+j*.23;cx=x+(tip[0]-x)*t;cy=y+h*t;cz=z+(tip[2]-z)*t
            d=a+j*2.4;dx,dz=math.cos(d)*.15*s,math.sin(d)*.15*s
            nx,nz=-math.sin(d)*.035*s,math.cos(d)*.035*s
            g.geom('leaf',[(cx,cy,cz),(cx+dx*.55+nx,cy+.02*s,cz+dz*.55+nz),(cx+dx,cy+.075*s,cz+dz),(cx+dx*.55-nx,cy+.02*s,cz+dz*.55-nz)],[(0,1,2,3),(3,2,1,0)])

def sign(text, p, size=.25, mat='cream'):
    curve=bpy.data.curves.new('Lettering','FONT');curve.body=text
    curve.align_x='CENTER';curve.align_y='CENTER';curve.size=size;curve.extrude=.0015;curve.resolution_u=3
    ob=bpy.data.objects.new('Lettering '+text,curve);bpy.context.collection.objects.link(ob)
    ob.location=(p[0],-p[2],p[1]);ob.rotation_euler=(math.pi/2,0,0)
    # Spartan's left-handed camera sees the +Z-facing sign with -X screen-right.
    # Reflect only the glyphs around their own centered origin, not the placement.
    ob.scale.x=-1
    ob['material']=mat;ob['group']='lettering'

def lamp(name,p,power=650):
    x,y,z=p
    tube('metal',(x,y+.6,z),(x,y+.1,z),.014)
    lathe('sage',(x,y,z),[(.34,0),(.30,.08),(.09,.23),(.055,.25)],24)
    lathe('amber',(x,y-.025,z),[(0,0),(.27,0),(.27,.035),(0,.035)],24)
    lights.append(dict(name=name,position=[x,y-.13,z],lumens=power,range=8))

def pot(x,z,s=1):
    lathe('roof',(x,.03,z),[(.19*s,0),(.29*s,.43*s),(.32*s,.45*s),(.32*s,.50*s),(.25*s,.5*s),(.24*s,.40*s)],20)
    lathe('soil',(x,.43*s,z),[(0,0),(.25*s,0)],20)
    for i in range(14):
        a=rng.random()*math.tau;r=rng.uniform(.12,.38)*s
        end=(x+math.cos(a)*r,.65*s+rng.random()*.38*s,z+math.sin(a)*r)
        tube('leaf',(x,.45*s,z),end,.012*s,n=5)
        foliage(end,.32*s,3)
        if i%3==0:lathe('flower',(end[0],end[1]+.12*s,end[2]),[(0,0),(.075*s,.025*s),(0,.06*s)],7)

def chair(x,z):
    for a in [-.29,.29]:
        for b in [-.28,.28]:tube('wood',(x+a,.06,z+b),(x+a,.52,z+b),.035)
    box('oxblood',(x,.52,z),(.7,.17,.72))
    box('oxblood',(x,.88,z-.32),(.72,.63,.18))
    for a in [-.4,.4]:box('wood',(x+a,.68,z),(.12,.12,.76))

def build_geometry():
    # Footprint fits the existing home terrace; +Z faces the island access road.
    start('foundation')
    box('stone',(0,-.30,2),(35,.6,28))
    box('floor',(-3,-.025,-2),(19,.08,17))
    box('paving',(0,-.005,10.5),(34,.08,10))
    g.geom('paving',[(-5,.035,15.5),(5,.035,15.5),(5,-.32,19),(-5,-.32,19),(-5,-.40,15.5),(5,-.40,15.5),(5,-.40,19),(-5,-.40,19)],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
    # Main workshop, 11 m wide. Open entrance and side opening into lounge.
    start('workshop')
    box('plaster',(-3.5,2.0,-10.3),(17,4,.35))
    box('plaster',(-12,2,-2.2),(.35,4,16.6))
    box('plaster',(-8.9,.65,6),(6.2,1.3,.35))
    box('plaster',(-8.9,3.30,6),(6.2,1,.35))
    for x in [-11.1,-6.7]:box('plaster',(x,2.05,6),(1.8,1.5,.35))
    box('glass',(-8.9,2.05,5.92),(2.6,1.5,.05))
    for x in [-10.27,-8.9,-7.53]:box('sage',(x,2.05,6.13),(.09,1.62,.12))
    for y in [1.25,2.05,2.85]:box('sage',(-8.9,y,6.13),(2.83,.08,.15))
    box('cream',(-8.9,1.20,6.22),(3.05,.13,.45))
    box('plaster',(3.9,1.9,6),(1.8,3.8,.35))
    box('wood',(-1.4,3.82,6),(8.7,.38,.42))
    for x in [-5.7,3.0]:box('sage',(x,1.85,5.91),(.16,3.7,.18))
    # Rolled-up sectional door, tracks and exposed rafters.
    for z in [4.8,5.05,5.3,5.55]:box('sage',(-1.35,3.66,z),(8.5,.09,.22))
    for x in [-5.5,2.8]:tube('chrome',(x,3.6,5.8),(x,3.6,1),.022)
    for z in [-9.8,-6,-2,2,5.7]:
        box('wood',(-3.6,4.1,z),(17,.20,.18))
        for x in [-11.8,4.5]:tube('wood',(x,3.3,z),(x+(.7 if x<0 else -.7),4.1,z),.065,n=4)
    # Low pitched roof: separate sloping panels, tile ribs and ridge.
    start('roof')
    for z in [-10.5,6.05]:
        g.geom('plaster',[(x,y,zz) for zz in [z-.1,z+.1] for x,y in [(-12.5,4.02),(-3.7,5.5),(5.1,4.02)]],[(0,1,2),(3,5,4),(0,3,4,1),(1,4,5,2),(2,5,3,0)])
    for x0,x1 in [(-12.7,-3.7),(-3.7,5.3)]:
        y0=4.15 if x0==-12.7 else 5.55;y1=5.55 if x1==-3.7 else 4.15
        g.geom('roof',[(x0,y0,-11),(x1,y1,-11),(x1,y1,6.7),(x0,y0,6.7),(x0,y0-.13,-11),(x1,y1-.13,-11),(x1,y1-.13,6.7),(x0,y0-.13,6.7)],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
        for z in np.arange(-10.9,6.7,.34):tube('roof',(x0,y0+.035,z),(x1,y1+.035,z),.045,n=6)
        tube('cream',(x0,y0,-11),(x1,y1,-11),.085,n=4)
        tube('cream',(x0,y0,6.7),(x1,y1,6.7),.085,n=4)
    tube('roof',(-3.7,5.57,-11),(-3.7,5.57,6.8),.12, n=12)
    for x in [-12.6,5.2]:tube('metal',(x,4.08,-11),(x,4.08,6.9),.07,n=12)
    tube('sage',(-12.6,.1,5.7),(-12.6,4.1,5.7),.055)
    # Attached lounge: cedar ceiling, open veranda and tall window apertures.
    start('lounge')
    box('plaster',(10.6,1.7,-10.3),(11.2,3.4,.35))
    box('plaster',(16.2,.48,-2.2),(.3,.96,16.6))
    box('plaster',(16.2,3.15,-2.2),(.3,.5,16.6))
    for z in [-10.3,-6.2,-2.1,2,6]:box('sage',(16.2,1.9,z),(.2,2,.16))
    for x in [5.2,10.7,16.2]:box('wood',(x,1.7,6),(.2,3.4,.2))
    box('wood',(10.7,3.35,6),(11.3,.26,.28))
    for x in np.arange(5.1,16.6,.22):box('oak',(x,3.48,-2.2),(.205,.075,17.3))
    box('roof',(10.7,3.60,-2.2),(11.6,.16,17.6))
    for x in np.arange(5.2,16.2,.19):box('oak',(x,.015,-2.1),(.18,.065,16.1))
    for y in np.arange(.2,1.1,.15):box('wood',(10.6,y,-10.06),(10.5,.13,.07))
    # Workshop furnishings, long workbench with legs, vise and pegboard.
    start('workbench')
    box('oak',(-5.6,1.03,-9.35),(10.4,.13,1.25))
    for x in [-10.3,-6,-1]:
        for z in [-9.8,-8.9]:box('metal',(x,.5,z),(.07,1,.07))
    box('wood',(-5.6,.3,-9.35),(10.2,.07,1.1))
    box('oak',(-6,2.25,-10.04),(8.8,1.7,.10))
    for x in np.arange(-10.2,-1.7,.21):
        for y in np.arange(1.55,2.95,.21):lathe('metal',(x,y,-9.98),[(.014,0),(.014,.008)],5)
    for i in range(15):
        x=-9.9+i*.52;y=2.22+(i%3)*.13
        tube('chrome',(x,y-.24,-9.91),(x,y+.15,-9.91),.022,n=8)
        if i%3:
            tube('chrome',(x-.055,y+.16,-9.91),(x+.055,y+.16,-9.91),.035,n=8)
        else:box('oxblood',(x,y-.19,-9.91),(.07,.19,.06))
    box('sage',(-2,1.15,-9.2),(.48,.25,.33));box('chrome',(-2,1.34,-9.2),(.42,.1,.3))
    tube('chrome',(-2.38,1.15,-9.2),(-1.7,1.15,-9.2),.025)
    # Tool chest with drawer gaps and casters.
    box('oxblood',(-10.4,.64,-6.7),(1.45,.95,.75))
    for y in [.3,.47,.64,.81,.98]:
        box('metal',(-10.4,y,-6.31),(1.34,.022,.025));box('chrome',(-10.4,y+.07,-6.27),(.93,.025,.04))
    box('rubber',(-10.4,1.15,-6.7),(1.53,.06,.79))
    for x in [-10.92,-9.88]:
        for z in [-6.95,-6.45]:lathe('rubber',(x,.03,z),[(.09,0),(.09,.15)],12)
    # Tires on wall rack, compressor, floor jack, oil and workshop clutter.
    for y in [.03,.26,.49]:
        lathe('rubber',(-10.5,y,-3.8),[(.20,0),(.34,.035),(.35,.19),(.30,.23),(.20,.23),(.20,0)],32)
    lathe('oxblood',(-10.4,.10,-1.9),[(.22,0),(.32,.12),(.32,.8),(.17,.91)],24)
    box('metal',(-10.4,1.13,-1.9),(.4,.3,.35))
    for i in range(7):
        x=-9.3+i*.38;box('sage' if i%2 else 'cream',(x,1.27,-9.5),(.20,.35,.15))
        box('metal',(x,1.48,-9.5),(.10,.065,.10))
    box('oxblood',(-7.8,.14,-4),(1,.17,.4));tube('metal',(-8.2,.2,-4),(-8.5,1,-4),.028)
    sign('GOOD MILES. GOOD MEMORIES.',(-5.9,3.42,-10.04),.25)
    sign('ISLAND MOTOR WORKS',(-1.4,4.34,6.82),.43)
    sign('EST. 2002',(-1.4,3.98,6.83),.15)
    # Lounge bar with panelled front, foot rail, shelves and bottles.
    start('bar')
    box('wood',(12.5,.56,-7),(5.5,1.12,1.1));box('oak',(12.5,1.17,-7),(5.8,.14,1.35))
    for x in np.arange(9.9,15.25,.16):box('oak',(x,.57,-6.435),(.025,.92,.02))
    tube('brass',(9.9,.23,-6.12),(15.1,.23,-6.12),.023)
    for y in [1.65,2.35]:
        box('oak',(12.5,y,-9.85),(5.7,.075,.5))
        for i in range(10):
            x=10+i*.49;lathe('sage' if i%3 else 'brass',(x,y+.04,-9.78),[(.055,0),(.057,.21),(.025,.25),(.025,.35)],12)
    for x in [10.3,12.3,14.3]:
        lathe('metal',(x,.05,-5.55),[(.26,0),(.26,.055),(.035,.12),(.035,.70)],20)
        lathe('oxblood',(x,.75,-5.55),[(0,0),(.31,0),(.31,.14),(0,.14)],24)
    sign('THE LONG WAY HOME',(12.45,2.95,-10.02),.28)
    for x in [11,13.5]:lamp('Bar pendant',(x,2.7,-6.8),330)
    # Small listening corner and sideboard.
    start('listening_corner')
    box('rug',(8.1,.061,1),(4.4,.022,4.1))
    for x in [6.03,10.17]:box('cream',(x,.075,1),(.025,.003,3.9))
    for z in [-.94,2.94]:box('cream',(8.1,.075,z),(4.15,.003,.025))
    for x in [7,8.1,9.2]:chair(x,-.7)
    box('wood',(8.1,.30,1.1),(2.55,.13,1.0))
    for x in [7.1,9.1]:box('wood',(x,.14,1.1),(.12,.3,.65))
    box('paper',(7.8,.39,1.2),(.47,.025,.33));box('oxblood',(8.35,.42,1.05),(.32,.04,.32))
    for x in [7.1,8.8]:lathe('cream',(x,.39,1),[(.075,0),(.08,.12),(.065,.14),(.065,.02)],16)
    # Record cabinet and old hi-fi, with sleeves collected over the years.
    box('wood',(14.8,.56,3.0),(2.1,1.05,.58));box('oak',(14.8,1.12,3),(2.2,.09,.67))
    for i in range(19):box(['oxblood','sage','cream','paper'][i%4],(14.0+i*.042,.73,3.32),(.033,.53,.26))
    box('metal',(15.2,1.25,3),(.91,.20,.42))
    box('chrome',(15.2,1.26,3.23),(.86,.15,.016))
    for x in [14.9,15.25,15.5]:lathe('metal',(x,1.3,3.25),[(.022,0),(.022,.035)],12)
    box('wood',(13.35,.42,3),(.45,.75,.40))
    # Jukebox: rounded crown, luminous twin arches, selector buttons, grille.
    start('jukebox')
    x,z=14.6,-1.9
    box('wood',(x,.75,z),(1.30,1.50,.65));box('oxblood',(x,.64,z+.35),(1.08,1.05,.06))
    for rad,mat in [(.64,'oak'),(.56,'amber'),(.49,'brass'),(.44,'mint_glow')]:
        for i in range(24):
            a=i*math.pi/24;b=(i+1)*math.pi/24
            tube(mat,(x+math.cos(a)*rad,1.47+math.sin(a)*rad,z+.4),(x+math.cos(b)*rad,1.47+math.sin(b)*rad,z+.4),.028)
        for s in [-1,1]:tube(mat,(x+s*rad,.15,z+.4),(x+s*rad,1.47,z+.4),.028)
    box('metal',(x,1.35,z+.4),(.76,.43,.06))
    for row in range(3):
        for col in range(6):box('paper',(x-.30+col*.12,1.21+row*.12,z+.442),(.085,.065,.012))
    for a in np.arange(-.40,.41,.08):tube('brass',(x+a,.26,z+.4),(x+a,.92,z+.4),.01,n=6)
    sign('SLOW ROADS',(x,1.81,z+.44),.105)
    lights.append(dict(name='Jukebox glow',position=[x,1.4,z+.7],lumens=65,range=2.5))
    # Trophy shelf: cups with handles, plaques and empty bays for future wins.
    start('memories')
    for y in [.72,1.65,2.58]:box('oak',(5.9,y,-9.7),(3.7,.10,.60))
    for x in [4.05,7.75]:box('wood',(x,1.65,-9.7),(.12,2.05,.64))
    for i,(x,y) in enumerate([(4.6,.78),(6,.78),(7.1,1.71),(4.9,1.71)]):
        box('wood',(x,y+.055,-9.58),(.34,.11,.28))
        box('brass',(x,y+.067,-9.425),(.18,.05,.015))
        lathe('brass',(x,y+.11,-9.58),[(.13,0),(.06,.05),(.035,.21),(.12,.26),(.20,.46),(.20,.50),(.17,.50),(.10,.29)],24)
        for s in [-1,1]:
            tube('brass',(x+s*.15,y+.29,-9.58),(x+s*.29,y+.42,-9.58),.018)
            tube('brass',(x+s*.29,y+.42,-9.58),(x+s*.19,y+.47,-9.58),.018)
    sign('MILES TO REMEMBER',(5.9,2.89,-10.04),.19)
    # Framed original racing illustrations, independently replaceable materials.
    for i,x in enumerate([-.5,1.3,3.1]):
        box('wood',(x,2.2,-10.04),(1.65,1.13,.11))
        box('paper',(x,2.2,-9.97),(1.47,.95,.025))
        box('print_'+str(i),(x,2.2,-9.949),(1.30,.78,.012))
    # Ceiling fan is authored around its own pivot and receives a rotation script.
    start('fan_mount')
    tube('metal',(9,3.42,-2),(9,2.93,-2),.025)
    start('fan')
    lathe('brass',(0,-.075,0),[(0,0),(.17,0),(.18,.12),(.11,.19),(0,.19)],24)
    for i in range(4):
        a=i*math.pi/2
        box('wood',(.61*math.cos(a),0,.61*math.sin(a)),(.9,.04,.20),-a)
    # Garden: lawn beds, low limestone wall, pergola and a clear vehicle corridor.
    start('garden')
    for x in [-13.5,11.2]:
        box('lawn',(x,.025,11.5),(5.4,.10,7))
        for z in [8,15]:box('stone',(x,.16,z),(5.7,.30,.25))
        for xx in [x-2.75,x+2.75]:box('stone',(xx,.16,11.5),(.25,.3,7))
        for i in range(26):
            xx=x+rng.uniform(-2.4,2.4);zz=rng.uniform(8.4,14.7)
            foliage((xx,.10,zz),rng.uniform(.8,1.2),18)
    for x in [-17,17]:box('stone',(x,.47,9),(.30,.94,14))
    for x,w in [(-11,12),(11,12)]:box('stone',(x,.47,16),(w,.94,.35))
    for x in [-5,5]:
        box('stone',(x,.71,16),(.55,1.42,.55));box('cream',(x,1.45,16),(.66,.10,.66))
        box('metal',(x,1.66,16),(.25,.34,.25));box('amber',(x,1.66,16),(.18,.25,.18))
        lights.append(dict(name='Gate lantern',position=[x,1.67,16],lumens=150,range=5))
    # Timber garden pergola to the left, climbing greenery and a bench.
    for x in [-15.8,-10.8]:
        for z in [8.6,13.8]:box('wood',(x,1.37,z),(.14,2.74,.14))
        box('wood',(x,2.75,11.2),(.17,.19,5.8))
    for z in np.arange(8.3,14.3,.45):box('oak',(-13.3,2.87,z),(5.8,.14,.09))
    for i in range(24):
        x=rng.uniform(-16,-10.6);z=rng.uniform(8.5,14)
        foliage((x,2.88,z),.6,12)
    for y,z in [(.5,11),(.83,10.63),(1.05,10.63)]:box('oak',(-13.3,y,z),(2.2,.13,.18 if y>.5 else .7))
    for x in [-14.1,-12.5]:box('metal',(x,.3,11),(.09,.5,.65))
    for x,z,s in [(5.6,6.5,1.2),(15.5,6.5,1.3),(-6.2,6.7,.9),(-16,15,1.1),(6.2,14,.8),(15,9,1.0)]:pot(x,z,s)
    # String lights over the veranda and warm workshop pendants.
    start('lighting')
    for i in range(17):
        x=5+i*.70;y=3.18-.20*math.sin(i/16*math.pi)
        if i: tube('metal',(x-.7,3.18-.20*math.sin((i-1)/16*math.pi),6.2),(x,y,6.2),.008,n=5)
        lathe('amber',(x,y-.13,6.2),[(0,0),(.045,.035),(.04,.075),(0,.10)],10)
    for x in [-7,-1]:
        for z in [-6,1]:lamp('Workshop pendant',(x,3.45,z),900)
    lamp('Porch pendant',(10.5,2.85,5.5),400)
    hero_details()

def ring(mat,p,r,t=.015,plane='xy',n=36):
    x,y,z=p
    def point(a):
        return (x+r*math.cos(a),y+r*math.sin(a),z) if plane=='xy' else (x+r*math.cos(a),y,z+r*math.sin(a))
    for i in range(n):tube(mat,point(i*math.tau/n),point((i+1)*math.tau/n),t,n=7)

def hero_details():
    # Trim and fasteners give the architecture a human scale without closing the bay.
    start('architectural_details')
    for y in [.14,1.12]:
        box('cream',(-3.6,y,-10.09),(16.8,.075,.07))
    box('sage',(-11.80,.15,-2.2),(.07,.22,16.3))
    box('oak',(10.6,.13,-10.06),(11,.18,.08))
    for x in [-12.05,4.9,16.2]:
        for z in [-10.3,6.05]:
            box('stone',(x,.28,z),(.34,.55,.38))
            for y in [.25,.43]:
                for dx in [-.105,.105]:tube('chrome',(x+dx,y,z+.19),(x+dx,y,z+.205),.022,n=8)
    for x in [-11.5,-9.6,-7.7]:
        box('cream',(x,.65,6.20),(.025,1.1,.035))
    # Solid fascia over the long lounge eave and discrete rafter tails.
    box('wood',(10.7,3.5,6.57),(11.6,.20,.12))
    for x in np.arange(5.25,16.2,.64):box('oak',(x,3.43,6.52),(.085,.14,.7))
    for z in [-9,-5.3,-1.6,2.1]:
        box('cream',(16.07,1.02,z),(.35,.10,3.4))
    # Entrance mats and a recessed-looking drainage grille across the threshold.
    box('rubber',(11.8,.068,5.5),(2.2,.026,.85))
    for x in np.arange(10.78,12.9,.09):box('cork',(x,.086,5.5),(.05,.008,.74))
    box('metal',(-1.3,.054,6.34),(8.2,.025,.20))
    for x in np.arange(-5.35,2.8,.11):box('chrome',(x,.070,6.34),(.026,.01,.19))
    # A numbered enamel door plate, wiring conduit and a power meter on the side.
    box('sage',(4.05,2.1,6.23),(.55,.64,.055));sign('02',(4.05,2.11,6.265),.30)
    tube('sage',(-11.75,.3,-7.8),(-11.75,3.8,-7.8),.022)
    box('sage',(-11.67,1.6,-7.8),(.16,.52,.36))
    # Painted service bay has clear wheel tracks; props stay outside its center.
    start('service_bay_details')
    for x in [-4.4,2]:box('cream',(x,.021,-1.8),(.055,.006,11.6))
    for z in [-7.6,4]:
        for x in [-3.9,1.5]:box('cream',(x,.021,z),(1,.006,.055))
    # Inspection trolley with a rolling seat and a folded shop cloth.
    box('sage',(-6.9,.77,-4.3),(1.05,.065,.62));box('sage',(-6.9,.21,-4.3),(1.05,.055,.62))
    for x in [-7.36,-6.44]:
        for z in [-4.55,-4.05]:
            tube('chrome',(x,.13,z),(x,.79,z),.018)
            ring('rubber',(x,.10,z),.07,.026)
    box('linen',(-6.75,.816,-4.3),(.39,.025,.32))
    for i in range(8):tube('chrome',(-7.25+i*.055,.825,-4.35),(-7.25+i*.055,.825,-4.15),.012,n=8)
    lathe('rubber',(-7.6,.47,-2.8),[(0,0),(.27,0),(.27,.1),(0,.1)],24)
    tube('chrome',(-7.6,.1,-2.8),(-7.6,.5,-2.8),.035)
    for i in range(4):
        a=i*math.pi/2;tube('metal',(-7.6,.15,-2.8),(-7.6+math.cos(a)*.3,.1,-2.8+math.sin(a)*.3),.025)
    # A pair of axle stands, kept against the equipment wall.
    for z in [-5.5,-4.7]:
        for dx in [-.18,.18]:
            for dz in [-.15,.15]:tube('oxblood',(-10.7+dx,.03,z+dz),(-10.7,.42,z),.033,n=6)
        box('chrome',(-10.7,.46,z),(.27,.05,.12))
    # Hanging air hose: actual coils and end fittings.
    for k in range(5):ring('rubber',(-10.8,2.0,-9.9+k*.014),.29,.012)
    tube('rubber',(-10.51,2,-9.84),(-10.51,1.21,-9.84),.012)
    tube('brass',(-10.51,1.21,-9.84),(-10.51,1.13,-9.84),.023)
    # Socket set, spanners, spare spark plugs and a labelled parts tray.
    start('bench_details')
    box('oxblood',(-5.1,1.13,-9.18),(1.16,.09,.48))
    for i in range(12):lathe('chrome',(-5.6+i*.09,1.18,-9.2),[(.028,0),(.028,.05+(i%6)*.012),(.018,.06+(i%6)*.012),(.018,.01)],12)
    for i in range(6):
        x=-6.3+i*.22
        tube('chrome',(x,1.105,-9.10),(x+.08,1.105,-8.81),.018,n=8)
        ring('chrome',(x,1.105,-9.10),.035,.010,plane='xz',n=12)
    for i in range(4):
        lathe('ceramic',(-3.7+i*.14,1.10,-9.5),[(.016,0),(.026,.04),(.022,.10),(.014,.15)],12)
    box('sage',(-3.1,1.15,-9.1),(.53,.12,.42))
    for i in range(14):
        x=-3.25+(i%5)*.065;z=-9.22+(i//5)*.07
        lathe('chrome',(x,1.21,z),[(.018,0),(.018,.018)],6)
    # Patched service notebook and pinned receipts rather than empty surfaces.
    box('wood',(-.85,1.12,-9.35),(.48,.055,.63))
    box('paper',(-.85,1.151,-9.35),(.44,.01,.59))
    for z in np.arange(-9.55,-9.13,.06):box('metal',(-.85,1.158,z),(.29,.001,.003))
    box('cork',(.85,3.3,-10.055),(2.1,.65,.07))
    for x in [.1,.65,1.25,1.65]:
        box('paper',(x,3.3,-10.008),(.3,.42,.013));tube('oxblood',(x,3.48,-9.99),(x,3.48,-9.976),.018)
    # Analog wall clock with a real dial, tick marks and hands.
    start('clock_and_memorabilia')
    cx,cy,cz=3.25,3.27,-10.02
    ring('chrome',(cx,cy,cz),.37,.035)
    # A disk is authored in XY, facing into the workshop.
    center=(cx,cy,cz+.012)
    vv=[center]+[(cx+.34*math.cos(i*math.tau/48),cy+.34*math.sin(i*math.tau/48),cz+.012) for i in range(48)]
    g.geom('cream',vv,[(0,i+1,(i+1)%48+1) for i in range(48)])
    for i in range(12):
        a=i*math.tau/12;tube('metal',(cx+.28*math.cos(a),cy+.28*math.sin(a),cz+.023),(cx+.315*math.cos(a),cy+.315*math.sin(a),cz+.023),.008,n=5)
    tube('metal',(cx,cy,cz+.035),(cx-.12,cy+.16,cz+.035),.014)
    tube('metal',(cx,cy,cz+.038),(cx+.25,cy+.03,cz+.038),.009)
    # Rally plaques and rosettes share the trophy shelves, leaving future bays free.
    for i,x in enumerate([5.7,6.5]):
        box('wood',(x,2.12,-9.59),(.47,.54,.10));box('brass',(x,2.12,-9.523),(.37,.42,.015))
        sign('CLASS WIN' if i==0 else 'ISLAND RUN',(x,2.12,-9.506),.047,mat='metal')
    for x in [6.6,7.1]:
        ring('oxblood',(x,1.23,-9.38),.10,.045,n=20)
        for dx in [-.055,.055]:box('oxblood',(x+dx,1.05,-9.38),(.06,.20,.016))
    # Coffee ritual and a well-used bar: cups, drainer, tea tin, towels, fruit bowl.
    start('bar_details')
    box('sage',(14.3,1.44,-7.22),(.73,.43,.45));box('chrome',(14.3,1.42,-6.98),(.58,.27,.055))
    tube('chrome',(14.3,1.51,-6.94),(14.3,1.42,-6.77),.035)
    box('metal',(14.3,1.25,-6.86),(.67,.04,.31))
    for x in [14.13,14.46]:
        lathe('cream',(x,1.28,-6.85),[(.055,0),(.065,.10),(.053,.11),(.047,.025)],18)
        ring('cream',(x+.065,1.34,-6.85),.035,.009,n=16)
    lathe('chrome',(15,1.26,-7.15),[(.10,0),(.13,.05),(.11,.21),(.08,.26),(0,.27)],24)
    tube('chrome',(15.09,1.35,-7.15),(15.25,1.49,-7.15),.025)
    ring('metal',(14.93,1.43,-7.15),.13,.018,n=24)
    box('linen',(12.9,1.255,-6.8),(.40,.018,.38))
    for x in [10.4,10.85,11.3]:
        lathe('ceramic',(x,1.25,-6.85),[(.08,0),(.09,.17),(.076,.19),(.065,.03)],20)
        ring('ceramic',(x+.09,1.35,-6.85),.04,.012,n=16)
    lathe('oak',(12,1.25,-7),[(.17,0),(.29,.11),(.27,.14),(.15,.035)],28)
    for dx,dz in [(-.09,0),(.08,0),(0,.11)]:lathe('roof',(12+dx,1.30,-7+dz),[(0,0),(.065,.02),(.078,.08),(.045,.14),(0,.15)],16)
    # Under-counter brass rails and shelf brackets make the cabinetry believable.
    for x in [10.1,12.5,14.9]:
        for y in [1.65,2.35]:tube('brass',(x,y-.23,-10.0),(x,y-.04,-9.65),.012)
    # Warm reading lamp, quilted seat seams and rug border stitching.
    start('listening_details')
    lathe('brass',(6,0,-.6),[(.25,0),(.25,.055),(.025,.10),(.025,1.65)],24)
    lathe('linen',(6,1.45,-.6),[(.30,0),(.19,.4),(.17,.4),(.28,0)],32)
    lights.append(dict(name='Reading lamp',position=[6,1.55,-.6],lumens=230,range=4))
    for x in [7,8.1,9.2]:
        for dx in [-.29,.29]:tube('linen',(x+dx,.63,-.918),(x+dx,1.10,-.918),.004,n=5)
        for yy in [.75,.96]:tube('linen',(x-.29,yy,-.918),(x+.29,yy,-.918),.004,n=5)
    for x in np.arange(6.2,10,.13):
        for z in [-.88,2.88]:box('linen',(x,.077,z),(.045,.002,.11))
    # Turntable with platter, spindle, tonearm and controls atop record cabinet.
    box('oak',(14,1.21,3),(.68,.10,.45))
    lathe('rubber',(13.96,1.265,3),[(0,0),(.18,0),(.18,.018),(0,.018)],40)
    lathe('paper',(13.96,1.284,3),[(0,0),(.056,0),(.056,.003),(0,.003)],24)
    tube('chrome',(14.25,1.30,2.90),(14.06,1.30,3.07),.012)
    box('metal',(14.05,1.295,3.08),(.033,.025,.065))
    box('metal',(13.35,.42,3.211),(.38,.65,.012))
    ring('rubber',(13.35,.31,3.23),.13,.027)
    ring('chrome',(13.35,.31,3.236),.055,.012)
    ring('rubber',(13.35,.62,3.23),.061,.018,n=24)
    for x in [14.88,15.03,15.18]:box('amber',(x,1.28,3.253),(.055,.028,.006))
    # Soft curtains gathered at window mullions, with visible folds and tiebacks.
    start('window_textiles')
    for z in [-5.9,-1.9,2.1]:
        for j in range(8):
            zz=z+j*.065
            xx=15.96+.035*math.sin(j*2.3)
            box('linen',(xx,2.05,zz),(.04,1.85,.072))
        box('brass',(15.90,1.65,z+.23),(.075,.05,.55))
    # Exterior craft: herb troughs, bistro table, coiled hose and garden tools.
    start('garden_details')
    for x in [6.2,15.6]:
        lathe('metal',(x,2.23,6.2),[(.15,0),(.25,.22),(.24,.25)],24)
        for dx in [-.19,.19]:tube('metal',(x+dx,2.46,6.2),(x,3.1,6.2),.007,n=5)
        foliage((x,2.44,6.2),.75,24)
    for x in [-15.8,-11.0]:
        box('wood',(x,.28,9.05),(1.2,.48,.54))
        for y in [.14,.30,.46]:box('oak',(x,y,9.34),(1.26,.09,.035))
        for dx in [-.4,0,.4]:foliage((x+dx,.52,9.05),.7,18)
    lathe('sage',(-13.3,0,12.7),[(.28,0),(.28,.035),(.045,.08),(.045,.73)],24)
    lathe('oak',(-13.3,.74,12.7),[(0,0),(.56,0),(.56,.045),(0,.045)],32)
    lathe('ceramic',(-13.3,.79,12.7),[(.09,0),(.12,.18),(.105,.2)],20)
    foliage((-13.3,.99,12.7),.38,20)
    # Watering can with top handle and rose, beneath the pergola.
    lathe('sage',(-15.2,.05,11.8),[(.14,0),(.20,.08),(.18,.35),(.09,.37)],24)
    ring('sage',(-15.2,.43,11.8),.17,.025)
    tube('sage',(-15.04,.19,11.8),(-14.69,.38,11.8),.04,.025)
    lathe('sage',(-14.69,.36,11.8),[(.05,0),(.06,.06)],16)
    for k in range(4):ring('sage',(-11.1,1.7,6.22+k*.018),.28,.014)
    tube('sage',(-10.82,1.7,6.29),(-10.82,.65,6.29),.014)
    tube('brass',(-10.82,.65,6.29),(-10.82,.55,6.29),.025)
    # Mailbox by the entry, rather than clutter inside the ten-metre vehicle gate.
    box('wood',(5.8,.6,15.6),(.10,1.2,.10));box('sage',(5.8,1.27,15.6),(.55,.40,.37))
    box('metal',(5.8,1.34,15.80),(.37,.035,.014));sign('POST',(5.8,1.19,15.802),.09)

def materials():
    result={};template=ET.parse(ROOT/'binaries/project/exo_chora/materials/exo_wood.xml').getroot()
    for i in range(3):MATS['print_'+str(i)]=((1,1,1),None,1,.85)
    for name,(rgb,texture,scale,rough) in MATS.items():
        m=bpy.data.materials.new('home_'+name);m.diffuse_color=(*rgb,1);m.use_nodes=True
        bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*rgb,1);bs.inputs['Roughness'].default_value=rough
        metal=.75 if name in ('chrome','brass','metal') else 0;bs.inputs['Metallic'].default_value=metal
        em=1.6 if name in ('amber','mint_glow') else 0
        bs.inputs['Emission Color'].default_value=(*rgb,1);bs.inputs['Emission Strength'].default_value=em
        root=copy.deepcopy(template)
        for key,val in dict(zip(['color_r','color_g','color_b'],rgb),roughness=rough,metalness=metal,normal=.55 if texture else 0,emissive_from_albedo=em,cull_mode=0).items():root.find(key).text=str(val)
        paths=[]
        if texture:
            for slot,ch in [(0,'diff'),(4,'rough'),(12,'nor_gl')]:
                p=ROOT/f'binaries/project/exo_chora/textures/{texture}_{ch}.jpg'
                if p.exists():paths.append((slot,p))
        if name in ('floor','lawn'):
            folder='concrete' if name=='floor' else 'whispy_grass_meadow'
            ext='jpg' if name=='floor' else 'png'
            paths=[(slot,ROOT/f'binaries/project/materials/{folder}/{channel}.{ext}') for slot,channel in [(0,'albedo'),(4,'roughness'),(12,'normal')]]
            root.find('normal').text='.45'
        if name.startswith('print_'):paths=[(0,OUT/'materials'/f'{name}.png')]
        for slot,p in paths:
            tex=root.find(f'textures/texture_{slot}');tex.set('texture_name',p.stem);tex.set('texture_path',p.relative_to(ROOT/'binaries').as_posix())
            if slot==0 and p.exists():
                node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(str(p));m.node_tree.links.new(node.outputs['Color'],bs.inputs['Base Color'])
        ET.ElementTree(root).write(OUT/'materials'/f'home_{name}.xml',encoding='utf-8',xml_declaration=True)
        result[name]=m
    return result

def prints():
    # Original flat racing art; no third-party photographs or game screenshots.
    for k in range(3):
        w,h=640,384; yy,xx=np.mgrid[0:h,0:w];u=xx/w;v=yy/h
        a=np.ones((h,w,4),dtype=np.float32);a[:,:,:3]=[.70,.69,.53]
        a[v>.46,:3]=[.39,.48,.45];a[v>.72,:3]=[.71,.53,.34]
        hill=.45+.08*np.sin(u*9+k)+.04*np.sin(u*22)
        a[v<hill,:3]=[.17,.29,.28]
        road=np.abs(u-(.40+.24*np.sin(v*3+k*.4)))<(.06+.12*(1-v))
        a[road & (v<.72),:3]=[.18,.19,.19]
        car=(abs(u-.52)<.14)&(v>.20)&(v<.31);a[car,:3]=[[.64,.12,.07],[.74,.63,.36],[.18,.35,.49]][k]
        a[(abs(u-.51)<.08)&(v>.31)&(v<.37),:3]=[.12,.19,.20]
        for cx in [.42,.62]:a[((u-cx)**2+(v-.20)**2<.001),:3]=[.025,.027,.027]
        im=bpy.data.images.new('print_'+str(k),width=w,height=h);im.pixels.foreach_set(a.reshape(-1));im.filepath_raw=str(OUT/'materials'/f'print_{k}.png');im.file_format='PNG';im.save()

def music():
    sr=22050;beat=60/78;bars=16;duration=bars*4*beat;n=int(duration*sr);mix=np.zeros(n)
    def note(midi,t,d,amp,kind='keys'):
        count=int(d*sr);q=np.arange(count)/sr;f=440*2**((midi-69)/12)
        if kind=='bass':s=np.sin(math.tau*f*q)+.20*np.sin(2*math.tau*f*q);env=(1-np.exp(-q*80))*np.exp(-q*4)
        else:s=np.sin(math.tau*f*q+1.2*np.sin(math.tau*f*q)*np.exp(-q*4))+.16*np.sin(math.tau*f*3*q);env=(1-np.exp(-q*120))*np.exp(-q*1.8)
        s*=env*amp*np.minimum(1,(d-q)*25);idx=int(t*sr);end=min(idx+count,n)
        mix[idx:end]+=s[:end-idx]
    chords=[[53,57,60,64],[52,55,59,62],[50,53,57,60],[48,52,55,59]]
    rr=np.random.default_rng(2002)
    for bar in range(bars):
        ch=chords[bar%4];base=bar*4*beat
        for off in [0,1.65,3.0]:
            for j,midi in enumerate(ch):note(midi,base+off*beat+j*.012,1.6,.035)
        for step in [0,1.5,2.5,3.5]:note(ch[0]-12,base+step*beat,.52,.105,'bass')
        for step in range(8):
            t=base+(step*.5+(.08 if step%2 else 0))*beat;idx=int(t*sr);q=np.arange(int(.08*sr))/sr
            s=rr.normal(0,1,len(q))*np.exp(-q*65)*.013;end=min(idx+len(s),n);mix[idx:end]+=s[:end-idx]
        if bar%2:
            for step,midi in zip([.5,1.5,2.5],[ch[3]+12,ch[2]+12,ch[1]+12]):note(midi,base+step*beat,.65,.023)
    fade=int(.08*sr);mix[:fade]*=np.linspace(0,1,fade);mix[-fade:]*=np.linspace(1,0,fade)
    stereo=np.column_stack([mix,mix*.92+np.roll(mix,int(.017*sr))*.08]);pcm=(np.clip(stereo,-1,1)*32767).astype('<i2')
    with wave.open(str(OUT/'audio/slow_roads.wav'),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr);f.writeframes(pcm.tobytes())

def main():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    prints();music();build_geometry();mats=materials();objects=list(bpy.context.scene.objects)
    for ob in objects:ob.data.materials.append(mats[ob['material']])
    for (group,mat),(vv,ff) in g.groups.items():
        mesh=bpy.data.meshes.new(group+'_'+mat);mesh.from_pydata([(x,-z,y) for x,y,z in vv],[],ff);mesh.update()
        ob=bpy.data.objects.new(group+'_'+mat,mesh);bpy.context.collection.objects.link(ob);ob.data.materials.append(mats[mat]);ob['material']=mat;ob['group']=group
        uv=mesh.uv_layers.new();scale=MATS[mat][2]
        for poly in mesh.polygons:
            axis=max(range(3),key=lambda i:abs(poly.normal[i]));axes=[i for i in range(3) if i!=axis]
            for li in poly.loop_indices:
                p=mesh.vertices[mesh.loops[li].vertex_index].co
                if mat.startswith('print_'):uv.data[li].uv=((p.x-(-.5+int(mat[-1])*1.8)+.65)/1.3,(p.z-2.2+.39)/.78)
                else:uv.data[li].uv=(p[axes[0]]/scale,p[axes[1]]/scale)
        if mat in ('wood','oak','plaster','sage','oxblood','cream'):
            b=ob.modifiers.new('Soft worn edges','BEVEL');b.width=.012;b.segments=2
        objects.append(ob)
    bpy.context.view_layer.update();packets=[];deps=bpy.context.evaluated_depsgraph_get()
    for ob in objects:
        ev=ob.evaluated_get(deps);mesh=ev.to_mesh();mesh.calc_loop_triangles();p=[];norm=[];uv=[];idx=[];lookup={}
        for tri in mesh.loop_triangles:
            for li in (tuple(reversed(tri.loops)) if ob.matrix_world.determinant()<0 else tri.loops):
                co=ob.matrix_world@mesh.vertices[mesh.loops[li].vertex_index].co;no=ob.matrix_world.to_3x3()@mesh.corner_normals[li].vector
                t=mesh.uv_layers.active.data[li].uv[:] if mesh.uv_layers.active else (0,0)
                key=tuple(round(v,5) for v in (co.x,co.z,-co.y,no.x,no.z,-no.y,*t))
                if key not in lookup:lookup[key]=len(p)//3;p.extend(key[:3]);norm.extend(key[3:6]);uv.extend(key[6:])
                idx.append(lookup[key])
        ev.to_mesh_clear()
        data=dict(positions=p,normals=norm,uv0=uv,indices=idx);digest=hashlib.sha256(json.dumps(data).encode()).hexdigest()[:10]
        name='home_'+ob.name.replace(' ','_').replace('.','_')+'_'+digest
        data['path']='project/home_garage/meshes/'+name+'.mesh'
        packet=OUT/'packets'/f'{name}.json';packet.write_text(json.dumps(data,separators=(',',':')))
        packets.append(dict(name=name,file=str(packet),mesh_path=data['path'],material=ob['material'],group=ob['group'],triangles=len(idx)//3))
    # Fan is local geometry at origin in engine; place it for the Blender preview only.
    for ob in objects:
        if ob['group']=='fan':ob.location=(9,2,2.93)
    (OUT/'manifest.json').write_text(json.dumps(dict(meshes=packets,lights=lights),indent=2))
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'sources/home_garage.blend'))
    print('HOME BUILT',len(packets),'meshes',sum(m['triangles'] for m in packets),'triangles')

if __name__=='__main__':main()
