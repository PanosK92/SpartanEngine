<p align="center">
  <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp" alt="Spartan Engine" width="100%"/>
</p>

<h2 align="center">Path traced. GPU driven. Started before AI and developed alongside me while in the game industry</h2>

<p align="center">
  <sub>12 YEARS &nbsp;·&nbsp; 1 ENGINEER &nbsp;·&nbsp; WINDOWS AND LINUX &nbsp;·&nbsp; DIRECTX 12 AND VULKAN</sub>
</p>

<p align="center">
  <a href="https://github.com/PanosK92/SpartanEngine/actions"><img src="https://github.com/PanosK92/SpartanEngine/actions/workflows/workflow.yml/badge.svg" alt="Build Status"></a>
  <a href="https://discord.gg/TG5r2BS"><img src="https://img.shields.io/discord/677302405263785986?logo=discord&label=Discord&color=5865F2&logoColor=white" alt="Discord"></a>
  <a href="https://github.com/PanosK92/SpartanEngine/blob/master/license.md"><img src="https://img.shields.io/badge/license-Spartan_Engine-blue.svg" alt="License"></a>
</p>

<p align="center">
  <a href="https://www.youtube.com/watch?v=TMZ0epSVwCk"><b>Trailer</b></a> &nbsp;·&nbsp;
  <a href="https://github.com/PanosK92/SpartanEngine/releases"><b>Download</b></a> &nbsp;·&nbsp;
  <a href="https://github.com/PanosK92/SpartanEngine/blob/master/plan.md"><b>The Plan</b></a> &nbsp;·&nbsp;
  <a href="https://github.com/PanosK92/SpartanEngine/wiki"><b>Wiki</b></a> &nbsp;·&nbsp;
  <a href="https://discord.gg/TG5r2BS"><b>Discord</b></a> &nbsp;·&nbsp;
  <a href="https://x.com/panoskarabelas"><b>X</b></a> &nbsp;·&nbsp;
  <a href="https://panoskarabelas.com/"><b>Website</b></a>
</p>

Spartan is a real-time C++ engine with path-traced global illumination, hardware ray tracing, a bindless GPU-driven renderer and a racing-grade vehicle simulation. Its rendering technology ships in **Godot Engine** and **S.T.A.L.K.E.R. Anomaly**, it features in a **published programming book**, and more than **600 engineers** follow its development on Discord. The source is open, but the engine is not a product: no tutorials, no support queue, no compatibility promise. The code is the documentation, and it moves fast.

<table>
  <tr>
    <td colspan="3"><a href="https://www.youtube.com/watch?v=TMZ0epSVwCk"><img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/video_promo.png" alt="Watch the Spartan Engine trailer"/></a></td>
  </tr>
  <tr>
    <td width="33%"><img src=".github/images/showcase_island_town.jpg" alt="Zakynthos: an open island some 30 km across, with towns, an airport, more than 200 spline roads, instanced forests and an FFT ocean"/></td>
    <td width="33%"><img src=".github/images/showcase_island_coast.jpg" alt="Island coast: dynamic time of day, atmospheric scattering and shallow-water caustics"/></td>
    <td width="33%"><img src=".github/images/showcase_ferrari.jpg" alt="Ferrari LaFerrari on a turntable, lit by three emissive tubes through volumetric fog"/></td>
  </tr>
  <tr>
    <td><img src=".github/images/showcase_sponza.jpg" alt="Sponza: ReSTIR path tracing, no light probes, no bake"/></td>
    <td><img src=".github/images/showcase_liminal.jpg" alt="Liminal space: procedural and streamed, lit by path-traced global illumination"/></td>
    <td><img src=".github/images/showcase_ferrari_close.jpg" alt="Showroom: layered car paint with clearcoat and ray-traced reflections"/></td>
  </tr>
  <tr>
    <td colspan="3" align="center"><sub>Real-time captures. Zakynthos: an island some 30 km across with towns, an airport and 200+ spline roads &nbsp;·&nbsp; Sponza and the liminal office: path traced, no bake &nbsp;·&nbsp; Showroom: layered clearcoat paint and ray-traced reflections. Every world is fully simulated: walkable, interactive and drivable.</sub></td>
  </tr>
</table>

## Features

<table>
  <tr>
    <td width="33%" valign="top"><b>No bake.</b><br/><sub>ReSTIR path-traced global illumination every frame. No probes, no lightmaps.</sub></td>
    <td width="33%" valign="top"><b>One draw per pass.</b><br/><sub>The GPU owns the data. Bindless buffers, GPU culling, a single indirect draw.</sub></td>
    <td width="33%" valign="top"><b>200 Hz.</b><br/><sub>Racing-sim vehicle dynamics inside a general-purpose engine.</sub></td>
  </tr>
