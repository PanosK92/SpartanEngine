# Console variables

Console variables (cvars) are the engine's settings: renderer features, debug views, physics threading, weather effects. They are defined in C++, set from the console, Lua, the MCP bridge or a world file, and persisted to `binaries/settings/spartan.xml`.

## Declaring

A cvar is a static `TConsoleVar<T>` in a `.cpp` file ([`ConsoleCommands.h`](../source/commands/console/ConsoleCommands.h)); construction registers it. `T` is `int32_t`, `float`, `bool` or `std::string`.

```cpp
TConsoleVar<float> cvar_bloom("r.bloom", 0.04f, "bloom intensity", on_bloom_change);
```

The optional last argument is a callback run when the value changes. Read with `cvar_bloom.GetValue()` or `GetValueAs<bool>()`; expose it to other files with `extern TConsoleVar<float> cvar_bloom;` in a header.

By name, through the registry:

```cpp
ConsoleRegistry::Get().SetValueFromString("r.bloom", "0.1");
std::string value = ConsoleRegistry::Get().GetValueAsString("r.bloom");
```

## Setting

| From | How |
| ---- | --- |
| Editor console | `r.bloom` prints the value, `r.bloom 0.1` sets it |
| Renderer Options panel | Toggles and sliders for most `r.*` cvars |
| Lua | `Console.Set("r.bloom", "0.1")` (the value is a string), `Console.Get("r.bloom")` |
| MCP bridge | `cvar_get`, `cvar_set`, `cvar_list` |
| A world | `<ConsoleVariables><Variable name="r.bloom" value="0.1"/></ConsoleVariables>`, applied on load |
| `settings/spartan.xml` | Loaded at start-up, before the GPU device exists; saved on exit |

Cvars marked startup-only (most `debug.*`) take effect on the next launch.

### World-owned cvars

Some cvars belong to the world rather than the user, and are reset on every world load so one world does not inherit another's look:

- `r.atmosphere.mist_*`: reset to defaults, then set by the world's `ConsoleVariables` and saved with it.
- `r.fog.debug`: always reset to off.
- `r.watermark`: reset to off and not saved to `spartan.xml`. Only Plan turns it on, from `project/plan_resources/scripts/plan.lua`.

## Reference

Grouped by subsystem. Each cvar's description string, shown in the console and the Renderer Options panel, is the authoritative documentation.

### Debug (`source/core/Settings.cpp`)

| Cvar | Default | Purpose |
| ---- | ------- | ------- |
| `debug.validation_layer` | off | Graphics API validation (startup) |
| `debug.gpu_assisted_validation` | off | GPU-assisted validation (startup) |
| `debug.breadcrumbs` | off | GPU breadcrumbs for crash tracing (startup) |
| `debug.renderdoc` | off | RenderDoc integration (startup) |
| `debug.gpu_marking` | off | Debug markers for GPU captures (startup) |
| `debug.gpu_timing` | on | GPU timestamp queries for the profiler (startup) |
| `debug.shader_optimization` | on | Shader compiler optimizations |
| `debug.log_to_file` | off | Write `logs/log.txt` |
| `debug.steam` | off | Initialize Steamworks (startup) |
| `debug.d3d12_enhanced_barriers` | off | DirectX 12 enhanced barriers |
| `debug.d3d12_async_compute` | on | Real async compute queue on DirectX 12 |

### Renderer: lighting and global illumination

`r.ssao`, `r.ray_traced_reflections`, `r.ray_traced_reflections_checkerboard`, `r.ray_traced_shadows`, `r.rt_light_culling`, `r.restir_pt`, `r.restir_pt_async`, `r.restir_pt_direct`, `r.restir_pt_emissive_pool`, `r.restir_pt_scale`, `r.restir_pt_reference`, `r.path_tracing`, `r.path_tracing_bounces`.

### Renderer: atmosphere

`r.atmosphere.mist_density`, `r.atmosphere.mist_height`, `r.atmosphere.ground_mist`, `r.atmosphere.mist_variation`, `r.fog.debug`. The legacy names `r.fog`, `r.fog.height`, `r.fog.ground` and `r.fog.variation` map to the mist cvars.

### Renderer: post-processing

`r.bloom`, `r.bloom_scatter`, `r.motion_blur`, `r.depth_of_field`, `r.film_grain`, `r.chromatic_aberration`, `r.vhs`, `r.lens_rain`, `r.dithering`, `r.sharpness`, `r.tonemapping` (ACES, AgX, Reinhard, ACES Nautilus, Gran Turismo 7), `r.watermark`, `r.watermark_interval`.

### Renderer: resolution, upscaling and output

`r.antialiasing_upsampling` (none, FXAA, TAAU, XeSS, DLSS, FSR), `r.dlss_reactivity`, `r.frame_generation`, `r.resolution_scale`, `r.dynamic_resolution`, `r.variable_rate_shading`, `r.hdr`, `r.gamma`, `r.vsync`.

### Renderer: geometry and culling

`r.mesh_shaders`, `r.hiz_occlusion`, `r.hiz_depth_bias`, `r.gpu_lod`, `r.meshlet_cull_skinned`, `r.tessellation_gpu_driven`, `r.shadows_gpu_driven`, `r.foliage_impostors`, `r.tree_wind_cache_entries`.

### Renderer: grass and vegetation

`r.grass_specialized`, `r.grass_lod_pixels`, `r.grass_impostor_distance`, `r.grass_impostor_thin`, `r.grass_impostor_start`, `r.grass_track_radius`, `r.grass_track_recovery`, `r.vegetation_interaction_frequency`, `r.vegetation_interaction_damping`.

### Renderer: texture streaming

`r.texture_streaming`, `r.texture_streaming_budget_mb`, `r.texture_streaming_auto_budget`, `r.texture_streaming_upload_mb`.

### Editor overlays and debug views

`r.grid`, `r.transform_handle`, `r.transform_snap`, `r.transform_snap_translate`, `r.transform_snap_rotate`, `r.transform_snap_scale`, `r.selection_outline`, `r.entity_icons`, `r.volumes`, `r.performance_metrics`, `r.screenshot_ui`, `r.aabb`, `r.picking_ray`, `r.physics`, `r.ragdoll`, `r.wireframe`, `r.meshlet_visualize`, `r.cluster_visualize`, `r.cluster_visualize_cap`.

### World, physics, audio and game

| Cvar | Defined in |
| ---- | ---------- |
| `world.spatial_render` | `world/World.cpp` |
| `physics.threaded`, `physics.assist` | `physics/PhysicsWorld.cpp` |
| `terrain.deformation`, `terrain.deformation_softness`, `terrain.deformation_resistance`, `terrain.deformation_debug` | `world/TerrainDeformation.cpp` |
| `ocean.wake`, `ocean.wake_strength`, `ocean.wake_damping`, `ocean.wake_depth` | `world/OceanWake.cpp` |
| `audio.ambience_volume` | `world/components/AudioSource.cpp` |
| `car.dirt`, `car.dirt_moisture`, `car.dirt_airspeed`, `car.scratch` | `car/CarDirt.cpp`, `car/CarWeather.cpp` |
| `game.tree_respawn_distance`, `game.tree_strength_scale` | `world/BreakableTrees.h` |
| `showroom.turntable_speed` | `car/CarHud.cpp` |

To list every registered cvar with its current value, use the MCP `cvar_list` command or search the source for `TConsoleVar`.
