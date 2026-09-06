# Copyright(c) 2015-2026 Panos Karabelas
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
# copies of the Software, and to permit persons to whom the Software is furnished
# to do so, subject to the following conditions :
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

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
