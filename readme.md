<p align="center">
  <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp" alt="Spartan Engine"/>
</p>

<p align="center">
  <strong>A game engine with a fully bindless, GPU-driven renderer featuring real-time path-traced global illumination, hardware ray tracing, and a physics simulation running at 200Hz — built over 10+ years of R&D</strong>
</p>

<p align="center">
  <a href="https://github.com/PanosK92/SpartanEngine/actions"><img src="https://github.com/PanosK92/SpartanEngine/actions/workflows/workflow.yml/badge.svg" alt="Build Status"></a>
  <a href="https://discord.gg/TG5r2BS"><img src="https://img.shields.io/discord/677302405263785986?logo=discord&label=Discord&color=5865F2&logoColor=white" alt="Discord"></a>
  <a href="https://github.com/PanosK92/SpartanEngine/blob/master/license.md"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
</p>

<p align="center">
  <a href="https://discord.gg/TG5r2BS">Discord</a> •
  <a href="https://x.com/panoskarabelas">X</a> •
  <a href="https://github.com/PanosK92/SpartanEngine/wiki">Wiki</a> •
  <a href="https://github.com/PanosK92/SpartanEngine/issues">Issues</a>
</p>

---

## The Engine

Spartan started as a university project and has been in active development for over a decade, growing into a community of **600+ members** on Discord — including industry professionals sharing knowledge and pushing boundaries together. Its rendering technology has been adopted by **Godot Engine** and **S.T.A.L.K.E.R. Anomaly**, and featured in a **published programming book**.

**There's a destination that gives all this tech a purpose.** Curious? **[Read the plan →](https://github.com/PanosK92/SpartanEngine/blob/master/plan.md)**

---

## See It In Action

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

| Sponza 4K | Subway |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_sponza.png"/><br>Classic Dubrovnik building—ideal for path tracing | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_subway.jpg"/><br>Emissive materials & GI testing |

| Minecraft | Liminal Space |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_minecraft.jpg"/><br>A familiar blocky world | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_liminal.jpg"/><br>Reality shifts to a nearby frequency |

| Showroom | [The Plan](plan.md) |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_showroom.png"/><br>Clean showcase—no experimental tech | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_car_playground.png"/><br>A neon-soaked city, built to push the engine toward real-time path-traced driving through rain-slicked streets at 60fps, that's the plan. |

---

## Rendering

The renderer is built around a single principle: **the GPU owns the data**. Every resource — geometry, materials, textures, lights, transforms, AABBs — lives in persistent, globally accessible buffers. There are no per-draw descriptor set updates, no per-draw resource binding, and no CPU-side draw loops.

### Architecture

- **Zero-binding draw path** — all per-draw data is stored in a single bindless storage buffer; push constants carry only a 4-byte index into it, keeping the entire push constant footprint at 80 bytes
- **Single global vertex and index buffer** for all geometry (inspired by id Tech) — the CPU never re-binds geometry between draws
- **GPU-driven indirect rendering** — a compute shader performs frustum and occlusion culling entirely on the GPU, emitting a compacted indirect argument buffer; the CPU issues a single `DrawIndexedIndirectCount` per pass, replacing thousands of individual draw calls
- **Bindless materials, lights, and samplers** — all accessed through global descriptor arrays with no per-object binding
- **Uber shaders** — minimal pipeline state object (PSO) permutations eliminate draw call state changes
- **Universal HLSL** — all shaders are written once in HLSL and compiled for both Vulkan (via SPIR-V) and DirectX 12
- **Tightly packed 10-byte instance format** — enables hundreds of millions of instances (procedural grass, foliage)
- **On-the-fly GPU mip generation** (FidelityFX SPD) **and texture compression** (FidelityFX Compressonator) — assets are processed on the GPU at load time, not baked offline
- **Unified deferred rendering with transparency** — opaque and transparent surfaces share the same BSDF and render path, no separate forward pass

### Lighting and Global Illumination

- **ReSTIR path tracing** — reservoir-based spatiotemporal resampling for real-time multi-bounce global illumination, denoised via NVIDIA ReLAX (NRD)
- **Ray-traced reflections and shadows** via hardware ray queries
- **Atmospheric scattering** with real-time filtering and image-based lighting with bent normals
- **Volumetric fog** with temporal reprojection
- **Volumetric clouds** with procedural noise generation and shadow casting
- **Screen-space shadows** (inspired by Days Gone) and **ambient occlusion** (XeGTAO with visibility bitfield)
- **Shadow map atlas** with fast filtering and penumbra estimation

### Performance and Upscaling

- **GPU-driven frustum and occlusion culling** (Hi-Z) — the CPU never touches per-object visibility
- **Variable rate shading** and **dynamic resolution scaling**
- **Upscaling** with Intel XeSS 2 and AMD FSR 3
- **Temporal anti-aliasing** and **FXAA**
- **Custom breadcrumbs** for GPU crash tracing and post-mortem debugging

