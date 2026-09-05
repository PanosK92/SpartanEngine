"""
stamp the real zakynthos road network from openstreetmap onto the island in worlds/plan.world

usage, from the repository root, with the editor closed:
    python tools/map/osm_roads.py
    python tools/map/osm_roads.py --classes primary,secondary,tertiary,unclassified
    python tools/map/osm_roads.py --dry-run

what it does:
    1. fetches (or reuses a cached copy of) the osm ways for the requested highway classes
    2. builds the road graph, splits ways at junctions and chains them into continuous routes,
       a route continues straight through a junction, side roads end on it
    3. simplifies, resamples to an even spacing, projects to world space using the crs in
       worlds/plan_map.json, reads the terrain height for every point and clips roads that run
       into the sea
    4. replaces the children of the roads entity in worlds/plan.world, backing the file up first
    5. rewrites routes, junctions and stats in worlds/plan_map.json and regenerates plan_map.svg
"""

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict
from xml.sax.saxutils import escape as xml_escape

import numpy as np
from PIL import Image

# ---------------------------------------------------------------------------------------------
# configuration
# ---------------------------------------------------------------------------------------------

REPO_ROOT       = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CACHE_DIR       = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cache")
WORLD_PATH      = os.path.join(REPO_ROOT, "worlds", "plan.world")
ATLAS_PATH      = os.path.join(REPO_ROOT, "worlds", "plan_map.json")
SVG_PATH        = os.path.join(REPO_ROOT, "worlds", "plan_map.svg")
HEIGHTMAP_PATH  = os.path.join(REPO_ROOT, "binaries", "project", "height_maps", "zakynthos_heightmap.png")

ISLAND_RELATION = 543822
OVERPASS_URLS   = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
]

DEFAULT_CLASSES = ["primary", "secondary", "tertiary"]

# road width in meters and the gameplay tier per osm highway class
CLASS_WIDTH = {
    "primary":       12.0,
    "secondary":     10.0,
    "tertiary":       8.0,
    "unclassified":   6.0,
    "residential":    5.0,
    "living_street":  5.0,
    "service":        4.0,
    "track":          4.0,
}
CLASS_TIER = {
    "primary":       "main",
    "secondary":     "main",
    "tertiary":      "country",
    "unclassified":  "country",
    "residential":   "village",
    "living_street": "village",
    "service":       "village",
    "track":         "track",
}
CLASS_RANK = {cls: i for i, cls in enumerate(CLASS_WIDTH)}

# svg styling per class, fill colour and width, casing is derived
SVG_ROAD_STYLE = {
    "primary":       ("#ffffff", 6.0),
    "secondary":     ("#ffd166", 4.6),
    "tertiary":      ("#e8d8b0", 2.8),
    "unclassified":  ("#c9c2a6", 2.0),
    "residential":   ("#aab4ae", 1.4),
    "living_street": ("#aab4ae", 1.4),
    "service":       ("#8d978f", 1.0),
    "track":         ("#7f8a7a", 1.0),
}

ID_BASE          = 9004200000000000000
ID_ROUTE_STRIDE  = 1000
MAX_POINTS       = ID_ROUTE_STRIDE - 1

ROADS_PARENT_NAME = "roads"

# fallback component templates, used when the world has no road to copy them from
DEFAULT_PHYSICS = (
    '<physics mass="1" friction="0.400000006" friction_rolling="0.400000006" restitution="0.200000003" '
    'is_static="true" is_kinematic="false" position_lock_x="0" position_lock_y="0" position_lock_z="0" '
    'rotation_lock_x="0" rotation_lock_y="0" rotation_lock_z="0" center_of_mass_x="0" center_of_mass_y="0" '
    'center_of_mass_z="0" body_type="4" cloth_stiffness="0.899999976" cloth_damping="0.00999999978" '
    'cloth_iterations="8" cloth_wind_enabled="true" cloth_pin_direction_x="0" cloth_pin_direction_y="1" '
    'cloth_pin_direction_z="0" />'
)
DEFAULT_RENDER = (
    '<render mesh_name="" mesh_path="" sub_mesh_index="0" material_name="road" '
    'material_path="./project/plan_resources/road.xml" material_default="false" flags="1" '
    'max_render_distance="3.40282347e+38" max_shadow_distance="3.40282347e+38" />'
)
DEFAULT_SPLINE = (
    '<spline closed_loop="false" resolution="8" road_width="8" curve_alpha="0.5" mesh_enabled="true" '
    'has_road_mesh="true" profile="0" height="3" thickness="0.300000012" tube_sides="12" road_width_end="8" '
    'uv_tiling_u="1" uv_tiling_v="1" sidewalk_enabled="false" sidewalk_width="2" curb_height="0.150000006" '
    'conform_to_terrain="true" terrain_offset="0.25" grade_limit_enabled="true" max_grade_degrees="8" '
    'max_cut="20" grade_smoothing="0.899999976" smoothing_length="160" embankment_enabled="true" '
    'embankment_slope_degrees="50" embankment_max_height="8" carve_terrain="true" carve_bed_drop="0.150000006" '
    'carve_fill_slope_degrees="33" carve_cut_slope_degrees="45" carve_max_shoulder="60" instance_spacing="5" '
    'align_instances="true" instance_mesh_path="" instance_template_id="0" instance_lateral_offset="0" '
    'instance_mirror="false" instance_face_inward="false" instance_random_offset="0" '
    'instance_random_scale_min="1" instance_random_scale_max="1" instance_random_yaw="0" source_spline_id="0" '
    'attach_mode="0" attach_lateral_offset="0" attach_vertical_offset="0" attach_inherit_closed_loop="true" '
    'attach_sample_count="0" />'
)


