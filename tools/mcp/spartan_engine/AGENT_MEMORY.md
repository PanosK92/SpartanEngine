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
- The editor sequencer (camera cut timeline) is controlled with `sequencer_get`, `sequencer_set`, `sequencer_playback`, `sequencer_event_add`, `sequencer_event_update`, and `sequencer_event_remove`; `camera` accepts an entity id or name, events re-sort by time, and state auto-saves to `sequencer.xml` under the project directory.
- `spline_query` with no arguments auto-picks the followed spline and projects every camera in the world onto it, returning per camera `arc_distance` and `pass_time_seconds` (when the follower passes that camera); this is the whole camera cut calculation in one call, never sample the spline manually with Lua.
- `spline_distribute` with no arguments respaces every camera child of the spline entity evenly along it by arc length, keeping order, lateral offset, and framing; use it for any request like spread or place cameras evenly along the road, never compute placements yourself.
- `spline_distribute` takes optional `edge_offset` (signed meters beyond the road edge, positive = right of travel, tracks varying road width), `lateral_offset` (signed meters from centerline), and `height` (meters above the road); requests like move cameras to the side of the road are one call, use `edge_offset` 2 so they clear the asphalt regardless of road width.
- Spline followers move at constant world speed (progress is arc-length based), so `pass_time_seconds` from `spline_query` is exact.

- `resource_read` is an assistant alias: material path or name reads use `material_get`; list queries use `resource_list`.
- `prefab_create` is an assistant alias for `prefab_save`. Focused assets still allow only the finalizer to save the prefab.
- `async_task_start`, `async_task_get`, and `async_task_list` are available through both the MCP server and Cursor custom bridge; nested async tasks are rejected.
- `scene_benchmark_score` executes locally in the Cursor bridge and must not be forwarded to C++.
- `scene_quality_audit` supports the canonical `prop` profile. It requires one renderable material and skips scene lights, scene-scale counts, advanced-mesh pressure, per-part collision, and spatial-layout checks.
- Focused assets use one construction pass, one game-ready pass, one stable catalog upsert, and one Asset Viewer screenshot. There is no version or promotion stage.
- Focused runs stop after the first bridge failure and never automatically retry a timed-out mutation.
- Engine clients use separate connection and command timeouts. A command timeout closes the socket and rejects pending requests. Queued MCP jobs expire after 25 seconds, but an executing main-thread handler cannot be preempted.
- Catalog writes use process-local serialization, a cross-process lock, staged files, backups, and rollback.
- Glass materials use `color_a`, `ior`, `absorption`, and `thickness`. `transmission` and `transparency` alias inverted `color_a`.
- `entity_describe` (alias of `entity_get`), `entity_list_children` (one level of children with name, components and local transform), `agent_memory_update` (alias of `agent_memory_append`, section defaults to Corrections) and `spartan_engine_command` (`{command, args}` forwarder) are bridge aliases now.

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
- Use `material_textured_create` for any real surface; it creates the material and generates and attaches its color, normal, and packed maps in one call.
- `texture_generate` composites layers: fill, linear_gradient, radial_gradient, noise, checker, stripes, bricks, tiles, spots, scratches, shape, text. Layer `relief` drives the normal map, `roughness`/`roughness_b`/`metalness`/`occlusion` drive the packed map.
- Texture responses report mean color, contrast, and `seam_error`; tune tiling textures from those numbers instead of guessing. Labels and decals should set `seamless` false.
- Parametric shapes include box/cube, plane/quad, sphere, ellipsoid, hemisphere, cylinder, cone, frustum, arc, sector, disk, ring and tube, alongside lofts/sweeps and the existing architectural shapes. Read `spartan://engine/parametric-modeling` for axes and dimensions.
- Use resource lifecycle tools for asset cache load/reload/save/remove and new material creation.
- Use `viewport_frame` and `camera_set_view` before manual camera transform scripts.
- Use `renderer_debug_set` and `physics_state` for visual debugging and vehicle/rigid body inspection.
- To drive and inspect a car: `engine_set_mode` play, `vehicle_enter`, hold `vehicle_set_input` for a stretch, then `vehicle_telemetry` and report anomalies. Agents cannot compile; stop at diagnosis unless the user asks for code changes.
- Use `screenshot_take` when visual verification matters; it waits briefly for the async save and returns the image when ready.
- Before deleting or rebuilding geometry that should preserve look, call `entity_render_materials` on the target and reuse material names.
- Focused reusable assets have no authored part, component, material or triangle cap. Create as many as the object needs. Hero bodies use dense loft profiles (24 to 64 points, 12 to 32 stations). Merge geometry that shares a material on save, and put fasteners, print and wear into textures.
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
- For repeated scene work, prefer `entity_create_primitive_batch` for built-ins, or `mesh_generate_batch` / `compound_create` for parametric shapes. Batches retain profiles, openings, modifiers and UV controls.
- For blockouts, resolve or create the parent first, then build with `entity_create_primitive_batch` and `entity_create_light`; do not probe Lua APIs.
- For repositioning many entities, use `entity_set_transform_batch` instead of one `entity_set_transform` call per entity.
- For source questions, use `search_codebase`, then `read_source_file` for focused context.