</table>

<details>
<summary><b>Lighting and global illumination</b> &nbsp;·&nbsp; ReSTIR, ray tracing, IES profiles, clouds, fog, FFT ocean</summary>

<br/>

- **ReSTIR path tracing** with spatiotemporal reservoir resampling for real-time multi-bounce global illumination
- **Clustered deferred shading** with a GPU-built logarithmic-Z grid and cone-vs-AABB culling for spots, so many local lights cost near-constant time per pixel
- **IES photometric light profiles** (LM-63): spot lights emit the measured beam of a real fixture, from car headlights with regulation cut-offs to architectural downlights, with correct lumens and candela across direct lighting, volumetric fog and path tracing. A feature usually reserved for the big commercial engines
- **Hardware ray-traced reflections and shadows** via ray queries
- **Atmospheric scattering** and image-based lighting with bent normals
- **Volumetric clouds** (Nubis-style) baked into the sky panorama, with cumulus and cirrus layers, multi-scatter lighting and aerial perspective
- **Froxel volumetric fog**: one view-aligned volume for height and distance fog, sun shafts and underwater caustic shafts, with temporal reprojection and shadowing
- **FFT ocean** with a Tessendorf spectrum and multi-cascade IFFT in compute, driving a camera-following clipmap with choppy displacement, dynamic normals and crest foam
- **Screen-space shadows** (inspired by Days Gone) and **XeGTAO** ambient occlusion
- **Shadow map atlas** with fast filtering and penumbra estimation

</details>

<details>
<summary><b>Rendering architecture</b> &nbsp;·&nbsp; bindless, GPU-driven, meshlets, universal HLSL</summary>

<br/>

Geometry, materials, textures, lights, transforms and bounds live in persistent, globally accessible buffers. No per-draw descriptor updates, no per-draw resource binding, no CPU-side draw loops.

- **Zero-binding draw path**: all per-draw data lives in a single bindless storage buffer, push constants carry only an index
- **Single global vertex and index buffer** for all geometry (inspired by id Tech), with **vertex pulling** that bypasses the Input Assembler and is shared by rasterization and ray tracing
- **GPU-driven indirect rendering** with per-meshlet frustum, Hi-Z occlusion and backface cone culling; the CPU issues a single `DrawIndexedIndirectCount` per pass
- **Meshlet clustering** via meshoptimizer, with mesh shaders when available and a vertex-pulling fallback
- **Bindless everything**: materials, lights, samplers, uber shaders, minimal PSO permutations
- **Universal HLSL** compiled for both Vulkan (SPIR-V) and DirectX 12
- **GPU-side asset processing**: mip generation (FidelityFX SPD) and texture compression (Compressonator) at load time, not baked offline
- **Unified deferred rendering**: opaque and transparent surfaces share the same BSDF and render path
- **Async compute** for SSAO and screen-space shadows, in parallel with shadow rasterization

</details>

<details>
<summary><b>Performance, camera and post-processing</b> &nbsp;·&nbsp; DLSS 4.5, XeSS, FSR 3.1 frame generation, TAAU, VRS, GT7 tonemapping, HDR10</summary>

<br/>

- **NVIDIA DLSS 4.5** Super Resolution (SDK 310.9.1) and **Intel XeSS** 2.0.2 (SDK 3.0.2)
- **AMD FSR 3.1 frame generation** (FidelityFX SDK 1.1.4) on Vulkan and D3D12, works on any vendor's GPU, with frame-generation-aware fps, latency and frame rate limiting
- **TAAU**: temporal anti-aliasing with built-in upsampling, Halton jitter and variance-clipped history
- **Variable rate shading** and **dynamic resolution scaling**
- **Foliage impostors** as the final GPU LOD, so dense forests stay cheap at distance
- **Texture streaming** within a fixed memory budget
- **Custom GPU breadcrumbs** for crash tracing and post-mortem debugging
- **Physically based camera** with auto-exposure and physical light units (lux, lumens and kelvin)
- **Tonemappers**: ACES, AgX and Gran Turismo 7 (default), with HDR10 output
- **Post-processing**: bloom, motion blur, depth of field, chromatic aberration, film grain and sharpening (CAS)

</details>

<details>
<summary><b>Vehicle dynamics at 200 Hz</b> &nbsp;·&nbsp; Pacejka tires, thermal brakes, LSD, aero, force feedback</summary>

<br/>

