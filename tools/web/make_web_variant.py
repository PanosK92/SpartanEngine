# Copyright(c) 2015-2026 Panos Karabelas
# Licensed under the Spartan Engine License. See license.md in the repository root.
# https://github.com/PanosK92/SpartanEngine/blob/master/license.md
# Commercial use requires written permission and negotiated payment terms.

# writes the browser variant of a project asset, name.ext -> name.web.ext, which tools/web_build.py packages in place of the original
#   blender --background --python tools/web/make_web_variant.py -- <model.obj> <triangle ratio>   decimates a model
#   blender --background --python tools/web/make_web_variant.py -- <image> <max size>             downscales an image
# the browser resamples material textures to 1024 at most, so 1024 loses nothing there

import sys
from pathlib import Path

import bpy

args       = sys.argv[sys.argv.index("--") + 1:]
source     = Path(args[0]).resolve()
amount     = float(args[1])
target     = source.with_name(source.stem + ".web" + source.suffix)
is_model   = source.suffix.lower() == ".obj"

if is_model:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.wm.obj_import(filepath=str(source))
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        before = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        modifier = obj.modifiers.new("decimate", "DECIMATE")
        modifier.ratio = amount
        modifier.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        after = len(obj.data.polygons)
        print(f"{obj.name}: {before} -> {after} triangles")
    bpy.ops.wm.obj_export(filepath=str(target), export_materials=False, export_uv=True, export_normals=True, export_selected_objects=False)
else:
    image = bpy.data.images.load(str(source))
    width, height = image.size
    scale = min(1.0, amount / max(width, height))
    image.scale(max(1, round(width * scale)), max(1, round(height * scale)))
    image.filepath_raw = str(target)
    image.file_format = "JPEG" if source.suffix.lower() in (".jpg", ".jpeg") else "PNG"
    image.save()
    print(f"{source.name}: {width}x{height} -> {image.size[0]}x{image.size[1]}")

print(f"wrote {target}")
