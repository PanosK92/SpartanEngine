import bpy
import os

addon_path = r"C:\Users\panos\Desktop\spartan_engine\tools\blender-mcp\addon.py"

print(f"installing blender mcp addon from {addon_path}")
bpy.ops.preferences.addon_install(filepath=addon_path, overwrite=True)

# module name follows the file name without extension
module_name = "addon"
try:
    bpy.ops.preferences.addon_enable(module=module_name)
    print(f"enabled addon module: {module_name}")
except Exception as e:
    print(f"enable as 'addon' failed: {e}")
    # try common renamed module paths
    for name in ("bl_ext.user_default.addon", "blender_mcp", "BlenderMCP"):
        try:
            bpy.ops.preferences.addon_enable(module=name)
            print(f"enabled addon module: {name}")
            break
        except Exception as e2:
            print(f"enable as '{name}' failed: {e2}")

bpy.ops.wm.save_userpref()
print("saved user preferences")
