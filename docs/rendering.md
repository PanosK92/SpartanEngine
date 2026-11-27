# Rendering

The GPU-driven, bindless renderer supports hybrid deferred lighting, ReSTIR PT and a reference path tracer. It is written once in HLSL and C++ against the RHI, which has DirectX 12, Vulkan and Metal backends.

The public API is the `spartan::Renderer` static class in [`Renderer.h`](../source/rendering/Renderer.h). The whole frame is readable top to bottom in `Renderer::ProduceFrame()` in [`Renderer.cpp`](../source/rendering/Renderer.cpp).

## Files

| File | Contents |
| ---- | -------- |
| `Renderer.cpp` | `Initialize`, `Tick`, `ProduceFrame`, draw call building, material and light upload, acceleration structure updates |
| `Renderer_Definitions.h` | `Renderer_Shader`, `Renderer_RenderTarget`, `Renderer_Buffer` enums, budgets and tonemapper/upscaler enums |
| `Renderer_Resources.cpp` | Creation of every shader, render target, buffer and sampler. New shaders are registered here |
| `Renderer_ConsoleVariables.cpp` | The `r.*` [console variables](console_variables.md) |
| `Renderer_Passes_Geometry.cpp` | Culling, visibility pass, Hi-Z, material resolve (G-buffer), shadows, meshlets, grass |
| `Renderer_Passes_Lighting.cpp` | Light clustering, deferred lighting, IBL, fog, flares, ReSTIR |
| `Renderer_Passes_Sky.cpp` | Sky, atmosphere and cloud LUTs, volumetric clouds |
| `Renderer_Passes_Ocean.cpp` | FFT ocean |
| `Renderer_Passes_Post.cpp` | SSAO, anti-aliasing and upscaling, the post-processing chain |
| `Renderer_Passes_Editor.cpp` | Grid, gizmo lines, selection outline, icons, text |
| `GeometryBuffer.cpp` | The global vertex, index, meshlet and instance buffers |
| `Material.cpp` | Material properties and XML |
| `IesProfile.cpp` | IES LM-63 photometric profiles |
| `Decals.md` | [Design notes](../source/rendering/Decals.md) for decals, vehicle dirt and paint scratches |

## The frame

Before recording, `Renderer::Tick` streams textures, uploads changed geometry and materials, rebuilds the draw call list and fills the frame constant buffer. `ProduceFrame` then records work on three queues (graphics, compute A, compute B) synchronized with timeline semaphores:

1. **Shader hot reload** applies any recompiled shaders.
2. **Compute A**: update ray tracing acceleration structures, assign lights to clusters, update sky and cloud LUTs.
3. If `r.path_tracing` is on, the **reference path tracer** replaces everything below.
4. **Simulation passes**: wind field, vegetation interaction, terrain deformation (tire ruts), ocean wake, car rain, FFT ocean.
5. **Variable rate shading** image.
6. **Sky**.
7. **Geometry**, two-phase occlusion culling and visibility buffer:
   - Early cull against last frame's Hi-Z, **visibility pass** (`Pass_Visibility`) writing depth and a 32-bit `visibility_buffer` (encoding meshlet instance + triangle ID or impostor instance ID), build an early Hi-Z.
   - Late cull of what the early pass rejected against the new Hi-Z, late **visibility pass**, build the final Hi-Z.
   - Grass population and GPU-scattered grass, then **material resolve** (`Pass_Material_Resolve`): `visibility_classify` classifies 8×8 tiles into opaque, alpha-tested and impostor indirect dispatch lists (masking pixels already shaded by scatter-first grass), and three specialized compute dispatches reconstruct attributes with analytic screen-space derivatives and evaluate the shared `g_buffer.hlsl` material pipeline once per visible pixel. Tessellated geometry and skid marks draw onto the resolved G-buffer. Setting `r.visibility_buffer 0` (or running wireframe/vertices views, stereo XR or non-mesh-shader hardware) falls back to the legacy depth prepass + G-buffer raster pass.
8. **Hybrid lighting** computes ambient occlusion, shadows, deferred lighting, image-based lighting, transparent lighting and reflections according to the hybrid settings.
9. **ReSTIR PT** uses opaque and transparent G-buffer surfaces to initialize paths, resamples their direct and indirect contributions across frames and neighboring pixels, and composites the denoised lighting with visible emission and sky. Selecting this mode in Render Options bypasses the hybrid surface lighting passes. The saved hybrid settings apply again when switching back.
10. Cloud composition, light flares and volumetric fog follow surface lighting.
11. **Particles**.
12. **Anti-aliasing and upscaling**: TAAU, DLSS, XeSS or FSR.
13. **Post-processing**: depth of field, motion blur, lens rain, auto exposure, bloom, tonemapping, then dithering, sharpening (CAS), film grain, VHS, chromatic aberration and the cinematic overlay.

With XR active, lighting through post-processing runs once per eye.

## GPU-driven, bindless design

The CPU does not loop over draws. It uploads data once and issues one indirect draw per pass.