def log(message):
    print(message, flush=True)


# ---------------------------------------------------------------------------------------------
# fetching
# ---------------------------------------------------------------------------------------------

def overpass_fetch(query, cache_name, refresh):
    os.makedirs(CACHE_DIR, exist_ok=True)
    cache_path = os.path.join(CACHE_DIR, cache_name)
    if os.path.exists(cache_path) and not refresh:
        with open(cache_path, "r", encoding="utf-8") as f:
            return json.load(f)

    data    = urllib.parse.urlencode({"data": query}).encode()
    headers = {"User-Agent": "spartan-engine-map/1.0 (osm_roads.py)"}
    last_error = None
    for url in OVERPASS_URLS:
        for attempt in range(2):
            try:
                log(f"  fetching {url} (attempt {attempt + 1})")
                request = urllib.request.Request(url, data=data, headers=headers)
                with urllib.request.urlopen(request, timeout=240) as response:
                    payload = response.read()
                result = json.loads(payload)
                if "elements" not in result:
                    raise ValueError("overpass reply has no elements")
                with open(cache_path, "wb") as f:
                    f.write(payload)
                return result
            except (urllib.error.URLError, ValueError, TimeoutError) as error:
                last_error = error
                log(f"  failed: {error}")
                time.sleep(3)

    if os.path.exists(cache_path):
        log("  all endpoints failed, falling back to the stale cache")
        with open(cache_path, "r", encoding="utf-8") as f:
            return json.load(f)

    raise SystemExit(f"could not fetch overpass data: {last_error}")


def fetch_roads(classes, refresh):
    # link ramps belong to their parent class so junction approaches are not lost
    pattern = "|".join(sorted(set(classes) | {f"{c}_link" for c in classes}))
    query = (
        "[out:json][timeout:240];\n"
        f'way["highway"~"^({pattern})$"](area:{3600000000 + ISLAND_RELATION});\n'
        "out body geom;\n"
    )
    digest = hashlib.sha1(pattern.encode()).hexdigest()[:10]
    return overpass_fetch(query, f"zakynthos_roads_{digest}.json", refresh)


def fetch_island(refresh):
    query = f"[out:json][timeout:240];\nrelation({ISLAND_RELATION});\nout geom;\n"
    return overpass_fetch(query, "zakynthos_island.json", refresh)


# ---------------------------------------------------------------------------------------------
# projection and terrain
# ---------------------------------------------------------------------------------------------

class Projection:
    def __init__(self, crs):
        self.ax, self.bx = crs["lonlat_to_pixel"]["px = ax*lon + bx"]
        self.az, self.bz = crs["lonlat_to_pixel"]["py = az*lat + bz"]
        hm               = crs["heightmap"]
        self.width_px    = int(hm["width_px"])
        self.height_px   = int(hm["height_px"])
        self.meters      = float(hm["meters_per_px"])
        self.min_y       = float(hm["min_y"])
        self.max_y       = float(hm["max_y"])
        self.terrain_y   = float(hm.get("terrain_entity_y", 0.0))
        self.half_x      = (self.width_px - 1) * self.meters / 2.0
        self.half_z      = (self.height_px - 1) * self.meters / 2.0
        self.heights     = None

    def lonlat_to_world(self, lon, lat):
        px = self.ax * lon + self.bx
        py = self.az * lat + self.bz
        x  = px * self.meters - self.half_x
        z  = ((self.height_px - 1) - py) * self.meters - self.half_z
        return x, z

    def world_to_pixel(self, x, z):
        px = (x + self.half_x) / self.meters
        py = (self.height_px - 1) - (z + self.half_z) / self.meters
        return px, py

    def load_heightmap(self, path):
        image = Image.open(path)
        data  = np.asarray(image)
        if data.ndim == 3:
            data = data[..., 0]
        if data.shape[1] != self.width_px or data.shape[0] != self.height_px:
            log(f"  warning: heightmap is {data.shape[1]}x{data.shape[0]}, atlas says {self.width_px}x{self.height_px}")
        full_scale   = 65535.0 if data.dtype == np.uint16 else 255.0
        self.heights = data.astype(np.float64) / full_scale

    def sample_height(self, x, z):
        # bilinear, the engine flips image rows so row 0 is the north edge
        h  = self.heights
        px, py = self.world_to_pixel(x, z)
        px = min(max(px, 0.0), h.shape[1] - 1.0)
        py = min(max(py, 0.0), h.shape[0] - 1.0)
        x0 = int(math.floor(px))
        y0 = int(math.floor(py))
        x1 = min(x0 + 1, h.shape[1] - 1)
        y1 = min(y0 + 1, h.shape[0] - 1)
        fx = px - x0
        fy = py - y0
        v  = (h[y0, x0] * (1 - fx) * (1 - fy) + h[y0, x1] * fx * (1 - fy)
              + h[y1, x0] * (1 - fx) * fy + h[y1, x1] * fx * fy)
        return self.terrain_y + self.min_y + v * (self.max_y - self.min_y)


# ---------------------------------------------------------------------------------------------
# graph
# ---------------------------------------------------------------------------------------------

def normalize_class(highway):
    if highway.endswith("_link"):
        highway = highway[: -len("_link")]
    return highway