## Gotchas
- World loading blocks many engine commands until loading completes.
- `component_set` supports friendly properties and registered raw component member names for all component types; metadata is advisory and the engine still validates writes.
- Long Lua scripts run on the main thread, so they should do a bounded amount of work and return a short summary.
- Tool errors are advisory data for recovery, not transport failures.
- Sphere, cylinder, and cone primitives have radius 1, so diameter is 2x the xz scale, while cube and quad are 1x1x1 per scale unit; halve xz scale versus a cube for the same footprint. Lua has `World.GetEntityByName(name)` (exact match) and `World.GetEntityById(id_string)`; ids exceed lua number precision, so pass them as strings.
- Batch positions are parent-local when `parent_id` is set. Lua entities expose `GetName`, and the render component enum is `ComponentType.Render`.
- Prompt phrases like `parent under an entity called dockyard` must resolve to `dockyard`, not filler text such as `parent under an`.
- Do not call `pairs()` or `next()` on raw C++ entity containers from Lua; use the table wrappers or `ForEachChild`.
- spline_junction snaps nearest endpoints only, not mid-spline points. For a mid-route T, split the arterial into two legs that both end at the junction, then join those ends with the spur.
- `spline_reroute` preserves and redistributes its own furniture while excluding descendants of foreign spline roads from reclaim.
- `detail_pattern_create` with `pattern: slats` treats `size` as each slat mesh size, not the total array span; use a narrow per slat size and control total coverage with `count * spacing`, otherwise scene bounds can expand dramatically.
- Visual-review and screenshot paths are normalized to a safe filename under shared `project/mcp/blockout/thumbnails`.
- Native district and city blockouts create static collision and coordinated surface, structure, and accent materials for every render component.
- Every persistent MCP-generated blockout resource belongs under shared `project/mcp/blockout`: meshes, materials, textures, prefabs, sources, thumbnails, and catalog metadata. Never write MCP output into `<world>_resources`. The curated Asset Viewer library is `project/mcp/library` and stays empty until assets are promoted there. Text layers rasterize with fonts from `data/fonts` (Calibri), so labels are latin only.
- `screenshot_take` captures the pre-tonemap renderer output only; ImGui (car driver hud, F3 telemetry hud, editor) is never in it, and no MCP tool toggles F3. Car hud work (source/car/CarHud.cpp, CarTelemetry.h) must be verified by the user. In plan.world the chase camera frames the car at ~36-64% of viewport width and ~51-96% of height.
- ImGui windows created with `ImGuiWindowFlags_NoBringToFrontOnFocus` are inserted at the back of the window list (`g.Windows.push_front`), so an overlay window with that flag is drawn behind the editor dockspace and looks missing. For click-through hud windows use `NoInputs` alone, or draw to `GetForegroundDrawList()` like the driver hud.

