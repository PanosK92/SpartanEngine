"""Recover OSM junction anchors in an existing world without replacing its roads or settings.

Run with the editor closed. Dry-run by default; --apply writes an exclusive backup first.
Only junctions within --tolerance of at least two existing road centerlines are recovered.
Unmatched junctions are reported and left for manual authoring rather than guessed.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import osm_roads as osm


def upgrade(text, node_world, junctions, tolerance=6.0):
    root = ET.fromstring(text)
    parent = next(e for e in root.iter("Entity") if e.get("name") == "roads")
    if parent.get("position", "0 0 0") != "0 0 0" or parent.get("rotation", "0 0 0 1") != "0 0 0 1" or parent.get("scale", "1 1 1") != "1 1 1":
        raise ValueError("the roads parent must have an identity transform")
    ids = {int(e.get("id")) for e in root.iter("Entity") if e.get("id")}
    roads = []
    for e in parent.findall("Entity"):
        spline = e.find("spline")
        if spline is None or spline.get("profile", "0") != "0":
            continue
        if e.get("rotation", "0 0 0 1") != "0 0 0 1" or e.get("scale", "1 1 1") != "1 1 1":
            continue
        origin = tuple(map(float, e.get("position").split()))
        children = [c for c in e.findall("Entity") if c.get("name", "").startswith("spline_point_")]
        points = [tuple(a + b for a, b in zip(origin, map(float, c.get("position").split()))) for c in children]
        roads.append(dict(entity=e, origin=origin, children=children, points=points, anchors=[]))

    recovered = 0
    unmatched = []
    for node in sorted(junctions):
        x, z = node_world[node]
        matches = []
        for road in roads:
            best = None
            for i, (a, b) in enumerate(zip(road["points"], road["points"][1:])):
                dx, dz = b[0] - a[0], b[2] - a[2]
                length_sq = dx * dx + dz * dz
                if length_sq < 1e-8:
                    continue
                t = min(1.0, max(0.0, ((x - a[0]) * dx + (z - a[2]) * dz) / length_sq))
                distance = math.hypot(a[0] + dx * t - x, a[2] + dz * t - z)
                if distance <= tolerance and (best is None or distance < best[0]):
                    best = (distance, i + t, a[1] + (b[1] - a[1]) * t)
            if best:
                matches.append((road, best))
        if len(matches) < 2:
            unmatched.append(node)
            continue
        recovered += 1
        for road, (_, order, y) in matches:
            road["anchors"].append((order, node, (x, y, z)))

    # Change only the existing control point lines and insert new anchors beside them.
    replacements = {}
    point_lines = {match.group(2): match for match in re.finditer(r'^([ \t]*)<Entity\b[^\n]*\bid="([0-9]+)"[^\n]*$', text, re.M)}
    inserted = 0
    next_id = osm.ID_BASE + 50000000
    for road in roads:
        insertions = collections.defaultdict(list)
        updates = {}
        for order, node, position in sorted(road["anchors"]):
            nearest = min(round(order), len(road["points"]) - 1)
            p = road["points"][nearest]
            # Reuse nearly coincident handles. Keep all other authored control points.
            if math.hypot(p[0] - position[0], p[2] - position[2]) <= 0.25 and nearest not in updates:
                updates[nearest] = (node, position)
            else:
                insertions[min(int(order), len(road["points"]) - 2)].append((order, node, position))
        for i, child in enumerate(road["children"]):
            if i not in updates and i not in insertions:
                continue
            entity_id = child.get("id")
            match = point_lines.get(entity_id)
            if not match:
                raise ValueError(f"cannot locate control point {entity_id}")
            line = match.group(0)
            indent = match.group(1)
            def coords(position):
                return " ".join(f"{p - o:.3f}" for p, o in zip(position, road["origin"]))
            if i in updates:
                node, position = updates[i]
                tags = [t for t in child.get("tags", "").split(",") if t and not t.startswith("road_node_")]
                tags.append(f"road_node_{node}")
                line = osm.set_attribute(line, "position", coords(position))
                line = osm.set_attribute(line, "tags", ",".join(tags))
            for _, node, position in sorted(insertions[i]):
                while next_id in ids:
                    next_id += 1
                ids.add(next_id)
                line += f'\n{indent}<Entity name="spline_point_junction_{node}" id="{next_id}" active="true" position="{coords(position)}" rotation="0 0 0 1" scale="1 1 1" tags="road_node_{node}" />'
                inserted += 1
                next_id += 1
            if line != match.group(0):
                replacements[match.span()] = line
    for (start, end), line in sorted(replacements.items(), reverse=True):
        text = text[:start] + line + text[end:]
    return text, dict(recovered=recovered, unmatched=len(unmatched), inserted=inserted, changed_lines=len(replacements))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", default=osm.WORLD_PATH)
    parser.add_argument("--tolerance", type=float, default=6.0)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    atlas = json.loads(Path(osm.ATLAS_PATH).read_text(encoding="utf-8"))
    projection = osm.Projection(atlas["crs"])
    elements = osm.fetch_roads(osm.DEFAULT_CLASSES, False)["elements"]
    node_world = {node: projection.lonlat_to_world(g["lon"], g["lat"]) for e in elements for node, g in zip(e.get("nodes", []), e.get("geometry", []))}
    _, junctions = osm.build_segments(elements, node_world)
    path = Path(args.world)
    original = path.read_bytes()
    newline = "\r\n" if b"\r\n" in original else "\n"
    text, report = upgrade(original.decode("utf-8").replace("\r\n", "\n"), node_world, junctions, args.tolerance)
    print(json.dumps(report, indent=2))
    if args.apply and report["changed_lines"]:
        backup = path.with_suffix(path.suffix + ".before_junctions.bak")
        with backup.open("xb") as f:
            f.write(original)
        path.write_bytes(text.replace("\n", newline).encode("utf-8"))
        print(f"Updated {path}; original saved to {backup}")


if __name__ == "__main__":
    main()
