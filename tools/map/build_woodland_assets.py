"""Build game-ready saplings, fallen trunks and branches from Poly Haven CC0 assets.

Run with Blender 4.2+ in background factory-startup mode:
    blender --background --factory-startup --python tools/map/build_woodland_assets.py

Downloads are cached and checksum-verified. Outputs live with the other external
project assets in binaries/project/models/island_biomes; no world is overwritten.
"""
import hashlib
import json
from pathlib import Path
import urllib.request

import bpy

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "binaries/project/models/island_biomes"


def download(entry, destination):
    if destination.exists():
        if hashlib.md5(destination.read_bytes()).hexdigest() == entry["md5"]:
            return
    request = urllib.request.Request(entry["url"], headers={"User-Agent": "SpartanEngine/1.0"})
    with urllib.request.urlopen(request, timeout=120) as response:
        data = response.read()
    if hashlib.md5(data).hexdigest() != entry["md5"]:
        raise ValueError(f"Checksum mismatch: {entry['url']}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)


def build(asset, prefix, budget, expected):
    source = OUT / "sources" / asset
    request = urllib.request.Request(f"https://api.polyhaven.com/files/{asset}",
                                     headers={"User-Agent": "SpartanEngine/1.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        entry = json.load(response)["gltf"]["2k"]["gltf"]
    source_gltf = source / f"{asset}.gltf"
    download(entry, source_gltf)
    for relative, dependency in entry["include"].items():
        target = (source / relative).resolve()
        if not target.is_relative_to(source.resolve()):
            raise ValueError(f"Invalid dependency path: {relative}")
        download(dependency, target)

    # Import into an isolated background process, never alter an artist's scene.
    bpy.ops.object.select_all(action="DESELECT")
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(source_gltf))
    plants = sorted((o for o in set(bpy.data.objects) - before if o.type == "MESH"),
                    key=lambda o: o.name)
    if len(plants) != expected:
        raise ValueError(f"Expected {expected} separate variants for {asset}")
    report = []
    for index, plant in enumerate(plants, 1):
        bpy.ops.object.select_all(action="DESELECT")
        plant.select_set(True)
        bpy.context.view_layer.objects.active = plant
        # The source's side-by-side display offsets are not part of the plant.
        plant.location = (0, 0, 0)
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        minimum = min(v.co.z for v in plant.data.vertices)
        for vertex in plant.data.vertices:
            vertex.co.z -= minimum
        plant.data.calc_loop_triangles()
        triangles = len(plant.data.loop_triangles)
        # Keep a detailed near mesh; the engine builds its screen-size LOD chain.
        modifier = plant.modifiers.new("Woodland triangle budget", "DECIMATE")
        modifier.ratio = min(1.0, budget / triangles)
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        for slot in plant.material_slots:
            if slot.material and "twig" in slot.material.name:
                slot.material.name = "pine_sapling_foliage"
        name = f"{prefix}_{index:02d}"
        plant.name = name
        folder = OUT / name
        folder.mkdir(parents=True, exist_ok=True)
        bpy.ops.export_scene.gltf(filepath=str(folder / f"{name}.gltf"),
                                  export_format="GLTF_SEPARATE", use_selection=True,
                                  export_materials="EXPORT", export_image_format="AUTO")
        plant.data.calc_loop_triangles()
        report.append({"name": name, "triangles": len(plant.data.loop_triangles),
                       "height_m": float(plant.dimensions.z)})
    (source / "build.json").write_text(json.dumps({
        "source": f"https://polyhaven.com/a/{asset}", "license": "CC0-1.0",
        "download": entry, "variants": report}, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    build("pine_sapling_small", "pine_sapling", 24000, 3)
    build("dead_tree_trunk", "fallen_trunk", 6000, 1)
    build("dry_branches_medium_01", "fallen_branches", 3000, 3)
