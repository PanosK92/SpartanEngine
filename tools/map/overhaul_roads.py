"""Replace island turnarounds with terrain-aware through routes.

Dry-run by default. Changes only the roads hierarchy, preserving other world XML.
Uses the existing local heightmap; no network downloads or temporary scene files.
"""
import argparse
from collections import defaultdict
import copy
import heapq
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import numpy as np
from scipy.spatial import cKDTree
from PIL import Image, ImageDraw
from scipy.spatial.transform import Rotation
from repair_road_ends import Repair, tags, vec, fmt, unit
from osm_roads import Projection, HEIGHTMAP_PATH, ATLAS_PATH


class IslandTerrain(Projection):
    def __init__(self, crs):
        super().__init__(crs)
        svg = ET.parse(Path(ATLAS_PATH).with_suffix('.svg')).getroot()
        mask = Image.new('1', (self.width_px, self.height_px))
        draw = ImageDraw.Draw(mask)
        for e in svg:
            if e.get('fill') != '#12362f':
                continue
            ring = [self.world_to_pixel((float(x)-1937.5)*10, (1578.8-float(y))*10)
                    for x, y in re.findall(r'[ML]([\d.-]+),([\d.-]+)', e.get('d', ''))]
            if len(ring) >= 3:
                draw.polygon(ring, fill=1)
        self.land = np.array(mask)

    def sample_height(self, x, z):
        height = super().sample_height(x, z)
        # The quantized heightmap has zero-valued holes in the inland coastal
        # plain too. The atlas coastline distinguishes those from open sea.
        if height < -5.5:
            px, py = self.world_to_pixel(x, z)
            ix, iy = round(px), round(py)
            if 0 <= ix < self.width_px and 0 <= iy < self.height_px and self.land[iy, ix]:
                return -2.9
        return height


def components(roads):
    graph = defaultdict(set)
    for i, road in enumerate(roads):
        graph[i]
        for point in road['p']:
            for tag in tags(point):
                graph[i].add(tag)
                graph[tag].add(i)
    seen, result = set(), []
    for node in range(len(roads)):
        if node in seen:
            continue
        group, stack = set(), [node]
        while stack:
            item = stack.pop()
            if item in group:
                continue
            group.add(item)
            stack.extend(graph[item] - group)
        seen.update(group)
        result.append({x for x in group if isinstance(x, int)})
    return sorted(result, key=len, reverse=True)


