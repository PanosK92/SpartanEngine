"""Repair unconnected island road ends, preserving unrelated scene XML.
Dry-run by default. --apply creates a backup and authors explicit junction tags.
Nearby routes are joined only at compatible elevations and approach angles.
Isolated ends receive a graded, two-lane return loop, selected against the heightmap.
"""
import argparse
from collections import Counter
import copy
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import numpy as np
from osm_roads import Projection, HEIGHTMAP_PATH, ATLAS_PATH
from remove_road_blocking_buildings import sample


def tags(p): return [t for t in p.get('tags','').split(',') if t.startswith('road_node_')]
def vec(e): return np.array(list(map(float,e.get('position','0 0 0').split())))
def fmt(v): return ' '.join(f'{x:.6f}' for x in v)
def unit(v):
    v=v.copy();v[1]=0
    return v/max(np.linalg.norm(v),1e-9)

class Repair:
    def __init__(self,text,terrain):
        self.text=text;self.root=ET.fromstring(text);self.terrain=terrain
        self.container=next(e for e in self.root.iter('Entity') if e.get('name')=='roads')
        assert self.container.get('position','0 0 0')=='0 0 0'
        self.roads=[];self.changed=set();self.added=[];self.report=[]
        self.ids={int(e.get('id')) for e in self.root.iter('Entity')}
        self.next_id=9104300000000000000
        for e in self.container.findall('Entity'):
            s=e.find('spline');p=[c for c in e.findall('Entity') if c.get('name','').startswith('spline_point_')]
            if s is None or s.get('profile')!='0' or len(p)<2:continue
            assert e.get('rotation','0 0 0 1')=='0 0 0 1' and e.get('scale','1 1 1')=='1 1 1'
            self.roads.append(dict(e=e,s=s,p=p,xyz=np.array([vec(c)+vec(e) for c in p])))
    def new_id(self):
        while self.next_id in self.ids:self.next_id+=1
        value=str(self.next_id);self.ids.add(self.next_id);self.next_id+=1;return value
    def degrees(self):
        deg=Counter()
        for r in self.roads:
            for i,p in enumerate(r['p']):
                for t in tags(p):deg[t]+=1 if i in (0,len(r['p'])-1) else 2
        return deg
    def ends(self):
        deg=self.degrees()
        return [(r,i) for r in self.roads for i in (0,len(r['p'])-1) if not tags(r['p'][i]) or max(deg[t] for t in tags(r['p'][i]))<2]
    def tag(self,p,t):
        old=[x for x in p.get('tags','').split(',') if x and not x.startswith('road_node_')]
        if t not in old:old.append(t)
        p.set('tags',','.join(old))
    def terrain_score(self,points,width):
        heights=[]
        for i,p in enumerate(points):
            tangent=unit(points[min(i+1,len(points)-1)]-points[max(i-1,0)])
            right=np.array([tangent[2],0,-tangent[0]])
            for side in (-1,0,1):
                q=p+right*(width/2+2)*side
                h=self.terrain.sample_height(q[0],q[2])
                if h < -5.85:return math.inf
                heights.append(abs(h-p[1]))
        return max(heights,default=0)+sum(heights)/max(1,len(heights))*.2
    def run(self):
        original=self.ends();print(f'{len(original)} unconnected ends')
        # Short endpoint adjustments heal coincident/near-coincident import gaps.
        for r,endpoint in [(r,r['p'][i]) for r,i in original]:
            i=r['p'].index(endpoint)
            if not any(rr is r and ii==i for rr,ii in self.ends()):continue
            origin=r['xyz'][i];previous=r['xyz'][1 if i==0 else -2]
            candidates=[]
            for other in self.roads:
                distances=np.linalg.norm((other['xyz']-origin)[:,[0,2]],axis=1)
                for j in np.where(distances<=125)[0]:
                    if other is r and abs(j-i)<4:continue
                    target=other['xyz'][j];distance=distances[j]
                    # Keep the end moving outward; snapping behind its previous
                    # handle creates a hairpin or duplicates a neighbouring node.
                    if distance>15 and np.dot(unit(target-previous),unit(origin-previous))<.35:continue
                    if 0<j<len(other['p'])-1:
                        if min(np.linalg.norm(target-other['xyz'][0]),np.linalg.norm(target-other['xyz'][-1]))<25:continue
                    if abs(target[1]-origin[1])>max(2,distance*.12):continue
                    incoming=unit(previous-target)
                    approaches=[unit(other['xyz'][k]-target) for k in (j-1,j+1) if 0<=k<len(other['xyz'])]
                    if distance>=20 and any(np.dot(incoming,a)>math.cos(math.radians(40)) for a in approaches):continue
                    line=[previous*(1-t)+target*t for t in np.linspace(0,1,15)]
                    if distance>=20 and self.terrain_score(line,float(r['s'].get('road_width','8')))>15:continue
                    candidates.append((distance,other,int(j),target.copy(),False))
                # Check the road between handles as well: a long span can run
                # directly past an end while both control points are far away.
                for j,(a,b) in enumerate(zip(other['xyz'],other['xyz'][1:])):
                    if other is r and min(abs(j-i),abs(j+1-i))<4:continue
                    delta=b-a;length2=delta[0]**2+delta[2]**2
                    if length2<1:continue
                    u=max(0.,min(1.,float(np.dot((origin-a)[[0,2]],delta[[0,2]])/length2)))
                    target=a+delta*u;distance=float(np.linalg.norm((target-origin)[[0,2]]))
                    if distance>50 or min(u,1-u)*math.sqrt(length2)<12:continue
                    if abs(target[1]-origin[1])>max(2,distance*.12):continue
                    if distance>15 and np.dot(unit(target-previous),unit(origin-previous))<.35:continue
                    if distance>=20 and abs(np.dot(unit(previous-target),unit(delta)))>math.cos(math.radians(40)):continue
                    candidates.append((distance,other,j,target,True))
            if not candidates:continue
            distance,other,j,target,insert=min(candidates,key=lambda x:x[0])
            if insert:
                point=ET.Element('Entity',name='spline_point_new',id=self.new_id(),active='true',position=fmt(target-vec(other['e'])),rotation='0 0 0 1',scale='1 1 1')
                other['e'].insert(list(other['e']).index(other['p'][j+1]),point)
                other['p'].insert(j+1,point);other['xyz']=np.insert(other['xyz'],j+1,target,axis=0);j+=1
                for k,p in enumerate(other['p']):p.set('name',f'spline_point_{k}')
            i=r['p'].index(endpoint)
            tag=next(iter(tags(other['p'][j])),f'road_node_game_join_{r["p"][i].get("id")}')
            self.tag(other['p'][j],tag);self.tag(r['p'][i],tag)
            r['p'][i].set('position',fmt(target-vec(r['e'])));r['xyz'][i]=target
            self.changed.update((r['e'].get('id'),other['e'].get('id')))
            self.report.append(dict(kind='join',road=r['e'].get('name'),end=i,target=other['e'].get('name'),distance=round(float(distance),2),position=target.tolist(),tag=tag))
        for r,endpoint in [(r,r['p'][i]) for r,i in self.ends()]:
            i=r['p'].index(endpoint)
            origin=r['xyz'][i];forward=unit(origin-r['xyz'][1 if i==0 else -2]);width=float(r['s'].get('road_width','8'))
            nearby=[]
            for other in self.roads:
                for j,(a,b) in enumerate(zip(other['xyz'],other['xyz'][1:])):
                    if other is r and min(abs(j-i),abs(j+1-i))<4:continue
                    if min(a[0],b[0])-220<=origin[0]<=max(a[0],b[0])+220 and min(a[2],b[2])-220<=origin[2]<=max(a[2],b[2])+220:
                        nearby.append((a,b,(width+float(other['s'].get('road_width','8')))/2+1))
            best=None
            # An elongated loop has generous bend radii and a three-way mouth.
            anchors=[(origin.copy(),forward,[],0.)]
            order=list(range(len(r['p']))) if i==0 else list(reversed(range(len(r['p']))))
            traveled=0.;discard=[]
            for aidx,bidx in zip(order,order[1:]):
                a,b=r['xyz'][aidx],r['xyz'][bidx]
                length=float(np.linalg.norm(b-a))
                if length<.001:continue
                for step in (20.,40.,60.,100.,150.):
                    if traveled<step<traveled+length and step<sum(np.linalg.norm(y-x) for x,y in zip(r['xyz'],r['xyz'][1:]))*.35:
                        anchor=a+(b-a)*(step-traveled)/length
                        anchors.append((anchor,unit(a-b),discard.copy(),step))
                traveled+=length
                if tags(r['p'][bidx]) or bidx==order[-1] or traveled>150:break
                discard.append(r['p'][bidx])
            for origin,forward,discard,retreat in anchors:
                for angle in (0,-25,25,-50,50,-70,70,-90,90):
                    a=math.radians(angle);f=np.array([forward[0]*math.cos(a)-forward[2]*math.sin(a),0,forward[0]*math.sin(a)+forward[2]*math.cos(a)])
                    right=np.array([f[2],0,-f[0]])
                    for scale in (1.,1.3,.85,.65):
                        shape=[(0,0),(22,22),(52,29),(74,16),(80,0),(74,-16),(52,-29),(22,-22),(0,0)]
                        pts=[origin+f*x*scale+right*z*scale for x,z in shape]
                        # Grade the loop deck gradually, anchored at the shared mouth.
                        distances=np.concatenate(([0],np.cumsum([np.linalg.norm(b-a) for a,b in zip(pts,pts[1:])])))
                        for k,p in enumerate(pts):
                            ground=self.terrain.sample_height(p[0],p[2])+.25
                            limit=min(distances[k],distances[-1]-distances[k])*math.tan(math.radians(6))
                            p[1]=origin[1]+max(-limit,min(limit,ground-origin[1]))
                        dense=[np.array(p) for _,p in sample(pts,.5)]
                        score=self.terrain_score(dense[::3]+dense[-1:],width)
                        if nearby and math.isfinite(score):
                            check=np.array(dense[::5]);check=check[np.linalg.norm((check-origin)[:,[0,2]],axis=1)>20]
                            for a,b,clearance in nearby:
                                delta=b-a;length2=delta[0]**2+delta[2]**2
                                if length2<.01 or not len(check):continue
                                u=np.clip(((check[:,0]-a[0])*delta[0]+(check[:,2]-a[2])*delta[2])/length2,0,1)
                                projected=a+u[:,None]*delta
                                if np.any((np.linalg.norm((check-projected)[:,[0,2]],axis=1)<clearance)&(np.abs(check[:,1]-projected[:,1])<8)):
                                    score=math.inf;break
                        score+=abs(angle)*.01
                        if best is None or score+retreat*.03<best[0]:best=(score+retreat*.03,pts,angle,scale,origin.copy(),retreat,discard.copy())
            score,pts,angle,scale,origin,retreat,discard=best
            if not math.isfinite(score) or score>40:
                self.report.append(dict(kind='unresolved',road=r['e'].get('name'),end=i,score=score));continue
            if retreat:
                endpoint.set('position',fmt(origin-vec(r['e'])))
                for old in discard:r['e'].remove(old)
                r['p']=[p for p in r['p'] if p not in discard]
                for k,p in enumerate(r['p']):p.set('name',f'spline_point_{k}')
                r['xyz']=np.array([vec(p)+vec(r['e']) for p in r['p']])
                i=r['p'].index(endpoint)
            eid=self.new_id();tag=f'road_node_game_return_{r["p"][i].get("id")}'
            self.tag(r['p'][i],tag);self.changed.add(r['e'].get('id'))
            e=ET.Element('Entity',name=f'return_loop_{r["e"].get("name")}_{i}',id=eid,active='true',position=fmt(origin),rotation='0 0 0 1',scale='1 1 1',tags='road,map_road,game_return_loop')
            for component in ('physics','render','spline'):
                e.append(copy.deepcopy(r['e'].find(component)))
            s=e.find('spline');s.set('resolution','12');s.set('sidewalk_enabled','false');s.set('conform_to_terrain','true');s.set('grade_smoothing','0.3');s.set('smoothing_length','20')
            for child in list(s):s.remove(child)
            controls=[]
            for k,p in enumerate(pts):
                c=ET.SubElement(e,'Entity',name=f'spline_point_{k}',id=self.new_id(),active='true',position=fmt(p-origin),rotation='0 0 0 1',scale='1 1 1')
                if k in (0,len(pts)-1):self.tag(c,tag)
                controls.append(c)
            self.added.append(e);self.roads.append(dict(e=e,s=s,p=controls,xyz=np.array(pts)))
            self.report.append(dict(kind='loop',road=r['e'].get('name'),end=i,position=origin.tolist(),tag=tag,entity=eid,terrain_error=round(score,2),angle=angle,scale=scale,retreat=round(retreat,2)))
        print(Counter(x['kind'] for x in self.report));print(f'{len(self.ends())} ends remain')
        return self.report
    def output(self):
        if not self.changed and not self.added:return self.text
        text=self.text
        def span(e):
            m=re.search(r'<Entity\b[^>]*\bid="'+e.get('id')+r'"[^>]*>',text);assert m
            depth=1
            for token in re.finditer(r'</?Entity\b[^>]*>',text[m.end():]):
                depth+=-1 if token.group().startswith('</') else (0 if token.group().endswith('/>') else 1)
                if depth==0:return m.start(),m.end()+token.end()
            raise ValueError('Unbalanced Entity')
        edits=[]
        for r in self.roads:
            if r['e'].get('id') in self.changed:
                start,end=span(r['e']);edits.append((start,end,ET.tostring(r['e'],encoding='unicode').rstrip()))
        _,end=span(self.container)
        edits.append((end-len('</Entity>'),end-len('</Entity>'),'\n'.join(ET.tostring(e,encoding='unicode') for e in self.added)+'\n'))
        for start,end,replacement in sorted(edits,reverse=True):text=text[:start]+replacement+text[end:]
        ET.fromstring(text)
        return text

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--apply',action='store_true');args=parser.parse_args()
    terrain=Projection(json.loads(Path(ATLAS_PATH).read_text())['crs']);terrain.load_heightmap(HEIGHTMAP_PATH)
    path=Path('worlds/plan.world');text=path.read_text(encoding='utf-8');repair=Repair(text,terrain);report=repair.run()
    if report:Path('binaries/road_end_repair.json').write_text(json.dumps(report,indent=2))
    if args.apply:
        assert not repair.ends(),'Unresolved ends; nothing written'
        backup=Path('binaries/project/backups/plan.before_road_returns.world')
        if not backup.exists():backup.write_text(text,encoding='utf-8')
        assert path.read_text(encoding='utf-8')==text,'World changed during repair; rerun'
        path.write_text(repair.output(),encoding='utf-8')
        if report:Path('tools/map/road_end_repairs.json').write_text(json.dumps(report,indent=2))
