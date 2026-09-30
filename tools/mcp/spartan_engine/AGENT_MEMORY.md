# Spartan Agent Memory

Shared memory for agents working on Spartan Engine. Keep it short, factual and current: replace wrong notes instead of stacking corrections, state the rule and where it lives, drop the story. One note stays under 1200 chars. When the file passes its limit, dated notes move to AGENT_MEMORY_ARCHIVE.md oldest first, then undated topic notes; the first four sections are never archived. Search the archive with agent_memory_read {archive_query} (e.g. "ocean shore", "d3d12 build", "foot ik").

## Engine Facts
- MCP commands run on the engine main thread at the start of the frame; request ids are echoed in responses and debug logs. Mutating scene tools require edit mode. World loading blocks most commands.
- `context_snapshot` is the fastest first read (status, world, selection, camera). `async_task_start`/`async_task_get` run long tools in the background.
- `engine_command {command, args}` forwards any native bridge command, for commands added after the MCP server started. New dedicated tools appear only after Cursor respawns the server (change an env var such as SPARTAN_MCP_REVISION in ~/.cursor/mcp.json).
- `shader_reload` recompiles shaders whose .hlsl or includes changed on disk (`name` filters, `force` recompiles). Compiles run in the background (~129 shaders per edit): wait 15-20 s via screenshot_take wait_ms instead of polling, then check `failed` and `console_read`. Edit the repo's data/shaders, not binaries/data/shaders.
- `component_get` exposes properties, raw members and metadata; `component_set`/`component_set_batch` write them; `component_action` runs component methods (terrain generate, spline road mesh, particle presets, light fit_to_mesh...).
- `terrain_scatter_get` / `terrain_scatter_set {layer, values}` edit scatter layers with the world file's <layer> attribute names. Grass updates instantly; mesh layers rescatter in the background (poll `mesh_rescatter_running`).
- `world_save` prunes unreferenced files from the world resources directory; `world_resources_clean` returns a receipt.
- `camera_set_view` works in edit mode and paused play; in first person play it teleports the player (position is the eye). `viewport_frame` frames descendant bounds from a named view.
- `screenshot_take` returns a 1024 px preview (full PNG under binaries/project/mcp/blockout/thumbnails); `{ui:true}` captures the whole editor window. Screenshots freeze while play is paused or the window is minimized; parallel calls grab the same frame, so shoot series sequentially.
- Cars: `vehicle_list/get/enter/exit`, `vehicle_set_input`, `vehicle_shift`, `vehicle_set_view`, `vehicle_reset`, `vehicle_set_tire_pressure {psi|bar}` (stored in bar), `vehicle_telemetry`, `vehicle_ai`, `vehicle_spectate`. Sequencer: `sequencer_*`, state in `sequencer_<world>.xml` under the project directory.
- `spline_query` (no args) returns each spline camera's `arc_distance` and `pass_time_seconds`; `spline_distribute` respaces cameras (`edge_offset` 2, or 1 on roads with side walls).
- Glass materials use `color_a`, `ior`, `absorption`, `thickness`.
- cvar_set blocks r.resolution_scale, r.hdr and r.antialiasing_upsampling; value must be a string. Render cvars persist to binaries/spartan.xml, so restore what you change. debug.* cvars are startup only.

## Good Agent Strategies
- `search_capabilities` / `get_capability_details` before guessing tool names; `debug_log_read` after failures.
- Resolve targets with `entity_resolve` before mutating; read world transforms from it.
- Prefer native tools over Lua: batch creation (`entity_create_primitive_batch`, `mesh_generate_batch`, `compound_create`), `entity_set_transform_batch`, `selection_update`, `entity_clone`, prefab tools, `material_set_property`/`material_set_texture`. Exploratory Lua API probing has crashed the engine.
- `material_textured_create` is the one call for a real surface; `texture_generate` layers: fill, gradients, noise, checker, stripes, bricks, tiles, spots, scratches, shape, text; `relief` drives normals. Tune tiling from the reported contrast and `seam_error`; labels set `seamless` false.
- Parametric shapes: see `spartan://engine/parametric-modeling`. Hero bodies use dense lofts (24-64 points, 12-32 stations); merge same-material geometry, put fasteners and wear into textures.
- `entity_create_light` for every light. Intensity is lux for directional, lumens otherwise; visible defaults point/spot 8500, area 12000, directional 120000. `lights_calibrate` fixes existing lights.
- Cities: `city_blockout`/`district_blockout`, roads from `world_landmarks`, `spline_junction`, `spline_decorate`, `spline_reroute`. Never hand-build `spline_point_*` children.
- Before rebuilding geometry that should keep its look, `entity_render_materials` the target and reuse the materials.
- Sync sequencer cuts to a follower: set its speed, `spline_query`, place each cut midway between pass times.
- Compare fresh world loads with fresh loads (grass and terrain stream differently in long sessions); auto exposure needs ~10 s after a big view change.

