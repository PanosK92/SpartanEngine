import unittest
from pathlib import Path
from add_populated_sidewalks import intervals, upgrade


class SidewalkAuthoringTests(unittest.TestCase):
    def test_circle_clips_a_long_road(self):
        self.assertEqual(intervals([(0, 0, 0), (100, 0, 0)], [(50, 0, 10)]), [(0.4, 0.6)])

    def test_remote_road_has_no_sidewalk(self):
        self.assertEqual(intervals([(0, 0, 0), (100, 0, 0)], [(50, 40, 10)]), [])

    def test_overlapping_areas_merge(self):
        self.assertEqual(intervals([(0, 0, 0), (100, 0, 0)], [(40, 0, 20), (60, 0, 20)]), [(0.2, 0.8)])

    def test_authored_island_is_idempotent(self):
        source = (Path(__file__).resolve().parents[2] / "worlds/plan.world").read_text(encoding="utf-8")
        self.assertEqual(upgrade(source), source)


if __name__ == "__main__":
    unittest.main()
