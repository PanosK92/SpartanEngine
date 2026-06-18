# Spartan Engine MCP

Spartan exposes engine-aware tools to MCP clients through a Node adapter and the in-engine TCP bridge.

## Architecture

- `server.mjs` is the MCP server used by Cursor and other MCP clients over stdio.
- `assistant.mjs` is the local helper used by the in-editor Spartan AI widget.
- The engine bridge listens on `127.0.0.1:47777` and executes commands on the engine main thread.
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
- `entity_render_materials` before deleting/rebuilding existing geometry when materials should be preserved.
- `camera_snapshot` before camera-relative placement.
- `world_raycast` before ground or surface-relative placement.
- `entity_resolve` before mutating a named or selected entity.
- `component_get` before `component_set`.
- `search_codebase`, then `read_source_file`, for source questions.
- `entity_create_primitive_batch` or `execute_lua` for repeated scene work.
- `agent_memory_append` after a durable lesson, correction, recurring problem, or maintainer improvement idea.

Live scene edits should route through deterministic tools or generic operations. If no deterministic operation matches, fail fast and add the missing tool or operation instead of falling through to long Cursor fallback. Missing deterministic capabilities should be appended to `AGENT_MEMORY.md` under `Problem Reports` immediately.

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
