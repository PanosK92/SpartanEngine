import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from repair_road_ends import Repair,tags

class RoadEndTests(unittest.TestCase):
    def test_world_has_no_unconnected_road_ends(self):
        repair=Repair(Path('worlds/plan.world').read_text(encoding='utf-8'),None)
        self.assertEqual([(r['e'].get('name'),i) for r,i in repair.ends()],[])
    def test_return_loops_rejoin_their_approach(self):
        repair=Repair(Path('worlds/plan.world').read_text(encoding='utf-8'),None)
        deg=repair.degrees();loops=[r for r in repair.roads if 'game_return_loop' in r['e'].get('tags','')]
        self.assertGreater(len(loops),0)
        for r in loops:
            self.assertEqual(tags(r['p'][0]),tags(r['p'][-1]))
            self.assertEqual(deg[tags(r['p'][0])[0]],3)
            self.assertLess(float(((r['xyz'][0]-r['xyz'][-1])**2).sum()),1e-8)
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
    def test_ids_are_unique(self):
        root=ET.parse('worlds/plan.world').getroot();ids=[e.get('id') for e in root.iter('Entity')]
        self.assertEqual(len(ids),len(set(ids)))

if __name__=='__main__':unittest.main()