GREEK_DIGRAPHS = (
    ("ου", "ou"), ("αι", "ai"), ("ει", "ei"), ("οι", "oi"), ("υι", "yi"),
    ("αυ", "av"), ("ευ", "ev"), ("ηυ", "iv"), ("μπ", "mp"), ("ντ", "nt"),
    ("γγ", "ng"), ("γκ", "gk"), ("γχ", "nch"), ("γξ", "nx"),
)
GREEK_LETTERS = {
    "α": "a", "β": "v", "γ": "g", "δ": "d", "ε": "e", "ζ": "z", "η": "i", "θ": "th",
    "ι": "i", "κ": "k", "λ": "l", "μ": "m", "ν": "n", "ξ": "x", "ο": "o", "π": "p",
    "ρ": "r", "σ": "s", "ς": "s", "τ": "t", "υ": "y", "φ": "f", "χ": "ch", "ψ": "ps", "ω": "o",
}


def transliterate_greek(text):
    # strip accents, then map digraphs and letters, keeping the original capitalisation pattern
    stripped = "".join(c for c in unicodedata.normalize("NFD", text) if not unicodedata.combining(c))
    result = []
    i = 0
    while i < len(stripped):
        chunk = stripped[i:i + 2]
        lower = chunk.lower()
        replaced = None
        for greek, latin in GREEK_DIGRAPHS:
            if lower == greek:
                replaced = latin
                break
        if replaced is not None:
            result.append(replaced.capitalize() if chunk[0].isupper() else replaced)
            i += 2
            continue
        c = stripped[i]
        latin = GREEK_LETTERS.get(c.lower())
        if latin is None:
            result.append(c)
        else:
            result.append(latin.capitalize() if c.isupper() else latin)
        i += 1
    return "".join(result)


def pick_name(tags):
    for key in ("name:en", "int_name", "name"):
        value = tags.get(key, "").strip()
        if value:
            return transliterate_greek(value)
    ref = tags.get("ref", "").strip()
    if ref:
        return transliterate_greek(ref)
    return ""


def slugify(text, fallback):
    slug = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    slug = slug[:40].strip("_")
    return slug or fallback


def polyline_length(points):
    total = 0.0
    for (x0, z0), (x1, z1) in zip(points, points[1:]):
        total += math.hypot(x1 - x0, z1 - z0)
    return total


class Segment:
    __slots__ = ("nodes", "cls", "name", "length", "links")

    def __init__(self, nodes, cls, name):
        self.nodes  = nodes
        self.cls    = cls
        self.name   = name
        self.length = 0.0
        # index of the segment continuing past each end, or None
        self.links  = [None, None]


def build_segments(elements, node_world):
    # every way is cut at junction nodes, degree counts a mid way node twice
    ways   = []
    degree = defaultdict(int)
    for element in elements:
        if element.get("type") != "way" or "nodes" not in element:
            continue
        nodes = element["nodes"]
        if len(nodes) < 2:
            continue
        for i, node in enumerate(nodes):
            degree[node] += 1 if i in (0, len(nodes) - 1) else 2
        ways.append(element)

    junctions = {node for node, count in degree.items() if count >= 3}

    segments = []
    for way in ways:
        tags  = way.get("tags", {})
        cls   = normalize_class(tags.get("highway", ""))
        if cls not in CLASS_WIDTH:
            continue
        name  = pick_name(tags)
        nodes = way["nodes"]
        current = [nodes[0]]
        for node in nodes[1:]:
            if node == current[-1]:
                continue
            current.append(node)
            if node in junctions:
                segments.append(Segment(current, cls, name))
                current = [node]
        if len(current) >= 2:
            segments.append(Segment(current, cls, name))

    for segment in segments:
        segment.length = polyline_length([node_world[n] for n in segment.nodes])

    return segments, junctions


def end_heading(segment, end, node_world):
    # direction leaving the segment through the given end
    nodes = segment.nodes
    if end == 0:
        a, b = node_world[nodes[1]], node_world[nodes[0]]
    else:
        a, b = node_world[nodes[-2]], node_world[nodes[-1]]
    return math.atan2(b[1] - a[1], b[0] - a[0])


def link_segments(segments, node_world):
    # ends meeting at a node, grouped by class rank, the straightest pair continues
    ends_at = defaultdict(list)
    for index, segment in enumerate(segments):
        ends_at[segment.nodes[0]].append((index, 0))
        ends_at[segment.nodes[-1]].append((index, 1))

    for node, ends in ends_at.items():
        by_class = defaultdict(list)
        for index, end in ends:
            by_class[segments[index].cls].append((index, end))

        for cls, group in by_class.items():
            free = list(group)
            while len(free) >= 2:
                best = None
                for i in range(len(free)):
                    for j in range(i + 1, len(free)):
                        a = free[i]
                        b = free[j]
                        if a[0] == b[0]:
                            continue
                        ha = end_heading(segments[a[0]], a[1], node_world)
                        hb = end_heading(segments[b[0]], b[1], node_world)
                        # headings leave the node in opposite directions when the road goes straight through
                        turn = abs(math.remainder(ha - hb - math.pi, math.tau))
                        if best is None or turn < best[0]:
                            best = (turn, a, b)
                if best is None or best[0] > math.radians(80.0):
                    break
                _, a, b = best
                segments[a[0]].links[a[1]] = b
                segments[b[0]].links[b[1]] = a
                free.remove(a)
                free.remove(b)


