# Unified air and water transport

The camera samples one world-space density model into a frustum-aligned voxel grid. It replaces the old camera-dependent fog range, homogeneous water-color overlay, particle distance tint and underwater post-process. Surface foam remains a water material feature and receives the same camera transport as other surfaces.

The approach follows the frustum-volume architecture described in [Frostbite's unified volumetric lighting presentation](https://www.advances.realtimerendering.com/s2015/index.html). Voxels organize density and lighting; rendering still integrates light through them.

## Implementation

- A fixed 384 × 216 × 160 grid spans the camera to 32 km. The first 112 slices cover 64 metres; the first cell is about 6 cm deep. Moving across sea level never changes the grid mapping.
- Air density combines an altitude profile, terrain-relative ground mist, terrain flow/concavity/shelter and wind-advected 3D noise. Terrain lookup uses the mesh's triangle interpolation. This is procedural participating media, not a simulated weather fluid.
- RGB water absorption and scattering are distinct. A cell containing a water interface preserves both lighting sources and integrates air and water in their actual order. Partial-cell sampling uses the same transport equations. This matters when a distant water surface intersects a coarse cell.
- Distant interface reconstruction filters completed transport from adjacent columns at matching heights. Water lighting uses the Beer-weighted contribution centroid; visible portions of cells are sampled before the opaque surface. These prevent distant rings caused by blending air/water fractions or placing a lighting sample below the seabed.
- Directional and local lights retain shadow visibility; the sun also receives cloud shadows. Surface and volume caustics share the FFT wave focus and refracted sunlight absorption. Surface caustics modulate shadowed incident light instead of adding an unshadowed glow.
- Air lighting history rejects density changes, lighting changes and camera motion. Water lighting stays current to preserve moving caustics. Secondary/stereo views do not reuse another eye's history.
- Camera fog is composed after IBL, reflections and clouds. Transparent materials sample the segment behind their surface; final composition adds their foreground segment once. Particle billboards and smoke composition sample this same transport.

The renderer owns two air-light history textures, one water-light texture, one two-channel material texture, and separate RGB integrated scattering/transmission textures. Integrated textures include an identity boundary at the camera. This high-detail allocation uses approximately **558 MiB** (about **315 MiB more** than the previous fog grid), independent of render resolution.

## Controls

Render Options → Atmosphere exposes density (`r.fog`), altitude scale in metres (`r.fog.height`, default 300), ground mist (`r.fog.ground`, default 1) and wind-driven variation (`r.fog.variation`, default 0.65). Setting air density to zero does not disable water absorption. Water Clarity remains the water material control.

`r.fog.debug`: 0 normal, 1 scattering only, 2 transmission only, 3 surface lighting before foreground fog. Debug values go through the normal exposure/output pipeline.

## Validation

Run from the repository root:

```powershell
cmd /c tools\fog_tests\run.cmd
node tools/ocean_tests/compile_shaders.mjs
```

The transport harness executes production shared math on the CPU and a D3D11 GPU: 16,001 grid cases, 152 CPU transport cases and 12,288 GPU transport cases. It checks zero-distance identity, monotonic/invertible mapping, thin/dense Beer integration, subdivision invariance, ordered partial air/water cells, contribution centroids and interface reconstruction. All 16 fog/consumer Vulkan variants and all 12 ocean variants compile. The existing lighting shader suite also passes. These tests complement live rendering; they do not replace it.

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

Validated on 2026-09-08 using the development Vulkan build and RTX 5070 Ti at 1280 × 720. Live checks cover the Dreamcore ocean from above, within the moving waterline and below, with ray tracing enabled and disabled, plus a terrain/coast fixture at 650 metres altitude. Zero air density preserves water absorption and shafts. The source worlds remain unchanged. Captures are in `binaries/project/mcp/blockout/thumbnails/unified_fog_*.png`. The fog allocation is measured from texture dimensions/formats above. Per-pass timing snapshots intermittently returned zero or mismatched durations, so they are not used as a reliable performance benchmark.

## Limits

Lighting is sampled per voxel with analytic single-scattering integration, not spectral or fully coupled multiple scattering. Terrain mist follows the heightfield; it does not solve airflow around arbitrary meshes. Cloud and particle simulations keep their existing density representations. Refraction remains a screen-space approximation, while its participating-media transport is shared. Long-distance volumetric shadow edges can still show coarse grid steps near the horizon; the dense near-camera grid does not provide uniform world resolution. Very thin beams, moving shadows, off-screen reflection paths, stereo and extreme density settings require further scene-specific tuning.
