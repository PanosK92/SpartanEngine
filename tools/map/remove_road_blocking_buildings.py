"""Remove city building boxes intersecting road corridors; dry-run unless --apply.
Uses hierarchy transforms and sampled Catmull-Rom roads, including local paving.
A 0.5 m clearance covers sampling error and keeps pedestrians off building walls.
"""
import argparse
import itertools
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def transform(p, e):
    s = list(map(float, e.get('scale', '1 1 1').split()))
    q = list(map(float, e.get('rotation', '0 0 0 1').split()))
    v = [p[i]*s[i] for i in range(3)]
    x,y,z,w = q
    t = [2*(y*v[2]-z*v[1]), 2*(z*v[0]-x*v[2]), 2*(x*v[1]-y*v[0])]
    cross = [y*t[2]-z*t[1], z*t[0]-x*t[2], x*t[1]-y*t[0]]
    offset = list(map(float,e.get('position','0 0 0').split()))
    return tuple(v[i]+w*t[i]+cross[i]+offset[i] for i in range(3))


def intersects(a,b,lo,hi,radius):
    low, high = 0., 1.
    for k in (0,2):
        d = b[k]-a[k]
        if abs(d)<1e-10:
            if not lo[k]-radius <= a[k] <= hi[k]+radius: return False
        else:
            u,v = sorted(((lo[k]-radius-a[k])/d,(hi[k]+radius-a[k])/d))
            low,high = max(low,u),min(high,v)
            if low>high: return False
    return max(a[1],b[1])+3 >= lo[1] and min(a[1],b[1]) <= hi[1]


def sample(points,alpha):
    for i in range(len(points)-1):
        p0,p1,p2,p3 = [points[j] for j in (max(0,i-1),i,i+1,min(len(points)-1,i+2))]
        dt = [math.dist(a,b)**alpha for a,b in ((p0,p1),(p1,p2),(p2,p3))]
        if dt[1]<1e-4: dt[1]=1
        if dt[0]<1e-4: dt[0]=dt[1]
        if dt[2]<1e-4: dt[2]=dt[1]
        d0,d1,d2=dt
        m1=[((p1[k]-p0[k])/d0-(p2[k]-p0[k])/(d0+d1)+(p2[k]-p1[k])/d1)*d1 for k in range(3)]
        m2=[((p2[k]-p1[k])/d1-(p3[k]-p1[k])/(d1+d2)+(p3[k]-p2[k])/d2)*d1 for k in range(3)]
        steps=max(8,math.ceil(math.dist(p1,p2)))
        for j in range(steps):
            t=j/steps
            yield (i+t)/(len(points)-1),tuple(p1[k]+m1[k]*t+(3*(p2[k]-p1[k])-2*m1[k]-m2[k])*t*t+(2*(p1[k]-p2[k])+m1[k]+m2[k])*t*t*t for k in range(3))
    yield 1.,points[-1]


def remove_blockers(text):
    root=ET.fromstring(text)
    parents={c:p for p in root.iter() for c in p}
    def world(e,p=(0,0,0)):
        while e is not None:
            if e.tag=='Entity': p=transform(p,e)
            e=parents.get(e)
        return p
    city=next(e for e in root.iter('Entity') if e.get('name','').startswith('city_grid_1km__'))
    buildings=[]
    for e in city.iter('Entity'):
        render=e.find('render')
        if render is None or render.get('mesh_name')!='standard_cube' or not re.search(r'_(building|tower|podium)_?',e.get('name','')): continue
        corners=[world(e,p) for p in itertools.product((-.5,.5),repeat=3)]
        buildings.append((e,tuple(min(p[k] for p in corners) for k in range(3)),tuple(max(p[k] for p in corners) for k in range(3))))
    segments=[]
    for e in root.iter('Entity'):
        s=e.find('spline')
        if s is None or s.get('profile')!='0': continue
        pts=[world(p) for p in e.findall('Entity') if p.get('name','').startswith('spline_point_')]
        if len(pts)<2: continue
        # Only sample roads near the city.
        cx,_,cz=world(city)
        if all(p[0]<cx-1100 for p in pts) or all(p[0]>cx+1100 for p in pts) or all(p[2]<cz-1100 for p in pts) or all(p[2]>cz+1100 for p in pts): continue
        values=list(sample(pts,float(s.get('curve_alpha','.5'))))
        ranges=[(float(r.get('start')),float(r.get('end'))) for r in s.findall('sidewalk_range')]
        for (t,a),(u,b) in zip(values,values[1:]):
            width=max(float(s.get('road_width','10')),float(s.get('road_width_end',s.get('road_width','10'))))/2
            if s.get('sidewalk_enabled')=='true' and (not ranges or any(t<=hi and u>=lo for lo,hi in ranges)): width+=float(s.get('sidewalk_width','2'))
            segments.append((a,b,width+.5))
    # The city also contains box-shaped grid streets.
    for e in city.iter('Entity'):
        name=e.get('name','')
        if not (name.startswith('east_west_road_') or name.startswith('north_south_road_')): continue
        along_x=name.startswith('east_west_road_')
        a=world(e,(-.5,0,0) if along_x else (0,0,-.5))
        b=world(e,(.5,0,0) if along_x else (0,0,.5))
        side=world(e,(0,0,.5) if along_x else (.5,0,0))
        radius=math.dist(world(e),side)
        segments.append((a,b,radius+.5))
    removed=[]
    for e,lo,hi in buildings:
        if any(intersects(a,b,lo,hi,r) for a,b,r in segments): removed.append(e)
    for e in removed:
        # Remove the complete element without reserializing unrelated world data.
        token=re.search(r'<Entity\b[^>]*\bid="'+re.escape(e.get('id'))+r'"[^>]*>',text)
        assert token,e.get('name')
        depth=1; end=token.end()
        for m in re.finditer(r'</?Entity\b[^>]*>',text[token.end():]):
            depth+= -1 if m.group().startswith('</') else (0 if m.group().endswith('/>') else 1)
            if depth==0: end=token.end()+m.end();break
        start=token.start()
        line_start=text.rfind('\n',0,start)+1
        if not text[line_start:start].strip(): start=line_start
        if text[end:end+1]=='\n': end+=1
        text=text[:start]+text[end:]
    print(f'Removed {len(removed)} of {len(buildings)} city buildings overlapping road/paving corridors')
    for e in removed: print(e.get('name'))
    return text

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--apply',action='store_true');args=p.parse_args()
    path=Path('worlds/plan.world'); result=remove_blockers(path.read_text(encoding='utf-8'))
    if args.apply: path.write_text(result,encoding='utf-8')
