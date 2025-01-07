
<img align="center" padding="2" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp"/>

<p>Spartan Engine is one of the most advanced one-man game engines out there, pushing the limits of real-time approaches. What started as a portfolio project has evolved into a cutting-edge project for developers to explore, learn, and contribute. This isn't an engine for the average user, it's designed for advanced research and experimentation, ideal for industry veterans looking to experiment, not to build a game (yet). With a thriving Discord community of over 440 members, including industry veterans, and contribution perks that you won't believe when you see, it's one of the most unique projects you'll come across.</p>

- <img align="left" width="32" src="https://i.pinimg.com/736x/99/65/5e/99655e9fe24eb0a7ea38de683cedb735.jpg"/>For occasional updates regarding the project's development, you can follow me on <a href="https://twitter.com/panoskarabelas1?ref_src=twsrc%5Etfw">X</a>.
  
- <img align="left" width="32" height="32" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/discord.png">For a community like no other, join our group of 400+ members on [discord](https://discord.gg/TG5r2BS).
  
- <img align="left" width="32" height="32" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/github.png">For issues and anything directly related to the project, feel free to open an issue.
  
- <img align="left" width="32" height="32" src="https://i0.wp.com/opensource.org/wp-content/uploads/2023/01/cropped-cropped-OSI_Horizontal_Logo_0-e1674081292667.png">Please adhere to the <a href="https://en.wikipedia.org/wiki/MIT_License">MIT license</a>. You're free to copy the code, provided you include the original license.
  
#### Status

![build_status](https://github.com/PanosK92/SpartanEngine/actions/workflows/workflow.yml/badge.svg)
[![Discord](https://img.shields.io/discord/677302405263785986?logo=discord&label=Discord&color=5865F2&logoColor=white)](https://discord.gg/TG5r2BS)

# Media

| Video: Livestream of FSR 2 integration | Video: The engine and the community |
|:-:|:-:|
|[![Image1](https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/readme_1.4.jpg)](https://www.youtube.com/watch?v=QhyMal6RY7M) | [![Image2](https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/video_promo.png)](https://www.youtube.com/watch?v=TMZ0epSVwCk)

# Worlds

<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_forest.png"/>
<img align="left" width="450" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_selection_4.png"/>

Upon launching the engine, you'll be greeted with a selection of default worlds to load. Each world is physics-enabled, allowing you to walk around, pick objects using your mouse, and even drive a car. These worlds are designed to offer a diverse and enjoyable experience  
<br clear="left"/>

| Sponza | Forest |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_sponza.png"/><br>The Sponza building, found in Dubrovnik, is showcased here with a true-to-life scale. | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_forest.png"/><br>A height map-generated forest featuring water bodies amidst tens of thousands of trees and plants, all set in a walkable terrain. |
| Car | Doom |
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_car.png"/><br>A drivable car implemented with a highly realistic tire friction formula, simulation of gearbox, anti-roll bar, and more | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_doom.png"/><br>This is a simple scene with the soundtrack from E1M1 |
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
- Frustum & occlusion culling (software and hardware hybrid).
- Physically based camera.
- Volumetric fog.
- HDR10 output.
- Post-process effects like fxaa, bloom, motion-blur, depth of field, chromatic aberration etc.

#### General

- One-click project generation for easy setup.
- Universal input support, keyboard & mouse, controllers (tested a PS5 controller) and steering wheels.
- Comprehensive physics features.
- CPU & GPU profiling.
- XML support for data handling.
- Thread pool that can consume any workload.
- Entity-component, event systems and most things you'll expect to find in a modern engine.
- Wide file format support: 10+ for fonts, 20+ for audio, 30+ for images, and 40+ for models.

# Wiki

The [wiki](https://github.com/PanosK92/SpartanEngine/wiki/Wiki) can answer most of your questions, here are some of it's contents:

- [Compiling](https://github.com/PanosK92/SpartanEngine/wiki/Compiling)
- [Contributing](https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md)
- [Perks of a contributor](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor)

# Interesting facts

- This engine started as a way to learn and enrich my portfolio while I was a university student, circa 2014, represeting over a decade of non-stop development.
- It's one of the most rewarding projects in terms of the perks you receive should you become a contributor, more [here](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor).

# Use cases

Are you utilizing any components from the Spartan Engine, or has it inspired aspects of your work? If yes, then reach out to me, I'd love to showcase your project.

- Godot uses Spartan's TAA, see [here](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2)
- Stalker Anomaly has an addon which enchances rendering based on Spartan's source [here](https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders)
- Jesse Guerrero, a contributor, wrote a [book](https://www.amazon.com/dp/B0CXG1CMNK?ref_=cm_sw_r_cp_ud_dp_A14WVAH86VH407JE95MG_1) on beginning programming, showcasing Spartan's code, Discord community and the leadership within it.
