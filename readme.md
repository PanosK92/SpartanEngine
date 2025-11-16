
<img align="center" padding="2" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp"/>

<p>Spartan Engine is a highly capable one-man game engine I've been working on for over 10 years, it's designed for research, experimentation, and pushing the boundaries of rendering and simulation. The engine is particularly suited for industry professionals exploring advanced workflows rather than users focused solely on game creation.</p>

<p>Originally developed as a personal learning project, Spartan Engine has grown into a community that fosters collaboration and experimentation. Its Discord community of over 600 members, including seasoned professionals, provides an active space for knowledge-sharing and networking (jobs channel).</p>

<p>Contributors can access a range of <a href="https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor">perks</a> designed to accelerate learning, skill development, and experimentation. Ambitious contributors are supported directly to maximize their growth and contribution potential.</p>

<p>Spartan Engine now serves a broader purpose: enabling others to explore, experiment, and push the boundaries of what’s possible in game and rendering technology.</p>

- <img align="left" width="32" src="https://i.pinimg.com/736x/99/65/5e/99655e9fe24eb0a7ea38de683cedb735.jpg"/>For occasional updates regarding the project's development, you can follow me on <a href="https://twitter.com/panoskarabelas1?ref_src=twsrc%5Etfw">X</a>.
  
- <img align="left" width="32" height="32" src="https://e7.pngegg.com/pngimages/705/535/png-clipart-computer-icons-discord-logo-discord-icon-rectangle-logo.png">For a community like no other, join our group of 600+ members on [discord](https://discord.gg/TG5r2BS).
  
- <img align="left" width="32" height="32" src="https://cdn-icons-png.flaticon.com/512/25/25231.png">For issues and anything directly related to the project, feel free to open an issue.
  
- <img align="left" width="32" height="32" src="https://i0.wp.com/opensource.org/wp-content/uploads/2023/01/cropped-cropped-OSI_Horizontal_Logo_0-e1674081292667.png">Please adhere to the <a href="https://en.wikipedia.org/wiki/MIT_License">MIT license</a>. You're free to copy the code, provided you include the original license.
  
#### Status

![build_status](https://github.com/PanosK92/SpartanEngine/actions/workflows/workflow.yml/badge.svg)
[![Discord](https://img.shields.io/discord/677302405263785986?logo=discord&label=Discord&color=5865F2&logoColor=white)](https://discord.gg/TG5r2BS)


# Worlds

<img align="left" width="450" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_selection_4.png"/>

Upon launching the engine, you'll be greeted with a selection of default worlds to load. Each world is physics-enabled, allowing you to walk around, pick objects using your mouse, and even drive a car. These worlds are designed to offer a diverse and enjoyable experience  
<br clear="left"/>

**Forest**
<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_forest.jpg"/>
The Forest is the most advanced and demanding world. It features **256 million** procedurally generated grass blades, fully simulated and inspired by **Ghost of Tsushima**. The world spans **64.1 km²**, covered in thousands trees and rocks.

**Some of the other worlds**
| Sponza 4K | Subway |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_sponza.png"/><br>The Sponza building, found in Dubrovnik, is showcased here with a true-to-life scale. | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_subway.jpg"/><br>Emissive materials and GI test |

| Minecraft | Liminal Space |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_minecraft.jpg"/><br>A good old minecraft world | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_liminal.jpg"/><br>Shifts your frequency to a nearby reality |

| Showroom |
|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_showroom.png"/><br>Car Showroom - Ideal for YouTubers/Press as it doesn't use experimental tech |

# Features

#### Rendering

- Renderer
  - Cutting-edge Vulkan renderer
  - 128-byte push constant buffer across passes for lightning-fast CPU-to-GPU data transfer
  - Capable of handling hundreds of millions of instances thanks to its tightly packed 10-byte instance format
  - On-the-fly single-dispatch GPU-based mip generation for render targets (FidelityFX SPD)
  - On-the-fly compression and mip generation for material textures (FidelityFX Compressonator)
  - Vulkan (main) and DirectX 12 (wip) backends with universal HLSL shaders
  - Fully bindless design (materials, lights, samplers)
  - Unified deferred rendering with transparency (BSDF with same render path)
- Lighting & Shadows
  - Atmospheric scattering, real-time filtering, IBL with bent normals
  - Screen-space shadows (from *Days Gone*)
  - Screen-space ambient occlusion (XeGTAO + visibility bitfield)
  - Screen-space reflections (FidelityFX SSSR)
  - Fast shadow mapping with penumbra, supporting dozens of lights via a shadow map atlas
  - Volumetric fog
- Performance & Upscaling
  - Variable rate shading
  - Upscaling: XeSS 2 & FSR 3
  - Dynamic resolution scaling
  - Temporal anti-aliasing
  - Breadcrumbs for tracing GPU crashes on AMD (FidelityFX Breadcrumbs)
- Camera & Environment
  - Physically based camera
  - Auto-exposure
  - Physical light units (lumens & kelvin)
  - Frustum & occlusion (Hi-Z) culling
- Post-processing
  - Tonemappers: ACES, AgX, others
  - HDR10 output
  - Post-processing effects: FXAA, bloom, motion blur, depth of field, chromatic aberration

###### General

- Project & Input
  - One-click project generation
  - Keyboard, mouse, controllers, steering wheels
- Physics & Profiling
  - Comprehensive physics features (PhysX)
  - CPU & GPU profiling
- Data & Systems
  - XML support
  - Thread pool for any workload
  - Entity-component and event systems
- File support
  - 10+ font formats, 30+ image formats, 40+ model formats

# Documentation

The [wiki](https://github.com/PanosK92/SpartanEngine/wiki/Wiki) can answer most of your questions, here are some of it's contents:

- [Building](https://github.com/PanosK92/SpartanEngine/wiki/Building)
- [Contributing](https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md)
- [Perks of a contributor](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor)

#### Tutorials

While the engine is designed primarily for experienced game developers to experiment, there are resources available for those who prefer a more guided approach.

To get started, take a look at [Game.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/runtime/Game/Game.cpp). This file contains all the logic for loading and setting up the default worlds within the engine, and it's a great place to understand the core structure of the engine.

# Media

### Podcast

<table>
  <tr>
    <td>
      <img align="left" width="512" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/refs/heads/master/.github/images/podcast.png"/>
    </td>
    <td>
      Join me as I explore the tech world and beyond, meeting the brightest minds across cutting-edge industries and uncovering their stories.<br><br>
      <img align="left" width="32" height="32" src="https://cdn-icons-png.flaticon.com/512/174/174883.png"/>Watch on <a href="https://youtu.be/OZRwCZhglsQ">YouTube</a>.<br><br>
      <img align="left" width="32" height="32" src="https://cdn-icons-png.flaticon.com/512/174/174872.png"/>Listen on <a href="https://open.spotify.com/show/5F27nWKn9TZdClc5db9efY">Spotify</a>.
    </td>
  </tr>
</table>
<br clear="left"/>

### Engine Trailer

[![Image2](https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/video_promo.png)](https://www.youtube.com/watch?v=TMZ0epSVwCk)
# Support

I cover the costs for Dropbox hosting to ensure library and assets bandwidth is available. If you enjoy the ease of running a single script and having everything download, compile and work seamlessly, please consider [sponsoring](https://github.com/sponsors/PanosK92) to help keep it that way. Sponsoring directly is much more helpful than boosting the Discord server since boosts only benefit Discord, not the project. Your support goes directly into maintaining and improving everything.

# Interesting Facts & Use Cases  

- This engine started as a personal learning project and a way to enhance my portfolio while I was a university student. I also used it for my thesis at [my university](https://en.wikipedia.org/wiki/University_of_Thessaly).  
- Contributing to this project comes with great perks! Learn more about the benefits [here](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor).  
- **Godot** integrates Spartan’s TAA. See it in action [here](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2).  
- **Stalker Anomaly** features an addon that enhances rendering using Spartan's source. Check it out [here](https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders).  
- Jesse Guerrero, a contributor, wrote a [book](https://www.amazon.com/dp/B0CXG1CMNK?ref_=cm_sw_r_cp_ud_dp_A14WVAH86VH407JE95MG_1) on beginner programming, featuring Spartan's code, its Discord community, and leadership.  

Are you utilizing any code from Spartan Engine, or has it inspired aspects of your work? If yes, reach out to me, I'd love to showcase your project.
