# Directus3D
Directus3D is a game engine that aims to be powerful yet simple. This is achieved by prioriziting a coherent and modern design and a minimalistic approach when it comes to adding new features. 
The editor is greatly inspired by Unity, it should make the engine feel familiar and pleasurable to use.

The engine isn't production ready yet, meaning you can certainly make playble scenes within the editor but don't expect to export a complete game. 
On the plus side, this is an ongoing project, so it will get there. Exporting on Windows will receive higher priority over other platforms.

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
- Ability to save anything in a custom engine format, total revamp of scene IO, will bring great flexibility to the editor <- Currently working on this
- Audio source and listener components.
- DirectX 12 Renderer
- Draw call batching (static & dynamic).
- Export on Windows.
- Font loader & renderer.
- SSAO, SSR, Volumetric Lighting.
- UI components.
- Vulkan Renderer.

Note: Regarding DirectX 12 and Vulkan, these are the most high priority planned features, research is being conducted as to avoid a simple porting from DirectX 11 which can result in lower performance. Most likely a new rendering approach will be written so there are actual gains in terms of performance & memory usage.

### Download
- [Directus v0.1.1 (Windows x64)] (https://onedrive.live.com/download?resid=96760D43099D7718%21125980&authkey=AC4fwz-Zxcv-dJo)

### Dependencies
- Runtime: [DirectX End-User Runtimes](https://www.microsoft.com/en-us/download/details.aspx?id=8109), [Visual C++ 2015 (x64) runtime package](https://www.microsoft.com/en-us/download/details.aspx?id=48145).
- Editor: [Qt](https://www.qt.io/).

### License
- Licensed under the MIT license, see [LICENSE.txt](https://github.com/PanosK92/Directus3D/blob/master/LICENSE.txt) for details.

This project is devoted to my father.
