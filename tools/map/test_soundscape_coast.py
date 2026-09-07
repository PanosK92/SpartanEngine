"""Regression checks for processed-terrain coastlines and inland surf rejection."""
import json
import unittest
import xml.etree.ElementTree as ET

import numpy as np
from shapely.geometry import Point, Polygon
from shapely.ops import nearest_points, unary_union

from build_soundscapes import coastal_land, ROOT
from organize_world import snapshot


class CoastTopologyTests(unittest.TestCase):
    def test_landlocked_depression_is_not_a_beach(self):
        axis = np.arange(21)*100
        height = np.full((21,21),-5.)
        height[3:18,3:18] = .1  # Almost the entire island is near sea level.
        height[9:12,9:12] = -20  # An inland hollow must not generate surf.
        land = coastal_land(axis,axis,height,0,simplify=0)
        self.assertTrue(land.contains(Point(1000,1000)))
        self.assertGreater(land.boundary.distance(Point(1000,1000)),600)

    def test_ocean_connected_inlet_remains_a_coast(self):
        axis = np.arange(21)*100
        height = np.full((21,21),-5.)
        height[3:18,3:18] = 5
        height[:11,9:12] = -5
        land = coastal_land(axis,axis,height,0,simplify=0)
        self.assertFalse(land.contains(Point(1000,900)))
        self.assertAlmostEqual(land.boundary.distance(Point(1000,1100)),50)


class IslandCoastTests(unittest.TestCase):
    @unittest.skipUnless((ROOT/'binaries/project/soundscapes/regions.json').exists(), 'Generate island soundscapes first')
    def test_authored_world_inland_and_beach_probes(self):
        root = ET.parse(ROOT/'worlds/plan.world').getroot()
        entities = {e.get('name'):e for e in root.iter('Entity')}
        transforms = snapshot(root)
        shores = [e for e in root.iter('Entity') if e.find('volume') is not None and e.find('volume').get('audio_group') == 'island_shore']
        self.assertTrue(shores)
        coast = unary_union([Polygon([(float(p.get('x')),float(p.get('z'))) for p in e.find('volume/AudioPolygon')]).boundary for e in shores])
        probes = []
        for name in ['player_car','physics_body_camera','airport']:
            e = entities[name]
            p = transforms[e.get('id')][1][:3,3]
            distance = coast.distance(Point(p[0],p[2]))
            self.assertGreater(distance,180,name)
            # Same X/Z at sea level must also be silent, independent of elevation.
            for y in [0,30]:
                probes.append(dict(name=f'{name} at y={y}',position=[p[0],y,p[2]],surf=False,distance=distance))
        airport = probes[-1]['position']
        inland = Point(airport[0],airport[2])
        beach = nearest_points(inland,coast)[1]
        toward_land = np.array([inland.x-beach.x,inland.y-beach.y])
        toward_land /= np.linalg.norm(toward_land)
        for offset, audible in [(0,True),(50,True),(250,False),(-50,True),(-250,False)]:
            p = np.array([beach.x,beach.y])+toward_land*offset
            distance = coast.distance(Point(*p))
            self.assertEqual(distance < 180,audible)
            probes.append(dict(name=f'Airport coast {offset} m toward land',position=[p[0],30,p[1]],surf=audible,distance=distance))
        output = ROOT/'binaries/audio_tests/coast_probes.json'
        output.parent.mkdir(parents=True,exist_ok=True)
        output.write_text(json.dumps(probes,indent=2))
        print('\nCoast distances:',', '.join(f'{p["name"]}: {p["distance"]:.1f} m' for p in probes[::2]))


if __name__ == '__main__':
    unittest.main()