A full vehicle dynamics model running inside the PhysX fixed-timestep loop. Not a gameplay approximation: racing-sim fidelity inside a general-purpose engine.

| System              | Details                                                                                            |
| ------------------- | -------------------------------------------------------------------------------------------------- |
| **Tires**           | Pacejka MF 5.2 with combined slip, thermal model, pressure, wear and multiple surfaces             |
| **Suspension**      | Convex hull sweep contact, spring-damper, anti-roll bars, bump stops, bump steer, camber and toe   |
| **Weight transfer** | Geometric and elastic lateral split via roll centre heights and roll stiffness                     |
| **Drivetrain**      | Engine torque curve, turbo, 7-speed gearbox, rev-match, open, locked and limited-slip differentials, RWD, FWD and AWD |
| **Brakes**          | Thermal model with fade, front/rear bias and slip-threshold ABS                                    |
| **Aerodynamics**    | Drag, front and rear downforce, ground effect, DRS and rolling resistance                          |
| **Steering**        | Ackermann geometry, high-speed reduction and self-aligning torque                                  |
| **Assists**         | ABS, traction control and handbrake                                                                |
| **Integration**     | Semi-implicit Euler with consolidated net torque per wheel                                         |
| **Input**           | Controllers and steering wheels with haptic feedback                                               |
| **Camera**          | GT7-inspired chase camera with speed-based dynamics                                                |

</details>

<details>
<summary><b>Engine systems</b> &nbsp;·&nbsp; physics, animation, particles, weather, roads, Lua, audio, editor, profiler, VR</summary>

<br/>

