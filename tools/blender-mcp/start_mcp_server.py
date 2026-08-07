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
