# Unified air and water transport

The camera samples one world-space density model into a frustum-aligned voxel grid. It replaces the old camera-dependent fog range, homogeneous water-color overlay, particle distance tint and underwater post-process. Surface foam remains a water material feature and receives the same camera transport as other surfaces.

The approach follows the frustum-volume architecture described in [Frostbite's unified volumetric lighting presentation](https://www.advances.realtimerendering.com/s2015/index.html). Voxels organize density and lighting; rendering still integrates light through them.

## Implementation

- A fixed 512 × 288 × 192 grid spans the camera to 32 km. The first 144 slices cover 64 metres; the first cell is about 5 cm deep. This is 2.13 times the former cell count. Moving across sea level never changes the grid mapping.
- Air density combines an altitude profile, terrain-relative ground mist, terrain flow/concavity/shelter and wind-advected 3D noise. Terrain lookup uses the mesh's triangle interpolation. This is procedural participating media, not a simulated weather fluid.
- RGB water absorption and scattering are distinct. A cell containing a water interface preserves both lighting sources and integrates air and water in their actual order. Partial-cell sampling uses the same transport equations. This matters when a distant water surface intersects a coarse cell.
- Distant interface reconstruction filters completed transport from adjacent columns at matching heights and rescales optical path length. Its transition follows the projected footprint and rejects columns across the horizon. Interface fractions use 32-bit floats: half precision can shift the boundary by metres in far cells. Water lighting uses the Beer-weighted contribution centroid.
- Injection does not read or clamp to opaque screen depth. Long air cells average four world-space samples and filter unresolved density noise, avoiding object silhouettes and camera-dependent density oscillation. Raster consumers compensate for projection jitter before sampling the unjittered grid.
- Directional visibility combines ray tracing with an alpha-tested raster atlas for excluded terrain scatter. Both render scheduling and off-screen caster collection retain that atlas in RT mode. Its near cascade includes all casters, so nearby fog uses that complete detailed map without redundant TLAS traces. Outside the near interior, RT supplies solid meshes and the atlas supplies excluded scatter. Leaf cutouts use their actual wind-displaced world position. The detailed cascade extends 64 metres from the camera and blends into the far cascade instead of an unshadowed ring. Surface lighting combines the same visibility sources.
- Shadow submission rejects individual scatter instances outside their authored shadow distance or light slice, using bounds expanded for wind. Surviving contiguous instance runs keep the same geometry, LOD, transforms and cutouts. Drawing entire tiles spent tens of milliseconds on trees that could not contribute.
- Air cells average two direct-light samples (four in long cells); smooth ambient skylight is evaluated once. Nearby water cells average two positions, each integrating eight FFT caustic irradiance samples over its projected footprint. Sunlight uses the same wave focus and refracted absorption as surface caustics; only unresolved distant detail averages to its mean.
- Air lighting history reprojects the grid centre, preserving stationary texel identity, leaves mixed air/water cells current, and rejects substantial density/lighting changes. Water radiance uses a separate short 60 ms history with coverage, camera-cut and lighting-change rejection; interface coverage stays current. Ocean sunlight remains active when a directional light's atmospheric-volumetrics flag is disabled. Secondary/stereo views do not reuse another eye's history.
- Camera fog is composed after IBL, reflections and clouds. Transparent materials sample the segment behind their surface; final composition adds their foreground segment once. Particle billboards and smoke composition sample this same transport.
- Distant water refraction integrates four optically weighted intervals from the same injected water-source texture instead of subtracting quantized camera integrals. Filtering normalizes water coverage, samples above the physical seabed and averages unresolved caustics to their mean. Where a far cell exceeds the visible water column, its representative lighting is anchored to the ocean entry rather than the moving slice boundary. The near-camera grid retains detailed shafts.

The renderer owns two air-light history textures, two water-light history textures, one RG32 material texture, and separate RGB integrated scattering/transmission textures. Integrated textures include an identity boundary at the camera. This allocation uses **1514.25 MiB**, independent of render resolution, versus 608.77 MiB before the quality upgrade. The higher spatial resolution and independent water history deliberately spend more memory on detail.

Final quality captures on the RTX 5070 Ti at 1280 × 720: `quality_forest_verified` has 11 fresh GPU samples, with median fog injection 8.49 ms and shadow maps 0.30 ms. The earlier same-view fog injection was 3.53 ms, with no foliage shadow atlas. The final frame-rate snapshot was about 44 FPS versus 51 before the upgrade. `quality_water_verified` has 19 fresh samples, median injection 10.62 ms and a roughly 73 FPS snapshot. These are individual views with evolving wind/clouds, not a gameplay benchmark. The intermediate whole-tile shadow pass cost 29.67 ms; per-instance culling removes that cost without reducing the retained leaf geometry.

## Controls

Render Options → Atmosphere exposes density (`r.fog`), altitude scale in metres (`r.fog.height`, default 300), ground mist (`r.fog.ground`, default 1) and wind-driven variation (`r.fog.variation`, default 0.65). Setting air density to zero does not disable water absorption. Water Clarity remains the water material control.

`r.fog.debug`: 0 normal, 1 scattering only, 2 transmission only, 3 surface lighting before foreground fog. Debug values go through the normal exposure/output pipeline.

## Validation

Run from the repository root:

```powershell
cmd /c tools\fog_tests\run.cmd
node tools/ocean_tests/compile_shaders.mjs
```

The transport harness executes production shared math on the CPU and a D3D11 GPU: 16,001 grid cases, 152 CPU transport cases and 16,384 GPU transport cases. It checks zero-distance identity, monotonic/invertible mapping, thin/dense Beer integration, subdivision invariance, ordered partial air/water cells, contribution centroids, interface reconstruction and grazing-angle optical-path rescaling. It also compiles all 24 fog/consumer/alpha-shadow Vulkan variants. These tests complement live rendering; they do not replace it.

For isolated live validation, build with a relative intermediate directory:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' spartan.vcxproj /p:Configuration=development /p:Platform=x64 /p:PreferredToolArchitecture=x64 '/p:IntDir=binaries\fog_tests\obj\' '/p:OutDir=binaries\fog_tests\' /p:TargetName=spartan_fog /m /nologo /verbosity:minimal
& ./tools/fog_tests/prepare_live.ps1
```

The isolated runtime needs the usual engine DLLs and data/project resources. Copy `binaries/spartan.xml` beside the isolated executable **before launching it** so settings lookup cannot save into the main editor's file. Start it with `--mcp-control --mcp-port=47779`. The live helper only targets that port:

```powershell
node tools/fog_tests/live.mjs water
node tools/fog_tests/live.mjs status
node tools/fog_tests/live.mjs view below
node tools/fog_tests/live.mjs capture fog_underwater.png
node tools/fog_tests/live.mjs water_raster
node tools/fog_tests/live.mjs landscape
node tools/fog_tests/live.mjs status
node tools/fog_tests/live.mjs view landscape
node tools/fog_tests/live.mjs capture fog_landscape.png
node tools/fog_tests/live.mjs profile
```

Wait for loading to finish before positioning the camera, and allow frames for exposure/history to settle before capture. Inspect above/below/surface views, shoreline silhouettes and distant ridges. Also compare zero air density, water clarity, volumetric local lights and enclosed rooms.

The reduced landscape fixture is only a quick terrain diagnostic. For regressions, load the actual `worlds/dreamcore.world` or `worlds/plan.world` into the isolated editor, then run `node tools/fog_tests/regressions.mjs water` or `mountains`. The water sequence includes disabling atmospheric sun beams in memory, an open-sea horizon and the waterline. The mountain sequence translates 800 metres in 5-metre steps with fixed rotation and captures five positions. Neither sequence saves the world. Repeat with ray tracing enabled and disabled in the isolated settings.

Shutdown regression: load the full plan, enter play mode, verify `grass_interaction` appears in the GPU profiler, then close the isolated editor normally. `Renderer::DestroyResources` resets the pass state before allocator destruction, releasing grass fields/contacts/body buffers. The allocator assertion remains enabled and now reports names/sizes if another owner leaks.

`node tools/fog_tests/quality.mjs water <label>` or `forest <label>` captures a fixed view, six evolving frames and an eight-second recording with fresh GPU timestamps. Load the corresponding actual world first. The forest check explicitly enables the sun's volumetric flag in memory. The saved plan sun now also enables it; earlier captures needed the override. Use `tools/forest_perf/summarize_recording.py` on the resulting CSV. Ordinary profiler snapshots are not accepted per-pass timing evidence. `node tools/fog_tests/regressions.mjs forest` checks 101 camera positions toward the sun through the canopy. The intermediate `quality_forest_after` still omitted the atlas at the scheduler; `quality_forest_final` lacked per-instance shadow culling. Both are superseded by `quality_forest_verified`.

Validated on 2026-09-08 using the development Vulkan build and RTX 5070 Ti at 1280 × 720. Regression checks use the actual Dreamcore ocean above/below/at the waterline and the full plan coastline with an 800-metre moving camera path. A full-plan play/close cycle exercised grass interaction and exited without the allocator assertion. Captures are in `binaries/project/mcp/blockout/thumbnails/fog_regression_*.png`; the shutdown log and GPU snapshot are in `binaries/fog_tests`. The tests never save worlds. The only authored world change is enabling the saved plan sun's volumetric flag (3 to 7), so edit-mode canopy shafts are visible too. The fog allocation is measured from texture dimensions/formats above. Per-pass timing snapshots intermittently returned zero or mismatched durations, so they are not used as a reliable performance benchmark.

## Limits

Lighting is sampled per voxel with analytic single-scattering integration, not spectral or fully coupled multiple scattering. Terrain mist follows the heightfield; it does not solve airflow around arbitrary meshes. Cloud and particle simulations keep their existing density representations. Refraction remains a screen-space approximation, while its participating-media transport is shared. Long-distance volumetric shadow edges can still show coarse grid steps near the horizon; the dense near-camera grid does not provide uniform world resolution. Very thin beams, moving shadows, off-screen reflection paths, stereo and extreme density settings require further scene-specific tuning.
