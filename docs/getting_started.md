# Getting started

> **Just want to try Spartan?** Prebuilt binaries for Windows (DirectX 12, Vulkan, WebGPU), Linux, macOS and Web ship with every [GitHub release](https://github.com/PanosK92/SpartanEngine/releases). On first launch, the **World Launcher** downloads the project assets and starter worlds automatically. You can also try the [live WebGPU browser demo](https://panoskarabelas.com/engine/live/) with nothing installed.

---

## Supported platforms and backends

| Platform | Toolchain | CPU / GPU | Graphics backend | Output |
| -------- | --------- | --------- | ---------------- | ------ |
| **Windows 10/11 (x64)** | Visual Studio 2026 (`vs2022` also generates) | AVX2 CPU; D3D12 or Vulkan 1.3 GPU | **DirectX 12** *(recommended on Windows)*, **Vulkan**, **WebGPU** (Dawn) | `spartan.slnx` |
| **Linux (x64)** | GCC or Clang + `make` | AVX2 / FMA / F16C CPU; Vulkan 1.3 GPU | **Vulkan** *(recommended on Linux & non-Windows)* | `Makefile` |
| **macOS 26.0+ (Apple silicon)** | Xcode 26, Homebrew `cmake` & `ninja`, [Metal Shader Converter](https://developer.apple.com/metal/shader-converter/) | Apple silicon | **Metal 4** *(recommended on macOS)* | `spartan.xcworkspace` |
| **Web (Browser)** | [Emscripten](https://emscripten.org/) (`emsdk`), CMake, Ninja, Python 3 | Chrome, Edge or Brave with WebGPU | **WebGPU** (WebAssembly), see [WebGPU and the web](webgpu.md) | `binaries/web/spartan.html` |

Hardware ray tracing is optional. When supported, it powers ReSTIR path tracing, ray-traced shadows and ray-traced reflections; on GPUs without it, those features turn off cleanly.

---

## Project generator

Project files are generated with [Premake](https://premake.github.io/) via [`tools/premake.lua`](../tools/premake.lua) and [`tools/setup.lua`](../tools/setup.lua). Run the launcher script for your platform with **no arguments** for the interactive menu, or pass `<action> <api>` directly for scripts and CI.

### Interactive launcher

Double-click **`generate_project_files.bat`** (Windows) or run **`./generate_project_files.sh`** (Linux / macOS).

- **Workspace status**: shows the currently generated backend and whether the prebuilt library archive is cached locally.
- **Single-keypress selection**: press a key directly (no Enter required):
  - **`[1]`** — Visual Studio 2026 · **DirectX 12** *(recommended on Windows; includes D3D12 Agility SDK)*
  - **`[2]`** — Visual Studio 2026 · **Vulkan**
  - **`[3]`** — Visual Studio 2026 · **WebGPU** *(Windows, via Google Dawn)* or **GNU Make · Vulkan** *(recommended on Linux & non-Windows)*
  - **`[4]`** — **Xcode · Metal** *(recommended on macOS, in `generate_project_files.sh`)*
  - **`[S]`** — **Sync dependencies only** *(download, verify & stage SDKs without regenerating project files)*
  - **`[F]`** — **Fast regenerate** *(toggles `--skip-setup` to skip dependency checks when SDKs are already staged)*
  - **`[0]`** — Exit
- **Live pipeline feedback**: step-by-step progress bars for downloads (with size, throughput and ETA), SHA-256 verification, 7-Zip extraction and runtime DLL staging.
- **One-key open** *(Windows)*: once generation finishes, press **`[O]`** to open `spartan.slnx` in Visual Studio, **`[R]`** to return to the menu, or **`[Q]`** to exit.

### Non-interactive CLI (scripts and CI)

Pass the Premake action and optionally the graphics backend directly (omitting the backend defaults to `d3d12` on Visual Studio, `vulkan` on GNU Make, and `metal` on Xcode). CI environments (`CI`, `GITHUB_ACTIONS`, `NO_COLOR`, or `SPARTAN_PLAIN=1`) automatically switch to clean plain-text output without escape codes.

```powershell
# Windows: full setup + project generation (d3d12 is default; pass vulkan or webgpu for other backends)
generate_project_files.bat vs2026 d3d12

# Windows: fast regenerate without re-running dependency setup
generate_project_files.bat vs2026 d3d12 --skip-setup
```

---

## Build by platform

### Windows

1. **Generate the solution** (interactive menu, or pick a backend directly):

   ```powershell
   # interactive launcher (press 1 for DirectX 12, 2 for Vulkan, 3 for WebGPU)
   generate_project_files.bat

   # or non-interactive (d3d12 is default; pass vulkan or webgpu for other backends)
   generate_project_files.bat vs2026 d3d12
   ```

2. **Build and run**:
   - Press **`[O]`** in the launcher (or open `spartan.slnx` in Visual Studio 2026), choose **`development`** *(default)*, and build.
   - Or build from the command line with MSBuild:

   ```powershell
   msbuild spartan.vcxproj /p:Configuration=development /p:Platform=x64 /m
   ```

3. The executable lands in `binaries/` (for example `binaries/spartan_d3d12_development.exe`).

### Linux

Install the system/third-party dependencies once, generate the Makefile, and build:

```bash
tools/linux_dependencies.sh
./generate_project_files.sh gmake vulkan
make config=development_x64 -j$(nproc)   # or config=release_x64
```

### macOS

Requires **macOS 26.0+** on Apple silicon, **Xcode 26**, Homebrew (`brew install cmake ninja`), and Apple's [Metal Shader Converter](https://developer.apple.com/metal/shader-converter/).

```bash
tools/macos_dependencies.sh
./generate_project_files.sh xcode4 metal
```

Open `spartan.xcworkspace` in Xcode and build.

### Web (WebGPU / WebAssembly)

**Quick try (prebuilt):** download `spartan_web_webgpu_*.zip` from [releases](https://github.com/PanosK92/SpartanEngine/releases), unzip, run `python serve.py`, and open the printed URL in Chrome, Edge or Brave.

**Build from source:** browsers cannot run DXC at runtime, so shaders are compiled to WGSL once by the native Windows WebGPU build and cached for the browser package.

1. **Generate and build the native Windows WebGPU backend** (setup automatically downloads and links Google Dawn):

   ```powershell
   generate_project_files.bat vs2026 webgpu   # or press [3] in the interactive menu
   msbuild spartan.vcxproj /p:Configuration=development /p:Platform=x64 /m
   ```

2. **Populate the WGSL shader cache** (runs headless and exits as soon as all shaders finish compiling; add `--software-gpu` if no GPU is present):

   ```powershell
   .\binaries\spartan_webgpu_development.exe --bake-shaders
   ```

3. **Build the WebAssembly dependencies, package a world, and serve locally** (requires `emsdk`, CMake, Ninja, Git and Python 3):

   ```powershell
   python tools/web_build.py deps                                        # once: wasm32 PhysX, Assimp, FreeImage, Lua, meshoptimizer
   python tools/web_build.py engine --world pbr_materials_example.world  # outputs binaries/web/spartan.html, .js, .wasm, .data
   python tools/web_build.py serve                                       # serves http://localhost:8080/spartan.html
   ```

See [WebGPU and the web](webgpu.md) for architecture details, caching rules and limitations.

---

## Generator reference

### Actions, backends and options

| Argument | Values | Notes |
| -------- | ------ | ----- |
| **Action** | `vs2026`, `vs2022`, `gmake`, `gmake2`, `codelite`, `xcode4`, `setup` | `setup` syncs dependencies and runtime files without generating project files |
| **Graphics API** | `d3d12`, `vulkan`, `metal`, `webgpu` | Defines `API_GRAPHICS_<API>` and selects the matching RHI backend and vendor SDKs (defaults to `d3d12` on Visual Studio, `vulkan` on GNU Make, `metal` on Xcode) |
| `--skip-setup` | *(flag)* | Regenerates project files using already staged dependencies (same as `[F]` in the launcher) |
| `--build-tag=<name>` | letters, digits, `_` | Generates an isolated project under `binaries/build/<name>/` with a separate exe name so a running editor binary stays unlocked |

### Build configurations

Generation creates one project, `spartan`, with three configurations:

| Configuration | Purpose | Optimization & symbols | Example Windows executable |
| ------------- | ------- | ---------------------- | -------------------------- |
| **`development`** | Default daily driver | Optimized, full PDB symbols, incremental link, no LTO | `spartan_d3d12_development.exe` |
| **`debug`** | Stepping & diagnostics | Unoptimized, debug CRT | `spartan_d3d12_debug.exe` |
| **`release`** | Shipping / benchmarks | Optimized with LTO, symbols kept for crash dumps | `spartan_d3d12.exe` |

Executables are written to `binaries/`; compiler intermediates and symbols go to `binaries/build/`.

### What setup does

[`tools/setup.lua`](../tools/setup.lua) runs automatically during generation (unless `--skip-setup` is passed). On Windows it executes a verified, cached pipeline:

1. **Sync engine data** — copies `data/` (shaders, fonts, engine textures, prefabs) into `binaries/data/`.
2. **Core libraries archive** — downloads `libraries.7z` into `third_party/libraries/` and verifies its SHA-256 checksum (skipped automatically when cached).
3. **Extract libraries** — extracts the archive with 7-Zip, keeping only the files needed for the selected backend.
4. **Upscaling & frame generation SDKs** *(Vulkan / D3D12)* — verifies or fetches Intel XeSS, NVIDIA DLSS and AMD FidelityFX / FSR runtimes.
5. **DirectX 12 Agility SDK** *(D3D12 / WebGPU)* — fetches and installs the pinned Agility SDK headers and binaries.
6. **Steamworks SDK** — fetches the optional Steamworks redistributable headers and binaries.
7. **Google Dawn** *(WebGPU only)* — downloads Dawn, links `webgpu_dawn.dll`, and stages `tint.exe`.
8. **Stage runtime binaries** — copies required vendor DLLs into `binaries/third_party/`, Agility DLLs into `binaries/third_party/d3d12/`, and writes `binaries/steam_appid.txt` (Spacewar, app id `480`, for local development).

On Linux and macOS, `tools/linux_dependencies.sh` / `tools/macos_dependencies.sh` build the native third-party libraries, and setup syncs `binaries/data/` and the shader compiler shared libraries.

> **Editing shaders vs other files in `data/`:** when running from a source tree, the engine compiles shaders directly from the repository's `data/shaders/`. After editing non-shader assets in `data/`, re-run setup (`[S]` in the launcher or `generate_project_files.bat setup`) so `binaries/data/` picks up the changes.

### World assets

World definitions (`.world` files) are versioned in `worlds/`. Their binary assets (meshes, textures, materials, audio, scripts and `.car` files) live in `binaries/project/`, which is excluded from Git and downloaded on first launch by the **World Launcher**. See [`worlds/README.md`](../worlds/README.md).

---

## Run

The engine always switches its working directory to the executable's folder (`binaries/`, see [`source/core/main.cpp`](../source/core/main.cpp)), so resource paths are relative to `binaries/` (`project/...`, `data/...`, `../worlds/...`).

- Launch the executable and choose a world in the **World Launcher**, or press **Ctrl+O**.
- Press **F5** to play, **F6** to pause, and **Ctrl+P** to search every shortcut.

### Command-line arguments

| Argument | Effect |
| -------- | ------ |
| `-game` | Starts with editor panels hidden (editor code stays in the binary) |
| `--headless`, `-headless` | Hidden window, no editor UI, no Steam, does not overwrite `spartan.xml`, logs to `binaries/logs/log_headless.txt` |
| `--mcp-control` | Starts the [MCP bridge](mcp.md) on `127.0.0.1` |
| `--mcp-port=<n>` | Sets the MCP bridge port (default `47777`) |
| `--mcp-hidden` | Combined with `--mcp-control`, runs headless |
| `--no-steam` | Skips Steamworks initialization |
| `--bake-shaders` | Compiles all renderer shaders headless and exits without rendering a frame (populates shader caches, including WebGPU WGSL) |
| `--software-gpu` | WebGPU only: forces Dawn onto Windows WARP software rasterization |
| `--ci-smoke-test` | CPU-only startup test: no window or GPU, ten idle ticks, then exit (see [Testing and CI](testing_and_ci.md)) |
| `-ci_test` | Runs the GPU smoke-test suite after the first frame |
| `--ci-exit` | Closes the window once `-ci_test` finishes |

Example (driving a live instance through the MCP bridge):

```powershell
cd binaries
.\spartan_d3d12_development.exe --mcp-control --mcp-port=47777 --no-steam
```

The editor does not take a world path on the command line; load a world through the UI, the MCP `world_load` tool, or a packaged game's `game.xml`.

### Shipping a game

A packaged game is the same executable placed next to a `game.xml` manifest ([`source/core/GameManifest.h`](../source/core/GameManifest.h)). When `game.xml` is present, the editor and MCP bridge are disabled and the start world loads and plays directly. Open **Build Game** (**Ctrl+Shift+B**) in the editor to produce a distributable package; see [Editor](editor.md#game-builder).

---

## The `binaries/` layout

| Path | Contents |
| ---- | -------- |
| `binaries/` | Executables, `tint.exe` *(WebGPU)*, and `steam_appid.txt` |
| `binaries/third_party/` | Vendor DLLs (DXC, DLSS, XeSS, FidelityFX, Steam, Dawn); `third_party/d3d12/` holds the Agility SDK. Ship this folder next to the executable |
| `binaries/data/` | Engine data staged from the repository's `data/` |
| `binaries/project/` | Project and world assets downloaded by the World Launcher (not in Git) |
| `binaries/settings/` | `spartan.xml` (persisted cvars), `editor.ini`, game build options |
| `binaries/logs/` | `log.txt`, `log_headless.txt`, and previous session logs |
| `binaries/cache/` | Engine-wide generated data (pipeline caches, WebGPU WGSL shader cache) |
| `binaries/crashes/` | Crash dumps |
| `binaries/build/` | Compiler intermediates, symbols, and `--build-tag` private builds |

Older layouts that kept settings, logs and caches at the top level migrate automatically on first launch without overwriting existing files.

---

## Next steps

- [Architecture](architecture.md) — how the engine loop, threading, events and frame graph fit together.
- [Rendering](rendering.md) — frame pass order, bindless GPU-driven rendering, RHI backends and shaders.
- [WebGPU and the web](webgpu.md) — native Dawn builds, WGSL caching and Emscripten browser packaging.
- [Worlds, entities and components](world.md) — authoring scenes, prefabs and assets.
- [Lua scripting](scripting.md) and [Visual scripting](visual_scripting.md) — writing gameplay.
