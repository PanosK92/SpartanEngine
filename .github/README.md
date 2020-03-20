<img align="left" width="128" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/logo.png"/>

# Spartan Engine

<p>Spartan game engine is the result of my never-ending quest to understand how things work and has become my go-to place for research.</p>

<img align="right" width="128" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/rotating_gun.gif"/>

<p>If you're also on the lookout for knowledge, then you might find this engine to be a helpful study resource. This is because a lot of effort is being put into building and maintaining a clean, modern and overall high quality architecture, an architecture that will ensure continuous development over the years. Have a look at the source ;-) </p> 

<p><img align="left" width="32" src="https://valentingom.files.wordpress.com/2016/03/twitter-logo2.png"/>For occasional updates regarding the project's development, you can follow me on <a href="https://twitter.com/panoskarabelas1?ref_src=twsrc%5Etfw">twitter</a>.</p> 

<img align="left" width="32" src="https://www.freepnglogos.com/uploads/discord-logo-png/discord-logo-vector-download-0.png">For questions, suggestions, help and any kind of general discussion join the [discord server](https://discord.gg/TG5r2BS).

<img align="left" width="32" src="https://image.flaticon.com/icons/svg/25/25231.svg">For issues and anything directly related to the project, feel free to open an issue.

<img align="left" width="32" src="https://opensource.org/files/OSIApproved_1.png">Embracing the open source ethos and respecting the <a href="https://en.wikipedia.org/wiki/MIT_License">MIT license</a> is greatly appreciated. This means that you can copy all the code you want as long as you include a copy of the original license.</p>

## Download
Platform | API | Status | Quality | Binaries | :+1:
:-:|:-:|:-:|:-:|:-:|:-:|
<img src="https://cdn.iconscout.com/icon/free/png-256/windows-221-1175066.png" width="20"/>|<img src="https://1.bp.blogspot.com/-i3xzHAedbvU/WNjGcL4ujqI/AAAAAAAAADY/M3_8wxw9hVsajXefi65wY_sJKgFC8MPxQCK4B/s1600/directx11-logo.png" width="100"/>|[![Build status](https://ci.appveyor.com/api/projects/status/p5duow3h4w8jp506/branch/master?svg=true)](https://ci.appveyor.com/project/PanosK92/spartanengine-d3d11/branch/master)|[![Codacy Badge](https://api.codacy.com/project/badge/Grade/da72b4f208284a30b7673abd86e8d8d3)](https://www.codacy.com/app/PanosK92/Directus3D?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=PanosK92/Directus3D&amp;utm_campaign=Badge_Grade)|[Download](https://ci.appveyor.com/api/projects/PanosK92/spartanengine-d3d11/artifacts/Binaries/release_d3d11.zip?branch=master)|[![](https://www.paypalobjects.com/en_GB/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=CSP87Y77VNHPG&source=url)
<img src="https://cdn.iconscout.com/icon/free/png-256/windows-221-1175066.png" width="20"/>|<img src="https://upload.wikimedia.org/wikipedia/commons/thumb/f/f8/Vulkan_API_logo.svg/1280px-Vulkan_API_logo.svg.png" width="100"/>|[![Build status](https://ci.appveyor.com/api/projects/status/txlx815l43ytodij/branch/master?svg=true)](https://ci.appveyor.com/project/PanosK92/spartanengine-vulkan/branch/master)|*Experimental*|[Download](https://ci.appveyor.com/api/projects/PanosK92/spartanengine-vulkan/artifacts/Binaries/release_vulkan.zip?branch=master)|[![](https://www.paypalobjects.com/en_GB/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=CSP87Y77VNHPG&source=url)

## Media
[![](https://i.imgur.com/j6zIEI9.jpg)](https://www.youtube.com/watch?v=RIae1ma_DSo)
<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_screen_1.jpeg"/>
<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_screen_2.JPG"/>
<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_screen_3.JPG"/>

## Features (v0.3)
- 10+ font file formats support (FreeType)
- 20+ audio file formats support (FMOD)
- 30+ image file formats support (FreeImage)
- 40+ model file formats support (Assimp)
- Deferred rendering with transparency.
- Vulkan and DirectX 11.
- Bloom (Based on a study of Resident Evil 2's RE Engine)
- Custom mip chain generation (Higher texture fidelity using Lanczos3 scaling)
- Lights (Directional, point and spot lights)
- Shadows with colored translucency (Cascaded and omnidirectional shadow mapping with vogel filtering)
- Font rendering
- Frustum culling
- Per-Pixel motion blur
- Physically based rendering
- Post-process effects (Tone-Mapping, FXAA, Sharpening, Dithering, Chromatic aberration etc.)
- SSAO (Screen space ambient occlusion)
- SSR (Screen space reflections)
- SSS (Screen space shadows)
- TAA (Temporal anti-aliasing based on Uncharted 4)
- Physics (Rigid bodies, Constraints, Colliders)
- Entity-component system
- Event system
- Input (Keyboard, Mouse, Xbox controller)
- Debug rendering (Transform gizmo, scene grid, bounding boxes, colliders, raycasts, g-buffer visualization etc)
- Thread pool
- Engine rendered platform agnostic editor
- Profiling (CPU & GPU)
- C/C++ (Using AngelScript)
- XML files
- Windows 10 and a modern/dedicated GPU (The target is high-end machines, old setups or mobile devices are not officially supported)
- Easy to build (Single click project generation which includes editor and runtime)

# Roadmap

##### v0.31 (WIP)
Feature     					| Completion    | Notes 
:-          					| :-            | :-
Volumetric Lighting				| 100%          | -
Screen space contact shadows 	| 100%          | -
Parallax Mapping 				| 100%          | -
Shader Editor 					| 100%          | Real-time shader editing tool.
Translucent colored shadows 	| 100%          | -
Shadows 						| 98%           | Enable point & spot light shadows.
Vulkan      					| 90%           | Don't port it, re-architect the engine instead.

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
- Atmospheric Scattering.
- Dynamic resolution scaling.
- Global Illumination.
- Export on Windows.
- UI components.
- Make editor more stylish.

# Documentation
- [Compiling](https://github.com/PanosK92/SpartanEngine/blob/master/.github/documentation/compiling_from_source.md) - A guide on how to compile from source.
- [Contributing](https://github.com/PanosK92/SpartanEngine/blob/master/.github/CONTRIBUTING.md) - Guidelines on how to contribute.

# Dependencies
- [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109).
- [Microsoft Visual C++ Redistributable for Visual Studio 2019](https://aka.ms/vs/16/release/VC_redist.x64.exe).
- Windows 10.

# License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/SpartanEngine/blob/master/LICENSE.txt) for details.
