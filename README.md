[![Build status](https://ci.appveyor.com/api/projects/status/p5duow3h4w8jp506?svg=true)](https://ci.appveyor.com/project/PanosK92/directus3d)

# About Directus3D
Directus3D is a game engine that started as a hobby project and evolved into something greater.
The source code is clean, modern and tackles a lot of aspects of engine development. Have fun!

Note: This project is in an early development stage and there is a lot experimentation going on, regarding what works best.
As a result, no design decision is set in stone and it will take a while until the engine is ready to produce games.
Code quality is prioritized over development speed.

![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Prerequisites/screenshot-v0.3_preview.jpg)

# Features
- 20+ audio file formats support.
- 30+ image file formats support.
- 40+ 3D file formats support.
- Cascaded shadow mapping.
- Component-based game object system.
- Cross-Platform state of the art editor.
- D3D11 rendering backend
- Deferred rendering.
- Frustum culling.
- Multithreading.
- Physically based shading.
- Physics.
- Post-process effects like FXAA & LumaSharpen.
- Scripting (C/C++).
- Windows support.

# Upcoming features (v0.3)
Feature       		            | Completion | Notes 
------------- 		            | :--: | -
Easy to build               	| 100% | Single click project generation which includes editor and runtime.
New editor               		| 60% | Replace Qt editor with ImGui editor.
Debug Rendering    				| 90% | Gizmos, scene grid and G-Buffer visualization.
Improved shadows         		| 90% | Sharper shadows with smoother edges and no shimmering.
SSAO         					| 90% | -
Custom mipchain generation 		| 100% | Higher texture fidelity using Lanczos3 scaling.
Improved debug view             | 100% | Bounding boxes, colliders, raycasts, etc.
Point light support             | 100% | -
XML I/O                         | 100% | -
Architecture improvements       | 100% | Performance improvements, bug fixes and overall higher quality codebase.
Font importing and rendering    | 100% | Ability to load any font file

# Roadmap
- C# scripting.
- Vulkan rendering backend.
- Dynamic resolution scaling.
- Draw call batching (static & dynamic).
- Export on Windows.
- Skeletal Animation.
- Screen space reflections.
- Volumetric Lighting.
- Global Illumination.
- UI components.

### Download
Note: Binaries for v0.2 are old and don't reflect the current state of the engine. 
Until v0.3 comes out, it's advised that you compile from source.
- [Binaries](https://onedrive.live.com/download?cid=96760D43099D7718&resid=96760D43099D7718%21130409&authkey=AEEN_tM_7MOzWzc) - Binaries for v0.2 64-bit (65.8 MB).
- [Compiling](https://github.com/PanosK92/Directus3D/blob/master/Documentation/CompilingFromSource/CompilingFromSource.md) - A guide on how to compile the engine.

### Dependencies
- [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109).
- [Visual C++ 2017 (x64) runtime package](https://go.microsoft.com/fwlink/?LinkId=746572).

### License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/Directus3D/blob/master/LICENSE.txt) for details.