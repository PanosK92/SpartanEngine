
<img align="center" padding="2" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp"/>

<p> Spartan Engine is more than an engine; it's a vibrant hub for learning and connection, redefining the open-source community experience. While centered around a research-focused game engine for real-time solutions, our core mission is to foster a unique, collaborative environment. Join us for exclusive perks like contributor recognition, networking opportunities with tech professionals, and valuable learning resources, including our active Discord community and insightful YouTube tutorials. Spartan Engine isn't just about code; it's about building a community like no other.</p>

- <img align="left" width="32" src="https://clipart.info/images/ccovers/1534043159circle-twitter-logo-png.png"/>For occasional updates regarding the project's development, you can follow me on <a href="https://twitter.com/panoskarabelas1?ref_src=twsrc%5Etfw">twitter</a>.
  
- <img align="left" width="32" height="32" src="https://www.freeiconspng.com/thumbs/discord-icon/discord-icon-7.png">For questions, suggestions, help and any kind of general discussion join the [discord server](https://discord.gg/TG5r2BS).
  
- <img align="left" width="32" height="32" src="https://www.freeiconspng.com/uploads/git-github-hub-icon-25.png">For issues and anything directly related to the project, feel free to open an issue.
  
- <img align="left" width="32" height="32" src="https://cdn3d.iconscout.com/3d/premium/thumb/ai-5143193-4312366.png?f=webp">My AI replica is equipped with engine knowledge and my personality traits, click [here](https://chat.openai.com/g/g-etpaCChzi-spartan) here to get it to help you.

- <img align="left" width="32" height="32" src="https://i0.wp.com/opensource.org/wp-content/uploads/2023/01/cropped-cropped-OSI_Horizontal_Logo_0-e1674081292667.png">Please adhere to the <a href="https://en.wikipedia.org/wiki/MIT_License">MIT license</a> You're free to copy the code, provided you include the original license.
  
#### Status
![build_status](https://github.com/PanosK92/SpartanEngine/actions/workflows/workflow.yml/badge.svg)
[![Discord](https://img.shields.io/discord/677302405263785986?logo=discord&label=Discord&color=5865F2&logoColor=white)](https://discord.gg/TG5r2BS)

# Media

| Video: Livestream of FSR 2 integration | Video: A demonstration of the engine's capabilities (old) |
|:-:|:-:|
| [![Image1](https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/readme_1.4.jpg)](https://www.youtube.com/watch?v=QhyMal6RY7M) | [![Image2](https://i.imgur.com/j6zIEI9.jpg)](https://www.youtube.com/watch?v=RIae1ma_DSo) |
| Screenshot: Bistro | Screenshot: Minecraft |
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/readme_1.1.jpg"/> | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/readme_1.2.jpg"/> |

# Worlds
<img align="left" width="450" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_selection.png"/>

Upon launching the engine, you'll be greeted with a selection of default worlds to load. Each world is physics-enabled, allowing you to walk around, pick objects using your mouse, and even drive a car. These worlds are designed to offer a diverse and enjoyable experience.  
<br clear="left"/>

| Sponza | Forest |
|:-:|:-:|
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/screenshot-v0.3_preview5.jpg"/><br>The Sponza building, found in Dubrovnik, is showcased here with a true-to-life scale. | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_forest.jpg"/><br>A height map-generated forest featuring water bodies amidst tens of thousands of trees and plants, all set in a walkable terrain. |
| Car | Doom |
| <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_car.jpg"/><br>A drivable car implemented with a highly realistic tire friction formula, simulation of gearbox, anti-roll bar, and more. | <img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/.github/images/world_doom.jpg"/><br>This is a simple scene with the soundtrack from E1M1 |

# Features
#### Rendering
- 128-byte push constant buffer for lightning fast CPU to GPU data transfer.
- On the fly GPU-based mip generation - with only one dispatch.
- Bindless design.
- Unified deferred rendering with transparency (BSDF with same render path).
- Vulkan and DirectX 12 (WIP) backends with universal HLSL shaders.
- Screen space shadows from Bend Studio's Days Gone.
- Screen space global illumination, reflections.
- Advanced shadow features with penumbra and colored translucency.
- Physical light units (intensity from lumens and color from kelvin).
- Comprehensive debug rendering options.
- Frustum culling.
- Physically based camera.
- Atmospheric scattering.
- Temporal anti-aliasing for smooth visuals.
- Post-process effects like fxaa, bloom, motion-blur, depth of field, chromatic aberration etc.
- AMD FidelityFX features like FSR 2, SPD, etc.
#### General
- One-click project generation for easy setup.
- Universal input support, including mouse, keyboard, and controllers (tested with a PS5 controller).
- Comprehensive physics features.
- CPU & GPU profiling.
- XML support for data handling.
- Thread pool for that can consume any workload.
- Entity-component, event systems and most things you'll expect to find in a modern engine.
- Wide file format support: 10+ for fonts, 20+ for audio, 30+ for images, and 40+ for models.

# Wiki
The [wiki](https://github.com/PanosK92/SpartanEngine/wiki/Wiki) can answer most of your questions, here are some of it's contents:
- [Compiling](https://github.com/PanosK92/SpartanEngine/wiki/Compiling) 
- [Contributing](https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md)
- [Perks of a contributor](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor)

Questions can also be answered by my AI replica, [Panos](https://chat.openai.com/g/g-etpaCChzi-spartan).

# Interesting facts
- This engine started as a way to enrich my portfolio while I was a university student, circa 2015.
- It's one of the oldest, yet still active, one man game engines on GitHub.
- It's one of the most rewarding projects in terms of the perks you receive should you become a contributor, more [here](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor).

# Use cases
Are you utilizing any components from the Spartan Engine, or has it inspired aspects of your work? If yes, then reach out to me, I'd love to showcase your project.
- Godot uses Spartan's TAA, see [here](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2)

