# Directus3D
Directus3D is a game engine that aims to be powerful yet simple. This is achieved by prioriziting a coherent and modern design and a minimalistic approach when it comes to adding new features. 
The editor is greatly inspired by Unity, it should make the engine feel familiar and pleasurable to use.

![Screenshot](https://raw.githubusercontent.com/PanosK92/Directus3D/master/Directus3D/Assets/screenshot.jpg)

# Features
- 30+ image file formats support.
- 40+ 3D file formats support.
- Build as single static or dynamic library.
- Cascaded shadow mapping.
- Component-based game object system.
- Cross-Platform state of the art editor.
- D3D11 Renderer
- Deferred rendering.
- Frustum culling.
- Physically based shading.
- Physics.
- Post-process effects like FXAA & Sharpening.
- Scripting.
- Windows support.

# Planned Features
- Ability to save materials, meshes, textures in a custom format, total revamp of scene IO <- Currently working on this
- Ability to use worker threads in the engine runtime too, not just the editor.
- Audio source and listener components.
- DirectX 12 Renderer
- Draw call batching (static & dynamic).
- Export on Windows.
- Font loader & renderer.
- SSAO, SSR, Volumetric Lighting.
- UI components.
- Vulkan Renderer.

Note: Regarding DirectX 12 and Vulkan, these are the most high priority planned features, research is being conducted as to avoid a simple porting from DirectX 11 which can result in lower performance. 
Most likely a new rendering approach will be written so there are actual gains in terms of performance & memory usage. I am also looking into adding dynamic resolution support, similar to iD Tech 6 (Doom)

### Download
- [Directus v0.1.1 (Windows x64)] (https://onedrive.live.com/download?resid=96760D43099D7718%21125980&authkey=AC4fwz-Zxcv-dJo)

### Dependencies
- Runtime: [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109), [Visual C++ 2015 (x64) runtime package](https://www.microsoft.com/en-us/download/details.aspx?id=48145).
- Editor: [Qt](https://www.qt.io/).

### License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/Directus3D/blob/master/LICENSE.txt) for details.

This project is devoted to my father.
