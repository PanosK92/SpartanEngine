<p align="center">
  <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp" alt="Spartan Engine"/>
</p>

<h3 align="center">One engineer. Ten years. A bindless, GPU-driven engine with real-time path-traced global illumination.</h3>

<p align="center">
  <em>A personal R&D engine, not a commercial product. No roadmap promises, no support queue, no compromises on the vision.</em>
</p>

<p align="center">
  <a href="https://github.com/PanosK92/SpartanEngine/actions"><img src="https://github.com/PanosK92/SpartanEngine/actions/workflows/workflow.yml/badge.svg" alt="Build Status"></a>
  <a href="https://discord.gg/TG5r2BS"><img src="https://img.shields.io/discord/677302405263785986?logo=discord&label=Discord&color=5865F2&logoColor=white" alt="Discord"></a>
  <a href="https://github.com/PanosK92/SpartanEngine/blob/master/license.md"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
</p>

<p align="center">
  <a href="https://discord.gg/TG5r2BS">Discord</a> &nbsp;&middot;&nbsp;
  <a href="https://x.com/panoskarabelas">X</a> &nbsp;&middot;&nbsp;
  <a href="https://github.com/PanosK92/SpartanEngine/wiki">Wiki</a> &nbsp;&middot;&nbsp;
  <a href="https://github.com/PanosK92/SpartanEngine/issues">Issues</a>
</p>

---

## The Engine

Spartan started as a university project. Ten years of nights and weekends later, its rendering technology runs in **Godot Engine** and **S.T.A.L.K.E.R. Anomaly**, ships in a **published programming book**, and anchors a community of **600+** engineers on Discord.

It is built around one philosophy: **favor real-time over baked, dynamic over static, modern over safe.** Every system here exists to serve that.

