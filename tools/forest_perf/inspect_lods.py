"""Inspect serialized v6 mesh LOD chains without loading an engine."""
import argparse
import json
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("meshes", nargs="+", type=Path)
args = parser.parse_args()
for path in args.meshes:
    data = path.read_bytes()
    version, = struct.unpack_from("<I", data)
    if version != 6:
        raise ValueError(f"{path}: expected mesh format 6, got {version}")
    submesh_count, = struct.unpack_from("<I", data, 16)
    offset = 20
    submeshes = []
    for submesh in range(submesh_count):
        count, = struct.unpack_from("<I", data, offset)
        offset += 4
        levels = []
        for lod in range(count):
            vertex_offset, vertices, index_offset, indices, meshlet_offset, meshlets = struct.unpack_from("<6I", data, offset)
            offset += 48
            if indices == 0 or indices % 3:
                raise ValueError(f"{path}: invalid index count in submesh {submesh}, LOD {lod}")
            if levels and indices >= levels[-1]["triangles"] * 3:
                raise ValueError(f"{path}: LOD {lod} does not reduce geometry")
            levels.append({"lod": lod, "vertices": vertices, "triangles": indices // 3, "meshlets": meshlets})
        submeshes.append({"submesh": submesh, "levels": levels})
    print(json.dumps({"mesh": str(path), "submeshes": submeshes}, indent=2))
