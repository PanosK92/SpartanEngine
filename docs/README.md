
<img align="left" width="128" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/logo.png"/>

# Spartan Engine [![Discord](https://img.shields.io/discord/677302405263785986?label=Discord)](https://discord.gg/TG5r2BS)

<p>An engine which is the result of my never-ending quest to understand how things work and has become my go-to place for research. It's designed around the philosophy of favoring fast real-time solutions over baked/static ones, a spartan approach offering a truly dynamic experience.</p>

<img align="right" width="128" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/rotating_gun.gif"/>

<p>If you're also on the lookout for knowledge, then you might find this engine to be a helpful study resource. This is because a lot of effort goes into into building and maintaining a clean, modern and overall high quality architecture, an architecture that will ensure continuous development over the years ;-) </p> 

<p><img align="left" width="32" src="https://valentingom.files.wordpress.com/2016/03/twitter-logo2.png"/>For occasional updates regarding the project's development, you can follow me on <a href="https://twitter.com/panoskarabelas1?ref_src=twsrc%5Etfw">twitter</a>.</p> 

<img align="left" width="32" src="https://www.freepnglogos.com/uploads/discord-logo-png/discord-logo-vector-download-0.png">For questions, suggestions, help and any kind of general discussion join the [discord server](https://discord.gg/TG5r2BS).

<img align="left" width="32" src="https://image.flaticon.com/icons/svg/25/25231.svg">For issues and anything directly related to the project, feel free to open an issue.

<img align="left" width="32" src="https://opensource.org/files/OSIApproved_1.png">Embracing the open source ethos and respecting the <a href="https://en.wikipedia.org/wiki/MIT_License">MIT license</a> is greatly appreciated. This means that you can copy all the code you want as long as you include a copy of the original license.</p>

## Download
Platform | API | Status | Quality | Binaries
:-:|:-:|:-:|:-:|:-:|
<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/docs/logo_windows.png" width="20"/>|<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/docs/logo_d3d11.png" width="100"/>|[![Build status](https://ci.appveyor.com/api/projects/status/p5duow3h4w8jp506/branch/master?svg=true)](https://ci.appveyor.com/project/PanosK92/spartanengine-d3d11/branch/master)|[![Codacy Badge](https://api.codacy.com/project/badge/Grade/da72b4f208284a30b7673abd86e8d8d3)](https://www.codacy.com/app/PanosK92/Directus3D?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=PanosK92/Directus3D&amp;utm_campaign=Badge_Grade)|[Download](https://ci.appveyor.com/api/projects/PanosK92/spartanengine-d3d11/artifacts/Binaries/release_d3d11.zip?branch=master)
<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/docs/logo_windows.png" width="20"/>|<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/docs/logo_vulkan.png" width="100"/>|[![Build status](https://ci.appveyor.com/api/projects/status/txlx815l43ytodij/branch/master?svg=true)](https://ci.appveyor.com/project/PanosK92/spartanengine-vulkan/branch/master)|*Beta*|[Download](https://ci.appveyor.com/api/projects/PanosK92/spartanengine-vulkan/artifacts/Binaries/release_vulkan.zip?branch=master)
<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/docs/logo_windows.png" width="20"/>|<img src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/docs/logo_d3d12.png" width="100"/>|[![Build status](https://ci.appveyor.com/api/projects/status/j77vml2hrt5o0oy0?svg=true)](https://ci.appveyor.com/project/PanosK92/spartanengine-d3d12)|*WIP*|WIP

## Media
[![](https://i.imgur.com/j6zIEI9.jpg)](https://www.youtube.com/watch?v=RIae1ma_DSo)
<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_screen_1.1.jpg"/>
<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_screen_2.1.jpg"/>
<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_screen_3.2.jpg"/>

## Features (v0.31)
- 10+ font file formats support (FreeType)
- 20+ audio file formats support (FMOD)
- 30+ image file formats support (FreeImage)
- 40+ model file formats support (Assimp)
- Vulkan and DirectX 11 backends (same HLSL shaders compile everywhere)
- Deferred rendering with transparency (under a single render path and using the same shaders)
- Principled BSDF supporting anisotropic, clearcoat and cloth materials (combined with things like normal mapping, parallax, masking, occlusion etc)
- Bloom (Based on a study of Resident Evil 2's RE Engine)
- Volumetric lighting
- Lights with physical units (lux for directional, candelas for point and spot lights)
- Shadows with penumbra and colored translucency (Cascaded and omnidirectional shadow mapping with Vogel filtering)
- SSAO (Screen space ambient occlusion)
- SSR (Screen space reflections)
- SSS (Screen space shadows)
- TAA (Temporal anti-aliasing based on Uncharted 4)
- Physically based camera (Aperture, Shutter Speed, ISO)
- Depth of field (controlled by the aperture of the camera)
- Motion blur (controlled by the shutter speed of the camera)
- Real-time shader editor
- Custom mip chain generation (Higher texture fidelity using Lanczos3 scaling)
- Font rendering
- Frustum culling
- Post-process effects (Tone-Mapping, FXAA, Sharpening, Dithering, Chromatic aberration etc.)
- Physics (Rigid bodies, Constraints, Colliders)
- Entity-component system
- Event system
- Input (Keyboard, Mouse, Xbox controller)
- Debug rendering (Transform gizmo, scene grid, bounding boxes, colliders, raycasts, g-buffer visualization etc)
- Thread pool
- Engine rendered platform agnostic editor
- Profiling (CPU & GPU)
- C# scripting (Mono)
- XML files
- Windows 10 and a modern/dedicated GPU (The target is high-end machines, old setups or mobile devices are not officially supported)
- Easy to build (Single click project generation which includes editor and runtime)

# Roadmap

##### v0.32-35 (WIP)
Feature     					 	| Completion 	| Notes 
:-          					 	| :-         	| :-
Continuous Vulkan optimisation 	 	| -		  		| Outperforms D3D11. Need to make more stable and uber optimise.
Screen space global illumination 	| 100%		  	| One bounce of indirect diffuse and specular light
Depth-of-field					 	| 100%       	| Controlled by Camera aperture
C# scripting (Replace AngelScript) 	| 50%			| Using Mono (no engine API exposed yet)
DirectX 12						 	| -				| Low priority
Eye Adaptation 					 	| -          	| Low priority
Subsurface Scattering 			 	| -          	| Low priority
Ray traced shadows				 	| -          	| -
Ray traced reflections			 	| -          	| -

###### Future
- Skeletal Animation.
- Atmospheric Scattering.
- Dynamic resolution scaling.
- Export on Windows.
- UI components.
- Make editor more stylish.

# Documentation
- [Compiling](https://github.com/PanosK92/SpartanEngine/blob/master/docs/compiling_from_source/compiling_from_source.md) - A guide on how to compile from source.
- [Contributing](https://github.com/PanosK92/SpartanEngine/blob/master/docs/CONTRIBUTING.md) - Guidelines on how to contribute.

# Dependencies
- [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109).
- [Microsoft Visual C++ Redistributable for Visual Studio 2019](https://aka.ms/vs/16/release/VC_redist.x64.exe).
- Windows 10.

# License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/SpartanEngine/blob/master/docs/LICENSE.txt) for details.
