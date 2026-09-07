"""Author local sidewalk intervals without splitting roads or changing junction tags.

The marker list deliberately excludes beaches, viewpoints and race/event pins.
Run from the repository root; --apply updates only spline XML elements.
"""
import argparse
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

TOWNS = "bochali argassi vasilikos kalamaki laganas agios_sostis lithakia keri_lake keri machairado mouzaki tsilivi tragaki alykes katastari agios_nikolaos volimes anafonitria maries exo_chora agios_leon kiliomeno agalas gyri".split()
SERVICES = "port dealer_town dealer_exotic tune_shop license_school tires paint wash gas_north gas_south gas_west gas_volimes gas_ferry".split()


def intervals(points, zones):
    found = []
    for i, (a, b) in enumerate(zip(points, points[1:])):
        dx, dz = b[0] - a[0], b[2] - a[2]
        aa = dx * dx + dz * dz
        if aa < 0.001:
            continue
        for x, z, radius in zones:
            ox, oz = a[0] - x, a[2] - z
            bb = 2 * (ox * dx + oz * dz)
            cc = ox * ox + oz * oz - radius * radius
            disc = bb * bb - 4 * aa * cc
            if disc < 0:
                continue
            lo = max(0, (-bb - math.sqrt(disc)) / (2 * aa))
            hi = min(1, (-bb + math.sqrt(disc)) / (2 * aa))
            if hi > lo:
                found.append(((i + lo) / (len(points) - 1), (i + hi) / (len(points) - 1)))
    merged = []
    for lo, hi in sorted(found):
        if merged and lo <= merged[-1][1] + 1e-7:
            merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
        else:
            merged.append((lo, hi))
    return merged


def upgrade(text):
    root = ET.fromstring(text)
    zones = []
    # Landmark positions in plan.world are local to identity parents.
    for e in root.iter("Entity"):
        name = e.get("name", "")
        radius = 350 if name in {"pin_" + t for t in TOWNS} else 0
        if name in {"pin_" + s for s in SERVICES}:
            radius = 140
        if name.startswith("city_grid_1km__"):
            radius = 800
        if name.startswith("gas_station_showcase__") or name == "player_garage_hub":
            radius = 160
        if radius:
            x, _, z = map(float, e.get("position").split())
            zones.append((x, z, radius))
    replacements = []
    enabled = sections = 0
    for e in root.iter("Entity"):
        spline = e.find("spline")
        if spline is None:
            continue
        points = [c for c in e.findall("Entity") if c.get("name", "").startswith("spline_point_")]
        if spline.get("profile") != "0" or len(points) < 2:
            replacements.append(None)
            continue
        assert e.get("rotation") == "0 0 0 1" and e.get("scale") == "1 1 1", e.get("name")
        origin = list(map(float, e.get("position").split()))
        positions = [tuple(a + b for a, b in zip(origin, map(float, p.get("position").split()))) for p in points]
        ranges = intervals(positions, zones)
        if not ranges:
            replacements.append(None)
            continue
        for old in list(spline.findall("sidewalk_range")):
            spline.remove(old)
        spline.set("sidewalk_enabled", "true" if ranges else "false")
        spline.set("sidewalk_width", "2")
        spline.set("curb_height", "0.15")
        for lo, hi in ranges:
            ET.SubElement(spline, "sidewalk_range", start=f"{lo:.9f}", end=f"{hi:.9f}")
        enabled += bool(ranges)
        sections += len(ranges)
        replacements.append(ET.tostring(spline, encoding="unicode").strip())
    it = iter(replacements)
    result = re.sub(r"<spline\b[^>]*?(?:/>|>.*?</spline>)", lambda m: next(it) or m.group(), text, flags=re.S)
    print(f"{len(zones)} populated areas; {enabled}/{len(replacements)} roads; {sections} sidewalk intervals")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    path = Path("worlds/plan.world")
    source = path.read_text(encoding="utf-8")
    result = upgrade(source)
    if args.apply:
        path.write_text(result, encoding="utf-8")