def chain_routes(segments):
    # walk linked segments into ordered node lists
    used   = [False] * len(segments)
    routes = []

    for start in range(len(segments)):
        if used[start]:
            continue

        # walk backwards to the chain start, stop on loops
        index, end = start, 0
        visited = {start}
        while True:
            link = segments[index].links[end]
            if link is None or link[0] in visited:
                break
            index = link[0]
            visited.add(index)
            # arrived through link[1], leave through the other end
            end = 1 - link[1]

        # now walk forward from that segment
        nodes   = []
        members = []
        forward = end == 0
        while True:
            used[index] = True
            segment = segments[index]
            members.append(segment)
            seq = segment.nodes if forward else list(reversed(segment.nodes))
            if nodes and nodes[-1] == seq[0]:
                seq = seq[1:]
            nodes.extend(seq)
            exit_end = 1 if forward else 0
            link = segments[index].links[exit_end]
            if link is None or used[link[0]]:
                break
            index   = link[0]
            forward = link[1] == 0

        names = defaultdict(float)
        for member in members:
            if member.name:
                names[member.name] += member.length
        name = max(names.items(), key=lambda item: item[1])[0] if names else ""
        routes.append({"nodes": nodes, "cls": members[0].cls, "name": name})

    return routes


# ---------------------------------------------------------------------------------------------
# geometry
# ---------------------------------------------------------------------------------------------

def dedupe(points):
    result = []
    for point in points:
        if not result or math.hypot(point[0] - result[-1][0], point[1] - result[-1][1]) > 0.05:
            result.append(point)
    return result


def douglas_peucker(points, tolerance):
    if len(points) < 3:
        return list(points)
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        first, last = stack.pop()
        ax, az = points[first]
        bx, bz = points[last]
        dx, dz = bx - ax, bz - az
        length_sq = dx * dx + dz * dz
        max_distance, max_index = -1.0, first
        for i in range(first + 1, last):
            px, pz = points[i]
            if length_sq > 1e-9:
                t = max(0.0, min(1.0, ((px - ax) * dx + (pz - az) * dz) / length_sq))
                distance = math.hypot(px - (ax + dx * t), pz - (az + dz * t))
            else:
                distance = math.hypot(px - ax, pz - az)
            if distance > max_distance:
                max_distance, max_index = distance, i
        if max_distance > tolerance:
            keep[max_index] = True
            stack.append((first, max_index))
            stack.append((max_index, last))
    return [p for p, k in zip(points, keep) if k]


def turn_angle(points, i):
    ax, az = points[i - 1]
    bx, bz = points[i]
    cx, cz = points[i + 1]
    h0 = math.atan2(bz - az, bx - ax)
    h1 = math.atan2(cz - bz, cx - bx)
    return abs(math.remainder(h1 - h0, math.tau))


def point_at_distance(points, cumulative, distance):
    hi = 1
    while hi < len(cumulative) - 1 and cumulative[hi] < distance:
        hi += 1
    lo = hi - 1
    span = cumulative[hi] - cumulative[lo]
    t = (distance - cumulative[lo]) / span if span > 1e-9 else 0.0
    t = max(0.0, min(1.0, t))
    return (points[lo][0] + (points[hi][0] - points[lo][0]) * t,
            points[lo][1] + (points[hi][1] - points[lo][1]) * t)


def resample(points, spacing, min_spacing, sharp_degrees):
    # sharp bends are kept as points, everything between them is spaced evenly
    if len(points) < 2:
        return list(points)
    sharp = math.radians(sharp_degrees)
    anchors = [0] + [i for i in range(1, len(points) - 1) if turn_angle(points, i) > sharp] + [len(points) - 1]

    result = [points[0]]
    for a, b in zip(anchors, anchors[1:]):
        piece = points[a:b + 1]
        cumulative = [0.0]
        for p, q in zip(piece, piece[1:]):
            cumulative.append(cumulative[-1] + math.hypot(q[0] - p[0], q[1] - p[1]))
        length = cumulative[-1]
        if length < 1e-6:
            continue
        spans = max(1, int(round(length / spacing)))
        if length / spans < min_spacing:
            spans = max(1, int(math.floor(length / min_spacing)))
        for k in range(1, spans + 1):
            result.append(point_at_distance(piece, cumulative, length * k / spans))
        result[-1] = piece[-1]
    return result


def split_at_sea(points, heights, water_level):
    # a single wet point between dry ones is a fit error, two in a row is the sea
    wet = [h < water_level for h in heights]
    for i in range(1, len(wet) - 1):
        if wet[i] and not wet[i - 1] and not wet[i + 1]:
            wet[i] = False

    pieces = []
    current = []
    for point, height, is_wet in zip(points, heights, wet):
        if is_wet:
            if len(current) >= 2:
                pieces.append(current)
            current = []
        else:
            current.append((point[0], height, point[1]))
    if len(current) >= 2:
        pieces.append(current)
    return pieces


def chunk_points(points, max_points):
    if len(points) <= max_points:
        return [points]
    chunks = []
    start = 0
    while start < len(points) - 1:
        end = min(start + max_points, len(points))
        chunks.append(points[start:end])
        start = end - 1
    return chunks


# ---------------------------------------------------------------------------------------------
# world file
# ---------------------------------------------------------------------------------------------

def fmt(value):
    text = f"{value:.1f}"
    if text == "-0.0":
        text = "0.0"
    return text.rstrip("0").rstrip(".") if text.endswith(".0") else text


def set_attribute(tag, name, value):
    pattern = re.compile(rf'\b{re.escape(name)}="[^"]*"')
    replacement = f'{name}="{value}"'
    if pattern.search(tag):
        return pattern.sub(replacement, tag, count=1)
    return tag.replace(" />", f" {replacement} />", 1)


