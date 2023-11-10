
<img align="center" padding="2" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/data/textures/banner.bmp"/>

<p>Spartan is a research-focused game engine designed for real-time solutions, providing a dynamic experience. Not intended for game development yet, it serves as a valuable study resource due to its clean, modern, and high-quality architecture.</p>

- <img align="left" width="32" src="https://clipart.info/images/ccovers/1534043159circle-twitter-logo-png.png"/>For occasional updates regarding the project's development, you can follow me on <a href="https://twitter.com/panoskarabelas1?ref_src=twsrc%5Etfw">twitter</a>.
  
- <img align="left" width="32" height="32" src="https://www.freeiconspng.com/thumbs/discord-icon/discord-icon-7.png">For questions, suggestions, help and any kind of general discussion join the [discord server](https://discord.gg/TG5r2BS).
  
- <img align="left" width="32" height="32" src="https://www.freeiconspng.com/uploads/git-github-hub-icon-25.png">For issues and anything directly related to the project, feel free to open an issue.
  
- <img align="left" width="32" height="32" src="https://i0.wp.com/opensource.org/wp-content/uploads/2023/01/cropped-cropped-OSI_Horizontal_Logo_0-e1674081292667.png">Adhering to the <a href="https://en.wikipedia.org/wiki/MIT_License">MIT license</a> is appreciated. This means that you can copy all the code you want as long as you include a copy of the original license.

- <img align="left" width="32" height="32" src="https://cdn3d.iconscout.com/3d/premium/thumb/ai-5143193-4312366.png?f=webp">There is an AI replica of me, it has Spartan specific knowledge and fragments of my personality, it can assist you with many things, talk with it [here](https://chat.openai.com/g/g-etpaCChzi-spartan).

#### Status
![build_status](https://github.com/PanosK92/SpartanEngine/actions/workflows/workflow.yml/badge.svg)
[![Discord](https://img.shields.io/discord/677302405263785986?label=Discord)](https://discord.gg/TG5r2BS)

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
- AMD FidelityFX suite for enhanced visuals and performance.
- Bloom effect inspired by Resident Evil 2's RE Engine.
- Unified deferred rendering with transparency (BSDF with same render path and shaders).
- Camera-controlled depth of field, motion blur and chromatic aberration.
- Comprehensive debug rendering options.
- Frustum culling.
- Physical light units (intensity from lumens and color from kelvin).
- Physically based camera.
- Atmospheric Scattering.
- GPU-based mip generation (single dispatch).
- Advanced shadow features with penumbra and colored translucency.
- Screen space global illumination, reflections, and shadows.
- Temporal anti-aliasing for smooth visuals.
- Vulkan and DirectX 12 backends with universal HLSL shaders.
- Volumetric lighting for atmospheric effects.
#### System
- One-click build for easy setup.
- Entity-component and event systems for flexible architecture.
- Universal input support, including mouse, keyboard, and controllers (tested with a PS5 controller).
- Comprehensive physics features.
- CPU & GPU profiling for performance tuning.
- XML support for data handling.
- Thread pool for efficient multitasking.
#### Formats
- Wide file format support: 10+ for fonts, 20+ for audio, 30+ for images, and 40+ for models.

# Roadmap
- Keep an eye on Vulkan, it's stable but you never know.
- Continue switching to bindless.
- Continue work on D3D12 (on going and non blocking since Vulkan is there).
- Skeletal Animation.
- Eye Adaptation.
- Subsurface scattering.
- Ray traced reflections & shadows.
- Dynamic resolution scaling (and use FSR 2, to properly reconstuct).
- Export on Windows.
- Improved the editor style/theme.
- Scripting.
- Linux port.
  
If you are looking at what to do, there are more ideas in the [issues]([https://github.com/PanosK92/SpartanEngine/wiki/Wiki](https://github.com/PanosK92/SpartanEngine/issues)) section.

# Wiki
Don't forget that there is a [wiki](https://github.com/PanosK92/SpartanEngine/wiki/Wiki) that can help answer some of your questions. Here are some of it's contents:
- [Compiling](https://github.com/PanosK92/SpartanEngine/wiki/Compiling) 
- [Contributing](https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md)
- [Perks of a contributor](https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor)

# Use cases
- Godot uses Spartan's TAA, see [here](https://github.com/godotengine/godot/blob/37d51d2cb7f6e47bef8329887e9e1740a914dc4e/servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl#L2)