- **Global geometry**: every mesh is appended to a single vertex buffer and a single index buffer, plus meshlet and instance buffers (`GeometryBuffer`). Shaders pull vertices from these buffers directly, bypassing the input assembler, and ray tracing reads the same data.
- **Per-draw data**: `Renderer::WriteDrawData` fills one bindless storage buffer with transforms, material index and flags (up to 32,768 draws). Push constants carry only pass parameters and an index.
- **Materials and lights**: `UpdateMaterials` and `UpdateLights` write `MaterialParameters` and `LightParameters` arrays. Material textures are one bindless texture array.
- **Culling**: compute shaders cull instances and meshlets (frustum, Hi-Z occlusion, backface cones) and write indirect arguments; the CPU issues indirect draws (`DrawMeshTasksIndirect` with mesh shaders when `r.mesh_shaders` is active, otherwise `DrawIndirect` with vertex pulling).
- **Visibility buffer and material resolve**: when `r.visibility_buffer` is active on the mesh-shader path, geometry is rasterized once in `Pass_Visibility` into `gbuffer_depth` and a 32-bit `R32_Uint` `visibility_buffer`. `Pass_Material_Resolve` then runs an 8×8 tile classifier (`visibility_classify.hlsl`) and three indirect compute resolve passes (`visibility_resolve.hlsl` for opaque, alpha-tested and impostor tiles) that compute homogeneous adjoint barycentrics and analytic UV/world/normal derivatives directly from `GeometryBuffer`, sharing `evaluate_gbuffer()` in `g_buffer.hlsl` with zero 2×2 pixel-quad overshading and no duplicate geometry pass.
- **Layouts**: CPU and GPU share structure definitions through [`data/shaders/shared_buffers.h`](../data/shaders/shared_buffers.h). Bindless register spaces are declared in `common_resources_bindless.hlsl`.

To add a pass: declare it in `Renderer.h`, register its shader in `Renderer::CreateShaders()` and any render targets in `CreateRenderTargets()`, implement it in the relevant `Renderer_Passes_*.cpp` using the existing `Pass_*` functions as templates, and call it from `ProduceFrame`.

## RHI

[`source/rhi`](../source/rhi) abstracts the graphics API. The shared interface is `RHI_Device` (with plain POD capabilities via `RHI_Device::Caps()`), `RHI_Queue`, `RHI_CommandList` (pass setters, `CopyBuffer`, barriers and draw/dispatch), `RHI_Buffer`, `RHI_Texture`, `RHI_Shader`, `RHI_PipelineState` (with value enums `RHI_BlendMode`, `RHI_DepthMode`, `RHI_RasterizerMode`), `RHI_SwapChain`, `RHI_AccelerationStructure`, `RHI_Sampler` and sync types. Vulkan-only descriptor set caching lives in `rhi/vulkan/`. Typed renderer slot binding helpers (`Renderer::BindTexture`, `BindBuffer`, `BindConstantBuffer`, `BindAccelerationStructure`) live in `Renderer_Internal.h` so `source/rhi` stays independent of renderer enums. Each backend implements the RHI in its own folder:

| Backend | Folder | Shader path |
| ------- | ------ | ----------- |
| DirectX 12 | `d3d12/` | HLSL to DXIL with DXC; Agility SDK when present |
| Vulkan | `vulkan/` | HLSL to SPIR-V with DXC |
| Metal 4 | `metal/` | HLSL through DXC and the Metal Shader Converter |
| WebGPU | `webgpu/` | HLSL to SPIR-V with DXC, then WGSL with Tint; the browser reads a prebuilt WGSL cache, see [WebGPU and the web](webgpu.md) |

The backend is chosen at build time by the `API_GRAPHICS_*` define; [`RHI_Implementation.h`](../source/rhi/RHI_Implementation.h) maps it to backend headers. Only the selected backend folder is compiled.

Also in the RHI: `RHI_TextureStreaming` (mip residency within a memory budget), `RHI_VendorTechnology` (DLSS, XeSS), `RHI_Fsr` (FSR upscaling), `RHI_FrameGeneration` (FSR 3.1 frame generation), `RHI_Nrd` (NVIDIA Real-time Denoisers for ReSTIR).

## Shaders

All shaders are HLSL in [`data/shaders`](../data/shaders):

| Group | Files |
| ----- | ----- |
| Common | `common.hlsl`, `common_structs.hlsl`, `common_resources*.hlsl`, `common_vertex_processing.hlsl`, `common_culling.hlsl`, `shared_buffers.h` |
| Geometry | `depth_prepass.hlsl`, `visibility_classify.hlsl`, `visibility_resolve.hlsl`, `g_buffer.hlsl`, `meshlet_*.hlsl`, `impostor_mesh.hlsl`, `indirect_cull*.hlsl`, `instance_cull.hlsl`, `grass_*.hlsl` |
| Lighting | `light.hlsl`, `light_composition.hlsl`, `light_image_based.hlsl`, `light_cluster*.hlsl`, `shadow_*.hlsl` |
| Ray tracing | `reflections_*.hlsl`, `ray_traced_shadows.hlsl`, `restir_*.hlsl`, `path_tracing.hlsl`, `nrd/` |
| Atmosphere | `sky/`, `fog*.hlsl`, `particles*.hlsl` |
| Ocean | `ocean/` |
| Post | `taau.hlsl`, `bloom.hlsl`, `auto_exposure.hlsl`, `output.hlsl`, `amd_fidelity_fx/` |