def find_roads_block(lines):
    start = None
    for i, line in enumerate(lines):
        if re.match(rf'\s*<Entity name="{ROADS_PARENT_NAME}"', line):
            start = i
            break
    if start is None:
        return None, None, None

    indent = len(lines[start]) - len(lines[start].lstrip(" "))
    close_pattern = re.compile(rf'^ {{{indent}}}</Entity>\s*$')
    for j in range(start + 1, len(lines)):
        if close_pattern.match(lines[j]):
            return start, j, indent
    raise SystemExit("roads entity has no matching close tag")


def read_templates(lines, start, end):
    templates = {"physics": DEFAULT_PHYSICS, "render": DEFAULT_RENDER, "spline": DEFAULT_SPLINE}
    for line in lines[start:end]:
        stripped = line.strip()
        for key in templates:
            if stripped.startswith(f"<{key} ") and templates[key] in (DEFAULT_PHYSICS, DEFAULT_RENDER, DEFAULT_SPLINE):
                templates[key] = stripped
    return templates


def route_tags(route):
    tags = ["road", "map_road", route["tier"], route["osm_class"]]
    if route["name"]:
        tags.append(route["name"].upper().replace(",", " "))
    return ",".join(tags)


def emit_route_xml(route, index, templates, indent):
    pad_e = " " * indent
    pad_c = " " * (indent + 1)
    points = route["points_xyz"]
    ox, oy, oz = points[0]
    entity_id = ID_BASE + index * ID_ROUTE_STRIDE

    spline = templates["spline"]
    for name, value in (
        ("closed_loop", "false"),
        ("resolution", "8"),
        ("road_width", fmt(route["width"])),
        ("road_width_end", fmt(route["width"])),
        ("curve_alpha", "0.5"),
        ("mesh_enabled", "true"),
        ("has_road_mesh", "true"),
        ("conform_to_terrain", "true"),
        ("source_spline_id", "0"),
        ("attach_mode", "0"),
    ):
        spline = set_attribute(spline, name, value)

    out = [
        f'{pad_e}<Entity name="{xml_escape(route["id"])}" id="{entity_id}" active="true" '
        f'position="{fmt(ox)} {fmt(oy)} {fmt(oz)}" rotation="0 0 0 1" scale="1 1 1" '
        f'tags="{xml_escape(route_tags(route), {chr(34): "&quot;"})}">',
        f"{pad_c}{templates['physics']}",
        f"{pad_c}{templates['render']}",
        f"{pad_c}{spline}",
    ]
    for j, (x, y, z) in enumerate(points):
        out.append(
            f'{pad_c}<Entity name="spline_point_{j}" id="{entity_id + 1 + j}" active="true" '
            f'position="{fmt(x - ox)} {fmt(y - oy)} {fmt(z - oz)}" rotation="0 0 0 1" scale="1 1 1" />'
        )
    out.append(f"{pad_e}</Entity>")
    return out


def write_world(path, routes, dry_run):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(newline)

    start, end, indent = find_roads_block(lines)
    if start is None:
        raise SystemExit(f'no <Entity name="{ROADS_PARENT_NAME}"> in {path}, create an empty one first')

    templates = read_templates(lines, start + 1, end)
    body = []
    for index, route in enumerate(routes):
        body.extend(emit_route_xml(route, index, templates, indent + 1))

    new_lines = lines[: start + 1] + body + lines[end:]
    if dry_run:
        log(f"  dry run, would write {len(body)} lines into {path}")
        return

    backup = path + ".bak"
    shutil.copyfile(path, backup)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(newline.join(new_lines))
    log(f"  wrote {path} ({len(body)} lines replaced the roads block), backup at {backup}")


# ---------------------------------------------------------------------------------------------
# atlas json
# ---------------------------------------------------------------------------------------------

def nearest_route_point(routes, x, z):
    best = (float("inf"), None, None)
    for route in routes:
        for px, _, pz in route["points_xyz"]:
            distance = math.hypot(px - x, pz - z)
            if distance < best[0]:
                best = (distance, px, pz)
    return best


def write_atlas(path, atlas, routes, junction_points, classes, dry_run):
    atlas["routes"] = [
        {
            "id":        route["id"],
            "name":      route["name"],
            "tier":      route["tier"],
            "osm_class": route["osm_class"],
            "width":     route["width"],
            "length_m":  round(route["length_m"], 1),
            "points":    [[round(x, 1), round(z, 1)] for x, _, z in route["points_xyz"]],
        }
        for route in routes
    ]
    atlas["junctions"] = [{"x": round(x, 1), "z": round(z, 1)} for x, z in junction_points]

    km_by_tier  = defaultdict(float)
    km_by_class = defaultdict(float)
    for route in routes:
        km_by_tier[route["tier"]]       += route["length_m"] / 1000.0
        km_by_class[route["osm_class"]] += route["length_m"] / 1000.0
    atlas["stats"] = {
        "route_count":    len(routes),
        "km_by_tier":     {k: round(v, 1) for k, v in km_by_tier.items()},
        "km_by_class":    {k: round(v, 1) for k, v in km_by_class.items()},
        "pin_count":      len(atlas.get("pins", [])),
        "control_points": sum(len(route["points_xyz"]) for route in routes),
    }
    atlas["crs"]["source"] = (
        f"openstreetmap, island relation {ISLAND_RELATION}, roads {'/'.join(classes)} "
        f"via tools/map/osm_roads.py"
    )

    for pin in atlas.get("pins", []):
        distance, px, pz = nearest_route_point(routes, pin["x"], pin["z"])
        if px is not None:
            pin["road_x"]    = round(px, 1)
            pin["road_z"]    = round(pz, 1)
            pin["road_dist"] = round(distance, 1)

    if dry_run:
        log(f"  dry run, would write {path}")
        return
    with open(path, "w", encoding="utf-8") as f:
        json.dump(atlas, f, indent=1, ensure_ascii=False)
        f.write("\n")
    log(f"  wrote {path}")


