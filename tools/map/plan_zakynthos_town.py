"""Plan a road-aware town with reusable building lots and two civic spaces."""
from pathlib import Path
import json, math, random
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'binaries/project/zakynthos_town'
CORE=(7710,-1120,8920,300)
BASE=8.0

def weight(x,z):
    distance=max(CORE[0]-x,x-CORE[2],CORE[1]-z,z-CORE[3],0)
    t=max(0,1-distance/160)
    return t*t*(3-2*t)

def footprint(x,z,w,d,yaw,pad=0):
    a=math.radians(yaw);c,s=math.cos(a),math.sin(a)
    return Polygon([(x+c*u+s*v,z-s*u+c*v) for u,v in [(-w/2-pad,-d/2-pad),(w/2+pad,-d/2-pad),(w/2+pad,d/2+pad),(-w/2-pad,d/2+pad)]])

def main():
    roads=json.loads((OUT/'sources/site_roads.json').read_text())
    new=[]
    for x in (7800,8000,8200,8400):
        new.append(dict(name=f'Town avenue {x}',width=14,xy=[[x,z] for z in range(-1050,151,40)]))
    for z in (-1050,-850,-650,-450,-250,-50,150):
        # Reach an existing eastern street instead of leaving an isolated grid.
        ray=LineString([(8600,z),(8930,z)])
        crossings=[]
        for road in roads:
            hit=ray.intersection(LineString(road['xy']))
            if hit.geom_type=='Point':crossings.append(hit.x)
            elif hit.geom_type=='MultiPoint':crossings.extend(p.x for p in hit.geoms)
        end=max(crossings) if crossings else 8870
        count=math.ceil((end-7800)/40)
        new.append(dict(name=f'Town cross street {z}',width=12,xy=[[7800+(end-7800)*i/count,z] for i in range(count+1)]))
    allroads=roads+new
    corridors=unary_union([LineString(r['xy']).buffer(r['width']/2+4.5) for r in allroads])
    plazas=[dict(name='Solomos civic quarter',x=8480,z=-350,w=124,d=128),dict(name='Agios Markos market',x=8090,z=-750,w=124,d=128)]
    reservations=[box(p['x']-p['w']/2,p['z']-p['d']/2,p['x']+p['w']/2,p['z']+p['d']/2) for p in plazas]
    for p in reservations:assert not p.intersects(corridors),'Civic space overlaps a road'
    occupied=reservations.copy();buildings=[];rng=random.Random(29100)
    sizes=[(16+2*(i%4),15+2*((i//4)%3),2+i%3) for i in range(24)]
    # Alternate setbacks and facade types along the actual curved streets.
    for road in allroads:
        line=LineString(road['xy'])
        for t in range(18,int(line.length)-18,31):
            p=line.interpolate(t);a=line.interpolate(max(0,t-2));b=line.interpolate(min(line.length,t+2))
            dx,dz=b.x-a.x,b.y-a.y;ll=math.hypot(dx,dz)
            if ll<.1:continue
            dx/=ll;dz/=ll
            for side in (-1,1):
                typ=rng.randrange(24);w,d,f=sizes[typ]
                nx,nz=-dz*side,dx*side
                offset=road['width']/2+6+d/2+3
                x,z=p.x+nx*offset,p.y+nz*offset
                if not (7750<x<8875 and -1080<z<210):continue
                yaw=math.degrees(math.atan2(nx,nz))
                poly=footprint(x,z,w,d,yaw,3.6)
                if poly.intersects(corridors) or any(poly.intersects(q) for q in occupied):continue
                occupied.append(poly);buildings.append(dict(name=f'Town frontage {len(buildings)+1:03}',x=x,z=z,w=w,d=d,floors=f,yaw=yaw,prototype=f'block_{typ:02}'))
    # Plant street trees only in clear ground; never in a driving corridor.
    trees=[]
    for road in new:
        line=LineString(road['xy'])
        for t in range(28,int(line.length),55):
            p=line.interpolate(t);a=line.interpolate(max(0,t-1));b=line.interpolate(min(line.length,t+1));dx,dz=b.x-a.x,b.y-a.y;length=math.hypot(dx,dz)
            for side in (-1,1):
                x,z=p.x-dz/length*(road['width']/2+3.1)*side,p.y+dx/length*(road['width']/2+3.1)*side
                q=Point(x,z)
                if any(q.distance(o)<2 for o in occupied) or any(q.distance(LineString(r['xy']))<r['width']/2+1.8 for r in allroads):continue
                trees.append(dict(x=x,z=z,scale=.65+rng.random()*.35))
    plan=dict(core=CORE,base=BASE,feather=160,roads=new,buildings=buildings,plazas=plazas,trees=trees,prototype_sizes=sizes)
    (OUT/'plan.json').write_text(json.dumps(plan,indent=2))
    im=Image.new('RGB',(1100,1250),'#d2ceae');draw=ImageDraw.Draw(im)
    def pixel(x,z):return ((x-7650)*.82,(400-z)*.82)
    for r in allroads:draw.line([pixel(*p) for p in r['xy']],fill='#606365',width=max(2,int(r['width']*.82)))
    for b in buildings:draw.polygon([pixel(x,z) for x,z in footprint(b['x'],b['z'],b['w'],b['d'],b['yaw']).exterior.coords],fill=['#b98561','#d1b079','#bb7567'][b['floors']-2],outline='#684d3a')
    for p in plazas:
        draw.rectangle([pixel(p['x']-p['w']/2,p['z']+p['d']/2),pixel(p['x']+p['w']/2,p['z']-p['d']/2)],fill='#eae0bf');draw.text(pixel(p['x']-55,p['z']),p['name'],fill='black')
    for t in trees:
        x,y=pixel(t['x'],t['z']);draw.ellipse((x-2,y-2,x+2,y+2),fill='#526844')
    (OUT/'previews').mkdir(exist_ok=True);im.save(OUT/'previews/site_plan.png')
    print(f'TOWN PLAN: {len(buildings)} buildings, {len(new)} connected streets, {len(trees)} street trees, two civic spaces')

if __name__=='__main__':main()