| System               | Details                                                                                           |
| -------------------- | ------------------------------------------------------------------------------------------------- |
| **Platforms**        | Windows (DirectX 12 and Vulkan) and Linux (Vulkan), with prebuilt binaries in every **[release](https://github.com/PanosK92/SpartanEngine/releases)** |
| **Physics**          | PhysX with rigid bodies, character kinematics and vehicle dynamics                                |
| **Animation**        | Skeletal animation with keyframed clips, crossfade blending, four-bone skinning and two-bone IK with ground-aware foot planting |
| **Particles**        | GPU-driven compute emission and simulation, depth-buffer collision and soft blending              |
| **Weather**          | Rain that soaks exposed surfaces, fills puddles and keeps covered areas dry, feeding tire grip    |
| **Procedural roads** | Catmull-Rom spline roads with extruded profiles, terrain conforming, instanced props and path followers |
| **Scripting**        | Lua 5.4 via Sol2 with the full engine API and lifecycle callbacks                                 |
| **Audio**            | 3D positional audio, streaming, reverb and procedural engine synthesis via SDL3                   |
| **Cinematics**       | Camera cut sequencer with entity tracking, spline-driven movement and exact cut timing            |
| **Entity system**    | Component-based with transform hierarchies, prefabs and XML serialization                         |
| **Threading**        | Hardware-aware thread pool with parallel loops and nested parallelism detection                   |
| **Editor**           | Hierarchy, asset browser, inspector, script and shader editors, gizmos, profiler and memory viewer |
| **Profiling**        | Nsight/RGP-style timeline with graphics and async compute lanes, RenderDoc integration and a GPU/CPU memory fragmentation map |
| **Asset import**     | 40+ model formats (Assimp), 30+ image formats (FreeImage), 10+ font formats (FreeType)            |
| **VR (WIP)**         | OpenXR with multiview single-pass stereo on Vulkan and DirectX 12                                 |

</details>

## Build

Prebuilt binaries ship with every **[release](https://github.com/PanosK92/SpartanEngine/releases)**. From source, see the **[Building Guide](https://github.com/PanosK92/SpartanEngine/wiki/Building)**.

```powershell
# Windows: then open spartan.slnx (use d3d12 for DirectX 12, or no arguments for a menu)
generate_project_files.bat vs2026 vulkan
```

```bash
# Linux
tools/linux_dependencies.sh && ./generate_project_files.sh gmake vulkan && make config=release_x64
```

Worlds live in `worlds/` as XML; their assets live in `binaries/project/`, excluded from Git and downloaded during setup. Start reading at **[Engine.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/core/Engine.cpp)**, the entry point, and the **[Lua Scripting Guide](https://github.com/PanosK92/SpartanEngine/wiki/Scripting)** for gameplay code.

<details>
<summary><b>Build targets and architecture notes</b></summary>

<br/>

Project generation creates these targets, all using the selected graphics backend:

| Target               | What it is                                                           | Executable                                   |
| -------------------- | -------------------------------------------------------------------- | -------------------------------------------- |
| **`Spartan`**        | The editor                                                           | `spartan_<backend>_development.exe`          |
| **`SpartanRuntime`** | The game and HUD, without editor widgets                             | `spartan_<backend>_runtime_development.exe`  |

The runtime targets take an existing world with `--world <absolute-world-path>` and start play once it is prepared. Resource paths remain relative to the executable, as in the editor. The editor's `-game` option hides its panels.

With dependencies already installed, regenerate without downloading them:

```powershell
tools/premake5.exe --file=tools/premake.lua --skip-setup vs2026 vulkan
```

**Engine and world boundary.** There is no game layer in C++. `source/world/WorldSystems.cpp` installs the world lifecycle hooks: camera controls, vehicle render submissions, breakable trees and the optional world systems (ambient wildlife, ambient atmosphere, road details, layered road surfaces). Vehicle physics and state live under `source/car`, vehicle MCP commands in `source/mcp/McpCommandsCar.cpp`, and traffic, race drivers, route drivers, car resets and skid marks are regular components in `source/world/components`. The optional world systems are off by default; a world turns them on through the `WorldSystems` Lua table from its companion script, the `.lua` with the same name next to the `.world` (`worlds/plan.lua` for `plan.world`), which runs on every load before the roads are built. Worker model imports publish their entity hierarchy through `World::EntityBatch::Commit()` after construction succeeds.

**Physics.** Scene stepping and contact observation use game-independent callbacks; the game installs vehicle contact handling. Vehicle state is opaque to engine consumers, while the existing `Physics` authoring and Lua API remains compatible.

**Asset ownership.** World saves record native asset ownership in `OwnedResources`. Automatic cleanup only removes unreferenced files recorded there; unlisted files, shared libraries, sculpt data and generated caches keep their separate lifetimes. Legacy worlds acquire ownership of referenced assets when saved and do not prune unrelated legacy files.

**Component attributes.** Attributes exposed for editing go through the component's setters. Raw copy-only or derived fields are read-only; use the named authoring properties for edits.

**Play lifecycle.** Play startup captures all authored transforms before any component starts, with both phases spread across frames. Stopping play drains completed worker batches before restoration and removal. World unload cancels staged work and clears renderer scene references and temporal history while preserving device lookup tables.

</details>

## In the Wild

- **Godot Engine** integrates Spartan's TAA ([source](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2))
- **S.T.A.L.K.E.R. Anomaly** ships a rendering addon built on Spartan's source ([ModDB](https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders))
- **Jesse Guerrero's [beginner programming book](https://www.amazon.com/dp/B0CXG1CMNK)** features Spartan's code and community
- **Origin**: a portfolio piece at the [University of Thessaly](https://en.wikipedia.org/wiki/University_of_Thessaly) under Professor [Fotis Kokkoras](https://ds.uth.gr/en/staff-en/faculty-en/kokkoras/)

Projects built on Spartan code can be featured here. **[Get in touch](https://x.com/panoskarabelas)**.

## Community

**[Discord](https://discord.gg/TG5r2BS)**: rendering deep dives and work in progress, 600+ engineers &nbsp;·&nbsp; **[Contribute](https://github.com/PanosK92/SpartanEngine/wiki/Contributing)**: contributors get [exclusive perks](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor) designed to accelerate learning &nbsp;·&nbsp; **[Sponsor](https://github.com/sponsors/PanosK92)**: funds the hosting behind the one-click setup &nbsp;·&nbsp; **Podcast**: *Exploring the tech world and beyond*, conversations across cutting-edge industries, on [YouTube](https://youtu.be/OZRwCZhglsQ) and [Spotify](https://open.spotify.com/show/5F27nWKn9TZdClc5db9efY)

## License

Free for noncommercial use under the **[Spartan Engine License 1.0](license.md)**. **Commercial use requires prior written permission and negotiated payment terms.**

<details>
<summary>License summary</summary>

<br/>

The license permits free noncommercial use, modification, and redistribution, including reuse of covered code in other projects. Keep the copyright notice identifying **Panos Karabelas** and the license with redistributed code; compiled distributions must carry them in documentation or accessible legal notices.

Commercial fees, royalties, or a combination are agreed case by case, with no automatic rate or revenue threshold. This applies to the engine and covered code copied into other games, engines, libraries, tools, or applications. Contact [Panos Karabelas](https://panoskarabelas.com/) to discuss your project.

Third-party components retain their own licenses. Copies and versions previously released under MIT retain their existing permissions. See [license.md](license.md) for the complete terms.

</details>