### Camera and Post-Processing

- Physically based camera with auto-exposure and physical light units (lumens and kelvin)
- Tonemappers: ACES, AgX, Gran Turismo 7 (default)
- HDR10 output
- Bloom, motion blur, depth of field, chromatic aberration, film grain, sharpening (CAS)

---

## Car Simulation

A full vehicle dynamics simulation running at **200Hz** for precise tire and suspension response.

| System | Details |
|--------|---------|
| **Tires** | Pacejka magic formula, combined slip, load sensitivity, temperature model, camber thrust, relaxation length, multiple surfaces (asphalt, concrete, wet, gravel, grass, ice) |
| **Suspension** | 7-ray contact patch per wheel, spring-damper with bump/rebound split, anti-roll bars, camber/toe alignment, bump steer |
| **Drivetrain** | Piecewise engine torque curve, 7-speed gearbox (auto/manual), rev-match downshifts, LSD with preload, turbo with wastegate, engine braking |
| **Brakes** | Thermal model (cold/optimal/fade zones), front/rear bias, ABS with configurable slip threshold |
| **Aerodynamics** | Drag with frontal/side area, front/rear downforce, ground effect, yaw-dependent forces, pitch-dependent balance, rolling resistance |
| **Steering** | Ackermann geometry, high-speed reduction, non-linear response, self-aligning torque |
| **Assists** | ABS, traction control, handbrake lock |
| **Input** | Controllers with analog throttle/brake/steering, haptic feedback (tire slip, ABS, drifting) |
| **Camera** | GT7-inspired chase camera with speed-based dynamics and orbit controls |
| **Debug** | Raycast, suspension, and aero force visualization with telemetry logging |

---

## Engine Systems

| System | Details |
|--------|---------|
| **Physics** | PhysX integration with rigid bodies, character kinematics, and vehicle physics |
| **Scripting** | Lua 5.4 with full engine API access — entities, components, math, physics, audio — via Sol2 bindings with lifecycle callbacks (Start, Tick, Save, Load) |
| **Audio** | 3D positional audio, streaming, reverb, and procedural synthesis via SDL3 — supports WAV, OGG, MP3, FLAC, and more |
| **Input** | Keyboard, mouse, controllers, and steering wheels with haptic feedback |
| **Entity system** | Component-based architecture with transform hierarchies, prefabs, and XML serialization |
| **Threading** | Custom thread pool with hardware-aware scaling, parallel loops, nested parallelism detection, and progress tracking |
| **Profiling** | CPU and GPU profiling with scoped time blocks, draw call and binding stats, stutter detection, and RenderDoc integration |
| **Asset import** | 40+ model formats (via Assimp), 30+ image formats (via FreeImage), 10+ font formats (via FreeType), mesh optimization (via meshoptimizer) |
| **Editor** | ImGui-based editor with world hierarchy, asset browser, property inspector, script and shader editors, texture viewer, viewport with transform gizmos, profiler, and console |

---

## Getting Started

### Building

One-click project generation—see the **[Building Guide](https://github.com/PanosK92/SpartanEngine/wiki/Building)** for details.

### Learning the Engine

Start with **[Game.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/runtime/Game/Game.cpp)** — it shows how default worlds are loaded and is the best entry point for understanding the engine's structure. For gameplay scripting, check out the **[Lua Scripting Guide](https://github.com/PanosK92/SpartanEngine/blob/master/scripting.md)** — it covers the full API, lifecycle callbacks, and examples.

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

## Community & Support

### Contributing

Contributors get access to **[exclusive perks](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor)** designed to accelerate learning and skill development.

**[Read the Contributing Guide →](https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md)**

### Sponsorship

I cover the costs for Dropbox hosting to ensure library and asset bandwidth is available. If you enjoy running a single script and having everything download, compile, and work seamlessly, please consider **[becoming a sponsor](https://github.com/sponsors/PanosK92)**. Direct sponsorship helps more than Discord boosts—it goes directly into maintaining and improving the project.

---

## Projects Using Spartan

| Project | Description |
|---------|-------------|
| **Godot Engine** | Integrates Spartan's TAA ([view source](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2)) |
| **S.T.A.L.K.E.R. Anomaly** | Rendering addon using Spartan's source ([ModDB](https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders)) |
| **Programming Book** | Jesse Guerrero's [beginner programming book](https://www.amazon.com/dp/B0CXG1CMNK) features Spartan's code and community |
| **University Thesis** | Originally created as a portfolio piece at [University of Thessaly](https://en.wikipedia.org/wiki/University_of_Thessaly) with Professor [Fotis Kokkoras](https://ds.uth.gr/en/staff-en/faculty-en/kokkoras/) |

**Using code from Spartan?** [Reach out](https://twitter.com/panoskarabelas), I'd love to showcase your project!

---

## License

**[MIT License](https://github.com/PanosK92/SpartanEngine/blob/master/license.md)** — free to use with attribution.