class Overhaul(Repair):
    def __init__(self, text, terrain):
        super().__init__(text, terrain)
        self.next_id = 9104400000000000000
        self.removed = []
        self.extra_edits = []
        self.before = dict(roads=len(self.roads), networks=len(components(self.roads)))

    def refresh(self, road):
        road['xyz'] = np.array([vec(p) + vec(road['e']) for p in road['p']])
        for i, point in enumerate(road['p']):
            point.set('name', f'spline_point_{i}')
        self.changed.add(road['e'].get('id'))

    def insert(self, road, segment, position):
        # Reuse a nearby handle so junctions do not create tiny approach segments.
        distances = np.linalg.norm((road['xyz']-position)[:, [0, 2]], axis=1)
        closest = int(np.argmin(distances))
        if distances[closest] < 12:
            return road['p'][closest], road['xyz'][closest].copy()
        point = ET.Element('Entity', name='spline_point_new', id=self.new_id(),
                           active='true', position=fmt(position-vec(road['e'])),
                           rotation='0 0 0 1', scale='1 1 1')
        road['e'].insert(list(road['e']).index(road['p'][segment+1]), point)
        road['p'].insert(segment+1, point)
        self.refresh(road)
        return point, position.copy()

    def index(self):
        samples, refs = [], []
        for ri, road in enumerate(self.roads):
            for si, (a, b) in enumerate(zip(road['xyz'], road['xyz'][1:])):
                count = max(1, math.ceil(np.linalg.norm((b-a)[[0, 2]])/25))
                for u in np.linspace(0, 1, count+1):
                    samples.append((a+(b-a)*u)[[0, 2]])
                    refs.append((ri, si))
        return cKDTree(samples), refs

    def route(self, start, end):
        """Find a low-gradient dry-ground corridor, then simplify gentle runs."""
        distance = np.linalg.norm((end-start)[[0, 2]])
        if distance < 1:
            return [start.copy(), end.copy()], 0
        # First try a densely checked straight corridor; most import gaps are short.
        direct = [start+(end-start)*u for u in np.linspace(0, 1, max(3, math.ceil(distance/15)))]
        heights = [self.terrain.sample_height(p[0], p[2]) for p in direct]
        grade = max(abs(b-a)/(distance/(len(heights)-1)) for a, b in zip(heights, heights[1:]))
        if min(heights) > -5.5 and grade < .14:
            for p, h in zip(direct[1:-1], heights[1:-1]):
                p[1] = h+.25
            return direct, distance*(1+grade*3)
        # Eight-neighbour search penalizes elevation changes. Search bounds prevent
        # unbounded work at coasts; water cells cannot be traversed.
        step = min(35.0, max(8.0, distance/40))
        padding = max(350, min(1600, distance*.6))
        low = np.minimum(start[[0, 2]], end[[0, 2]])-padding
        high = np.maximum(start[[0, 2]], end[[0, 2]])+padding
        shape = np.ceil((high-low)/step).astype(int)+1
        source = tuple(np.round((start[[0, 2]]-low)/step).astype(int))
        goal = tuple(np.round((end[[0, 2]]-low)/step).astype(int))
        cache = {source: self.terrain.sample_height(start[0],start[2]), goal: self.terrain.sample_height(end[0],end[2])}
        def height(cell):
            if cell not in cache:
                q = low+np.array(cell)*step
                cache[cell] = self.terrain.sample_height(q[0], q[1])
            return cache[cell]
        queue = [(0, source)]
        costs, parent = {source: 0}, {}
        moves = [(x, z) for x in (-1, 0, 1) for z in (-1, 0, 1) if x or z]
        while queue and len(costs) < 80000:
            _, cell = heapq.heappop(queue)
            if cell == goal:
                cells = [cell]
                while cells[-1] != source:
                    cells.append(parent[cells[-1]])
                points = [start.copy()]
                for c in reversed(cells[1:-1]):
                    q = low+np.array(c)*step
                    points.append(np.array([q[0], height(c)+.25, q[1]]))
                points.append(end.copy())
                # Chaikin corner cuts keep the route within its checked corridor.
                smooth = [points[0]]
                for a, b in zip(points, points[1:]):
                    smooth.extend([a*.75+b*.25, a*.25+b*.75])
                smooth.append(points[-1])
                for p in smooth[1:-1]:
                    p[1] = self.terrain.sample_height(p[0], p[2])+.25
                return smooth, costs[cell]
            for dx, dz in moves:
                other = cell[0]+dx, cell[1]+dz
                if min(other) < 0 or other[0] >= shape[0] or other[1] >= shape[1]:
                    continue
                h = height(other)
                span = step*math.hypot(dx, dz)
                slope = abs(h-height(cell))/span
                if h < -5.5 or (slope > .32 and abs(h-height(cell)) > 18):
                    continue
                if dx and dz and min(height((cell[0]+dx, cell[1])), height((cell[0], cell[1]+dz))) < -5.5:
                    continue
                cost = costs[cell]+span*(1+12*slope*slope+max(0, slope-.12)*25)
                if cost < costs.get(other, math.inf):
                    costs[other], parent[other] = cost, cell
                    heuristic = math.hypot(other[0]-goal[0], other[1]-goal[1])*step
                    heapq.heappush(queue, (cost+heuristic, other))
        return None, math.inf

    def connect(self, road, endpoint, allowed=None):
        tree, refs = self.index()
        start = road['xyz'][endpoint]
        forward = unit(start-road['xyz'][1 if endpoint == 0 else -2])
        _, nearest = tree.query(start[[0, 2]], k=min(len(refs), 1600))
        candidates, visited = [], set()
        for sample in np.atleast_1d(nearest):
            ri, si = refs[int(sample)]
            other = self.roads[ri]
            if other is road or (allowed is not None and ri not in allowed) or (ri, si) in visited:
                continue
            visited.add((ri, si))
            a, b = other['xyz'][si:si+2]
            delta = b-a
            span2 = np.dot(delta[[0, 2]], delta[[0, 2]])
            if span2 < 1:
                continue
            u = np.clip(np.dot((start-a)[[0, 2]], delta[[0, 2]])/span2, 0, 1)
            target = a+delta*u
            distance = np.linalg.norm((target-start)[[0, 2]])
            outward = np.dot(unit(target-start), forward)
            score = distance*(1+max(0, .4-outward)*1.8)+abs(target[1]-start[1])*6
            candidates.append((score, ri, si, target))
        candidates.sort(key=lambda c: c[0])
        best = None
        attempts = defaultdict(int)
        for candidate_score, ri, si, target in candidates:
            if attempts[ri] >= 2 or sum(attempts.values()) >= 36:
                continue
            attempts[ri] += 1
            points, cost = self.route(start, target)
            cost += candidate_score*.5
            if points is not None and (best is None or cost < best[0]):
                best = cost, ri, si, target, points
        if best is None:
            raise ValueError(f'No dry through route for {road["e"].get("name")} end {endpoint}')
        _, ri, si, target, points = best
        other = self.roads[ri]
        target_point, target = self.insert(other, si, target)
        points[-1] = target
        target_tag = next(iter(tags(target_point)), 'road_node_through_'+target_point.get('id'))
        self.tag(target_point, target_tag)
        self.changed.add(other['e'].get('id'))
        source_point = road['p'][endpoint]
        source_tag = next(iter(tags(source_point)), 'road_node_through_'+source_point.get('id'))
        distance = np.linalg.norm((target-start)[[0, 2]])
        if distance < 18:
            # Close a small import gap by moving the end, without a tiny extra road.
            old_tags = tags(source_point)
            for old in old_tags:
                for r in self.roads:
                    for p in r['p']:
                        if old in tags(p):
                            self.tag(p, target_tag)
                            p.set('position', fmt(target-vec(r['e'])))
                            self.refresh(r)
            self.tag(source_point, target_tag)
            source_point.set('position', fmt(target-vec(road['e'])))
            self.refresh(road)
            kind = 'close_gap'
        else:
            self.tag(source_point, source_tag)
            self.changed.add(road['e'].get('id'))
            entity = ET.Element('Entity', name=f'through_{road["e"].get("name")}_{endpoint}',
                                id=self.new_id(), active='true', position=fmt(start),
                                rotation='0 0 0 1', scale='1 1 1', tags='road,map_road,game_through_route')
            for name in ('physics', 'render', 'spline'):
                entity.append(copy.deepcopy(road['e'].find(name)))
            spline = entity.find('spline')
            for child in list(spline):
                spline.remove(child)
            spline.attrib.update(road_width='7', road_width_end='7', sidewalk_enabled='false',
                                 resolution='8', smoothing_length='60', grade_smoothing='0.65',
                                 instance_template_id='0', instance_mesh_path='')
            controls = []
            for i, p in enumerate(points):
                c = ET.SubElement(entity, 'Entity', name=f'spline_point_{i}', id=self.new_id(),
                                  active='true', position=fmt(p-start), rotation='0 0 0 1', scale='1 1 1')
                controls.append(c)
            self.tag(controls[0], source_tag)
            self.tag(controls[-1], target_tag)
            self.added.append(entity)
            self.roads.append(dict(e=entity, s=spline, p=controls, xyz=np.array(points)))
            kind = 'through_route'
        self.report.append(dict(kind=kind, road=road['e'].get('name'), end=endpoint,
                                target=other['e'].get('name'), distance=round(float(distance), 1)))
        print(self.report[-1], flush=True)

    def run(self):
        if any('game_through_route' in r['e'].get('tags', '') for r in self.roads):
            self.airport_bypass()
            self.smooth_connections()
            self.straighten_junction_hooks()
            self.refine_junctions()
            self.validate()
            return self.report
        # These two isolated paired strips trace the same short airport link in
        # opposite directions. Keep one two-way road instead of a narrow oval.
        duplicate_names = {'r060_zakynthos_aerodromio', 'r068_zakynthos_aerodromio'}
        for road in list(self.roads):
            if 'game_return_loop' in road['e'].get('tags', '') or road['e'].get('name') in duplicate_names:
                self.removed.append(road['e'])
                self.roads.remove(road)
        # Clear former loop identities; they no longer represent a junction.
        for road in self.roads:
            for point in road['p']:
                if any(t.startswith('road_node_game_return_') for t in tags(point)):
                    point.set('tags', ','.join(t for t in point.get('tags', '').split(',') if not t.startswith('road_node_game_return_')))
                    self.changed.add(road['e'].get('id'))
        while self.ends():
            road, endpoint = self.ends()[0]
            self.connect(road, endpoint)
        # A closed island can have zero loose ends. Connectivity must be checked
        # independently, and both approaches remain ordinary two-way roads.
        while len(components(self.roads)) > 1:
            groups = components(self.roads)
            group = groups[-1]
            main = set(range(len(self.roads)))-group
            road = self.roads[min(group)]
            self.connect(road, 0, main)
        self.airport_bypass()
        self.join_crossings()
        self.smooth_connections()
        self.straighten_junction_hooks()
        self.refine_junctions()
        self.validate()
        return self.report

    def smooth_connections(self):
        # A two-road join is best authored as one continuous spline. In particular,
        # remove the old dead-end tip when a shorter connection turns back inland;
        # retaining that tip would introduce a tight reversing hook.
        for link in list(self.roads):
            if not link['e'].get('name', '').startswith('through_'):
                continue
            # Remove the obsolete heading stub emitted by the first migration.
            while len(link['p']) > 3:
                a,b,c=link['xyz'][:3]
                if np.dot(unit(b-a),unit(c-b)) >= -.2 or tags(link['p'][1]): break
                link['e'].remove(link['p'][1]);link['p'].pop(1);self.refresh(link)
            source_tags=tags(link['p'][0])
            if not source_tags: continue
            tag=source_tags[0]
            participants=[(r,i) for r in self.roads for i,p in enumerate(r['p']) if tag in tags(p)]
            if len(participants)!=2: continue
            source,index=next(((r,i) for r,i in participants if r is not link),(None,None))
            if source is None or index not in (0,len(source['p'])-1): continue
            pairs=list(zip(source['p'],source['xyz']))
            if index==0: pairs.reverse()
            pairs+=list(zip(link['p'][1:],link['xyz'][1:]))
            # The internal join no longer needs a junction identity.
            for point,_ in pairs:
                if tag in tags(point): point.set('tags',','.join(t for t in point.get('tags','').split(',') if t!=tag))
            changed=True
            while changed:
                changed=False
                for i in range(1,len(pairs)-1):
                    if tags(pairs[i][0]): continue
                    a,b,c=(pairs[k][1] for k in (i-1,i,i+1))
                    if np.linalg.norm((b-a)[[0,2]])<2 or np.dot(unit(b-a),unit(c-b))<.2:
                        pairs.pop(i);changed=True;break
            for point in source['p']: source['e'].remove(point)
            source['p']=[]
            for point,position in pairs:
                point.set('position',fmt(position-vec(source['e'])))
                source['e'].append(point);source['p'].append(point)
            self.refresh(source)
            source['e'].set('tags',','.join(dict.fromkeys(source['e'].get('tags','').split(',')+['game_through_route'])))
            self.roads.remove(link)
            if link['e'] in self.added: self.added.remove(link['e'])
            else: self.removed.append(link['e'])
            self.report.append(dict(kind='continuous_spline',road=source['e'].get('name'),target=link['e'].get('name')))

    def straighten_junction_hooks(self):
        for road in self.roads:
            again=True
            while again:
                again=False
                visits={}
                for i,point in enumerate(road['p']):
                    for tag in tags(point):
                        if not tag.startswith(('road_node_crossing_','road_node_through_')): continue
                        if tag in visits:
                            first=visits[tag]
                            length=sum(np.linalg.norm((b-a)[[0,2]]) for a,b in zip(road['xyz'][first:i],road['xyz'][first+1:i+1]))
                            if length>200: continue
                            merged={t for p in road['p'][first:i+1] for t in tags(p)}
                            position=road['xyz'][first].copy()
                            for other in self.roads:
                                for p in other['p']:
                                    if merged.intersection(tags(p)):
                                        self.tag(p,tag);p.set('position',fmt(position-vec(other['e'])))
                                self.refresh(other)
                            for p in road['p'][first+1:i+1]: road['e'].remove(p)
                            del road['p'][first+1:i+1]
                            self.refresh(road)
                            self.report.append(dict(kind='collapse_junction_knot',road=road['e'].get('name'),node=tag))
                            again=True;break
                        visits[tag]=i
                    if again:break
        for road in list(self.roads):
            if len(road['p'])<2:
                self.roads.remove(road)
                if road['e'] in self.added:self.added.remove(road['e'])
                else:self.removed.append(road['e'])
                self.report.append(dict(kind='remove_redundant_link',road=road['e'].get('name')))
        while self.ends():
            road,index=self.ends()[0]
            if not road['e'].get('name','').startswith('through_'):break
            degree=self.degrees()
            order=list(range(len(road['p'])))
            if index:order.reverse()
            anchor=next((i for i in order[1:] if any(degree[t]>=2 for t in tags(road['p'][i]))),order[-1])
            if anchor==order[-1]:
                self.roads.remove(road)
                if road['e'] in self.added:self.added.remove(road['e'])
                else:self.removed.append(road['e'])
            else:
                removed=order[:order.index(anchor)]
                for i in removed:road['e'].remove(road['p'][i])
                road['p']=[p for i,p in enumerate(road['p']) if i not in removed]
                self.refresh(road)
            self.report.append(dict(kind='trim_redundant_spur',road=road['e'].get('name')))
        visited=set()
        for _ in range(100):
            repaired=False
            for road in self.roads:
                if 'game_through_route' not in road['e'].get('tags',''): continue
                for i in range(1,len(road['p'])-1):
                    a,b,c=road['xyz'][i-1:i+2]
                    if np.dot(unit(b-a),unit(c-b)) >= -.2: continue
                    node=next((t for t in tags(road['p'][i]) if t.startswith(('road_node_crossing_','road_node_through_'))),None)
                    if node is None:
                        if not tags(road['p'][i]):
                            road['e'].remove(road['p'][i]);road['p'].pop(i);self.refresh(road)
                            self.report.append(dict(kind='remove_reversing_handle',road=road['e'].get('name')))
                            repaired=True;break
                        continue
                    if node in visited:continue
                    visited.add(node)
                    position=a.copy()
                    previous=road['p'][i-1]
                    target_tag=next(iter(tags(previous)),node)
                    for other in self.roads:
                        for point in other['p']:
                            if node in tags(point):
                                self.tag(point,target_tag)
                                point.set('position',fmt(position-vec(other['e'])))
                                self.refresh(other)
                    self.tag(previous,target_tag)
                    road['e'].remove(road['p'][i]);road['p'].pop(i);self.refresh(road)
                    self.report.append(dict(kind='straighten_junction',road=road['e'].get('name'),node=node))
                    repaired=True
                    break
                if repaired: break
            if not repaired: break
        # Give the terminal turnaround a genuine semicircular bend rather than
        # three widely separated handles meeting in a reversing corner.
        access=next((r for r in self.roads if r['e'].get('name')=='airport_terminal_access'),None)
        if access is not None and len(access['p'])==17:
            airport=next(e for e in self.root.iter('Entity') if e.get('name')=='airport')
            rotation=Rotation.from_quat(list(map(float,airport.get('rotation').split())))
            arc=[]
            for angle in np.linspace(-math.pi/2,-3*math.pi/2,9)[1:-1]:
                local=np.array([-1500+15*math.cos(angle),.35,190+15*math.sin(angle)])
                position=rotation.apply(local)+vec(airport)
                point=ET.Element('Entity',name='spline_point_new',id=self.new_id(),active='true',
                                 position=fmt(position),rotation='0 0 0 1',scale='1 1 1')
                arc.append(point)
            access['e'].remove(access['p'][8]);access['p'][8:9]=arc
            for point in arc:access['e'].append(point)
            # Reinsert in evaluation order, which follows the child order.
            for point in access['p']: access['e'].remove(point)
            for point in access['p']: access['e'].append(point)
            self.refresh(access)
            self.report.append(dict(kind='terminal_turn_radius',road='airport_terminal_access',radius=15))

    def refine_junctions(self):
        # These staggered OSM forks have no room for separate four-lane mouths.
        # Author one shared anchor, retaining every external road connection.
        for suffixes in [('12941926543','12941926544'),
                         ('9755596181','9755596184','9755596185')]:
            cluster = {'road_node_'+s for s in suffixes}
            members = [(r,i) for r in self.roads for i,p in enumerate(r['p']) if cluster.intersection(tags(p))]
            if not members or not any(float(r['s'].get('road_width','8')) >= 14 for r,_ in members):continue
            if not any(set(tags(r['p'][i])) - {'road_node_'+suffixes[0]} for r,i in members):continue
            center = np.mean([r['xyz'][i] for r,i in members],axis=0)
            for road in self.roads:
                indices = [i for r,i in members if r is road]
                if not indices:continue
                lo,hi = min(indices),max(indices)
                assert all(not tags(p) or cluster.intersection(tags(p)) for p in road['p'][lo:hi+1])
                point = road['p'][lo]
                for p in road['p'][lo+1:hi+1]:road['e'].remove(p)
                del road['p'][lo+1:hi+1]
                point.set('position',fmt(center-vec(road['e'])))
                point.set('tags','road_node_'+suffixes[0])
                self.refresh(road)
            self.report.append(dict(kind='consolidate_racing_junction',nodes=sorted(cluster)))
        # Consolidate short duplicate strips that exactly retrace another road.
        for road in list(self.roads):
            if len(road['p'])!=2:continue
            a,b=road['xyz']
            if np.linalg.norm((b-a)[[0,2]])>30:continue
            duplicate=False
            for other in self.roads:
                if other is road:continue
                for c,d in zip(other['xyz'],other['xyz'][1:]):
                    if (np.linalg.norm((a-c)[[0,2]])<.5 and np.linalg.norm((b-d)[[0,2]])<.5) or \
                       (np.linalg.norm((a-d)[[0,2]])<.5 and np.linalg.norm((b-c)[[0,2]])<.5):duplicate=True
            if duplicate:
                self.roads.remove(road)
                if road['e'] in self.added:self.added.remove(road['e'])
                else:self.removed.append(road['e'])
                self.report.append(dict(kind='remove_duplicate_strip',road=road['e'].get('name')))
        problem_nodes=['1020923060','273733874','926614157','9755596181',
                       'airport_9104400000000003266','crossing_9104400000000002897',
                       'through_9004200000000036034','12941926543',
                       'game_join_9004200000000064001']
        for suffix in problem_nodes:
            tag='road_node_'+suffix
            members=[(r,p) for r in self.roads for p in r['p'] if tag in tags(p)]
            widened = suffix in ('12941926543','9755596181','game_join_9004200000000064001') and any(float(r['s'].get('road_width','8')) >= 14 for r,_ in members)
            marker = 'junction_layout_refined_racing' if widened else 'junction_layout_refined'
            if len(members)<2 or all(marker in p.get('tags','').split(',') for _,p in members):continue
            arms=[]
            for road,point in members:
                i=road['p'].index(point)
                for side in (-1,1):
                    j=i+side
                    if not 0<=j<len(road['p']):continue
                    v=road['xyz'][j]-road['xyz'][i]
                    arms.append([math.atan2(v[2],v[0])%(2*math.pi),road,point,side])
            minimum=math.radians(50)
            for _ in range(50):
                arms.sort(key=lambda x:x[0]%(2*math.pi))
                changed=False
                for i in range(len(arms)):
                    j=(i+1)%len(arms)
                    gap=(arms[j][0]-arms[i][0])%(2*math.pi)
                    if gap<minimum:
                        shift=(minimum-gap)*.5+.001
                        arms[i][0]=(arms[i][0]-shift)%(2*math.pi)
                        arms[j][0]=(arms[j][0]+shift)%(2*math.pi)
                        changed=True
                if not changed:break
            for angle,road,point,side in arms:
                i=road['p'].index(point)
                origin=road['xyz'][i].copy()
                j=i+side
                # Clear very short untagged handles, which leave no room for a
                # junction mouth. Preserve all other junction anchors.
                while 0<j<len(road['p'])-1 and not tags(road['p'][j]) and np.linalg.norm((road['xyz'][j]-origin)[[0,2]])<40:
                    road['e'].remove(road['p'][j]);road['p'].pop(j);self.refresh(road)
                    i=road['p'].index(point);j=i+side
                if not 0<=j<len(road['p']):continue
                distance=np.linalg.norm((road['xyz'][j]-origin)[[0,2]])
                radius=min(30,distance*.55)
                position=origin+np.array([math.cos(angle)*radius,0,math.sin(angle)*radius])
                position[1]=origin[1]+(road['xyz'][j][1]-origin[1])*min(1,radius/max(distance,1))
                control=ET.Element('Entity',name='spline_point_new',id=self.new_id(),active='true',
                                   position=fmt(position-vec(road['e'])),rotation='0 0 0 1',scale='1 1 1')
                insert=i+1 if side>0 else i
                road['p'].insert(insert,control)
                for p in road['p']:
                    if p in list(road['e']):road['e'].remove(p)
                for p in road['p']:road['e'].append(p)
                self.refresh(road)
                point.set('tags',','.join(dict.fromkeys(point.get('tags','').split(',')+[marker])))
            self.report.append(dict(kind='junction_approaches',node=tag,arms=len(arms)))

    def airport_bypass(self):
        if any(r['e'].get('name') == 'airport_perimeter_road' for r in self.roads):
            return
        airport = next(e for e in self.root.iter('Entity') if e.get('name') == 'airport')
        origin = vec(airport)
        rotation = Rotation.from_quat(list(map(float, airport.get('rotation').split())))
        def world(p):
            q = rotation.apply(p)+origin
            q[1] = self.terrain.sample_height(q[0], q[2])+.25
            return q
        template = self.roads[0]
        def make(name, points, width=8, explicit_height=False):
            entity = ET.Element('Entity', name=name, id=self.new_id(), active='true',
                                position='0 0 0', rotation='0 0 0 1', scale='1 1 1',
                                tags='road,map_road,airport_connection')
            for component in ('physics', 'render', 'spline'):
                entity.append(copy.deepcopy(template['e'].find(component)))
            spline = entity.find('spline')
            for child in list(spline):
                spline.remove(child)
            spline.attrib.update(road_width=str(width), road_width_end=str(width),
                                 sidewalk_enabled='false', smoothing_length='60', grade_smoothing='.5',
                                 conform_to_terrain='false' if explicit_height else 'true')
            controls = [ET.SubElement(entity, 'Entity', name=f'spline_point_{i}', id=self.new_id(),
                                     active='true', position=fmt(p), rotation='0 0 0 1', scale='1 1 1')
                        for i, p in enumerate(points)]
            road = dict(e=entity, s=spline, p=controls, xyz=np.array(points))
            self.added.append(entity)
            self.roads.append(road)
            return road
        outline = [(-3100,-900),(350,-900),(500,-750),(500,550),(350,700),
                   (-3100,700),(-3250,550),(-3250,-750),(-3100,-900)]
        ring_points = []
        for a, b in zip(outline, outline[1:]):
            for u in np.linspace(0, 1, max(2, math.ceil(math.dist(a,b)/55)), endpoint=False):
                x, z = np.array(a)+(np.array(b)-a)*u
                ring_points.append(world([x,0,z]))
        ring_points.append(ring_points[0].copy())
        original = list(self.roads)
        ring = make('airport_perimeter_road', ring_points, 10)
        closure = 'road_node_airport_ring'
        self.tag(ring['p'][0], closure)
        self.tag(ring['p'][-1], closure)
        def attach(position):
            starts, delta = ring['xyz'][:-1], np.diff(ring['xyz'],axis=0)
            length2 = np.maximum(np.sum(delta[:,[0,2]]**2,axis=1),1e-8)
            u = np.clip(np.sum((position-starts)[:,[0,2]]*delta[:,[0,2]],axis=1)/length2,0,1)
            projected = starts+delta*u[:,None]
            i = int(np.argmin(np.linalg.norm((projected-position)[:,[0,2]],axis=1)))
            point, q = self.insert(ring,i,projected[i])
            tag = next(iter(tags(point)), 'road_node_airport_'+point.get('id'))
            self.tag(point,tag)
            return q, tag
        affected = 0
        for road in original:
            local = rotation.inv().apply(road['xyz']-origin)
            pieces, current = [], []
            clipped = False
            # Clip centerline segments against the protected airport footprint.
            for i, (a,b) in enumerate(zip(local,local[1:])):
                delta=b-a
                lo,hi=0.,1.
                for axis,minimum,maximum in ((0,-3250,500),(2,-900,700)):
                    if abs(delta[axis])<1e-8:
                        if not minimum<a[axis]<maximum: lo,hi=1.,0.
                    else:
                        u,v=sorted(((minimum-a[axis])/delta[axis],(maximum-a[axis])/delta[axis]))
                        lo,hi=max(lo,u),min(hi,v)
                if lo>=hi:
                    if not current: current.append((road['xyz'][i],road['p'][i]))
                    current.append((road['xyz'][i+1],road['p'][i+1]))
                    continue
                clipped=True
                if lo>0:
                    if not current: current.append((road['xyz'][i],road['p'][i]))
                    current.append((road['xyz'][i]+(road['xyz'][i+1]-road['xyz'][i])*lo,None))
                if len(current)>1: pieces.append(current)
                current=[]
                if hi<1:
                    current=[(road['xyz'][i]+(road['xyz'][i+1]-road['xyz'][i])*hi,None),
                             (road['xyz'][i+1],road['p'][i+1])]
            if len(current)>1: pieces.append(current)
            if not clipped: continue
            affected+=1
            self.roads.remove(road)
            if road['e'] in self.added: self.added.remove(road['e'])
            else: self.removed.append(road['e'])
            for pi,piece in enumerate(pieces):
                if math.dist(piece[0][0],piece[-1][0])<1: continue
                fresh=make(road['e'].get('name')+('_airport_section_'+str(pi) if pi else ''),[p for p,_ in piece],float(road['s'].get('road_width','8')))
                for k,(_,old) in enumerate(piece):
                    if old is not None:
                        for tag in tags(old): self.tag(fresh['p'][k],tag)
                    else:
                        q,tag=attach(fresh['xyz'][k])
                        fresh['p'][k].set('position',fmt(q))
                        self.tag(fresh['p'][k],tag)
                self.refresh(fresh)
        # A graded terminal access road joins two different points of the ring.
        # It enters between the runways and meets the existing terminal service road.
        access_local=[(500,160),(300,160),(100,160),(-100,160),(-450,165),(-900,175),
                      (-1350,175),(-1500,175),(-1550,190),(-1500,205),(-1350,205),
                      (-900,205),(-450,240),(-100,240),(100,240),(300,240),(500,240)]
        points=[]
        for x,z in access_local:
            q=world([x,0,z])
            if x<=-100: q[1]=origin[1]+.35
            else:
                u=(500-x)/600
                q[1]=max(0,q[1])*(1-u)+(origin[1]+.35)*u
            points.append(q)
        access=make('airport_terminal_access',points,8,True)
        for i in (0,len(points)-1):
            q,tag=attach(points[i]);access['p'][i].set('position',fmt(q));self.tag(access['p'][i],tag)
        self.refresh(access)
        fence=next(e for e in airport.findall('Entity') if e.get('name')=='perimeter_fence_east')
        old_id=fence.get('id')
        upper=copy.deepcopy(fence)
        fence.set('position','-200 1.5 -285');fence.set('scale','0.15 3 850')
        upper.set('id',self.new_id());upper.set('name','perimeter_fence_east_north')
        upper.set('position','-200 1.5 385');upper.set('scale','0.15 3 250')
        self.extra_edits.append((old_id,ET.tostring(fence,encoding='unicode').rstrip()+'\n'+ET.tostring(upper,encoding='unicode').rstrip()))
        self.report.append(dict(kind='airport_bypass', rerouted_or_removed=affected,
                                road='airport_perimeter_road', target='airport_terminal_access'))

    def join_crossings(self):
        """New routes are at grade: crossings need shared mesh/traffic anchors."""
        segments, owners = [], []
        for ri, road in enumerate(self.roads):
            for a, b in zip(road['xyz'], road['xyz'][1:]):
                segments.append((a[[0, 2]], b[[0, 2]]))
                owners.append(ri)
        segments = np.array(segments)
        owners = np.array(owners)
        a, direction = segments[:, 0], segments[:, 1]-segments[:, 0]
        crossings = []
        def cross(v, w):
            return v[..., 0]*w[..., 1]-v[..., 1]*w[..., 0]
        for ri, road in enumerate(self.roads):
            if road['e'] not in self.added:
                continue
            for p, q in zip(road['xyz'], road['xyz'][1:]):
                start, delta = p[[0, 2]], (q-p)[[0, 2]]
                den = cross(delta, direction)
                safe = np.abs(den) > 1e-4
                with np.errstate(divide='ignore', invalid='ignore'):
                    u = cross(a-start, direction)/den
                    v = cross(a-start, delta)/den
                indices = np.where(safe & (owners != ri) & (u > 0) & (u < 1) & (v >= 0) & (v <= 1))[0]
                for index in indices:
                    point = p+(q-p)*u[index]
                    if min(np.linalg.norm((point-x)[[0, 2]]) for x in (road['xyz'][0], road['xyz'][-1])) < 20:
                        continue
                    if any(np.linalg.norm((point-old[2])[[0, 2]]) < 15 for old in crossings):
                        continue
                    crossings.append((ri, int(owners[index]), point))
        for ri, oi, position in crossings:
            tag = 'road_node_crossing_'+self.new_id()
            common = position.copy()
            for road_index in (oi, ri):
                road = self.roads[road_index]
                starts, deltas = road['xyz'][:-1], np.diff(road['xyz'], axis=0)
                lengths = np.maximum(np.sum(deltas[:, [0, 2]]**2, axis=1), 1e-8)
                u = np.clip(np.sum((common-starts)[:, [0, 2]]*deltas[:, [0, 2]], axis=1)/lengths, 0, 1)
                projected = starts+deltas*u[:, None]
                segment = int(np.argmin(np.linalg.norm((projected-common)[:, [0, 2]], axis=1)))
                point, actual = self.insert(road, segment, common)
                if road_index == oi:
                    common = actual
                    tag = next(iter(tags(point)), tag)
                # The second road must share the exact position, not two nearby handles.
                point.set('position', fmt(common-vec(road['e'])))
                self.tag(point, tag)
                self.refresh(road)
            self.report.append(dict(kind='crossing', road=self.roads[ri]['e'].get('name'),
                                    target=self.roads[oi]['e'].get('name'), position=common.tolist()))

    def validate(self):
        assert not self.ends(), 'unconnected road endpoint'
        assert len(components(self.roads)) == 1, 'disconnected road network'
        assert not any('game_return_loop' in r['e'].get('tags', '') for r in self.roads)
        nodes = {}
        for road in self.roads:
            assert len(road['p']) >= 2
            for point, position in zip(road['p'], road['xyz']):
                for tag in tags(point):
                    if tag in nodes:
                        assert np.linalg.norm((position-nodes[tag])[[0, 2]]) < .5, tag
                    nodes[tag] = position

    def audit(self):
        return dict(road_splines=len(self.roads), connected_networks=len(components(self.roads)),
                    unconnected_ends=len(self.ends()),
                    artificial_return_loops=sum('game_return_loop' in r['e'].get('tags','') for r in self.roads),
                    centerline_km=round(sum(float(np.linalg.norm(np.diff(r['xyz'],axis=0),axis=1).sum()) for r in self.roads)/1000,2),
                    airport_routes=[r['e'].get('name') for r in self.roads if r['e'].get('name','').startswith('airport_')],
                    continuous_routes=[r['e'].get('name') for r in self.roads if 'game_through_route' in r['e'].get('tags','')])

    def output(self):
        if not self.report:
            return self.text
        # Replace exactly the roads subtree. Unrelated text is byte-for-byte kept.
        for entity in self.removed:
            self.container.remove(entity)
        for entity in self.added:
            self.container.append(entity)
        match = re.search(r'<Entity\b[^>]*\bid="'+self.container.get('id')+r'"[^>]*>', self.text)
        depth = 1
        for token in re.finditer(r'</?Entity\b[^>]*>', self.text[match.end():]):
            depth += -1 if token.group().startswith('</') else (0 if token.group().endswith('/>') else 1)
            if depth == 0:
                end = match.end()+token.end()
                replacement = ET.tostring(self.container, encoding='unicode').rstrip()
                text = self.text[:match.start()]+replacement+self.text[end:]
                for eid, replacement in self.extra_edits:
                    start = re.search(r'<Entity\b[^>]*\bid="'+eid+r'"[^>]*>',text)
                    depth = 1
                    for token in re.finditer(r'</?Entity\b[^>]*>',text[start.end():]):
                        depth += -1 if token.group().startswith('</') else (0 if token.group().endswith('/>') else 1)
                        if depth == 0:
                            text=text[:start.start()]+replacement+text[start.end()+token.end():]
                            break
                return text
        raise ValueError('unbalanced road hierarchy')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    terrain = IslandTerrain(json.loads(Path(ATLAS_PATH).read_text())['crs'])
    terrain.load_heightmap(HEIGHTMAP_PATH)
    path = Path('worlds/plan.world')
    source = path.read_text(encoding='utf-8')
    repair = Overhaul(source, terrain)
    repair.run()
    print(json.dumps(dict(before=repair.before, removed=len(repair.removed),
                          connections=len(repair.report), roads=len(repair.roads), networks=len(components(repair.roads)))))
    if args.apply:
        if repair.report:
            assert path.read_text(encoding='utf-8') == source, 'world changed during authoring'
            path.write_text(repair.output(), encoding='utf-8')
        report_path = Path('tools/map/road_overhaul.json')
        report_path.write_text(json.dumps(repair.audit(), indent=2)+'\n')
