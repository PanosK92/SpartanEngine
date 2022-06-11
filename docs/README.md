


<img align="left" width="128" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/logo256.png"/>

# Spartan Engine [![Discord](https://img.shields.io/discord/677302405263785986?label=Discord)](https://discord.gg/TG5r2BS)

<p>An engine which is the result of my never-ending quest to understand how things work and has become my go-to place for research. It's designed around the philosophy of favoring fast real-time solutions over baked/static ones, a spartan approach offering a truly dynamic experience.</p>

<p>If you're also on the lookout for knowledge, then you might find this engine to be a helpful study resource. This is because a lot of effort goes into into building and maintaining a clean, modern and overall high quality architecture, an architecture that will ensure continuous development over the years ;-) </p> 

<p><img align="left" width="32" src="https://valentingom.files.wordpress.com/2016/03/twitter-logo2.png"/>For occasional updates regarding the project's development, you can follow me on <a href="https://twitter.com/panoskarabelas1?ref_src=twsrc%5Etfw">twitter</a>.</p> 

<img align="left" width="32" src="https://www.freepnglogos.com/uploads/discord-logo-png/discord-logo-vector-download-0.png">For questions, suggestions, help and any kind of general discussion join the [discord server](https://discord.gg/TG5r2BS).

<img align="left" width="32" src="https://cdn-icons-png.flaticon.com/512/25/25231.png">For issues and anything directly related to the project, feel free to open an issue.

<img align="left" width="32" src="https://opensource.org/files/OSIApproved_1.png">Embracing the open source ethos and respecting the <a href="https://en.wikipedia.org/wiki/MIT_License">MIT license</a> is greatly appreciated. This means that you can copy all the code you want as long as you include a copy of the original license.</p>

## Build status
![build_status](https://github.com/PanosK92/SpartanEngine/actions/workflows/d3d11_d3d12_vulkan.yml/badge.svg)

## Media
[![](https://i.imgur.com/j6zIEI9.jpg)](https://www.youtube.com/watch?v=RIae1ma_DSo)
<img align="center" width src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_1.0.jpg"/>
<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_1.1.jpg"/>
<img align="center" src="https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_1.2.jpg"/>

# Livestreams
A first livestream has taken place, it's an introduction to the engine and myself, as requested by the Discord community.
I'll explore various things, more technical as well, as we go forward.

[![](https://raw.githubusercontent.com/PanosK92/SpartanEngine/master/Data/readme_1.3.jpg)](https://www.youtube.com/watch?v=QcytU6AKwqk)

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
- TAA (Temporal anti-aliasing)
- Physically based camera (Aperture, Shutter Speed, ISO)
- Depth of field (controlled by the aperture of the camera)
- Motion blur (controlled by the shutter speed of the camera)
- Real-time shader editor
- Custom mip chain generation (Higher texture fidelity using Lanczos3 scaling)
- Font rendering
- Frustum culling
- Physics (Rigid bodies, Constraints, Colliders)
- Entity-component system
- Event system
- Input (Mouse, Keyboard, Controller)
- Debug rendering (Transform gizmo, scene grid, bounding boxes, colliders, raycasts, g-buffer visualization etc)
- Thread pool
- Engine rendered platform agnostic editor
- Profiling (CPU & GPU)
- C# scripting (Mono)
- XML files
- Windows 10 and a modern/dedicated GPU (The target is high-end machines, old setups or mobile devices are not officially supported)
- Easy to build (Single click project generation which includes editor and runtime)
- AMD FidelityFX Contrast Adaptive Sharpening
- AMD FidelityFX Single Pass Downsampler
- AMD FidelityFX Super Resolution
- AMD Compressonator for texture compression

# Roadmap

##### v0.32-35 (WIP)
Feature     					 	| Completion | Notes 
:-          					 	| :-         | :-
SDL integration 					| 100%		 | Use SDL for window creation and input (this also enables PlayStation controller support).
Screen space global illumination 	| 100%		 | One bounce of indirect diffuse and specular light.
Depth-of-field					 	| 100%        | Controlled by camera aperture.
Temporal upsampling					| 50%		 | Reconstruct a high resolution output from a low resolution input (fix some remaining bugs).
DirectX 12						 	| 10%		 | RHI has already matured thanks to adding Vulkan, this should be easy.
C# scripting (Replace AngelScript) 	| 50%		 | Using Mono (low priority since the engine is not ready for scripting games).
Vulkan polishing 	 				| -		  	 | Optimise to outperform D3D11 in all cases and improve stability, things which will also make the engine API better.
Ray traced shadows				 	| -          | Will start work on it once I get an RTX GPU and deprecate D3D11 in favour of D3D12.
Ray traced reflections			 	| -          | Will start work on it once I get an RTX GPU and deprecate D3D11 in favour of D3D12.
Eye Adaptation 					 	| -          | Low priority.
Subsurface Scattering 			 	| -          | Low priority.
Linux support			 	        | -          | Vulkan and SDL is there, working on a linux port is now possible.

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
- [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109) - For the DirectX build.
- [Microsoft Visual C++ Redistributable for Visual Studio 2022](https://aka.ms/vs/17/release/vc_redist.x64.exe).
- Windows 10.

# License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/SpartanEngine/blob/master/docs/LICENSE.txt) for details.