# ---------------------------------------------------------------------------------------------
# svg map
# ---------------------------------------------------------------------------------------------

SVG_W, SVG_H = 3875, 3158
SVG_SCALE    = 0.1


def svg_x(x):
    return SVG_SCALE * x + 1937.5


def svg_y(z):
    return 1578.8 - SVG_SCALE * z


def svg_path(points_xz):
    parts = [f"{'M' if i == 0 else 'L'}{svg_x(x):.1f},{svg_y(z):.1f}" for i, (x, z) in enumerate(points_xz)]
    return " ".join(parts)


def island_rings(island_json, projection):
    # chain member ways of the relation into closed rings by matching end coordinates
    ways = []
    for element in island_json.get("elements", []):
        for member in element.get("members", []):
            if member.get("type") == "way" and member.get("geometry"):
                ways.append([(g["lon"], g["lat"]) for g in member["geometry"]])

    rings = []
    remaining = ways
    while remaining:
        ring = list(remaining.pop())
        changed = True
        while changed and ring[0] != ring[-1]:
            changed = False
            for i, way in enumerate(remaining):
                if way[0] == ring[-1]:
                    ring.extend(way[1:])
                elif way[-1] == ring[-1]:
                    ring.extend(list(reversed(way))[1:])
                elif way[-1] == ring[0]:
                    ring = way[:-1] + ring
                elif way[0] == ring[0]:
                    ring = list(reversed(way))[:-1] + ring
                else:
                    continue
                remaining.pop(i)
                changed = True
                break
        rings.append([projection.lonlat_to_world(lon, lat) for lon, lat in ring])
    return rings


def pin_marker(pin):
    x, y = svg_x(pin["x"]), svg_y(pin["z"])
    role = pin.get("role", "")
    if role in ("arrival", "hub", "city"):
        return f'<rect x="{x - 7:.1f}" y="{y - 7:.1f}" width="14" height="14" fill="#ff6b6b" stroke="#08141c" stroke-width="1.6"/>'
    if role == "service":
        return f'<rect x="{x - 5:.1f}" y="{y - 5:.1f}" width="10" height="10" fill="#ffd166" stroke="#08141c" stroke-width="1.6"/>'
    if role in ("race", "event"):
        return f'<polygon points="{x:.1f},{y - 8:.1f} {x + 7:.1f},{y + 5:.1f} {x - 7:.1f},{y + 5:.1f}" fill="#06d6a0" stroke="#08141c" stroke-width="1.6"/>'
    if role in ("view", "viewpoint", "photo"):
        return f'<circle cx="{x:.1f}" cy="{y:.1f}" r="5" fill="#a5d8ff" stroke="#08141c" stroke-width="1.6"/>'
    if role == "ferry":
        return f'<rect x="{x - 5:.1f}" y="{y - 5:.1f}" width="10" height="10" fill="#48cae4" stroke="#08141c" stroke-width="1.6"/>'
    return f'<circle cx="{x:.1f}" cy="{y:.1f}" r="5" fill="#ffffff" stroke="#08141c" stroke-width="1.6"/>'


