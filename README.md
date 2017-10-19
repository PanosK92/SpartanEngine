# About Directus3D
Directus3D is a game engine that started as a hobby project and evolved into something greater.
The source code is simple, modern and tackles a lot of aspects of engine development. Have fun!

Note: This project is still in an early development stage and there is a lot experimentation in finding what works best.
As a result, no design decision is set in stone and it will take a while until it's production ready. Having a full-time
job, I invest whatever time I have left on it. However, if you wish for things to move faster and help shape it, feel free to contribute.

![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Runtime/Assets/screenshot-v0.3_preview.png)

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
Custom mipchain generation 		| 100% | Higher texture fidelity using Lanczos3 scaling.
New editor theme                | 100% | A minimal/clean look.
Improved debug view             | 100% | Bounding boxes, colliders, raycasts, etc.
Point light support             | 100% | -
XML I/O                         | 100% | -
Architecture improvements       | 100% | Performance improvements, bug fixes, less crashes and overall higher quality codebase.
Improved Shadowing         		| 80% | Improved shadow mapping technique, no shadow shimmering, SSAO.
Font importing and rendering    | 80% | Ability to load any font file. TextMesh component.
Debug Rendering    				| 90% | Gizmos, scene grid and G-Buffer visualization.
Skeletal Animation			    | 5% | -

# Roadmap
- C# scripting.
- Vulkan rendering backend.
- Dynamic resolution scaling.
- Draw call batching (static & dynamic).
- Export on Windows.
- Screen space reflections.
- Volumetric Lighting.
- Global Illumination.
- UI components.
- Replace Qt with a custom editor.

### Download
Note: Binaries form v0.2 requite old and don't reflect the current state of the engine. Until v0.3 comes out, it's adviced that you compile from source.
- [Binaries](https://onedrive.live.com/download?cid=96760D43099D7718&resid=96760D43099D7718%21130409&authkey=AEEN_tM_7MOzWzc) - v0.2 (64-bit, 65.8 MB).
- [Compiling](https://github.com/PanosK92/Directus3D/blob/master/Documentation/CompilingFromSource/CompilingFromSource.md) - Learn how to go from text to zeroes and ones :-)

### Dependencies
- [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109).
- [Visual C++ 2017 (x64) runtime package](https://go.microsoft.com/fwlink/?LinkId=746572).

### License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/Directus3D/blob/master/LICENSE.txt) for details.