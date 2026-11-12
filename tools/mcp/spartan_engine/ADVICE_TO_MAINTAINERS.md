# Advice To Maintainers

What the AI agents working on Spartan Engine ask the maintainers to fix. One line per item: where it happens, what goes wrong or is missing, and what would fix it. Agents add items with `advice_to_maintainers_append` and remove the ones they have verified as fixed with `advice_to_maintainers_resolve`. This is not agent memory: no how-tos, no lessons, no session notes.

## Bugs
- A second `world_load` in one engine session blows up GPU memory, so every world switch needs an engine restart.
- A second play mode entry in one session has hit VK_ERROR_DEVICE_LOST during car renders.
- 2026-10-12 plan.world: `terrain_scatter_set` on a mesh layer rescatters 25.8M instances and has killed the engine with VK_ERROR_DEVICE_LOST. Wanted: a bounded or staged rescatter.
- 2026-10-17 The first `screenshot_take` after a world load or thumbnail work is black.
- 2026-10-17 `compound_create`: some new parts never render until they are cloned and the original deleted (not reproduced on 2026-11-12 with box and revolved parts).
- 2026-11-08 `material_semantic_create` and other generators move a requested `path` outside the generated resources into blockout/materials/<name>.xml; the receipt now reports it as `path_rewritten`. Wanted: decide whether a library path should be honored.
- 2026-11-06 Pausing the sequencer releases the cars and shows the HUD, so a shot can only be frozen with `engine_set_mode` pause; drive event cars cannot start mid-take, `sequencer_playback {time}` only seeks the camera timeline.
- A pending `asset_viewer_screenshot` can block `screenshot_take`.
- Static collision and raycasts only exist within 40-80 m of the active camera: entering play with the camera far from a car drops the car through the floor, and `world_raycast` misses distant ground.
- ModelImporter reuses `<model_dir>/<material>.xml` and its cached packed textures, so changed GLB textures do not show until the xml is deleted and the engine restarted.
- Exploratory Lua (API probing, `pairs()` on raw containers) has crashed the engine. Wanted: bindings that raise an error. Ids already pass as strings (`entity:GetObjectID()`, `World.GetEntityById`).
- `world_save` after play has crashed in `Render::UpdateLodIndices` on a freed car Mesh (probably the freed mesh bug fixed 2026-10-19, unverified).
- The GeometryInfo storage buffer is rewritten at offset 0 on every TLAS rebuild while older frames may still read it.
- ReSTIR hit shaders do not apply puddles; ReSTIR direct lacks cloud shadows, ocean transmission and grass contact shadows outside the TLAS.
- 2026-11-12 Shader hot reload: while a compute PSO recompiles (about 10 s after the shader itself) its pass is skipped, e.g. the water composite goes black. Wanted: keep the old pipeline until the new one is ready.
- 2026-11-12 Ocean contact foam searches the opaque depth on screen, so an object whose waterline is hidden (looking down on a capped pile) gets none. Wanted: a world space source such as a ray query or a distance field.

## UX Improvements
- No MCP tool closes a visible engine instance (`engine_quit` only closes headless ones); agents fall back to WM_CLOSE.
- No MCP tool clicks editor UI and `input_inject` keys never reach ImGui widgets: popups, the Terrain Sculpt brush and the Memory Viewer CSV export need the user.
- No MCP capture of the full audio mix; `engine_sound_capture` records only the engine synth.
- No camera follow for a moving car.
- New dedicated tools appear only after Cursor respawns the MCP server. Wanted: register new tools without a respawn.
- 2026-11-12 data/shaders is shared by every running instance: `shader_reload` without `name` recompiles everything another agent touched (about 160 shaders and 90 s after a common.hlsl edit) and pulls their half finished work into your instance. Wanted: a shader directory per instance, or reload only what this session changed.
- 2026-11-06 A road `shader_reload` takes about 4 minutes headless.
- Visible instances share binaries/logs/log.txt and settings/editor.ini. Wanted: one per instance.
- A minimized engine window stops rendering, GPU timings and screenshots. Wanted: keep rendering while an MCP session is attached.
- An editor window moved past the editor edge becomes a separate OS window that `screenshot_take {ui: true}` does not capture.
- 2026-11-06 Guide splines made with `spline_create_road` (mesh off) are live only and must be recreated after every reload.
- `texture_generate` at a loaded path writes the next free suffix (reported in `requested_path`) instead of overwriting and reloading the texture, and material `color_r/g/b` can only darken a surface.
