# Spartan Agent Memory

This file is shared memory for agents working on Spartan Engine. Keep it short, factual, and useful for future runs. Replace wrong notes instead of piling corrections on top of them.

## Engine Facts
- Engine MCP commands run through the C++ bridge on the engine main thread.
- Bridge requests carry request ids that are echoed in engine responses and debug logs.
- `async_task_start`, `async_task_get`, and `async_task_list` provide pollable background MCP tool execution.
- Mutating scene tools require edit mode.
- `execute_lua` is available for focused procedural edits, but native batch tools are preferred for blockouts; exploratory Lua API probing has crashed the engine.
- Lua can sample splines via `entity:GetComponent(ComponentType.Spline)` with `GetPoint(t)`, `GetTangent(t)`, `GetLength()`, and can add cameras via `entity:AddComponent(ComponentType.Camera)`.
- `World.GetEntities()`, `World.GetEntitiesLights()`, and `entity:GetChildren()` return 1-based Lua tables; prefer `ForEachChild` for iteration.
- `context_snapshot` is the fastest first read for engine status, world summary, and selection.
- `component_get` exposes friendly properties, registered raw members, and metadata for ranges, units, enum values, side effects, recommended defaults, and read-only reasons.
- `component_action` invokes deterministic component methods that are not simple property writes.
- `resource_list` and `material_get` expose cached resources and material scalar/texture state.
- `resource_load`, `resource_reload`, `resource_save`, `resource_remove`, and `material_create` cover common resource lifecycle work.
- `world_save` prunes unreferenced files from the current world resources directory, and `world_resources_clean` returns an explicit cleanup receipt.
- `undo_redo` routes editor undo and redo through the command stack.
- `camera_set_view`, `viewport_frame`, `renderer_debug_get`, `renderer_debug_set`, and `physics_state` cover common viewport/debug inspection.
- Drivable cars in play mode: `vehicle_list`/`vehicle_get`, `vehicle_enter` (E + mcp pedal ownership), `vehicle_set_input`, `vehicle_shift`, `vehicle_set_view`, `vehicle_reset`, `vehicle_exit`, `vehicle_telemetry` (`car_telemetry.csv` in the working directory, not Excel).
- `screenshot_take` queues a renderer screenshot and can return the saved PNG as image content for visual inspection.
- The editor sequencer (camera cut timeline) is controlled with `sequencer_get`, `sequencer_set`, `sequencer_playback`, `sequencer_event_add`, `sequencer_event_update`, and `sequencer_event_remove`; `camera` accepts an entity id or name, events re-sort by time, and state auto-saves to `sequencer.xml` next to the loaded world.
- `spline_query` with no arguments auto-picks the followed spline and projects every camera in the world onto it, returning per camera `arc_distance` and `pass_time_seconds` (when the follower passes that camera); this is the whole camera cut calculation in one call, never sample the spline manually with Lua.
- `spline_distribute` with no arguments respaces every camera child of the spline entity evenly along it by arc length, keeping order, lateral offset, and framing; use it for any request like spread or place cameras evenly along the road, never compute placements yourself.
- `spline_distribute` takes optional `edge_offset` (signed meters beyond the road edge, positive = right of travel, tracks varying road width), `lateral_offset` (signed meters from centerline), and `height` (meters above the road); requests like move cameras to the side of the road are one call, use `edge_offset` 2 so they clear the asphalt regardless of road width.
- Spline followers move at constant world speed (progress is arc-length based), so `pass_time_seconds` from `spline_query` is exact.

