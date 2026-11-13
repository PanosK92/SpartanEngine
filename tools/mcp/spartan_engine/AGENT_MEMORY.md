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
- `viewport_frame` frames complete descendant bounds from perspective, front, back, left, right, or top views without keyboard focus. Use `camera_set_view` for custom poses.
- `screenshot_take` queues a renderer screenshot and can return the saved PNG as image content for visual inspection.
- The editor sequencer (shots, spline followers, physics drives) is controlled with `sequencer_get`, `sequencer_set`, `sequencer_playback`, `sequencer_event_*`, `sequencer_spline_*`, `sequencer_drive_*` and `sequencer_render`, with cinematic tracks in `sequencer_time` (slow motion, freeze), `sequencer_audio`, `sequencer_world`, `sequencer_overlay` and `sequencer_markers` (bpm grid, beat:N, bar:N, marker:NAME times); see it with `sequencer_capture`, `sequencer_review` and `sequencer_sample`; `camera` accepts an entity id or name, events re-sort by time, and state auto-saves to `sequencer_<world>.xml` under the project directory.
- Car camera views (CarView in source/car/Car.h) are C++, not car Lua: Chase, Hood, Interior, Wheel, V cycles them. Interior sits at the .car `<body><cockpit eye_x/eye_y/eye_z>` point (body model space) and right click drag / right stick turns the head, C recenters; cars without a cockpit skip Interior when cycling. Only rubra.car has a cockpit today.
- Physics toys sit behind Alt so plain clicks belong to gameplay: Alt+L-Click is the PhysX spring grab (PhysicsWorld.cpp picking), Alt+R-Click+L-Click shoots a cube (CameraController.cpp). The grab also stands down while a script outlines something with World.SetHighlight and never grabs the driven or spectated car. Keep the Controls & Shortcuts window (editor/GeneralWindows.cpp, controls namespace) in sync when adding bindings.
- plan.world play mode opens a front-end menu first; input_inject Enter picks Free Roam, which seats the player in player_car. The user may be driving the same instance by hand, so view/telemetry can change under you.
- Car scripts can own getting in and out: CarTuning.SetEntryScripted(car, true) every tick makes E raise ConsumeEnterRequest/ConsumeExitRequest instead of entering; the script answers with CarTuning.Enter, SetCameraScripted (camera hangs off the body, car holds pedals and ignores view keys), SetView and ExitTo(car, eye, look). rubra.lua uses it: E needs door_left open, the camera follows body-space keys (door, duck, sill, seat) on a Catmull-Rom path. Cars without a script keep instant E.

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
- Use `viewport_frame` with an explicit review angle before `camera_set_view` or manual camera transform scripts.
- Use `renderer_debug_set` and `physics_state` for visual debugging and vehicle/rigid body inspection.
- Use `scene_visual_review` when visual verification matters; it captures perspective and top views by default and returns both images when ready.
- Before deleting or rebuilding geometry that should preserve look, call `entity_render_materials` on the target and reuse material names.
- Use `entity_create_light` for every light; it fully initializes intensity, range, angle, area size, shadows, and distances. Never hand-roll lights with empty + add component + component_set.
- Light intensity is lux for directional and lumens otherwise. Visible blockout defaults: point/spot 8500, area 12000, directional 120000. Values like 25-100 are invisible.
- Use `lights_calibrate` to fix existing scene lights in one call; specialty car lights stay dim, blockout lights get lifted.
- For city massing: `city_blockout` / `district_blockout`. For city roads: scan `world_landmarks` and bounding boxes, invent an arterial that skirts large districts, spur to edges, `spline_junction`, then `spline_decorate`. Use `spline_reroute` to fix roads that cut through geometry while keeping lights/cameras. Never triangle center-to-center through an airway. Never hand-build `spline_point_*` children.
- Use `camera_snapshot` before interpreting camera-relative placement.
- Use `world_raycast` for ground or surface-relative placement when possible.
- Simple live scene edits should use deterministic tools; anything unmatched falls back to the Cursor agent with the engine MCP tools.
- Scene construction prompts such as `build a level`, `make rooms`, `backrooms`, or `liminal space` are live scene edits, not source-code search requests.
- Recurring gaps worth a dedicated fast path should be logged under Advice To Maintainers.
- Do not route delete plus rebuild prompts to `entity_delete`; preserve materials first, then rebuild through a complex scene path.
- User convention, `physics <primitive>` means dynamic non-static physics unless static, fixed, or immovable is explicitly requested.
- For repeated scene work, prefer `entity_create_primitive_batch` or one focused `execute_lua` script.
- For blockouts, resolve or create the parent first, then build with `entity_create_primitive_batch` and `entity_create_light`; do not probe Lua APIs.
- For source questions, use `search_codebase`, then `read_source_file` for focused context.

## Gotchas
- World loading blocks many engine commands until loading completes.
- `component_set` supports friendly properties and registered raw component member names for all component types; metadata is advisory and the engine still validates writes.
- Long Lua scripts run on the main thread, so they should do a bounded amount of work and return a short summary.
- Tool errors are advisory data for recovery, not transport failures.
- 2026-11-12 Re-exported car model (glTF) in a running engine: resource_reload on the mesh + vehicle_set_car reload=true scrambles material bindings when the material list changed and keeps stale textures. What works: world_load the world again, then vehicle_set_car <name> (car is the catalog name, e.g. `rubra`, not a path). For repeated checks prefer headless_start with worlds/car_playground.world (loads in seconds, car at (0,0.75,-160) facing +z, rubra_* viewpoints saved there) and headless_stop afterwards; engine_quit refuses visible editors, and dialogs left open in a visible editor end up in viewpoint_capture shots. Check viewpoint_list before viewpoint_save, saving an existing name silently replaces another agent's viewpoint.

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
- Add specific unresolved friction here, with the file or tool involved and why it matters.

