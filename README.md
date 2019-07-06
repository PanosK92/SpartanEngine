<img align="left" width="128" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/logo.png"/>

# Spartan Engine

<p>Spartan is a game engine that started as a hobby project and evolved into something bigger. The goal is to target high-end machines and deliver advanced graphical capabilities at high frame rates with minimum input latency. Then wrap all that in a nice editor.</p>

<p>The project is at an early development stage and there is a lot experimentation going on, regarding what works best. However a lot of effort is being put into building and maintaining a clean, modern and overall high quality architecture, an architecture that will ensure continuous development over the years. This means that while you shouldn't expect to make games with it yet, you might find it to be a helpful study resource.</p>

<p>For more updates regarding the project's development, you can
<a href="https://twitter.com/intent/follow?screen_name=panoskarabelas1"><img src="https://img.shields.io/twitter/follow/panoskarabelas1.svg"></a> me on twitter.</p>

<p>License: Embracing the open source ethos and respecting the MIT license is greatly appreciated. This means that you can copy all the code you want as long as you include a copy of the original license.</p>

<img align="left" width="468" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/screenshot-v0.3_preview4.jpg"/>
<img align="left" width="365" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/rotating_gun.gif"/>

[
![](https://i.imgur.com/NRxQhSm.jpg)](https://www.youtube.com/watch?v=RIae1ma_DSo)

## Download
Platform | API | Status | Quality | Binaries | :+1:
:-:|:-:|:-:|:-:|:-:|:-:|
<img src="https://doublslash.com/img/assets/Windows8AnimatedLogo.png" width="20"/>|<img src="https://1.bp.blogspot.com/-i3xzHAedbvU/WNjGcL4ujqI/AAAAAAAAADY/M3_8wxw9hVsajXefi65wY_sJKgFC8MPxQCK4B/s1600/directx11-logo.png" width="100"/>|[![Build status](https://ci.appveyor.com/api/projects/status/p5duow3h4w8jp506?svg=true)](https://ci.appveyor.com/project/PanosK92/directus3d)|[![Codacy Badge](https://api.codacy.com/project/badge/Grade/da72b4f208284a30b7673abd86e8d8d3)](https://www.codacy.com/app/PanosK92/Directus3D?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=PanosK92/Directus3D&amp;utm_campaign=Badge_Grade)|[Download](https://ci.appveyor.com/api/projects/PanosK92/directus3d/artifacts/Binaries/Release.zip?branch=master)|[![](https://www.paypalobjects.com/en_GB/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=CSP87Y77VNHPG&source=url)
<img src="https://doublslash.com/img/assets/Windows8AnimatedLogo.png" width="20"/>|<img src="https://upload.wikimedia.org/wikipedia/commons/thumb/f/f8/Vulkan_API_logo.svg/1280px-Vulkan_API_logo.svg.png" width="100"/>|WIP|WIP|WIP|WIP

## Features (v0.3)
- 10+ font file formats support (FreeType)
- 20+ audio file formats support (FMOD)
- 30+ image file formats support (FreeImage)
- 40+ model file formats support (Assimp)
- XML files
- Keyboard
- Mouse
- Xbox controller
- Bloom
- Shadows (Cascaded shadow mapping with smooth, clean and stable shadows)
- Custom mip chain generation (Higher texture fidelity using Lanczos3 scaling)
- Debug rendering (Transform gizmo, scene grid, bounding boxes, colliders, raycasts, g-buffer visualization etc)
- Deferred rendering
- DirectX 11 backend
- Lights (Directional, point and spot lights)
- Font Rendering
- Frustum culling
- Per-Pixel motion blur
- Physically based rendering
- Post-process effects (Tone-Mapping, FXAA, Sharpening, Dithering, Chromatic aberration etc.)
- SSAO (Screen space ambient occlusion)
- SSR (Screen space reflections)
- TAA (Temporal anti-aliasing based on Uncharted 4)
- Constraints
- Rigid bodies
- Colliders
- Entity-component system
- Event system
- Easy to build (Single click project generation which includes editor and runtime)
- Thread pool
- Engine rendered platform agnostic editor
- Profiling (CPU & GPU)
- C/C++ (Using AngelScript)
- Windows 10 and a modern/dedicated GPU (The target is high-end machines, old setups or mobile devices are not officially supported)

# Roadmap

##### v0.31 (WIP)
Feature     		| Completion    | Notes 
:-          		| :-            | :-
Parallax Mapping 	| 100%          | -
Shader Editor 		| 100%          | Real-time shader editing tool.
Shadows 			| 90%          	| Improve shadow mapping performance and enable spot & point light shadows.
Vulkan      		| 60%           | Don't port it, re-architect the engine instead.

###### v0.32
- C# scripting (Replace AngelScript).

###### v0.33
- Ray traced shadows.
- Ray traced reflections.

###### v0.34
- DirectX 12.

###### v0.35
- Skeletal Animation.

###### v0.36
- Eye Adaptation.
- Depth-of-field (Based on Doom approach).
- Subsurface Scattering.

###### Future
- Volumetric Lighting.
- Atmospheric Scattering.
- Dynamic resolution scaling.
- Global Illumination.
- Export on Windows.
- UI components.
- Make editor more stylish.

# Documentation
- [Compiling](https://github.com/PanosK92/SpartanEngine/blob/master/Documentation/CompilingFromSource/CompilingFromSource.md) - A guide on how to compile from source

# Dependencies
- [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109).
- [Visual C++ 2017 (x64) runtime package](https://go.microsoft.com/fwlink/?LinkId=746572).
- Windows 10.

# License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/SpartanEngine/blob/master/LICENSE.txt) for details.