## Good Agent Strategies
- Start engine tasks with `spartan_status` or `context_snapshot`.
- Use `debug_log_read` after failures to inspect actual engine command inputs and outputs.
- Use `search_capabilities` and `get_capability_details` before guessing tool names.
- Use `async_task_start` for long-running tools, then poll with `async_task_get`.
- Resolve targets with `entity_resolve` before mutating named or selected entities.
- Use `undo_redo` instead of keyboard shortcuts for editor command-stack undo or redo.
- Use `entity_find_by_component` to locate all entities with a component type.
- Inspect `component_get.property_metadata` and `component_get.member_metadata` before writing unfamiliar component fields.
- Use `component_set_batch` for multiple property/member edits on one component.
- Use `component_action` before falling back to Lua for terrain, spline, particle, physics, audio, light, or camera actions.
- Use `selection_update`, `entity_clone`, `entity_move_index`, and prefab tools before using Lua for common editor hierarchy workflows.
- Use `material_set_property` and `material_set_texture` for material edits instead of custom Lua.
- Use resource lifecycle tools for asset cache load/reload/save/remove and new material creation.
- Use `viewport_frame` and `camera_set_view` before manual camera transform scripts.
- Use `renderer_debug_set` and `physics_state` for visual debugging and vehicle/rigid body inspection.
- To drive and inspect a car: `engine_set_mode` play, `vehicle_enter`, hold `vehicle_set_input` for a stretch, then `vehicle_telemetry` and report anomalies. Agents cannot compile; stop at diagnosis unless the user asks for code changes.
- Use `screenshot_take` when visual verification matters; it waits briefly for the async save and returns the image when ready.
- Before deleting or rebuilding geometry that should preserve look, call `entity_render_materials` on the target and reuse material names.
- Use `entity_create_light` for every light; it fully initializes intensity, range, angle, area size, shadows, and distances. Never hand-roll lights with empty + add component + component_set.
- Light intensity is lux for directional and lumens otherwise. Visible blockout defaults: point/spot 8500, area 12000, directional 120000. Values like 25-100 are invisible.
- Use `lights_calibrate` to fix existing scene lights in one call; specialty car lights stay dim, blockout lights get lifted.
- For city massing: `city_blockout` / `district_blockout` (market, downtown, park, industrial, residential, parking, plaza, gas_station). Never hundreds of manual cubes.
- For city roads: scan `world_landmarks` and bounding boxes, invent an arterial that skirts large districts, spur to edges, `spline_junction`, then `spline_decorate`. Never triangle center-to-center through an airway. Never hand-build `spline_point_*` children.
- Use `camera_snapshot` before interpreting camera-relative placement.
- Use `world_raycast` for ground or surface-relative placement when possible.
- Simple live scene edits should use deterministic tools; anything unmatched falls back to the Cursor agent with the engine MCP tools.
- Scene construction prompts such as `build a level`, `make rooms`, `backrooms`, or `liminal space` are live scene edits, not source-code search requests.
- Recurring gaps worth a dedicated fast path should be logged under Advice To Maintainers.
- Simple entity deletes should resolve the target and call `entity_delete` directly, not fall through to Cursor fallback.
- Do not route delete plus rebuild prompts to `entity_delete`; preserve materials first, then rebuild through a complex scene path.
- Simple primitive creation, such as `create a physics cone`, should route directly to `entity_create_primitive`.
- User convention, `physics <primitive>` means dynamic non-static physics unless static, fixed, or immovable is explicitly requested.
- For repeated scene work, prefer `entity_create_primitive_batch` or one focused `execute_lua` script.
- For blockouts, resolve or create the parent first, then build with `entity_create_primitive_batch` and `entity_create_light`; do not probe Lua APIs.
- For repositioning many entities, use `entity_set_transform_batch` instead of one `entity_set_transform` call per entity.
- For source questions, use `search_codebase`, then `read_source_file` for focused context.

