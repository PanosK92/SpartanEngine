# Getting started

## Requirements

| Platform | Toolchain | Graphics |
| -------- | --------- | -------- |
| Windows 10/11 x64 | Visual Studio 2026 (2022 also generates), CPU with AVX2 | DirectX 12 or Vulkan |
| Linux x64 | GCC or Clang with `make`, CPU with AVX2/FMA/F16C | Vulkan |
| macOS 26.0+ on Apple silicon | Xcode 26, Homebrew `cmake` and `ninja`, [Metal Shader Converter](https://developer.apple.com/metal/shader-converter/) | Metal 4 |
| Web (WIP) | [Emscripten](https://emscripten.org/) (emsdk), CMake, Ninja, Python; a browser with WebGPU (Chrome, Edge, Brave) | WebGPU, see [WebGPU and the web](webgpu.md) |

Hardware ray tracing is needed for ReSTIR path tracing, ray-traced shadows and ray-traced reflections. The renderer runs without it, with those features off.

## Build

Project generation is done by [Premake](https://premake.github.io/) through [`tools/premake.lua`](../tools/premake.lua). The first argument is the Premake action, the second the graphics API.

### Windows

```powershell
generate_project_files.bat vs2026 vulkan   # or d3d12; no arguments shows a menu
```

Open `spartan.slnx` and build. Generation also runs the setup step described below.

### Linux

```bash
tools/linux_dependencies.sh && ./generate_project_files.sh gmake vulkan && make config=release_x64
```

### macOS

```bash
tools/macos_dependencies.sh && ./generate_project_files.sh xcode4 metal
```

Then open `spartan.xcworkspace`.

### Web

To just try it, download `spartan_web_release_*.zip` from the [releases](https://github.com/PanosK92/SpartanEngine/releases), unzip it, run `python serve.py` and open the printed address in Chrome, Edge or Brave.

The browser build is the WebGPU backend compiled to WebAssembly by [`tools/web_build.py`](../tools/web_build.py). Generate and build the Windows WebGPU backend first (`generate_project_files.bat vs2026 webgpu`, or option `[3]` in the menu; setup downloads Dawn), then run it once so the WGSL shader cache exists:

```powershell
python tools/web_build.py deps                        # once, wasm32 PhysX, Assimp, FreeImage, Lua, meshoptimizer
python tools/web_build.py engine --world pbr.world    # binaries/web/spartan.html, .js, .wasm, .data
python tools/web_build.py serve                       # http://localhost:8080/spartan.html
```

Details and limitations are in [WebGPU and the web](webgpu.md).

### Actions, APIs and configurations

| Argument | Values |
| -------- | ------ |
| Action | `vs2026`, `vs2022`, `gmake`, `gmake2`, `codelite`, `xcode4`, and `setup` (dependencies only) |
| Graphics API | `d3d12` (defines `API_GRAPHICS_D3D12`), `vulkan` (`API_GRAPHICS_VULKAN`), `metal` (`API_GRAPHICS_METAL`), `webgpu` (`API_GRAPHICS_WEBGPU`, Windows through Dawn) |
| Option | `--skip-setup` generates without downloading dependencies |

There is one project, `spartan`, built in three configurations:

| Configuration | Purpose | Windows executable (Vulkan) |
| ------------- | ------- | --------------------------- |
| `development` | Default. Optimized, full symbols, no LTO | `spartan_vulkan_development.exe` |
| `debug` | Unoptimized, debug CRT | `spartan_vulkan_debug.exe` |
| `release` | Optimized with LTO, symbols kept for crash dumps | `spartan_vulkan.exe` |

Executables are written to `binaries/`; intermediates go to `binaries/build/`.

### What setup does

[`tools/setup.lua`](../tools/setup.lua) runs during generation unless `--skip-setup` is passed. On Windows it:

1. Copies `data/` (shaders, fonts, engine textures, prefabs) to `binaries/data/`.
2. Downloads and verifies (SHA-256) the prebuilt third-party libraries into `third_party/libraries/`.
3. Fetches the DLSS, XeSS, FidelityFX/FSR and D3D12 Agility SDK runtimes and the Steamworks SDK.
4. Stages the runtime DLLs into `binaries/third_party/` and writes `binaries/steam_appid.txt` (Spacewar, app id 480, for local development).

On Linux and macOS the dependency scripts build the libraries and setup copies `data/` and the shader compiler libraries.

Shaders are the exception: when run from a source tree the engine compiles them straight from the repository's `data/shaders`. For anything else in `data/`, re-run setup so `binaries/data/` picks it up:

```powershell
tools/premake5.exe --file=tools/premake.lua setup
```

### World assets

World files live in `worlds/` and are versioned. Their assets (meshes, textures, materials, audio, `.car` files) live in `binaries/project/`, which is not in Git and is downloaded as `project.7z` during setup. See [`worlds/README.md`](../worlds/README.md).

## Run

The engine always runs with `binaries/` as its working directory ([`source/core/main.cpp`](../source/core/main.cpp) sets it to the executable's folder), so every resource path is relative to it: `project/...`, `data/...`, `../worlds/...`.

Launch the executable and pick a world from the **Worlds** launcher, or use **Ctrl+O**. Press **F5** to play, **F6** to pause. **Ctrl+P** lists every shortcut.

### Command-line arguments

| Argument | Effect |
| -------- | ------ |
| `-game` | Starts with the editor panels hidden (the editor code stays in the binary) |
| `--headless`, `-headless` | Hidden window, no editor UI, no Steam, does not save settings, logs to `logs/log_headless.txt` |
| `--mcp-control` | Starts the [MCP bridge](mcp.md) on `127.0.0.1` |
| `--mcp-port=<n>` | Bridge port (default `47777`) |
| `--mcp-hidden` | With `--mcp-control`, runs headless |
| `--no-steam` | Skips Steamworks initialization |
| `--ci-smoke-test` | CPU-only start-up test: no window or GPU, ten idle ticks, then exit (see [Testing and CI](testing_and_ci.md)) |
| `-ci_test` | Runs the GPU smoke-test suite after the first frame |
| `--ci-exit` | Closes the window once `-ci_test` finishes |

Example, as used by AI agents driving the engine:

```powershell
cd binaries
.\spartan_vulkan_development.exe --mcp-control --mcp-port=47777 --no-steam
```

The editor does not take a world on the command line; load one through the UI, the MCP `world_load` command, or a packaged game's `game.xml`.

### Shipping a game

A packaged game is the same executable next to a `game.xml` manifest ([`source/core/GameManifest.h`](../source/core/GameManifest.h)). When the manifest is present the editor and the MCP bridge are disabled and the start world loads and plays immediately. The **Build Game** panel (**Ctrl+Shift+B**) produces such a package; see [Editor](editor.md#game-builder).

## The `binaries/` folder

| Path | Contents |
| ---- | -------- |
| `binaries/` | Executables and `steam_appid.txt` |
| `binaries/third_party/` | Vendor DLLs (DXC, DLSS, XeSS, FidelityFX, Steam); `third_party/d3d12/` holds the Agility SDK. Ship this folder next to the executable |
| `binaries/data/` | Engine data staged from the repository's `data/` |
| `binaries/project/` | Project and world assets (downloaded, not in Git) |
| `binaries/settings/` | `spartan.xml` (persisted cvars), `editor.ini`, game build options |
| `binaries/logs/` | `log.txt`, `log_headless.txt`, and the previous session's logs |
| `binaries/cache/` | Engine-wide generated data such as pipeline caches |
| `binaries/crashes/` | Crash dumps |
| `binaries/build/` | Compiler intermediates and Windows symbols |

Older layouts that kept settings, logs and caches at the top level are migrated on first launch without overwriting existing files.

## Next

- [Architecture](architecture.md) for how a frame runs.
- [Worlds, entities and components](world.md) to start building a scene.
- [Lua scripting](scripting.md) to write gameplay.
