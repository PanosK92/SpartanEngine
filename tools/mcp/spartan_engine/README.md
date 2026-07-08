# Spartan Engine MCP

Spartan exposes engine-aware tools to MCP clients through a Node adapter and the in-engine TCP bridge.

## Architecture

- `server.mjs` is the MCP server used by Cursor and other MCP clients over stdio.
- `assistant.mjs` is the local helper used by the in-editor Spartan AI widget.
- The engine bridge listens on `127.0.0.1:47777` and executes commands on the engine main thread.
- Bridge commands include request ids so clients can correlate responses and debug logs.
- The editor assistant helper listens on `127.0.0.1:47778`.
- The optional development HTTP shim listens on `127.0.0.1:8765/mcp`.

## Cursor MCP Setup

Start Spartan with the MCP bridge enabled or open the MCP Assistant widget so it can start the bridge.

Example Cursor MCP entry:

```json
{
  "mcpServers": {
    "spartan_engine": {
      "type": "stdio",
      "command": "node",
      "args": [
        "C:/Users/panos/Desktop/spartan_engine/tools/mcp/spartan_engine/server.mjs"
      ],
      "cwd": "C:/Users/panos/Desktop/spartan_engine/tools/mcp/spartan_engine"
    }
  }
}
```

Use `--host=127.0.0.1`, `--port=47777`, or `--timeout-ms=30000` when overriding defaults.

## Read-Only Mode

Use read-only mode when an agent should inspect the engine and source without mutating the scene:

```json
{
  "mcpServers": {
    "spartan_engine": {
      "type": "stdio",
      "command": "node",
      "args": [
        "C:/Users/panos/Desktop/spartan_engine/tools/mcp/spartan_engine/server.mjs",
        "--read-only"
      ],
      "cwd": "C:/Users/panos/Desktop/spartan_engine/tools/mcp/spartan_engine"
    }
  }
}
```

Environment variables also work:

- `SPARTAN_MCP_READ_ONLY=1` for `server.mjs`.
- `SPARTAN_ASSISTANT_READ_ONLY=1` for `assistant.mjs`.

Mutating tools are not registered by `server.mjs` in read-only mode. The assistant fast path also refuses mutating engine tools.

## Recommended Agent Flow

Use these first:

- `agent_memory_read` to load durable project lessons and maintainer advice.
- `debug_log_read` after failures to inspect recent prompts, engine commands, arguments, durations, and outputs.
- `spartan_status` to check bridge, transport, read-only mode, and code index health.
- `search_capabilities` and `get_capability_details` to discover tools/resources without guessing.
- `context_snapshot` to read engine status, world summary, selection, and camera grounding in one call.
- `async_task_start`, `async_task_get`, and `async_task_list` for long-running tools that should not block the main MCP call.
- `undo_redo` for editor command-stack undo and redo.
- `entity_render_materials` before deleting/rebuilding existing geometry when materials should be preserved.
- `resource_list`, `material_get`, `material_set_property`, and `material_set_texture` for cached asset/material inspection and edits.
- `resource_load`, `resource_reload`, `resource_save`, `resource_remove`, and `material_create` for resource lifecycle work.
- `camera_set_view` and `viewport_frame` for editor camera positioning and framing.
- `sequencer_get`, `sequencer_set`, `sequencer_playback`, `sequencer_event_add`, `sequencer_event_update`, and `sequencer_event_remove` for the editor camera cut timeline, plus `sequencer_spline_add`, `sequencer_spline_update`, and `sequencer_spline_remove` for the spline follower track.
- `renderer_debug_get`, `renderer_debug_set`, and `physics_state` for debug overlays and physics inspection.
- `profiler_snapshot` for frame timing, fps, stutter flags, rhi counters, and per-pass CPU/GPU time blocks to find optimization targets.
- `selection_update`, `entity_clone`, and `entity_move_index` for editor selection and hierarchy operations.
- `prefab_types`, `prefab_save`, and `prefab_load` for prefab discovery and file prefab workflows.
- `screenshot_take` when visual confirmation is useful; it can return image content after the next rendered frame.
- `camera_snapshot` before camera-relative placement.
- `world_raycast` before ground or surface-relative placement.
- `entity_resolve` before mutating a named or selected entity.
- `entity_find_by_component` when targeting all entities with a component type.
- `component_get` before `component_set`; inspect `property_metadata` and `member_metadata` for ranges, units, enum values, side effects, and read-only reasons.
- `component_set_batch` for multiple edits on one component.
- `component_action` for supported component methods before using Lua.
- `search_codebase`, then `read_source_file`, for source questions.
- `entity_create_primitive_batch` for blockouts and repeated geometry; use `execute_lua` only for focused scripts with known bindings, never for API probing.
- `entity_set_transform_batch` for repositioning many entities in one call.
- `agent_memory_append` after a durable lesson, correction, recurring problem, or maintainer improvement idea.

Live scene edits should route through deterministic tools when one matches. If no deterministic operation matches, the assistant falls back to the Cursor agent, which can perform the edit through the engine MCP tools. Recurring gaps worth a dedicated fast path should be appended to `AGENT_MEMORY.md` under `Problem Reports`.

## Resources

The server exposes stable MCP resources:

- `spartan://engine/overview`
- `spartan://engine/edit-rules`
- `spartan://engine/component-schemas`
- `spartan://world/current`
- `spartan://console/recent`
- `spartan://source`
- `spartan://agent/memory`
- `spartan://agent/debug-log`

Use `read_source_file` for actual source file contents.

## Agent Memory

`AGENT_MEMORY.md` is a curated markdown memory file for future agent runs. The MCP server creates it if it is missing.

Use it for:

- Engine facts learned from previous runs.
- Good strategies that worked.
- Gotchas and corrections.
- Advice to future agents.
- Advice to maintainers about missing tools, architecture problems, or engine improvements.
- Problem reports when recurring friction is observed.

Do not use it as a chat transcript. Keep notes short, replace stale entries, and avoid duplicate bullets.

## Safety Rules

- Scene mutations require edit mode.
- `execute_lua` requires edit mode.
- Loading worlds blocks most commands until loading completes.
- World paths must be absolute `.world` paths.
- High-power or destructive tools return structured errors and partial receipts when possible.

## Troubleshooting

- If tools report `engine_timeout`, retry once, then reduce the operation size or use a batch/Lua path.
- If tools report `edit_mode_required`, switch the engine to edit mode before mutating.
- If the assistant cannot start, ensure Node.js is on `PATH` and run `npm install` in this directory.
- If the MCP port cannot bind, another Spartan instance or stale bridge may still be running.
- If source search looks stale, the index refreshes changed files on search; restart the MCP server for a full rebuild.
