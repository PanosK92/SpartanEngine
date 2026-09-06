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

# start blender mcp socket after the ui is ready
import bpy
import addon_utils


def start_server():
    if bpy.app.background:
        print("blender mcp: skip start in background mode")
        return None

    try:
        addon_utils.enable("addon", default_set=True, persistent=True)
    except Exception as e:
        print(f"blender mcp: enable failed: {e}")

    scene = bpy.context.scene
    if getattr(scene, "blendermcp_server_running", False):
        print("blender mcp: already running")
        return None

    try:
        bpy.ops.blendermcp.start_server()
        print("blender mcp: server started on localhost:9876")
    except Exception as e:
        print(f"blender mcp: start failed: {e}")

    return None


bpy.app.timers.register(start_server, first_interval=1.0)