## Gotchas
- World loading blocks many engine commands until loading completes.
- `component_set` supports friendly properties and registered raw component member names for all component types; metadata is advisory and the engine still validates writes.
- Long Lua scripts run on the main thread, so they should do a bounded amount of work and return a short summary.
- Tool errors are advisory data for recovery, not transport failures.
- Sphere, cylinder, and cone primitives have radius 1, so diameter is 2x the xz scale, while cube and quad are 1x1x1 per scale unit; halve xz scale versus a cube for the same footprint. Lua has `World.GetEntityByName(name)` (exact match) and `World.GetEntityById(id_string)`; ids exceed lua number precision, so pass them as strings.
- Batch positions are parent-local when `parent_id` is set. Lua entities expose `GetName`, and the render enum is `ComponentType.Renderable`, not Render.
- Prompt phrases like `parent under an entity called dockyard` must resolve to `dockyard`, not filler text such as `parent under an`.
- Do not call `pairs()` or `next()` on raw C++ entity containers from Lua; use the table wrappers or `ForEachChild`.
- spline_junction snaps nearest endpoints only, not mid-spline points. For a mid-route T, split the arterial into two legs that both end at the junction, then join those ends with the spur.
- `spline_reroute` preserves and redistributes its own furniture while excluding descendants of foreign spline roads from reclaim.
- `detail_pattern_create` with `pattern: slats` treats `size` as each slat mesh size, not the total array span; use a narrow per slat size and control total coverage with `count * spacing`, otherwise scene bounds can expand dramatically.
- Visual-review and screenshot paths outside `screenshots/` are normalized to a safe filename under that directory.
- Native district and city blockouts create static collision and coordinated surface, structure, and accent materials for every renderable.

## Verified Patterns
- A parent entity plus a single batch or Lua script is usually better than many individual entity tool calls.
- A small receipt after each meaningful engine action helps the editor assistant UI stay understandable.
- To make a surface emissive white, create a material with material_create (defaults to white albedo), set emissive_from_albedo to 1 with material_set_property, then assign it via component_set property material on the render component; the albedo color drives the emissive color.
- To sync sequencer cuts to a spline follower, set the follower speed, run `spline_query` for per camera `pass_time_seconds`, then place each cut at the midpoint between consecutive pass times; every camera then sees the car arrive, pass centered in its shot, and leave before the next cut.
- Gas-station style blockouts succeed with `entity_resolve` then one `entity_create_primitive_batch`; dockyard failed when the agent fell into Lua API probing instead.
- Dockyard lights were hand-rolled at 25-55 lumens and looked invisible; always use `entity_create_light`, which calibrates photometric intensity and related properties.
- Dockyard blockout (2026-07-08 retry): succeeded with entity_create_empty at ground via world_raycast, then entity_create_primitive_batch for pad/warehouse/containers/crane/fences and entity_create_light for pole/area/spot lights. No Lua.
- Use `lights_calibrate` for bulk light correction; it applies role-aware photometric defaults without Lua.

## Corrections
- Add corrections here when a previous note turns out to be wrong or incomplete.
- `spline_distribute` `edge_offset` 2 is too wide on roads with side walls (plan.world); cameras land outside the walls looking at them, use `edge_offset` 1 there.
- Target name extraction used to steal phrases ending in `entity` such as `parent under an`; it now prefers `called`/`named` names and filters stopwords.
- `city_blockout`, `district_blockout`, and `spline_reroute` are native MCP tools after rebuild. If you see `unknown command`, rebuild the engine and restart the MCP assistant.
- Use `spline_reroute` to fix an existing arterial that cuts through buildings/roads; it preserves and redistributes lights/cameras.
- Invalid scene-plan replacements preserve the previous valid plan and return `ok: false`.

## Advice To Future Agents
- Treat this file as advice, not absolute truth.
- Update this file only when a durable lesson was learned.
- Prefer replacing stale bullets over appending duplicates.
- Keep entries concise and tied to observed behavior.

## Advice To Maintainers
- Add native engine tools when agents repeatedly need the same multi-step command sequence.
- Keep MCP schemas close to engine component metadata so tool descriptions do not drift.
- Scene construction prompts should keep steering agents toward `entity_create_primitive_batch` rather than open-ended Lua discovery.
- Log unresolved capability gaps here with the failing tool, observed error, and required engine improvement.
- Rebuild the engine and restart the assistant bridge when deploying new native MCP commands.
- `scene_quality_audit` feature evidence includes the quality root, descendants, and render-material entity names so semantic hierarchies remain auditable.
- Capability gap: Native MCP command or tool `resource_read` is missing from the engine bridge or Node registry. Prompt: "Build a cozy, fully drivable Japanese tuning shop inspired by Gran Turismo 4 and Sega GT 2002, designed as a physical open-world location like Test Drive Unlimited 2. Create a p..."