## Verified Patterns
- A parent entity plus a single batch or Lua script is usually better than many individual entity tool calls.
- A small receipt after each meaningful engine action helps the editor assistant UI stay understandable.
- `material_textured_create` accepts `emissive_from_albedo: 1` directly; the albedo color drives emission. Use `entity_create_light` separately when an actual scene light is needed.
- To sync sequencer cuts to a spline follower, set the follower speed, run `spline_query` for per camera `pass_time_seconds`, then place each cut at the midpoint between consecutive pass times; every camera then sees the car arrive, pass centered in its shot, and leave before the next cut.
- Gas-station style blockouts succeed with `entity_resolve` then one `entity_create_primitive_batch`; dockyard failed when the agent fell into Lua API probing instead.
- Dockyard lights were hand-rolled at 25-55 lumens and looked invisible; always use `entity_create_light`, which calibrates photometric intensity and related properties.
- Dockyard blockout (2026-07-08 retry): succeeded with entity_create_empty at ground via world_raycast, then entity_create_primitive_batch for pad/warehouse/containers/crane/fences and entity_create_light for pole/area/spot lights. No Lua.
- Use `lights_calibrate` for bulk light correction; it applies role-aware photometric defaults without Lua.

## Corrections
- Add corrections here when a previous note turns out to be wrong or incomplete.
- `mesh_generate` `mirror_axis` reflects in place by default. Set `mirror_copy: true` to keep the original plus its reflection for symmetric pairs; this does not weld or boolean-union the surfaces.
- `material_textured_create` accepts texture and material controls together on both bridges. `height` is texture pixels; `displacement_height` sets material displacement. Glass, emission, flakes/pearl/coat tint, anisotropy and texture transforms are applied after attaching maps.
- `texture_generate` at a path that is already loaded writes the maps to the next free suffix (`name_2.png`) and rebinds `material_path` to it, because the resource cache keeps serving the texture it already has. Read `path` from the response, it may differ from what was asked for; `requested_path` and `note` are present when it moved.
- `mesh_generate` `uv_projection` box normalizes each axis over the whole mesh bounds, so one `uv_scale` cannot serve faces whose in-plane extents differ wildly. Keep arrayed copies out of the axes the visible faces project along, or instance a single-copy mesh.
- `spline_distribute` `edge_offset` 2 is too wide on roads with side walls (plan.world); cameras land outside the walls looking at them, use `edge_offset` 1 there.
- Target name extraction used to steal phrases ending in `entity` such as `parent under an`; it now prefers `called`/`named` names and filters stopwords.
- `city_blockout`, `district_blockout`, and `spline_reroute` are native MCP tools after rebuild. If you see `unknown command`, rebuild the engine and restart the MCP assistant.
- Use `spline_reroute` to fix an existing arterial that cuts through buildings/roads; it preserves and redistributes lights/cameras.
- Invalid scene-plan replacements preserve the previous valid plan and return `ok: false`.
- Prompt classification lives only in `route_intent`. Greybox and blockout prompts are live scene commands (`greybox: true`), not Asset Viewer library assets. Do not re-classify in `cursor_agent` or because an image is attached.
- `mesh_generate` `shape: revolved_profile` is the cheapest hero surface for any turned object (mug, bottle, plate, lamp): `profile` is radius,y pairs (3 to 128 points) and `segments` is 3 to 64. Normals follow profile order, so a profile listed bottom to top faces outward and the same profile listed top to bottom faces inward. Author inner shells and cavities by reversing the order, there is no flip flag needed.
- `mesh_generate` closed profiles (`curved_profile`, `loft`, `extruded_profile`, `tapered_extrusion`) are implicitly closed: list the points once, counter clockwise, at any scale (a 5 mm handle section is fine, tolerances follow the profile size). A repeated closing point is stripped. A rejection names the one problem (`consecutive duplicate points`, `edges cross each other`, `clockwise, reverse the point order`), fix that rather than rescaling or retrying.
- `material_textured_create` resolves its own material path (`material_create` puts bare names under `project/mcp/blockout/materials/`) and returns it as `material_path`; use that path for every later material call.
- `render_set_mesh` with `{id, mesh, material}` binds a generated mesh and its material in one call. The part loop for a focused asset is `mesh_generate`, `material_textured_create`, `entity_create_empty` with `parent_id`, then `render_set_mesh`.
- Attaching a roughness or metalness map sets that material scalar to 1 by design, `g_buffer.hlsl` does `roughness *= packed.g` so the map alone carries the finish. `roughness` and `metalness` on `material_textured_create` therefore go into the maps (`base_roughness` / `base_metalness`), and `material_get` showing `roughness: 1` on a textured material is normal, not a bug. Do not stack a low scalar on a low map, 0.1 x 0.1 is a mirror.
- Glazed ceramic, car paint and varnished wood need `clearcoat` 1 with `clearcoat_roughness` 0.04 and `ior` 1.5, not just low roughness. Pass them on `material_textured_create` itself, it applies them after the maps. The clearcoat lobe is independent of the roughness texture. `Material::ApplySurfacePreset` in `source/rendering/Material.cpp` is the reference table for plausible scalars per surface (glass, headlight lens, tire, carbon fiber, chrome).
- `material_set` can only darken albedo, `color_r/g/b` are 0 to 1 multipliers over the color texture. To lighten a material, call `texture_generate` at a NEW path with `material_path` pointing at the existing material, which rebinds every map without creating a second material.