## Gotchas
- Never world_load twice in one engine session (GPU memory blows up); restart the engine. First frames after a load build BLAS for seconds.
- Agents share port 47791 and the development exe, which is locked while any instance runs. Build a private exe with /p:TargetName=<name> and /p:IntDir=binaries\obj_<name>\x64\development\ (seed from binaries\obj\x64\development), run it on another port (spartan_engine_ui is 47792), delete it when done. Never close an instance you did not start.
- A minimized engine window stops rendering, GPU timings and screenshots; ShowWindow(h, 4) or SW_SHOWNOACTIVATE + SetWindowPos(HWND_BOTTOM) restores it without focus (stray keys move the camera when it has focus). Get-Process cannot see Spartan from the agent shell, use tasklist. There is no MCP quit tool; WM_CLOSE is the graceful exit.
- Lua runs on the main thread, keep it bounded. `World.GetEntities()`/`GetChildren()` are 1-based; prefer `ForEachChild`, never `pairs()` on raw containers. Entity ids exceed Lua number precision: use names or string ids.
- Sphere, cylinder and cone primitives have radius 1; the cone is 2 units tall; cube and quad are 1 unit. Batch positions are parent-local when `parent_id` is set.
- `detail_pattern_create` slats: `size` is one slat, span is `count * spacing`. `spline_junction` snaps nearest endpoints only; split an arterial for a mid-route T.
- `mesh_generate`: `mirror_axis` reflects in place, `mirror_copy: true` keeps both halves. Box UV normalizes over the whole mesh; rounded boxes map each face to 0-1. `revolved_profile` normals follow profile order; closed profiles listed once, counter clockwise.
- `material_textured_create` returns `material_path`; `height` is texture pixels, `displacement_height` is displacement. Roughness/metalness maps set those scalars to 1 by design. Glazed ceramic, car paint, varnish: `clearcoat` 1, `clearcoat_roughness` 0.04, `ior` 1.5.
- `texture_generate` at a loaded path writes the next free suffix; read `path` back. material color_r/g/b only darken; to lighten, generate at a new path.
- MCP output lives under project/mcp/blockout (material_create and mesh_generate always write there), never `<world>_resources`. Read binaries/project/README.md before adding or deleting assets; never add top-level project folders.
- No MCP tool clicks editor UI, injects WASD or OS mouse input, or captures audio: hover, popups and sound mixes need the user.
- Static collision exists only within 40-80 m of the active camera. Entering play with the camera far from a car drops it through the floor: frame the car first or `vehicle_reset`. Keep the player off live traffic lanes.
- `asset_viewer_preview_path` takes library/blockout .mesh, material and texture paths; preview .glb models through an entity with `asset_viewer_preview_entity`. A pending asset_viewer_screenshot can block screenshot_take; `asset_viewer_open {visible:false}` clears it.
- ModelImporter reuses `<model_dir>/<material>.xml` and caches packed textures: rename the material or delete that xml and restart after changing GLB textures. Engine glb needs external texture uris.
- Worlds sit ~7 km from origin: rasterize camera relative and build views with CreateLookToLH.
- Bugs that "a screenshot fixes" are frames-in-flight races (the readback drains the GPU): verify them with the user driving, not captures.

## Advice To Maintainers
- Add native engine tools when agents repeatedly need the same multi-step sequence; keep MCP schemas close to engine metadata. Log only unknown commands as capability gaps.
- Open gaps: a second play-mode entry per session has hit VK_ERROR_DEVICE_LOST during car renders; a second world_load per session blows up GPU memory; no MCP audio capture, UI clicking or camera follow for a moving car; Memory Viewer CSV export is button only; ReSTIR hit shaders do not apply puddles; restir direct lacks cloud shadows, ocean transmission and non-TLAS grass contact shadows; GeometryInfo storage buffer is rewritten at offset 0 per TLAS rebuild while older frames may read it; world_save after play has crashed in Render::UpdateLodIndices on a freed car Mesh (restart before playing again).

## Worlds
- plan.world: garage interior (6213,12.8,-2857) is the dry test; flat road stretch (4575..4625, y 4.9, z -3885); road view (4560,7.5,-3885)->(4640,5,-3885); safe open field (4600,5,-3960). Saved with rain 0 and traffic_manager 18446744073709551020 active. Before `world_save`, restore `component_camera` local (0,0.77,0) and ocean (6194,0,-2857.5).
- plan.world showcase: time_of_day ~0.73, camera (9250,70,-1500) -> (8100,10,-500); restore time_of_day 0.416667 and never save session-only hides. Soundscape boxes live under 9027000000000000101/102/110-117; do not deactivate 9018000000000000000 (Agalas root). Volume boxes toggle with cvar `r.volumes`.
- car_playground.world (resources project/car_playground_resources, layout sources/layout.py): cold load ~10 s, car spawn (0,0.95,-160); jumps x=35, suspension x=70, crests x=110, slalom x=-35, skidpad (-110,0,0); grass slab +-7000 m. Closed spline `race_track` (~2.9 km) with a `race_driver` component. prefab_load marks descendants prefab-owned, keep the car as a file prefab. entity_set_transform is rejected in play mode.
- liminal_space.world geometry comes from project/scripts/backrooms.lua; animated_character.world tests foot ik. Details: archive_query "liminal" / "foot ik".