## Radial motion blur
- Spin is detected per render entity from the transform delta in Renderer::UpdateFrameCb_RadialBlurHubs (no tags): quaternion delta gives axis/angle, angular speed carries frame to frame to unwrap whole turns, hysteresis 0.1/0.025 rad per frame; physics wheels keep their own pivot and angular velocity. Spinner data is packed into radial_blur_hubs/axes/extents/bounds (extents.zw = pivot screen travel); do not add new arrays to the frame cbuffer. r.motion_blur 2 is the classification debug view (green spinning, blue swept). To verify wheels: headless_start, engine_set_mode play, vehicle_enter, vehicle_set_view wheel, vehicle_set_input with handbrake 0 (vehicle_reset re-engages it); chase view hides the wheels. First headless screenshot can be black while shaders compile, and 4K saves take ~30 s so ready:false files appear late. The visible editor's screenshot_take can hang 'already pending' when its window is occluded; use headless instead.

## Cars
- 2026-11-12 Rubra pipeline: binaries/project/plan_resources/cars/rubra/concept (untracked, binaries is git-ignored; backup in concept/backup). scripts/rb_*.py build the whole car procedurally in Blender. Drive it through the user-blender MCP, not headless: start Blender with `blender.exe --python concept/scripts/rb_live.py` (no blend file, the session builds the car from the scripts on start), then execute_blender_code: `import rb_build; rb_build.reload(); rb_build.build('all')` (8 s), `rb_export.export()`. There is one model and no .blend by the user's choice: export writes rubra.gltf/.bin straight into the car folder (no save, export or install copies), the engine's assimp cannot read Blender 5 .blend files (tested compressed and uncompressed). Textures are synthesised by rb_textures.py into the car folder (call build(force=True) after palette colour changes) and written into the glTF by rb_export.dress; palette + per material tex/repeat live in rb_common.palette. Black paint regions are 2 mm skin_chunk recesses with mat_inner (see rb_exterior.build_canopy).
- 2026-11-12 Project package: from binaries/project, delete project.7z first, then 7z a -t7z -mx=5 -mmt=8 with -xr!project.7z -xr!generated_cache -xr!thumbnails -xr!__pycache__ -xr!*.lock and -x!plan_resources\cars\rubra\concept\backup (a before-copy; the README forbids shipping those). Test with 7z t, confirm no enclosing project/ folder. Copy over %USERPROFILE%\Dropbox\project.7z (it is a reparse point; Copy-Item -Force works), start Dropbox if it is not running, then confirm the dl=1 link's Content-Range size matches before updating data/project_package.xml. An unauthenticated curl of the link hashed identical to the local archive. Do not push the xml until that check passes.
- 2026-11-12 Rubra side intakes have to be channels cut into the body (rb_body.intake_slot, black only inside the slot). A black sheet with red stripes on top reads as a decal. Hood vents are recesses in the frunk lid (rb_exterior.build_hood); the hood spine is the cage centre lifted and row 2 creased harder in rb_skin.

## Profiling
- 2026-11-13 Frame timing: `engine_command {command: "profiler_record", args: {action: "start"|"stop"}}` writes binaries/profiler.csv (frame rows plus cpu and gpu block rows on one clock). Record 300+ frames and compare min/p10/median per block, one `profiler_snapshot` is too noisy. Before trusting any number check `nvidia-smi` and the process list: another game or a second Spartan instance on the GPU stretches frame times 2-4x. Short passes keep their p10 under that load, frame totals and any pass overlapping async compute do not (clouds_temporal reads 1.4 ms but its own cost is about 0.03 ms, the graphics queue waits there for the compute queue).
- 2026-11-13 animated_character.world, 3840x2088 output, 1920x1044 render, RTX 5070 Ti: the frame is GPU bound at about 8.5 ms and the scene content is not what costs. DLSS (preset M, 4K output) 2.7 ms, ray traced reflections 1.0, SSAO + screen space shadows + ray traced shadows + clouds + fog 1.5 to 2 (overlapping on two queues), post 0.75, editor UI 0.7 with glass (0.3 without).
- 2026-11-13 The editor UI shader (data/shaders/imgui.hlsl) is not in the renderer's shader registry: `shader_reload` matches 0, test it by restarting the editor. `screenshot_take {ui: true}` replays the UI through the SDR capture path, so it does not show what the HDR swapchain branch of that shader outputs.
- 2026-11-13 Never read from a mapped RHI_Buffer (GetMappedData): it is host visible GPU memory, reads stall the CPU. Thinning the glass vertex colors inside the mapped ImGui vertex buffer cost 2 ms a frame, reading ImGui's own vertices and only writing to the mapped copy cost 0.03 ms.
- 2026-11-13 To test a build while a visible editor still runs the old exe: msbuild spartan.vcxproj /p:Configuration=development /p:Platform=x64 /p:TargetName=<other name> links next to it (full rebuild, the pch is per target name), then address the second instance with `target_port`. `headless_smoke_test` fails Render.BasicCube on unmodified HEAD (890e9ee26) too, so that failure alone says nothing about a change.