Shaders compile asynchronously at start-up; compiled results are cached in `binaries/cache/`. They hot reload: edit a file (in the editor's **Shader Editor**, or externally followed by the MCP `shader_reload` command) and the new version is swapped in at the start of the next frame. ReSTIR shaders reload as a group and reset their history.

When the executable sits inside a source tree, the engine compiles straight from the repository's `data/shaders`, so edits there need no setup step. This is the `UseRootShaderDirectory` entry in `settings/spartan.xml` (on by default); turn it off to use the copy in `binaries/data/shaders`. Packaged games always use their own copy.

## Lights

[`Light`](../source/world/components/Light.h) supports **directional**, **point**, **spot** and **area** lights.

- **Physical units**: intensity in lux (directional) or lumens (point, spot, area), color from temperature in kelvin. Presets (`LightIntensity`) cover common bulbs through to stadium lights.
- **IES profiles**: spot lights can use an LM-63 profile for a measured beam, sampled from a shared atlas (up to 16 profiles) in direct lighting, volumetric fog and path tracing.
- **Shadows**: one 8192² shadow atlas with per-light regions, filtered with penumbra estimation, rendered GPU-driven. Ray-traced shadows replace it when enabled; screen-space shadows add contact detail.
- **Clustered shading**: lights are assigned to a logarithmic-Z cluster grid on the GPU, so many local lights cost near-constant time per pixel.
- The directional light drives the sun, sky, time of day and clouds.

## Camera

[`Camera`](../source/world/components/Camera.h) is physically based: aperture (f-stop), shutter speed and ISO determine exposure (EV100) in manual mode; automatic mode adapts from scene luminance with compensation. Aperture and shutter speed also drive depth of field and motion blur. Tonemappers are ACES, AgX, Reinhard, ACES Nautilus and Gran Turismo 7 (default), with HDR10 output.

## Materials

Materials are XML files ([`Material.cpp`](../source/rendering/Material.cpp)): a `<Material>` root, one child element per property, and a `<textures>` block.

- **Core PBR**: color, roughness, metalness, normal, height (with optional tessellation), occlusion, emission, alpha cutoff and blending.
- **Extended**: clearcoat, anisotropy, sheen, subsurface scattering, index of refraction, absorption and thickness for transmissive and thin-walled surfaces.
- **Automotive paint**: metallic flakes, pearl, coat tint and iridescence, with presets (`MaterialPaintPreset`).
- **Mapping**: UV tiling and offset, world-space UVs.
- **Special surfaces**: terrain, grass blades, foliage and flowers with wind, water, roads and skid marks.

**Glass and transparent surfaces.** A material is transparent when its color alpha (`color_a`) is below 1.
- With `alpha_blend` off, the surface is glass: the background is refracted and gets a Fresnel reflection on top.
  - `ior` sets how strongly light bends: 1.5 for window glass, 1.33 for water.
  - `thickness` is the glass thickness in metres. Below 0.05 the surface is treated as a thin pane: light passes straight through and only the Fresnel reflection remains, so give anything that should visibly bend light at least 0.05 (the curtains in `physics_example.world` use 0.08).
  - `roughness` frosts what is seen through it, and `absorption` tints it with distance through the glass.
  - The normal map bends the refraction too; keep its strength low (around 0.1) unless a rippled, crinkled look is wanted.
  - A color texture multiplies the tint, so a dark texture makes dark glass.
- With `alpha_blend` on, the surface is simply blended, with no refraction.

Texture slots (`MaterialTextureType`): color, roughness, metalness, normal, occlusion, emission, height, alpha mask and a packed slot, four layers each. Source images are compressed (Compressonator) and mipmapped on the GPU (FidelityFX SPD) at load time rather than baked offline.

Create materials in the editor, with `Material.New()` in [Lua](scripting.md), or with the MCP `material_create` and `material_textured_create` commands.

## Upscaling and frame generation

Selected with `r.antialiasing_upsampling`: none, FXAA, TAAU, XeSS, DLSS or FSR. FSR 4 is chosen automatically on supported Radeon GPUs and FSR 3.1 elsewhere. FSR 3.1 frame generation (`r.frame_generation`) works on any vendor's GPU on Vulkan and DirectX 12. Dynamic resolution (`r.dynamic_resolution`) and variable rate shading (`r.variable_rate_shading`) are independent of the upscaler.

## Debugging

- The **Renderer Options** panel exposes every feature toggle and debug view.
- `r.meshlet_visualize` and `r.cluster_visualize` show meshlets and light clusters; `r.wireframe`, `r.aabb` and `r.physics` draw debug geometry.
- The **Texture Viewer** inspects any render target and mip.
- `debug.renderdoc`, `debug.breadcrumbs`, `debug.validation_layer` and `debug.gpu_marking` take effect on restart.
- **F12** saves a screenshot.
