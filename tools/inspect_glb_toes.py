import json
import math
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "project/models/mannequiny/mannequiny.glb"

with open(path, "rb") as f:
    data = f.read()

magic, version, length = struct.unpack_from("<III", data, 0)
assert magic == 0x46546C67, "not a glb"

offset = 12
gltf = None
bin_chunk = None
while offset < length:
    chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
    body = data[offset + 8: offset + 8 + chunk_len]
    if chunk_type == 0x4E4F534A:
        gltf = json.loads(body.decode("utf-8"))
    elif chunk_type == 0x004E4942:
        bin_chunk = body
    offset += 8 + chunk_len + ((4 - chunk_len % 4) % 4)

nodes = gltf["nodes"]
accessors = gltf["accessors"]
views = gltf["bufferViews"]

COMP = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NUM = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def read_accessor(index):
    acc = accessors[index]
    fmt, size = COMP[acc["componentType"]]
    n = NUM[acc["type"]]
    view = views[acc["bufferView"]]
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or (size * n)
    out = []
    for i in range(acc["count"]):
        vals = struct.unpack_from("<" + fmt * n, bin_chunk, base + i * stride)
        out.append(vals if n > 1 else vals[0])
    return out


print("=== node names matching foot/ball/toe ===")
for i, node in enumerate(nodes):
    name = node.get("name", "")
    if any(k in name.lower() for k in ("foot", "ball", "toe", "calf", "thigh")):
        print(f"  [{i:3}] {name}")

print()
for anim in gltf.get("animations", []):
    print(f"=== animation '{anim.get('name')}' ===")
    per_node = {}
    for ch in anim["channels"]:
        t = ch["target"]
        node_i = t.get("node")
        if node_i is None:
            continue
        name = nodes[node_i].get("name", f"node{node_i}")
        per_node.setdefault(name, []).append((t["path"], ch["sampler"]))

    times_by_count = {}
    for name, chans in sorted(per_node.items()):
        for path_name, sampler_i in chans:
            s = anim["samplers"][sampler_i]
            times = read_accessor(s["input"])
            times_by_count.setdefault((len(times), round(times[0], 4), round(times[-1], 4)), []).append(
                f"{name}.{path_name}")

    print("  channel time-range buckets (count, t_start, t_end):")
    for key, members in sorted(times_by_count.items()):
        print(f"    {key}  x{len(members)}")
        interesting = [m for m in members if any(k in m.lower() for k in ("ball", "toe", "foot"))]
        if interesting:
            print(f"       includes: {interesting}")

    print("  ball/toe rotation curves:")
    for name, chans in sorted(per_node.items()):
        if not any(k in name.lower() for k in ("ball", "toe")):
            continue
        for path_name, sampler_i in chans:
            if path_name != "rotation":
                continue
            s = anim["samplers"][sampler_i]
            times = read_accessor(s["input"])
            vals = read_accessor(s["output"])
            angles = []
            for q in vals:
                w = max(-1.0, min(1.0, q[3]))
                angles.append(math.degrees(2.0 * math.acos(abs(w))))
            span = max(angles) - min(angles)
            print(f"    {name}: interp={s.get('interpolation','LINEAR')} keys={len(times)} "
                  f"t=[{times[0]:.3f},{times[-1]:.3f}] angle=[{min(angles):.1f},{max(angles):.1f}] span={span:.1f}deg")
            step = max(1, len(angles) // 12)
            print("      angle samples: " + " ".join(f"{a:.1f}" for a in angles[::step]))