## Rendering
- Push constants use named slots from data/shaders/shared_buffers.h (pass_float/pass_uint/pass_bool in HLSL, m_pcb_pass_cpu.set in C++); add params as a namespace there, never raw buffer_pass.values reads.
- RT reflections: get_rt_reflection_weight(roughness) in common.hlsl is the single source of how much specular RT owns; hits share common_ray_surface.hlsl and common_radiance_cache.hlsl. Hit shading must apply cloud shadows and stream all lights through a weighted pick. Details: archive_query "rt reflection".
- RHI: setters only record, the pipeline resolves at Draw/Dispatch/TraceRays; BeginPass/EndPass is the only scope. Adding a StructuredBuffer/ConstantBuffer raises the Vulkan dynamic offset count (cap rhi_max_dynamic_offsets 16).
- Compressed mips cache in `<world>_resources/generated_cache/texture_mips` (bump bake_key after encoder changes); opaque colour maps use BC7. R16_Float speckles: check vertex_pack::float_to_half.
- Namespace-scope GPU resources need a shutdown() called from Renderer::Shutdown or VMA asserts at exit; log.txt names each leak.
- Bundled ImGui is 1.92.8+: AddRect/AddPolyline/PathStroke take (thickness, flags). `NoBringToFrontOnFocus` windows draw behind the dockspace.
- Ocean shore surf: data/shaders/common_ocean_shore.hlsl plus the CPU mirror ocean_shore:: in Renderer_Passes_Ocean.cpp; displacement/height changes go in BOTH. Details: archive_query "ocean".

## Validation and builds
- Vulkan: compute-queue barriers must not use FRAGMENT_SHADER stages; depth clears are graphics-queue only. VVL GPU-AV falsely rejects compacted BLAS (disabled in validation_layer::get_settings); if a validated frame differs, suspect the layer.
- D3D12 build beside Vulkan: copy tools/premake.lua to a scratch folder at repo depth 1 (dofile ../tools/..., OBJ_DIR ../binaries/obj_d3d12, location '.', drop setup.run), run '..\tools\premake5.exe --file=premake.lua vs2026 d3d12', MSBuild development x64. A texture that differs per backend is usually missing (D3D12 null srv is black, Vulkan shows the grey checkerboard).
- New cvars must be added to a section in get_sections (Settings.cpp). Rerun generate_project_files.bat after adding .cpp files.
- Crash dumps land in %LOCALAPPDATA%\CrashDumps; copy cdb to %TEMP% and run `cdb -z dump -y binaries -c ".ecxr; kn 40; q"`.
- 2026-09-30 a #!/usr/bin/env node shebang must be line 1, above the license header, or node refuses the file (assistant.mjs was broken this way). Run node --check on every tools/mcp/spartan_engine/*.mjs after edits.

## Performance and memory
- plan.world (RTX 5070 Ti, 1920x968): geometry passes ~13 of ~22 ms in forest views; proven lever impostor_screen_fraction in common_culling.hlsl. Non-wins: mesh LOD thresholds, VRS, dropping local-light shadow rays.
- Memory: log.txt logs 'Cpu memory', 'Gpu memory' and 'Gpu geometry' at world ready and steady; '\GPU Process Memory(pid_N*)\Dedicated Usage' is VRAM ground truth; SPARTAN_HEAP_CENSUS=1 enables a heap census. plan.world peak VRAM ~6.8 GiB, CPU ~6.6 GiB. Remaining candidate: 4 per-frame copies of meshlet_instances/visible_triangles/surviving_instances (~1 GB).

## Cars and racing AI
- Racing AI: source/car/RacingLine + source/car/AiDriver; car->SetAiDriver(make_unique<AiDriver>(line, settings)), nullptr hands controls back. Telemetry lines `ai_driver <car>:` and `racing_line:` in binaries/log.txt. Far AI cars need Physics::SetDistanceStreaming(false).
- When a car won't steer or weaves on a straight, compare the commanded angle with Simulation::get_wheel_dynamic_toe before tuning a controller; low-pass per-frame tire forces (~0.1 s).
- The race car copies the player prefab (race_driver `car_prefab`), whose prefab_override blocks hold the correct LaFerrari fit. Spectating: Car::Spectate; Car::IsViewed gates HUD, cameras and the single engine synth. F3 opens telemetry and the CARS panel.
- Spawned cars use generated materials car_<id>_<src id>_<role>; material_get those, not the model xml. IES lamp profiles: binaries/project/lights/ies (archive_query "ies").
- Audio mix (current): rain 0.05, engine AudioSource volume 0.8..1.0 with synth master_gain 1.6, tire squeal 0.18. Rain must stay far quieter than the car.
- MCP driving: holding brake at standstill engages reverse; release pedals and handbrake before vehicle_reset. Round trips are ~4 s, too slow for closed-loop steering.