def write_svg(path, atlas, routes, rings, dry_run):
    out = []
    out.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{SVG_W}" height="{SVG_H}" viewBox="0 0 {SVG_W} {SVG_H}" font-family="Segoe UI, Arial, sans-serif">'
    )
    out.append(
        "<defs><style>"
        ".lbl{paint-order:stroke;stroke:#08141c;stroke-width:3px;stroke-linejoin:round;fill:#fff;font-weight:600}"
        ".pl{paint-order:stroke;stroke:#08141c;stroke-width:2.2px;stroke-linejoin:round;fill:#cfd8dc;font-style:italic;font-size:11px}"
        ".rn{paint-order:stroke;stroke:#08141c;stroke-width:2.5px;fill:#ffe9a8;font-size:10.5px;letter-spacing:1px;font-weight:600}"
        ".rg{fill:#ffffff;fill-opacity:0.55;stroke:#08141c;stroke-opacity:0.6;stroke-width:1.2px;paint-order:stroke;font-size:40px;font-weight:800;letter-spacing:6px}"
        ".grid{stroke:#ffffff;stroke-opacity:0.07;stroke-width:1}"
        ".gt{fill:#ffffff;fill-opacity:0.35;font-size:11px}"
        "</style></defs>"
    )
    out.append(f'<rect width="{SVG_W}" height="{SVG_H}" fill="#0b2233"/>')

    # grid every 5 km
    for x in range(-20000, 20001, 5000):
        gx = svg_x(x)
        out.append(f'<line class="grid" x1="{gx:.1f}" y1="0" x2="{gx:.1f}" y2="{SVG_H}"/>')
        out.append(f'<text class="gt" x="{gx + 3:.1f}" y="{SVG_H - 6}">x {x}</text>')
    for z in range(-20000, 20001, 5000):
        gy = svg_y(z)
        out.append(f'<line class="grid" x1="0" y1="{gy:.1f}" x2="{SVG_W}" y2="{gy:.1f}"/>')
        out.append(f'<text class="gt" x="4" y="{gy - 3:.1f}">z {z}</text>')

    # coastline
    for ring in rings:
        out.append(f'<path d="{svg_path(ring)} Z" fill="#12362f" fill-opacity="0.55" stroke="#f4f1e8" stroke-opacity="0.75" stroke-width="1.6"/>')

    # roads, casings first so fills sit on top, minor classes first so main roads win
    ordered = sorted(routes, key=lambda r: -CLASS_RANK.get(r["osm_class"], 99))
    for route in ordered:
        _, width = SVG_ROAD_STYLE.get(route["osm_class"], ("#ffffff", 1.0))
        out.append(f'<path d="{svg_path([(x, z) for x, _, z in route["points_xyz"]])}" fill="none" stroke="#1a2a33" stroke-width="{width + 3.4:.1f}" stroke-linecap="round" stroke-linejoin="round"/>')
    for route in ordered:
        colour, width = SVG_ROAD_STYLE.get(route["osm_class"], ("#ffffff", 1.0))
        out.append(f'<path d="{svg_path([(x, z) for x, _, z in route["points_xyz"]])}" fill="none" stroke="{colour}" stroke-width="{width:.1f}" stroke-linecap="round" stroke-linejoin="round"/>')

    # road names along the longer routes
    labelled = set()
    for route in sorted(routes, key=lambda r: -r["length_m"]):
        if not route["name"] or route["length_m"] < 1500 or route["name"] in labelled:
            continue
        labelled.add(route["name"])
        pts = [(x, z) for x, _, z in route["points_xyz"]]
        # text reads left to right, so the path has to run that way
        if pts[-1][0] < pts[0][0]:
            pts = list(reversed(pts))
        out.append(f'<defs><path id="t_{route["id"]}" d="{svg_path(pts)}" fill="none"/></defs>')
        out.append(
            f'<text class="rn" dy="-6"><textPath href="#t_{route["id"]}" startOffset="50%" text-anchor="middle">'
            f'{xml_escape(route["name"].upper())}</textPath></text>'
        )

    # places
    for place in atlas.get("places", []):
        x, y = svg_x(place["x"]), svg_y(place["z"])
        out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="1.6" fill="#cfd8dc"/>')
        out.append(f'<text class="pl" x="{x + 4:.1f}" y="{y - 3:.1f}">{xml_escape(place["name"])}</text>')

    # pins
    for pin in atlas.get("pins", []):
        out.append(pin_marker(pin))
        out.append(f'<text class="lbl" x="{svg_x(pin["x"]) + 10:.1f}" y="{svg_y(pin["z"]) + 4:.1f}" font-size="13">{xml_escape(pin["name"])}</text>')

    # regions
    for region in atlas.get("regions", []):
        out.append(f'<text class="rg" x="{svg_x(region["x"]):.1f}" y="{svg_y(region["z"]):.1f}" text-anchor="middle">{xml_escape(region["name"])}</text>')

    # legend
    legend_x, legend_y = 24, SVG_H - 190
    out.append(f'<rect x="{legend_x - 10}" y="{legend_y - 24}" width="470" height="180" rx="8" fill="#08141c" fill-opacity="0.7"/>')
    out.append(f'<text x="{legend_x}" y="{legend_y}" fill="#fff" font-size="16" font-weight="700">ZAKYNTHOS ROAD ATLAS</text>')
    row = legend_y + 24
    for cls in [c for c in CLASS_WIDTH if any(r["osm_class"] == c for r in routes)]:
        colour, width = SVG_ROAD_STYLE[cls]
        out.append(f'<line x1="{legend_x}" y1="{row}" x2="{legend_x + 60}" y2="{row}" stroke="#1a2a33" stroke-width="{width + 3.4:.1f}"/>')
        out.append(f'<line x1="{legend_x}" y1="{row}" x2="{legend_x + 60}" y2="{row}" stroke="{colour}" stroke-width="{width:.1f}"/>')
        out.append(f'<text x="{legend_x + 72}" y="{row + 4}" fill="#fff" font-size="12">{cls} ({CLASS_WIDTH[cls]:.0f} m)</text>')
        row += 20
    stats = atlas.get("stats", {})
    out.append(
        f'<text x="{legend_x}" y="{SVG_H - 12}" fill="#9fb3c8" font-size="11">'
        f'{stats.get("route_count", len(routes))} routes, {len(atlas.get("junctions", []))} junctions, '
        f'{len(atlas.get("pins", []))} pins.  +x east, +z north, 1 unit = 1 m.  y from heightmap.</text>'
    )

    # scale bar and north arrow
    bar_x, bar_y = SVG_W - 560, SVG_H - 46
    for k in range(5):
        fill = "#fff" if k % 2 == 0 else "#08141c"
        out.append(f'<rect x="{bar_x + k * 100}" y="{bar_y}" width="100" height="8" fill="{fill}" stroke="#fff" stroke-width="1"/>')
    out.append(f'<text x="{bar_x}" y="{bar_y - 6}" fill="#fff" font-size="12">0</text>')
    out.append(f'<text x="{bar_x + 480}" y="{bar_y - 6}" fill="#fff" font-size="12">5 km</text>')
    out.append(f'<text x="{SVG_W - 40}" y="60" fill="#fff" font-size="28" text-anchor="middle">N</text>')
    out.append(f'<polygon points="{SVG_W - 40},14 {SVG_W - 32},36 {SVG_W - 48},36" fill="#fff"/>')
    out.append("</svg>")

    if dry_run:
        log(f"  dry run, would write {path}")
        return
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
        f.write("\n")
    log(f"  wrote {path}")


# ---------------------------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------------------------