## Advice To Future Agents
- Treat this file as advice, not absolute truth.
- Update this file only when a durable lesson was learned.
- Prefer replacing stale bullets over appending duplicates.
- Keep entries concise and tied to observed behavior.
- Rounded-box generators map each face across the full 0-1 UV range; use a dedicated raw mesh UV island for unique non-tiled cover art or labels.

## Advice To Maintainers
- Add native engine tools when agents repeatedly need the same multi-step command sequence.
- Keep MCP schemas close to engine component metadata so tool descriptions do not drift.
- Log only unknown commands as capability gaps. Treat connection and command timeouts as bridge-health failures, and never store prompt snippets here.
- Rebuild the engine and restart the assistant bridge when deploying new native MCP commands.

## Lessons
- 2026-09-24: (1) Glass behind interiors rendered black because frame_render_opaque was blitted before Pass_Light_Ibl; fixed by running IBL for opaque before the blit and for transparents only afterwards (Renderer.cpp ProduceFrame_PerEye, light_image_based.hlsl). (2) Terrain pads store an owner anchor (anchor_x/y/z, anchor_q*); Terrain::FollowPlatformOwners carries a pad along whenever its owner moves by any means (editor, parent, MCP, Lua, undo, file edits), debounced 250 ms. (3) Never world_load twice in one engine session: GPU memory blows up and frames take seconds. (4) PowerShell Get-Process | Select-Object can print nothing; use Format-Table | Out-String before concluding the engine crashed. (5) home_garage installer computes the pad from the hub's composed world transform (hub position is parent-relative).
- 2026-09-25: Performance overlay (r.performance_metrics, 1 full, 2 compact) is drawn by Profiler::DrawPerformanceMetrics through the renderer's native text pass, not ImGui, because screenshot_take captures the renderer frame (Pass_Text runs before ImGui) and ImGui UI never appears in MCP screenshots. Font now has per-vertex color (Pos2dUvCol8), solid quads (AddRect, uv.x < 0 in font.hlsl), pixel-space AddText with tabular digits and GetTextWidth; Renderer::GetFont(Renderer_Font) exposes Standard plus Inter OverlaySmall/Overlay/OverlayLarge. Screenshots are 3065 px wide, crop the top right to judge overlay detail. The engine reopens the last world (plan.world) on launch; wait for it instead of world_load. After Stop-Process wait for the exe to exit or the link fails with LNK1104.
- 2026-09-25 ReSTIR PT noise: history was wiped every frame by UpdateLights memcmp over whole Sb_Light (shadow cascade matrices/atlas follow the camera) and by BuildEmissiveTriangleNeePool hashing every render transform revision; now only light count or renderable set changes clear reservoirs, continuous light/transform changes only reset the still-camera accumulator (temporal c cap 20 ages stale radiance). EmissiveTriangles buffer must be mappable (Update asserted and killed the engine whenever restir met an emissive material) and the pool is cached by an emitter signature (plan.world has ~774k emissive triangles, per-frame rebuild made frames take seconds). Several agents share port 47791 and the development exe; the exe is locked while any instance runs, so wait for a free window to build and expect other agents or the user to move the camera.
- ReSTIR PT follows Lin 2026 (ReSTIR PT Enhanced): paired spatial tables, dual footprint rc test, duplication map, vector weights, dual motion vectors are in. Unified direct+indirect (6.1) is behind r.restir_pt_direct (default 1, frame options bit 4): restir_pt.hlsl picks analytic lights by 32 unshadowed RIS candidates + one visibility ray, light.hlsl scales diffuse by 1 - restir_coverage(uv) and keeps specular/SSS. Primary point/spot NEE samples store the light index in endpoint_light and are re-evaluated at dst in try_reconnection_shift (unit jacobian). Restir direct lacks cloud shadows, ocean transmission and screen-space contact shadows from non-TLAS grass. Not done: light tiles, postponed reconnection past x2, 64-byte reservoirs, replay compaction.
- 2026-09-25: (1) Shaders are read from the repo's data/shaders (root shader directory, binaries/data/shaders is stale) and compiled only at startup; there is no MCP shader reload, so every shader tweak needs an engine restart + world_load. Validate offline first: C:/VulkanSDK/<ver>/Bin/dxc.exe -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout -Zpc -HV 2021 -T ps_6_6 -E main_ps g_buffer.hlsl (add -D GRASS_INSTANCED=1 for the grass vs). (2) Grass/tree wind: wind_field.hlsl bakes a 256^2 field (rg flow, b gust strength, a micro); wind_gust() in common_vertex_processing.hlsl turns it into travelling gust fronts shared by grass bend, tree trunk drive and the g_buffer grass wind sheen; grass_wind_response() is the grass speed curve and grass_populate.hlsl's LOD error envelope must stay an upper bound of the vertex bend. plan.world wind is only [2,0,1]. (3) Several agents share MCP port 47791 and relaunch/kill instances often; expect your instance to be replaced.
- 2026-09-25 engine audio (source/car/CarEngineSoundSynthesis.cpp): no MCP tool captures or plays audio, and engine_sound::synthesizer::begin_dump/save_dump are not wired to anything. To judge the synth, compile CarEngineSoundSynthesis.cpp standalone (empty pch.h on the include path, cl /std:c++20 /O2), drive it with scripted set_parameters at 60 Hz in 480-frame blocks, write wavs and inspect spectrograms plus A-weighted level vs rpm; costs ~3% of a core. Findings: WOT loudness is set by the collector tanh ceiling (collector_trim), so cars land within a few dB of each other and pulse amplitude changes barely move redline level. Identical primaries with a fixed valve-end reflection notch the firing frequency coherently (GT3 lost 11 dB toward redline); the reflection now follows exhaust valve lift. Limiter afterfire is a per second rate (limiter_pops_per_second), a per spark chance made a V12 hiss at ~80 pops/s. plan.world drives ferrari_laferrari.car (65 deg V12, 2 banks, NA).
- 2026-09-25 updating a third party lib (meshoptimizer 1.3): headers/sources live in third_party/<lib> (+ version.md), but Windows links prebuilt third_party/libraries/<lib>.lib and <lib>_debug.lib from libraries.7z (tools/setup.lua pins its dropbox url + sha256; only the user can re-upload). Rebuild libs with CMake, static CRT: -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug> (run from a .bat, cmd eats the > otherwise), no LTCG. Copy-Item/Expand-Archive keep upstream mtimes, so touch the copied headers or MSBuild will not recompile dependents. Meshlets/LODs are baked into .mesh files, so loading a world does not exercise meshoptimizer; mesh_generate + render_set_mesh does (a new renderable can take a frame or two to appear in screenshots). Linux links the system libmeshoptimizer.
- Engine sound aggression (CarEngineSoundSynthesis.cpp): lack of scream came from rasp ~40 dB under the pulses and a fixed tanh ceiling flattening WOT. Fixed with pulse-gated turbulent flow noise (900-7000 Hz, gain 6 * gas_speed^3), pulse strength * (rpm/idle)^0.3, and a collector ceiling that opens to 2.5x at redline*load. Front steepening via a variable delay was tried and only smeared peaks, don't retry. Offline harness + metrics live in %TEMP%/engine_audio_audit (A-weighted growth, 1.5-5k band, crest); MCP cannot capture audio.
- 2026-09-26 tire deformation (source/car/CarTireDeformation.h tire_cage::solve): visual tire snapped between shapes as PSI changed because the cage beams were nonlinear length springs; the flattened patch compresses circumferential beams, they buckle and the solve jumped between buckled equilibria (23 mm crown jumps at 1.30/2.51/3.78 bar, worse with more iterations). Beams now act along their rest axis (linear), result is converged at 32 iterations, max step per 0.01 bar is 0.4 mm. No MCP tool sets tire pressure (only the F3 telemetry HUD slider -> Simulation::set_tire_pressure), so sweep offline: compile a harness including car/CarTireDeformation.h with /I source, drive cage.solve with deflection = tire_spring_deflection(load, tire_radial_stiffness(...)). Screenshots stop (frame_number frozen) while play is paused or the window is minimized (swapchain tiny).
- 2026-09-26 puddliness: World::Get/SetPuddliness (0-1, saved as Environment puddliness in the .world, Lua World.SetPuddliness, editor slider under the sun's Weather section, MCP world_set_environment puddliness) -> Cb_Frame.puddliness -> data/shaders/common_puddles.hlsl puddle_apply, called from g_buffer.hlsl for terrain and road asphalt/paint (material flag bits 22/23) only, so interiors/props stay dry. One water level rises through a world-space basin field so low spots fill first. Ray traced reflection/restir hit shaders do not apply puddles yet (road_weathering is the place they share). A running MCP server keeps its old zod schema and silently strips new tool args until it is restarted; use the Lua binding meanwhile. plan.world test stretch: flat 4-lane road at (4575..4625, y 4.9, z -3885).
- 2026-09-26 rain: Light::SetRain 0-1 (sun, Lua World.GetDirectionalLight():SetRain) drives source/world/Weather.cpp: wetness, rain puddles, a camera-centred static-geometry height grid (t68 rain_occlusion) keeping covered areas dry, transient rain particles and synthesized sound. Look: data/shaders/common_rain.hlsl. Garage interior (6213,12.8,-2857) is the dry test. frame_number frozen = window minimized. Wet tires: Weather::GetWaterDepth mirrors puddle_basin, Physics.cpp water_resolver -> wheel.water_depth -> water_grip() (CarCalibration.h). Car drops: occupied car draws flag bit 7; Weather integrates slide per bucket per axis plane with gravity; rain_veins = rivulets on fixed 20deg plane dirs. See: vehicle_set_view wheel, crop png.

## Memory
- Measuring memory: log.txt gets 'Cpu memory (world ready|steady)', 'Gpu memory (...)' (per kind, top names, top textures, VMA heaps with used vs committed = slack) and 'Gpu geometry (world ready)' (per-mesh share of the global geometry buffer). Windows counter '\GPU Process Memory(pid_N*)\Dedicated Usage' is the ground truth for VRAM (MiB). SPARTAN_HEAP_CENSUS=1 env var enables callstack heap census (Allocator::LogLargestAllocationSites). The editor Memory Viewer widget exports the same GpuMemory data to CSV but only via its button; the MCP cannot click it. plan.world after 2026-09 fixes: peak VRAM ~6.8 GiB, CPU working set ~6.6 GiB (was 14+ GB VRAM, 20 GB CPU).
- Big VRAM wins and their rules: (1) guardrail W-beams are GPU instances of one 4 m module (road_guardrail::Beam, 'roadside_guardrail_beams'), never baked per chunk; baking cost 3.4 GB of geometry plus a BLAS per chunk. (2) Uncompacted BLAS and BLAS scratch go to a transient VMA pool (MemoryBufferCreate(..., transient=true)); mixing them into default 256 MB blocks left 1.4 GB of slack pinned by small textures. (3) The initial BLAS burst compacts as builds retire (no TLAS exists yet), otherwise all uncompacted BLAS coexist and peak +1 GB. (4) GeometryBuffer keeps no CPU mirror; released mesh CPU geometry is restored lazily via Mesh::SetCpuGeometrySource. (5) Physics cooks preserved geometry with a 1 mm weld tolerance; 1 cm folded the 3 mm guardrail sheet to zero triangles. Remaining fixed VRAM: 4 per-frame copies of meshlet_instances/visible_triangles/surviving_instances (~1 GB) are candidates if more headroom is needed.
