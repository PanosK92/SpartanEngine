import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from repair_road_ends import Repair,tags

class RoadEndTests(unittest.TestCase):
    def test_racing_widths_and_authoring_are_stable(self):
        from tune_racing_roads import tune
        source=Path('worlds/plan.world').read_text(encoding='utf-8')
        result,counts=tune(source)
        self.assertEqual(result,source)
        self.assertGreater(counts['racing_main'],0)
        self.assertGreater(counts['racing_technical'],0)
        root=ET.fromstring(source)
        roads=next(e for e in root.iter('Entity') if e.get('name')=='roads')
        for road in roads.findall('Entity'):
            spline=road.find('spline')
            if spline is None:continue
            expected=15 if 'racing_main' in road.get('tags','').split(',') else 8
            self.assertEqual(float(spline.get('road_width')),expected,road.get('name'))
            self.assertEqual(float(spline.get('road_width_end')),expected,road.get('name'))

    def test_world_has_no_unconnected_road_ends(self):
        repair=Repair(Path('worlds/plan.world').read_text(encoding='utf-8'),None)
        self.assertEqual([(r['e'].get('name'),i) for r,i in repair.ends()],[])
    def test_one_network_without_artificial_returns(self):
        from overhaul_roads import components
        repair=Repair(Path('worlds/plan.world').read_text(encoding='utf-8'),None)
        self.assertEqual(len(components(repair.roads)),1)
        self.assertFalse(any('game_return_loop' in r['e'].get('tags','') for r in repair.roads))

    def test_overhaul_is_idempotent(self):
        from overhaul_roads import Overhaul
        source=Path('worlds/plan.world').read_text(encoding='utf-8')
        repair=Overhaul(source,None)
        self.assertEqual(repair.run(),[])
        self.assertEqual(repair.output(),source)

    def test_airport_surface_clearance(self):
        import numpy as np
        from scipy.spatial.transform import Rotation
        repair=Repair(Path('worlds/plan.world').read_text(encoding='utf-8'),None)
        airport=next(e for e in repair.root.iter('Entity') if e.get('name')=='airport')
        origin=np.array(list(map(float,airport.get('position').split())))
        rotation=Rotation.from_quat(list(map(float,airport.get('rotation').split())))
        for road in repair.roads:
            if road['e'].get('name')=='airport_terminal_access':
                self.assertEqual(road['s'].get('conform_to_terrain'),'false')
                continue
            local=rotation.inv().apply(road['xyz']-origin)
            inside=(local[:,0]>-3100)&(local[:,0]<-100)&(local[:,2]>-750)&(local[:,2]<550)
            self.assertFalse(inside.any(),road['e'].get('name'))

    def test_shared_nodes_coincide(self):
        repair=Repair(Path('worlds/plan.world').read_text(encoding='utf-8'),None);nodes={}
        for r in repair.roads:
            for p,xyz in zip(r['p'],r['xyz']):
                for t in tags(p):
                    if not t.startswith('road_node_game_'):continue
                    if t in nodes:self.assertLess(float(((xyz-nodes[t])**2).sum()),.25,t)
                    nodes[t]=xyz
    def test_repair_is_idempotent(self):
        source=Path('worlds/plan.world').read_text(encoding='utf-8')
        repair=Repair(source,None)
        self.assertEqual(repair.run(),[])
        self.assertEqual(repair.output(),source)
    def test_endpoint_rejoins_a_distant_span_of_its_own_road(self):
        repair=Repair(Path('worlds/plan.world').read_text(encoding='utf-8'),None)
        road=next(r for r in repair.roads if r['e'].get('name')=='r004_zakynthou_volimon')
        node=tags(road['p'][0])[0]
        self.assertTrue(any(node in tags(p) for p in road['p'][4:-1]))
        self.assertFalse(any(r['e'].get('name').startswith('return_loop_r004_') for r in repair.roads))
    def test_repaired_routes_have_no_reversing_handles(self):
        import numpy as np
        from repair_road_ends import unit
        repair=Repair(Path('worlds/plan.world').read_text(encoding='utf-8'),None)
        for road in repair.roads:
            if 'game_through_route' not in road['e'].get('tags',''):continue
            for a,b,c in zip(road['xyz'],road['xyz'][1:],road['xyz'][2:]):
                self.assertGreaterEqual(float(np.dot(unit(b-a),unit(c-b))),-.2,road['e'].get('name'))

    def test_ids_are_unique(self):
        root=ET.parse('worlds/plan.world').getroot();ids=[e.get('id') for e in root.iter('Entity')]
        self.assertEqual(len(ids),len(set(ids)))

if __name__=='__main__':unittest.main()