def count_reversals(points_xz):
    count = 0
    for i in range(1, len(points_xz) - 1):
        if turn_angle(points_xz, i) > math.radians(120.0):
            count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description="stamp osm roads onto the zakynthos island world")
    parser.add_argument("--classes", default=",".join(DEFAULT_CLASSES), help="comma separated osm highway classes")
    parser.add_argument("--spacing", type=float, default=40.0, help="control point spacing in meters")
    parser.add_argument("--min-spacing", type=float, default=15.0, help="closest two control points may sit around bends")
    parser.add_argument("--sharp-degrees", type=float, default=15.0, help="bend angle that pins a control point")
    parser.add_argument("--simplify", type=float, default=2.0, help="douglas peucker tolerance in meters before resampling")
    parser.add_argument("--min-length", type=float, default=60.0, help="drop routes shorter than this")
    # pixel 0 of the 8 bit heightmap is open sea, pixel 1 is the coastal plain that the -5.9 terrain offset floods
    parser.add_argument("--water-level", type=float, default=-4.0, help="world y below which a point counts as sea")
    parser.add_argument("--refresh", action="store_true", help="ignore the overpass cache")
    parser.add_argument("--dry-run", action="store_true", help="report only, write nothing")
    parser.add_argument("--no-svg", action="store_true", help="skip the svg map")
    parser.add_argument("--world", default=WORLD_PATH)
    parser.add_argument("--atlas", default=ATLAS_PATH)
    parser.add_argument("--svg", default=SVG_PATH)
    parser.add_argument("--heightmap", default=HEIGHTMAP_PATH)
    args = parser.parse_args()

    classes = [c.strip() for c in args.classes.split(",") if c.strip()]
    unknown = [c for c in classes if c not in CLASS_WIDTH]
    if unknown:
        raise SystemExit(f"unknown classes {unknown}, known: {', '.join(CLASS_WIDTH)}")

    log("1/6 atlas and heightmap")
    with open(args.atlas, "r", encoding="utf-8") as f:
        atlas = json.load(f)
    projection = Projection(atlas["crs"])
    projection.load_heightmap(args.heightmap)

    log("2/6 openstreetmap data")
    roads_json  = fetch_roads(classes, args.refresh)
    island_json = fetch_island(args.refresh) if not args.no_svg else {"elements": []}
    log(f"  {len(roads_json.get('elements', []))} ways")

    log("3/6 graph")
    node_world = {}
    for element in roads_json.get("elements", []):
        if element.get("type") == "way" and "nodes" in element and "geometry" in element:
            for node, geometry in zip(element["nodes"], element["geometry"]):
                node_world[node] = projection.lonlat_to_world(geometry["lon"], geometry["lat"])

    segments, junction_nodes = build_segments(roads_json.get("elements", []), node_world)
    link_segments(segments, node_world)
    raw_routes = chain_routes(segments)
    log(f"  {len(segments)} segments, {len(junction_nodes)} junctions, {len(raw_routes)} chained routes")

    log("4/6 geometry")
    routes = []
    clipped = 0
    dropped_short = 0
    for raw in raw_routes:
        points = dedupe([node_world[n] for n in raw["nodes"]])
        if len(points) < 2:
            continue
        points = douglas_peucker(points, args.simplify)
        points = resample(points, args.spacing, args.min_spacing, args.sharp_degrees)
        heights = [projection.sample_height(x, z) for x, z in points]
        pieces = split_at_sea(points, heights, args.water_level)
        if len(pieces) != 1 or len(pieces[0]) != len(points):
            clipped += 1
        for piece in pieces:
            for chunk in chunk_points(piece, MAX_POINTS):
                length = polyline_length([(x, z) for x, _, z in chunk])
                if length < args.min_length:
                    dropped_short += 1
                    continue
                routes.append({
                    "name":       raw["name"],
                    "osm_class":  raw["cls"],
                    "tier":       CLASS_TIER[raw["cls"]],
                    "width":      CLASS_WIDTH[raw["cls"]],
                    "length_m":   length,
                    "points_xyz": chunk,
                })

    # main roads first, longest first, so the ids read like a road index
    routes.sort(key=lambda r: (CLASS_RANK[r["osm_class"]], -r["length_m"]))
    for index, route in enumerate(routes):
        route["id"] = f"r{index:03d}_{slugify(route['name'], route['osm_class'])}"

    junction_points = [node_world[n] for n in junction_nodes if n in node_world]
    reversals = sum(count_reversals([(x, z) for x, _, z in r["points_xyz"]]) for r in routes)
    total_points = sum(len(r["points_xyz"]) for r in routes)
    total_km = sum(r["length_m"] for r in routes) / 1000.0

    log(f"  {len(routes)} routes, {total_points} control points, {total_km:.1f} km")
    log(f"  {clipped} routes touched the sea and were clipped, {dropped_short} pieces under {args.min_length:.0f} m dropped")
    log(f"  reversals sharper than 120 degrees: {reversals}")
    by_class = defaultdict(lambda: [0, 0.0])
    for route in routes:
        by_class[route["osm_class"]][0] += 1
        by_class[route["osm_class"]][1] += route["length_m"] / 1000.0
    for cls in CLASS_WIDTH:
        if cls in by_class:
            log(f"    {cls:<14} {by_class[cls][0]:4d} routes  {by_class[cls][1]:7.1f} km")

    log("5/6 world")
    write_world(args.world, routes, args.dry_run)

    log("6/6 atlas")
    write_atlas(args.atlas, atlas, routes, junction_points, classes, args.dry_run)
    if not args.no_svg:
        rings = island_rings(island_json, projection)
        write_svg(args.svg, atlas, routes, rings, args.dry_run)

    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
