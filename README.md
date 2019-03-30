# Directus3D Game Engine
Platform | Status | Quality | Binaries | :+1:
:-:|:-:|:-:|:-:|:-:|
<img src="https://doublslash.com/img/assets/Windows8AnimatedLogo.png" width="20" height="20"/>|[![Build status](https://ci.appveyor.com/api/projects/status/p5duow3h4w8jp506?svg=true)](https://ci.appveyor.com/project/PanosK92/directus3d)|[![Codacy Badge](https://api.codacy.com/project/badge/Grade/da72b4f208284a30b7673abd86e8d8d3)](https://www.codacy.com/app/PanosK92/Directus3D?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=PanosK92/Directus3D&amp;utm_campaign=Badge_Grade)|[Download](https://ci.appveyor.com/api/projects/PanosK92/directus3d/artifacts/Binaries/Release.zip?branch=master)|[![](https://www.paypalobjects.com/en_GB/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=CSP87Y77VNHPG&source=url)

<p align="left">Directus3D is a game engine that started as a hobby project and evolved into something greater.
The source code aims to be clean, modern and tackles a lot of aspects of engine development.
The project is at an early development stage and there is a lot experimentation going on, regarding what works best.
This means that the wisest thing to do is to approach it as study material, without expecting to make games with it (yet).
Architectual quality is prioritized over development speed.</p>

<img align="left" width="468" src="https://raw.githubusercontent.com/PanosK92/Directus3D/master/Data/screenshot-v0.3_preview4.jpg">
<img align="left" width="365" src="https://raw.githubusercontent.com/PanosK92/Directus3D/master/Data/rotating_gun.gif">

[
![](https://i.imgur.com/NRxQhSm.jpg)](https://www.youtube.com/watch?v=RIae1ma_DSo)

## Features (v0.3)
Category       	| Feature                                  	| Details
:-              | :-                                        | :-
Importing       | 10+ font file formats support             | FreeType
Importing       | 20+ audio file formats support            | FMOD
Importing       | 30+ image file formats support            | FreeImage
Importing       | 40+ model file formats support            | Assimp
Importing       | XML files                                 | -
Input           | Keyboard                                  | -
Input           | Mouse                                     | -
Input           | Xbox controller                           | -
Rendering       | Bloom                                     | -
Rendering       | Shadows                                   | Cascaded shadow mapping with smooth, clean and stable shadows.
Rendering       | Custom mipchain generation                | Higher texture fidelity using Lanczos3 scaling.
Rendering       | Debug rendering                           | Transform gizmo, scene grid, bounding boxes, colliders, raycasts, g-buffer visualization etc.
Rendering       | Deferred rendering                        | -
Rendering       | DirectX 11 backend                        | -
Rendering       | Lights                                    | Directional, point and spot lights.
Rendering       | Font Rendering                            | -
Rendering       | Frustum culling                           | -
Rendering       | Per-Pixel motion blur                     | -
Rendering       | Physically based rendering                | -
Rendering       | Post-process effects                      | Tone-Mapping, FXAA, Sharpening, Dithering, Chromatic aberration etc.
Rendering       | SSAO                                      | Screen space ambient occlusion.
Rendering       | SSR                                       | Screen space reflections.
Rendering       | TAA                                       | Temporal anti-aliasing based on Uncharted 4.
Physics         | Constraints                               | -
Physics         | Rigid bodies                              | -
Physics         | Colliders                                 | -
General         | Entity-component system                   | -
General         | Event system                              | -
General         | Easy to build                             | Single click project generation which includes editor and runtime.
General         | Thread pool                               | -
General         | Engine rendered platform agnostic editor  | -
General         | Profiling  								| CPU & GPU.
Scripting       | C/C++                                     | Using AngelScript.
Support        	| Windows 10 and a modern/dedicated GPU		| This engine targets high-end machines, old setups or mobiles devices are not officially supported.

#### v0.31 (WIP)
Feature     | Completion    | Notes 
:-          | :-            | :-
Vulkan      | 20%           | Will become the default rendering backend.

#### v0.32 (Planned)
Feature                 	| Completion    | Notes 
:-                      	| :-            | :-
Eye Adaptation          	| -            	| -
Depth-of-field          	| -           	| Based on Doom approach
Subsurface Scattering   	| -            	| -
Point & Spot light shadows	| -            	| -
Improve editor appearance	| -            	| -
Bug fixes and improvements	| -            	| Improve bloom quality, reduce render time, etc.

# Roadmap
- Volumetric Lighting.
- Dynamic resolution scaling.
- Real-time ray tracing (experimental).
- Global Illumination.
- C# scripting (replace AngelScript).
- DirectX 12 rendering backend.
- Draw call batching (static & dynamic).
- Export on Windows.
- Skeletal Animation.
- UI components.

# Documentation
- [Compiling](https://github.com/PanosK92/Directus3D/blob/master/Documentation/CompilingFromSource/CompilingFromSource.md) - A guide on how to compile from source

# Dependencies
- [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109).
- [Visual C++ 2017 (x64) runtime package](https://go.microsoft.com/fwlink/?LinkId=746572).
- Windows 10.

# License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/Directus3D/blob/master/LICENSE.txt) for details.
