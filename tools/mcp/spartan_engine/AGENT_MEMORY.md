# Spartan Agent Memory

This file is shared memory for agents working on Spartan Engine. Keep it short, factual, and useful for future runs. Replace wrong notes instead of piling corrections on top of them.

## Engine Facts
- Engine MCP commands run through the C++ bridge on the engine main thread.
- Mutating scene tools require edit mode.
- `execute_lua` is the broad capability layer for procedural scene edits and is edit-mode guarded.
- `context_snapshot` is the fastest first read for engine status, world summary, and selection.
- `component_get` exposes editable properties for an entity component before calling `component_set`.

## Good Agent Strategies
- Start engine tasks with `spartan_status` or `context_snapshot`.
- Use `debug_log_read` after failures to inspect actual engine command inputs and outputs.
- Use `search_capabilities` and `get_capability_details` before guessing tool names.
- Resolve targets with `entity_resolve` before mutating named or selected entities.
- Use `camera_snapshot` before interpreting camera-relative placement.
- Use `world_raycast` for ground or surface-relative placement when possible.
- Live scene edits should use deterministic tools or fail fast, not Cursor fallback.
- Missing deterministic capabilities should be logged immediately under Problem Reports.
- Simple entity deletes should resolve the target and call `entity_delete` directly, not fall through to Cursor fallback.
- Simple primitive creation, such as `create a physics cone`, should route directly to `entity_create_primitive`.
- User convention, `physics <primitive>` means dynamic non-static physics unless static, fixed, or immovable is explicitly requested.
- For repeated scene work, prefer `entity_create_primitive_batch` or one focused `execute_lua` script.
- For source questions, use `search_codebase`, then `read_source_file` for focused context.

## Gotchas
- World loading blocks many engine commands until loading completes.
- `component_set` supports editable properties only for supported component types and known property names.
- Long Lua scripts run on the main thread, so they should do a bounded amount of work and return a short summary.
- Tool errors are advisory data for recovery, not transport failures.

## Verified Patterns
- A parent entity plus a single batch or Lua script is usually better than many individual entity tool calls.
- A small receipt after each meaningful engine action helps the editor assistant UI stay understandable.

## Corrections
- Add corrections here when a previous note turns out to be wrong or incomplete.

## Advice To Future Agents
- Treat this file as advice, not absolute truth.
- Update this file only when a durable lesson was learned.
- Prefer replacing stale bullets over appending duplicates.
- Keep entries concise and tied to observed behavior.

## Advice To Maintainers
- Add native engine tools when agents repeatedly need the same multi-step command sequence.
- Keep MCP schemas close to engine component metadata so tool descriptions do not drift.
- Consider viewport screenshot or render evidence tools so agents can verify visual scene edits.
- Consider request ids or framed messages for the C++ bridge if multiple clients become common.
- Consider async task reporting for long-running engine operations instead of blocking the main MCP request.

## Problem Reports
- Add specific recurring friction here, with the file/tool involved and why it matters.
- If `MCP_DEBUG.jsonl` is missing and assistant errors use old wording, check the running `node assistant.mjs` process start time; the editor helper may be stale and needs restart before source changes apply.
- The editor Restart button must kill untracked `node ... assistant.mjs` processes too; otherwise stale helpers can keep port 47778 and hide new debug/code changes.
- `Create a physics cylinder... 2 units above ground, 5 units in front of camera` should not require `world_raycast`; if raycast blocks or is missing, use camera XZ plus world Y height and reserve raycast for explicit surface snapping.
- `MCP_DEBUG.jsonl` showed `context_snapshot` succeeded, then immediate `entity_create_primitive` failed with `engine connection is not available`; do not close the engine TCP socket immediately between sequential assistant stages, use a short idle close window.
- Capability gap: Native MCP command `context_snapshot` timed out. Error: engine command context_snapshot timed out after 5000ms Prompt: "Create a physics cylinder and place it 2 units above the ground, and 5 units in front of the camera"
