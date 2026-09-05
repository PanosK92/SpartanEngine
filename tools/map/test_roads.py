"""Run with python -m unittest discover -s tools/map -p test_roads.py."""
import unittest
import xml.etree.ElementTree as ET
import osm_roads as osm
from upgrade_road_junctions import upgrade


class RoadImportTests(unittest.TestCase):
    def test_junction_survives_simplification_and_resampling(self):
        points = {1: (0, 0), 2: (23, 0.2), 3: (100, 0), 4: (23, 50)}
        main = osm.route_geometry([1, 2, 3], points, {2}, 2, 40, 15, 15)
        branch = osm.route_geometry([2, 4], points, {2}, 2, 40, 15, 15)
        self.assertIn(points[2], main)
        self.assertEqual(main[main.index(points[2])], branch[0])

    def test_geometry_crossing_does_not_create_graph_connection(self):
        points = {1: (-50, 0), 2: (50, 0), 3: (0, -50), 4: (0, 50)}
        ways = [dict(type="way", nodes=[1, 2], tags={"highway": "primary"}), dict(type="way", nodes=[3, 4], tags={"highway": "primary"})]
        _, junctions = osm.build_segments(ways, points)
        self.assertFalse(junctions)

    def test_junction_metadata_round_trip(self):
        points = [(0, 0, 0), (23, 2, 0), (100, 0, 0)]
        route = dict(id="test", name="", tier="main", osm_class="primary", width=8, points_xyz=points,
                     node_tags=osm.road_node_tags(points, {99: (23, 0)}, {99}))
        xml = "\n".join(osm.emit_route_xml(route, 0, dict(spline=osm.DEFAULT_SPLINE, render=osm.DEFAULT_RENDER, physics=osm.DEFAULT_PHYSICS), 0))
        root = ET.fromstring(xml)
        self.assertEqual(root.findall("Entity")[1].get("tags"), "road_node_99")
        self.assertIsNone(root.findall("Entity")[0].get("tags"))

    def test_upgrade_preserves_settings_and_is_idempotent(self):
        text = '<World><Entities>\n <Entity name="roads" id="1">\n'
        for r, points in enumerate([[(-100, 0, 0), (100, 0, 0)], [(0, 1, 0), (0, 1, 100)]]):
            text += f'  <Entity name="r{r}" id="{10+r}" position="0 0 0">\n   <spline profile="0" road_width="7" custom="keep" />\n'
            for i, p in enumerate(points):
                text += f'   <Entity name="spline_point_{i}" id="{100+r*10+i}" position="{p[0]} {p[1]} {p[2]}" />\n'
            text += '  </Entity>\n'
        text += ' </Entity>\n</Entities></World>'
        updated, report = upgrade(text, {99: (0, 0)}, {99})
        self.assertEqual(report["recovered"], 1)
        self.assertEqual(report["inserted"], 1)
        self.assertEqual(updated.count('custom="keep"'), 2)
        self.assertEqual(upgrade(updated, {99: (0, 0)}, {99})[0], updated)


if __name__ == "__main__":
    unittest.main()
