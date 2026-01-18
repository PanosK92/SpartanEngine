<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp"/>

<p align="center">
  <a href="https://github.com/PanosK92/SpartanEngine/actions"><img src="https://github.com/PanosK92/SpartanEngine/actions/workflows/workflow.yml/badge.svg" alt="Build Status"></a>
  <a href="https://discord.gg/TG5r2BS"><img src="https://img.shields.io/discord/677302405263785986?logo=discord&label=Discord&color=5865F2&logoColor=white" alt="Discord"></a>
  <a href="https://github.com/PanosK92/SpartanEngine/blob/master/license.md"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
</p>

# Overview

Spartan Engine is a research-focused game engine developed over 10+ years, designed for experimentation and pushing the boundaries of rendering and simulation. While it started as a personal learning project, it has evolved into an active community of 600+ members on Discord, including industry professionals sharing knowledge and exploring advanced technology.

<p align="center">
  <a href="https://discord.gg/TG5r2BS">Discord</a> •
  <a href="https://x.com/panoskarabelas">X</a> •
  <a href="https://github.com/PanosK92/SpartanEngine/wiki">Wiki</a> •
  <a href="https://github.com/PanosK92/SpartanEngine/issues">Issues</a>
</p>

# Worlds

<img align="left" width="450" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_selection_4.png"/>

Upon launching the engine, you'll be greeted with a selection of default worlds to load. Each world is physics-enabled, allowing you to walk around, pick objects using your mouse, and even drive a car.
<br clear="left"/>

### Forest

<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_forest.jpg"/>

The most advanced and demanding world featuring **256 million** procedurally generated grass blades (inspired by Ghost of Tsushima), spanning **64.1 km²** covered in thousands of trees and rocks.

### Other Worlds

| Sponza 4K | Subway |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_sponza.png"/><br>True-to-life scale recreation of the Sponza building from Dubrovnik | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_subway.jpg"/><br>Emissive materials and GI test |

| Minecraft | Liminal Space |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_minecraft.jpg"/><br>A classic Minecraft world | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_liminal.jpg"/><br>Shifts your frequency to a nearby reality |

| Showroom |
|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_showroom.png"/><br>Car showroom - ideal for YouTubers/press as it doesn't use experimental tech |

# Features

### Rendering

**Renderer**
- Cutting-edge Vulkan renderer with DirectX 12 backend (WIP)
- Fully bindless design (materials, lights, samplers)
- Universal HLSL shaders across both backends
- 128-byte push constant buffer for fast CPU-to-GPU transfer
- Tightly packed 10-byte instance format handling hundreds of millions of instances
- On-the-fly GPU mip generation (FidelityFX SPD) and texture compression (FidelityFX Compressonator)
- Unified deferred rendering with transparency (BSDF with same render path)

**Lighting & Shadows**
- Atmospheric scattering, real-time filtering, IBL with bent normals
- Screen-space shadows (from Days Gone) and ambient occlusion (XeGTAO + visibility bitfield)
- Ray-traced reflections
- Fast shadow mapping with penumbra via shadow map atlas
- Volumetric fog

**Performance & Upscaling**
- Variable rate shading and dynamic resolution scaling
- Upscaling: XeSS 2 & FSR 3
- Temporal anti-aliasing
- Breadcrumbs for GPU crash tracing (FidelityFX Breadcrumbs)

**Camera & Post-Processing**
- Physically based camera with auto-exposure
- Physical light units (lumens & kelvin)
- Frustum & occlusion (Hi-Z) culling
- Tonemappers: ACES, AgX, Gran Turismo 7 (default)
- HDR10 output
- FXAA, bloom, motion blur, depth of field, chromatic aberration

### Car Simulation

- Pacejka tire model with combined slip, load sensitivity, and low speed stability
- Tire thermodynamics with slip heating, rolling heat, airflow cooling, and grip windows
- Multi-ray suspension with spring damper dynamics and anti-roll bars
- Full drivetrain: engine torque curve, clutch, engine braking, automatic gearbox
- Limited slip differential with preload and asymmetric accel/decel locking
- Traction control, ABS, and handbrake-induced rear slip
- Aerodynamics: drag, rolling resistance, front/rear downforce

### General

- **Input**: Keyboard, mouse, controllers, steering wheels
- **Physics**: Comprehensive PhysX integration
- **Profiling**: CPU & GPU profiling tools
- **Data**: XML support, thread pool, entity-component and event systems
- **File Formats**: 10+ fonts, 30+ images, 40+ models

# Getting Started

### Building

One-click project generation - see the [Building Guide](https://github.com/PanosK92/SpartanEngine/wiki/Building) for details.

### Tutorials

Check out [Game.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/runtime/Game/Game.cpp) to understand how default worlds are loaded and set up - it's the best starting point for understanding the engine's structure.

# Media

### Podcast

<table>
  <tr>
    <td><img width="400" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/refs/heads/master/.github/images/podcast.png"/></td>
    <td>
      Exploring the tech world and beyond, meeting the brightest minds across cutting-edge industries.<br><br>
      📺 <a href="https://youtu.be/OZRwCZhglsQ">Watch on YouTube</a><br>
      🎧 <a href="https://open.spotify.com/show/5F27nWKn9TZdClc5db9efY">Listen on Spotify</a>
    </td>
  </tr>
</table>

### Engine Trailer

[![Engine Trailer](https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/video_promo.png)](https://www.youtube.com/watch?v=TMZ0epSVwCk)

# Community & Support

### Contributing

Contributors get access to [exclusive perks](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor) designed to accelerate learning and skill development. See the [Contributing Guide](https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md) to get started.

### Sponsorship

I cover the costs for Dropbox hosting to ensure library and asset bandwidth is available. If you enjoy running a single script and having everything download, compile, and work seamlessly, please consider [sponsoring](https://github.com/sponsors/PanosK92). Direct sponsorship is more helpful than Discord boosts since it goes directly into maintaining and improving the project.

# Projects Using Spartan

- This engine started as a personal learning project and a way to enhance my portfolio while I was a university student. I also used it for my thesis at [my university](https://en.wikipedia.org/wiki/University_of_Thessaly) with professor [Fotis Kokkoras](https://ds.uth.gr/en/staff-en/faculty-en/kokkoras/).
- **Godot Engine** - Integrates Spartan's TAA ([see code](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2))
- **Stalker Anomaly** - Rendering addon using Spartan's source ([ModDB](https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders))
- **Programming Book** - Jesse Guerrero's [beginner programming book](https://www.amazon.com/dp/B0CXG1CMNK) features Spartan's code and community

Using code from Spartan or inspired by it? [Reach out](https://twitter.com/panoskarabelas) - I'd love to showcase your project!

# License

[MIT License](https://github.com/PanosK92/SpartanEngine/blob/master/license.md) - free to use with attribution.
