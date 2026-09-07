import unittest
from remove_road_blocking_buildings import intersects, transform, sample

class BuildingClearanceTests(unittest.TestCase):
    def test_road_crosses_building_between_endpoints(self):
        self.assertTrue(intersects((-20,0,0),(20,0,0),(-1,0,-1),(1,10,1),2))
    def test_clear_building_remains(self):
        self.assertFalse(intersects((-20,0,0),(20,0,0),(-1,0,5),(1,10,7),2))
    def test_overpass_is_separate(self):
        self.assertFalse(intersects((-20,20,0),(20,20,0),(-1,0,-1),(1,10,1),2))
    def test_curve_preserves_endpoints(self):
        points=[(0,0,0),(10,2,5),(20,0,0)]
        result=list(sample(points,.5))
        self.assertEqual(result[0],(0.,points[0]))
        self.assertEqual(result[-1],(1.,points[-1]))

if __name__=='__main__': unittest.main()
