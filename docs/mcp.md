# MCP bridge

Spartan can be driven from outside while it runs: AI agents, scripts and tools load worlds, create and edit entities, take screenshots, drive cars and read the profiler. Two pieces make this work:

1. A **TCP command bridge inside the engine** ([`source/mcp`](../source/mcp)).
2. A **Node MCP server** ([`tools/mcp/spartan_engine`](../tools/mcp/spartan_engine)) that exposes the bridge as Model Context Protocol tools for Cursor, Claude and other MCP clients. Its [README](../tools/mcp/spartan_engine/README.md) is the full reference.

## Starting the bridge

```powershell
cd binaries
.\spartan_vulkan_development.exe --mcp-control --mcp-port=47777 --no-steam
```

| Flag | Effect |
| ---- | ------ |
| `--mcp-control` | Listen on `127.0.0.1` |
| `--mcp-port=<n>` | Port, default `47777`. Use one port per instance when several agents work at once |
| `--mcp-hidden` or `--headless` | No visible window or editor UI |

The bridge is never compiled into a packaged game.

## Connecting an MCP client

```json
{
  "mcpServers": {
    "spartan_engine": {
      "type": "stdio",
      "command": "node",
      "args": ["<repo>/tools/mcp/spartan_engine/server.mjs", "--port=47777"],
      "cwd": "<repo>/tools/mcp/spartan_engine"
    }
  }
}
```

Run `npm install` in `tools/mcp/spartan_engine` once. Server options: `--host`, `--port`, `--timeout-ms`, and `--read-only` (or `SPARTAN_MCP_READ_ONLY=1`) to block mutations.

### Tool families

| Family | Examples |
| ------ | -------- |
| Session | `ping`, `engine_status`, `context_snapshot`, `engine_ensure`, `engine_wait`, `engine_set_mode` |
| Worlds | `world_load`, `world_save`, `world_summary`, `world_raycast`, `world_set_environment` |
| Entities and components | `entity_find`, `entity_resolve`, `entity_create_primitive`, `entity_set_transform_batch`, `component_get`, `component_set` |
| Content | `mesh_generate`, `compound_create`, `material_textured_create`, `texture_generate`, `entity_create_light`, `prefab_save`, `prefab_load` |
| Roads and layout | `spline_create_road`, `spline_connect`, `spline_junction`, `district_blockout`, `city_blockout` |
| Seeing | `camera_set_view`, `screenshot_take`, `screenshot_compare`, `viewpoint_save`, `viewpoint_capture`, `scene_visual_review` |
| Vehicles | `vehicle_list`, `vehicle_enter`, `vehicle_set_input`, `vehicle_telemetry`, `vehicle_validate`, `vehicle_dyno` |
| Cinematics | `sequencer_get`, `sequencer_set`, `sequencer_playback`, `sequencer_render` |
| Diagnostics | `console_read`, `profiler_snapshot`, `gpu_memory_snapshot`, `cvar_get`, `cvar_set`, `shader_reload` |
| Scripting | `execute_lua` (edit mode only), and the `visual_script_*` bridge commands through `engine_command` to build [visual scripts](visual_scripting.md#using-it-from-tools) |
| Headless | `headless_start`, `headless_stop`, `headless_smoke_test` run a private hidden instance |
| Escape hatch | `engine_command` sends any bridge command by name |

`search_capabilities` finds the right tool for a task. Durable lessons for agents are kept in [`AGENT_MEMORY.md`](../tools/mcp/spartan_engine/AGENT_MEMORY.md) (`agent_memory_read`, `agent_memory_append`).

### Working rules for agents

- Mutations require edit mode (`engine_set_mode`); camera moves also work while paused.
- Look before and after every change: frame the area with `camera_set_view` and capture with `screenshot_take`.
- Do not hand-edit `.world` files; use the bridge, then `world_save` only when the change should be kept.
- Never close an engine instance you did not start; another agent may be using it.

## Protocol

One request per line, one JSON response per line:

```
world_load path=C%3A%2Fspartan%2Fworlds%2Fempty.world request_id=7
{"ok":true,...,"request_id":"7"}
```

Arguments are `key=value` pairs, URL-encoded. Requests are queued and executed on the engine's main thread between frames ([`McpQueue`](../source/mcp/McpQueue.h)), so handlers can touch the world and renderer directly. Errors return `{"ok":false,"error":"..."}`.

## Adding a command

Handlers are `std::string handler(const McpRequest& request)` returning JSON.

- **Engine commands**: add a function to [`McpCommands.cpp`](../source/mcp/McpCommands.cpp) and an entry in the table in `ExecuteMcpCommand`, or, for a separate module, call `RegisterMcpCommand("name", handler)` from a `Register()` function like `mcp_diagnostics::Register()` in [`McpCommandsDiagnostics.cpp`](../source/mcp/McpCommandsDiagnostics.cpp).
- **Editor commands** (panels, asset viewer, sequencer, game build): use `editor_mcp::add("name", handler)` in [`source/editor/mcp`](../source/editor/mcp), since the engine must not depend on the editor.

A new command is callable immediately through `engine_command`. To give it a dedicated tool with a schema, register it in [`server.mjs`](../tools/mcp/spartan_engine/server.mjs) and restart the MCP server.
