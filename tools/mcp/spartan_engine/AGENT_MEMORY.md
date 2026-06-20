# Spartan Agent Memory

This file is shared memory for agents working on Spartan Engine. Keep it short, factual, and useful for future runs. Replace wrong notes instead of piling corrections on top of them.

## Engine Facts
- Engine MCP commands run through the C++ bridge on the engine main thread.
- Mutating scene tools require edit mode.
- `execute_lua` is the broad capability layer for procedural scene edits and is edit-mode guarded.
- `context_snapshot` is the fastest first read for engine status, world summary, and selection.
- `component_get` exposes friendly properties plus registered raw members for an entity component before calling `component_set`.
- `component_action` invokes deterministic component methods that are not simple property writes.
- `resource_list` and `material_get` expose cached resources and material scalar/texture state.

## Good Agent Strategies
- Start engine tasks with `spartan_status` or `context_snapshot`.
- Use `debug_log_read` after failures to inspect actual engine command inputs and outputs.
- Use `search_capabilities` and `get_capability_details` before guessing tool names.
- Resolve targets with `entity_resolve` before mutating named or selected entities.
- Use `entity_find_by_component` to locate all entities with a component type.
- Use `component_set_batch` for multiple property/member edits on one component.
- Use `component_action` before falling back to Lua for terrain, spline, particle, physics, audio, light, or camera actions.
- Use `selection_update`, `entity_clone`, `entity_move_index`, and prefab tools before using Lua for common editor hierarchy workflows.
- Use `material_set_property` and `material_set_texture` for material edits instead of custom Lua.
- Before deleting or rebuilding geometry that should preserve look, call `entity_render_materials` on the target and reuse material names.
- Use `entity_create_light` for generic point, spot, directional, and area lights, and calibrate intensity, range, and area size to the scene scale.
- Use `camera_snapshot` before interpreting camera-relative placement.
- Use `world_raycast` for ground or surface-relative placement when possible.
- Simple live scene edits should use deterministic tools or fail fast; complex blockouts can use the higher-level scene path.
- Missing deterministic capabilities should be logged immediately under Problem Reports.
- Simple entity deletes should resolve the target and call `entity_delete` directly, not fall through to Cursor fallback.
- Do not route delete plus rebuild prompts to `entity_delete`; preserve materials first, then rebuild through a complex scene path.
- Simple primitive creation, such as `create a physics cone`, should route directly to `entity_create_primitive`.
- User convention, `physics <primitive>` means dynamic non-static physics unless static, fixed, or immovable is explicitly requested.
- For repeated scene work, prefer `entity_create_primitive_batch` or one focused `execute_lua` script.
- For source questions, use `search_codebase`, then `read_source_file` for focused context.

## Gotchas
- World loading blocks many engine commands until loading completes.
- `component_set` supports friendly properties and registered raw component member names for all component types.
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
