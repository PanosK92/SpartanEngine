
<img align="center" padding="2" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp"/>

<p>Spartan Engine is one of the most advanced one-man game engines out there, pushing the limits of real-time approaches.
What started as a portfolio project has evolved into a cutting-edge project for developers to explore, learn, and contribute.
This isn't an engine for the average user, it's designed for advanced research and experimentation, ideal for industry veterans, not for individuals to build a game. With a thriving Discord community of over 500 members, including industry
veterans, and contribution perks that you won't believe when you see, it's one of the most unique projects you'll come across.</p>

- <img align="left" width="32" src="https://i.pinimg.com/736x/99/65/5e/99655e9fe24eb0a7ea38de683cedb735.jpg"/>For occasional updates regarding the project's development, you can follow me on <a href="https://twitter.com/panoskarabelas1?ref_src=twsrc%5Etfw">X</a>.
  
- <img align="left" width="32" height="32" src="https://e7.pngegg.com/pngimages/705/535/png-clipart-computer-icons-discord-logo-discord-icon-rectangle-logo.png">For a community like no other, join our group of 500+ members on [discord](https://discord.gg/TG5r2BS).
  
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
The Forest is the most advanced and demanding world. It features **50 million** procedurally generated grass blades, fully simulated and inspired by **Ghost of Tsushima**. The world spans **9.4 km²**, covered in **10,000** trees and rocks, and includes a drivable car.

**Some of the other worlds**
| Sponza | Doom |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_sponza.png"/><br>The Sponza building, found in Dubrovnik, is showcased here with a true-to-life scale. | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_doom.png"/><br>This is a simple scene with the soundtrack from E1M1 |
| Bistro | Minecraft |
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_bistro.jpg"/><br>Amazon Lumberyard Bistro | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_minecraft.jpg"/><br>A good old minecraft world |

# Features

#### Rendering

- Cutting edge Vulkan renderer.
- 128-byte push constant buffer for lightning fast CPU to GPU data transfer.
- On the fly single dispatch GPU-based mip generation for render targets (FidelityFX SPD).
- On the fly compression and mip generation for material textures (FidelityFX Compressonator).
- Fully bindless design (materials, lights, even the samplers).
- Fast dual paraboloid point lights.
- Vulkan (main) and DirectX 12 (wip) backends with universal HLSL shaders.
- Unified deferred rendering with transparency (BSDF with same render path).
- Atmospheric scattering, real-time filtering and image-based lighting.
- Real-time global illumination (FidelityFX Brixelizer GI).
- Screen space shadows (Bend Studio's Days Gone).
- Screen space ambient occlusion.
- Screen space reflections (FidelityFX SSSR).
- Variable rate shading.
- Dynamic resolution scaling.
- Upscaling (FidelityFX FSR 3.1).
- Temporal anti-aliasing.
- Breadcrumbs for tracing GPU crashes on AMD (FidelityFX Breadcrumbs).
- Advanced shadow features with penumbra and colored translucency.
- Physical light units (intensity from lumens and color from kelvin).
- Frustum & occlusion (Hi-Z) culling.
- Physically based camera.
- Volumetric fog.
- HDR10 output.
- Post-process effects like fxaa, bloom, motion-blur, depth of field, chromatic aberration etc.

###### General

- One-click project generation for easy setup.
- Universal input support, keyboard & mouse, controllers (tested a PS5 controller) and steering wheels.
- Comprehensive physics features.
- CPU & GPU profiling.
- XML support for data handling.
- Thread pool that can consume any workload.
- Entity-component, event systems and most things you'll expect to find in a modern engine.
- Wide file format support: 10+ for fonts, 30+ for images, and 40+ for models.

# Documentation

The [wiki](https://github.com/PanosK92/SpartanEngine/wiki/Wiki) can answer most of your questions, here are some of it's contents:

- [Compiling](https://github.com/PanosK92/SpartanEngine/wiki/Compiling)
- [Contributing](https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md)
- [Perks of a contributor](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor)

#### Tutorials

While the engine is designed primarily for experienced game developers to experiment, there are resources available for those who prefer a more guided approach.

To get started, take a look at [game.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/runtime/Game/Game.cpp). This file contains all the logic for loading and setting up the default worlds within the engine, and it's a great place to understand the core structure of the engine.

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

- This engine started as a personal learning project and a way to enhance my portfolio while I was a university student. I also used it for my thesis at [my university](http://www.cs.teilar.gr/CS/Home.jsp).  
- Contributing to this project comes with great perks! Learn more about the benefits [here](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor).  
- **Godot** integrates Spartan’s TAA. See it in action [here](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2).  
- **Stalker Anomaly** features an addon that enhances rendering using Spartan's source. Check it out [here](https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders).  
- Jesse Guerrero, a contributor, wrote a [book](https://www.amazon.com/dp/B0CXG1CMNK?ref_=cm_sw_r_cp_ud_dp_A14WVAH86VH407JE95MG_1) on beginner programming, featuring Spartan's code, its Discord community, and leadership.  

Are you utilizing any code from Spartan Engine, or has it inspired aspects of your work? If yes, reach out to me, I'd love to showcase your project.