**There is a destination that gives all this tech a purpose.** Curious? **[Read the plan →](https://github.com/PanosK92/SpartanEngine/blob/master/plan.md)**

---

## The Trailer

[![Engine Trailer](https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/video_promo.png)](https://www.youtube.com/watch?v=TMZ0epSVwCk)

---

## Worlds

<img align="left" width="420" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_selection_4.png"/>

Launch the engine and choose from a selection of default worlds. Each is physics-enabled—walk around, pick up objects with your mouse, or take a car for a spin.

<br clear="left"/>

### Forest

<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_forest.jpg"/>

The most demanding world: **256 million** procedurally generated grass blades (inspired by Ghost of Tsushima), spanning **64.1 km²** covered with thousands of trees and rocks.

### More Worlds

| Sponza 4K | Basic |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_sponza.png"/><br>Classic Dubrovnik building—ideal for path tracing | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_cornell.jpg"/><br>Contains some render test objects |

| Liminal Space | Showroom |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_liminal_a.png"/><br>Reality shifts to a nearby frequency | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_showroom_a.png"/><br>Clean showcase—no experimental tech |

| [The Plan](plan.md) |
|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_car_playground.png"/><br>A neon-soaked city, built to push the engine toward real-time path-traced driving through rain-slicked streets at 60fps, that's the plan. |

---

## Rendering

The renderer is built around one principle: **the GPU owns the data.** Every resource (geometry, materials, textures, lights, transforms, AABBs) lives in persistent, globally accessible buffers. No per-draw descriptor updates, no per-draw resource binding, no CPU-side draw loops.

### Architecture

- **Zero-binding draw path**, all per-draw data lives in a single bindless storage buffer, push constants carry only an index
- **Single global vertex and index buffer** for all geometry (inspired by id Tech), with **vertex pulling** that bypasses the Input Assembler and is shared by rasterization and ray tracing
- **GPU-driven indirect rendering** with per-meshlet frustum, Hi-Z occlusion, and backface cone culling, the CPU issues a single `DrawIndexedIndirectCount` per pass
- **Meshlet clustering** via meshoptimizer, no mesh shader dependency
- **Bindless everything**: materials, lights, samplers, uber shaders, minimal PSO permutations
- **Universal HLSL** compiled for both Vulkan (SPIR-V) and DirectX 12
- **GPU-side asset processing**: mip generation (FidelityFX SPD) and texture compression (Compressonator) at load time, not baked offline
- **Unified deferred rendering**, opaque and transparent surfaces share the same BSDF and render path
- **Async compute** for SSAO, screen-space shadows, and cloud shadows, parallel with shadow rasterization

### Lighting and Global Illumination

- **ReSTIR path tracing** with spatiotemporal reservoir resampling for real-time multi-bounce global illumination
- **Hardware ray-traced reflections and shadows** via ray queries
- **Atmospheric scattering** and image-based lighting with bent normals
- **Volumetric fog and clouds** with temporal reprojection and shadow casting
- **Screen-space shadows** (inspired by Days Gone) and **XeGTAO** ambient occlusion
- **Shadow map atlas** with fast filtering and penumbra estimation

### Performance and Upscaling

- **Variable rate shading** and **dynamic resolution scaling**
- **TAAU**, our own temporal anti-aliasing with built-in upsampling, Halton-jittered with variance-clip history reprojection
- **Intel XeSS 2** upscaling
- **FXAA**
- **Custom breadcrumbs** for GPU crash tracing and post-mortem debugging

### Camera and Post-Processing

- Physically based camera with auto-exposure and physical light units (lumens and kelvin)
- Tonemappers: ACES, AgX, Gran Turismo 7 (default)
- HDR10 output
- Bloom, motion blur, depth of field, chromatic aberration, film grain, sharpening (CAS)

---

## Car Simulation — 200Hz

A full vehicle dynamics simulation running inside the PhysX fixed-timestep loop.

| System              | Details                                                                                            |
| ------------------- | -------------------------------------------------------------------------------------------------- |
| **Tires**           | Pacejka MF 5.2 with combined slip, thermal model, pressure, wear, multiple surfaces                |
| **Suspension**      | Convex hull sweep contact, spring-damper, anti-roll bars, bump stops, bump steer, camber/toe       |
| **Weight transfer** | Geometric + elastic lateral split via roll center heights and roll stiffness                       |
| **Drivetrain**      | Engine torque curve, turbo, 7-speed gearbox, rev-match, open/locked/LSD differentials, RWD/FWD/AWD |
| **Brakes**          | Thermal model with fade, front/rear bias, slip-threshold ABS                                       |
| **Aerodynamics**    | Drag, front/rear downforce, ground effect, DRS, rolling resistance                                 |
| **Steering**        | Ackermann geometry, high-speed reduction, self-aligning torque                                     |
| **Assists**         | ABS, traction control, handbrake                                                                   |
| **Integration**     | Semi-implicit Euler with consolidated net-torque per wheel                                         |
| **Input**           | Controllers and steering wheels with haptic feedback                                               |
| **Camera**          | GT7-inspired chase camera with speed-based dynamics                                                |

---

## Engine Systems

| System            | Details                                                                                           |
| ----------------- | ------------------------------------------------------------------------------------------------- |
| **Particles**     | GPU-driven with compute emission and simulation, depth-buffer collision, soft blending            |
| **Animation**     | Skeletal hierarchies with keyframed clips and four-bone vertex skinning                           |
| **Physics**       | PhysX with rigid bodies, character kinematics, and vehicle dynamics                               |
| **Scripting**     | Lua 5.4 via Sol2 with full engine API and lifecycle callbacks                                     |
| **Audio**         | 3D positional audio, streaming, reverb, procedural synthesis via SDL3                             |
| **Input**         | Keyboard, mouse, controllers, and steering wheels with haptic feedback                            |
| **Entity system** | Component-based with transform hierarchies, prefabs, and XML serialization                        |
| **Threading**     | Hardware-aware thread pool with parallel loops and nested parallelism detection                   |
| **VR (WIP)**      | OpenXR with Vulkan multiview single-pass stereo across the full pipeline                          |
| **Profiling**     | Nsight/RGP-style timeline with separate graphics and async compute lanes, RenderDoc integration   |
| **Asset import**  | 40+ model formats (Assimp), 30+ image formats (FreeImage), 10+ font formats (FreeType)            |
| **Editor**        | ImGui-based with hierarchy, asset browser, inspector, script and shader editors, gizmos, profiler |

---

## Getting Started

### Building

One-click project generation—see the **[Building Guide](https://github.com/PanosK92/SpartanEngine/wiki/Building)** for details.

### Learning the Engine

Start with **[Game.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/runtime/Game/Game.cpp)**, it shows how default worlds are loaded and is the best entry point for understanding the engine's structure. For gameplay scripting, check out the **[Lua Scripting Guide](https://github.com/PanosK92/SpartanEngine/blob/master/scripting.md)**, it covers the full API, lifecycle callbacks, and examples.

---

## Podcast

<table>
  <tr>
    <td><img width="400" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/refs/heads/master/.github/images/podcast.png"/></td>
    <td>
      <strong>Exploring the tech world and beyond</strong><br><br>
      Meeting the brightest minds across cutting-edge industries.<br><br>
      📺 <a href="https://youtu.be/OZRwCZhglsQ">Watch on YouTube</a><br>
      🎧 <a href="https://open.spotify.com/show/5F27nWKn9TZdClc5db9efY">Listen on Spotify</a>
    </td>
  </tr>
</table>

---

## Community

### Contributing

Contributors get access to **[exclusive perks](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor)** designed to accelerate learning and skill development.

**[Read the Contributing Guide →](https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md)**

### Sponsorship

I cover the Dropbox hosting that makes the one-click setup work. If Spartan has taught you something, or if you just want to fuel another decade of this, **[sponsorship](https://github.com/sponsors/PanosK92)** goes directly into keeping the lights on and the project moving.

---

## Projects Using Spartan

| Project                    | Description                                                                                                                                                                                                                      |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Godot Engine**           | Integrates Spartan's TAA ([view source](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2))                                   |
| **S.T.A.L.K.E.R. Anomaly** | Rendering addon using Spartan's source ([ModDB](https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders))                                                                                                         |
| **Programming Book**       | Jesse Guerrero's [beginner programming book](https://www.amazon.com/dp/B0CXG1CMNK) features Spartan's code and community                                                                                                         |
| **University Thesis**      | Originally created as a portfolio piece while studying at the [University of Thessaly](https://en.wikipedia.org/wiki/University_of_Thessaly) with Professor [Fotis Kokkoras](https://ds.uth.gr/en/staff-en/faculty-en/kokkoras/) |

**Using code from Spartan?** [Reach out](https://twitter.com/panoskarabelas), I'd love to showcase your project!

---

## License

**[MIT License](https://github.com/PanosK92/SpartanEngine/blob/master/license.md)**, free to use with attribution.
