# WebGPU and the web

WebGPU is Spartan's fourth RHI backend, next to DirectX 12, Vulkan and Metal. It lives in [`source/rhi/webgpu`](../source/rhi/webgpu) and is written against the standard `webgpu.h` C API, so the same code builds two ways:

| Build | Host | What implements `webgpu.h` | Underneath |
| ----- | ---- | -------------------------- | ---------- |
| **Native** | `spartan_webgpu_development.exe`, a normal Windows executable | [Dawn](https://dawn.googlesource.com/dawn), Google's WebGPU library | D3D12 |
| **Browser** | `spartan.html` + `spartan.wasm`, the engine compiled to WebAssembly by Emscripten | `emdawnwebgpu`, which forwards to the browser's `navigator.gpu` | the browser's own WebGPU (Dawn on D3D12 in Chrome, Edge and Brave on Windows) |

The native build is where the backend is developed and debugged: it compiles shaders, runs the MCP bridge and is fast to iterate on. The browser build runs the same engine and the full editor in a tab, with nothing installed.

Both are work in progress. The editor and worlds such as `pbr.world` render in the browser; the gaps are listed under [Limitations](#limitations).

## Shaders

The browser can't run DXC, so shaders follow one path and are cached:

1. The engine preprocesses the HLSL (includes, defines), the same as on every backend.
2. DXC compiles it to SPIR-V (`-fvk-use-gl-layout`, `-Zpc`).
3. Tint converts the SPIR-V to WGSL, and the backend rewrites the parts WGSL can't express (see `rewrite_source` in [`WebGPU_Shader.cpp`](../source/rhi/webgpu/WebGPU_Shader.cpp)).
4. The WGSL and its reflected bindings are written to `binaries/cache/generated_cache/webgpu_shaders`, keyed by a hash of the preprocessed source, defines, stage, entry point and profile.

The browser build only reads that cache. A shader missing from it logs `isn't in the shader cache` and the pass is skipped. After a shader edit, run the native WebGPU build once so it recompiles, then rebuild the web package. Line endings are stripped before hashing, so a cache filled on Windows matches the browser's lookups.

## Emulated features

The renderer is bindless and keeps buffers persistently mapped. WebGPU has neither, so the backend emulates them.

- **Persistently mapped buffers**: every buffer the engine treats as mapped has a CPU shadow. `GetMappedData()` returns the shadow, and before each submit the backend uploads the changed ranges with `wgpuQueueWriteBuffer`, using tracked dirty ranges or a page compare. Readbacks copy to a staging buffer and wait on `wgpuBufferMapAsync`. See [`WebGPU_Buffer.cpp`](../source/rhi/webgpu/WebGPU_Buffer.cpp).
- **Bindless buffers**: the engine's global buffers (materials, lights, geometry, draw data) get fixed slots in bind group 1. Each pipeline builds that group from the bindings its shader reflects, and a generation counter rebuilds it when a buffer is replaced.
- **Bindless textures**: not implemented. Core WebGPU has no binding arrays, so every material samples a single white texture for now. Shapes, lighting and material colours are correct; texture detail is missing.
- **Vertex pulling**: std430 aligns `float3` to 16 bytes, so pulled vertices are read as scalars (`PulledVertexScalars`, `load_pulled_vertex` in `common_resources_bindless.hlsl`).
- **Storage texture formats**: WGSL storage textures declare a format. When the bound texture's format differs from the reflected one, the backend creates a pipeline variant with the WGSL patched to match (`pipeline_variant`).
- **No dynamic state**: cull mode, blend and similar states are baked into pipelines, and variants are created on first use. A variant that is still compiling logs a one-off `can't bind` warning.

## Running a release

Every CI release on master carries two WebGPU downloads:

| Artifact | How to run it |
| -------- | ------------- |
| `spartan_webgpu_release_*.zip` | Unzip and run `spartan_webgpu.exe` (Windows 10/11, any D3D12 GPU). |
| `spartan_web_release_*.zip` | Unzip, run `python serve.py` in that folder (it opens `http://localhost:8080/spartan.html`; pass another port as the first argument), and use Chrome, Edge or Brave. Opening `spartan.html` straight from disk doesn't work, because browsers don't load WebAssembly from `file://`. |

The release web package boots the editor with an empty world. CI has no copy of the project assets, so a world with models has to be packaged locally (see [Browser build](#browser-build)).

## Native build (Windows)

```powershell
generate_project_files.bat vs2026 webgpu
```

Or run `generate_project_files.bat` and pick `[3] visual studio 2026 - webgpu`. Setup ([`tools/setup.lua`](../tools/setup.lua)) only fetches Dawn when the API is `webgpu`:

1. It downloads the pinned Dawn release from GitHub (`DAWN_VERSION`, sha256-checked, about 120 MB).
2. It links `webgpu_dawn.dll` from Dawn's static library with the MSVC linker, using the export list in `third_party/webgpu/webgpu_dawn.def`. Dawn ships `/MD` and the engine is `/MT`, so the DLL keeps the two runtimes apart.
3. It stages the DLL in `binaries/third_party` and `tint.exe` in `binaries`.

The headers in `third_party/webgpu` are vendored and match that version. To update Dawn, change `DAWN_VERSION`, `DAWN_FOLDER` and `DAWN_HASH` together with the headers and the `.def`.

Open `spartan.slnx` and build `development` or `release`; the executable is `binaries/spartan_webgpu_development.exe` (`spartan_webgpu.exe` in release). The first load of a world compiles every pipeline through Tint, which takes about a minute.

`--software-gpu` makes Dawn use WARP, the D3D12 software rasterizer that every Windows machine has. On a machine without a GPU, Dawn falls back to WARP on its own. CI uses this together with `--bake-shaders` to compile the browser's shaders. `spartan_webgpu.exe --bake-shaders` is also the fastest way to refresh the cache locally after a shader edit.

The MCP bridge works as on the other backends (`--mcp-control --mcp-port=<port>`). Use `screenshot_take {ui:true}`; the default viewport-cropped capture comes back black on this backend.

## Browser build

[`tools/web_build.py`](../tools/web_build.py) does everything; it needs [emsdk](https://emscripten.org/docs/getting_started/downloads.html) (found through `EMSDK` or `~/emsdk`), CMake, Ninja, Git and Python.

| Command | What it does |
| ------- | ------------ |
| `python tools/web_build.py deps` | Fetches and builds the wasm32 libraries (PhysX, Assimp, FreeImage, Lua, meshoptimizer) into `third_party/libraries/web`; sources go to `~/spartan_web_deps` (`SPARTAN_WEB_DEPS` overrides it) |
| `python tools/web_build.py engine --world <name>.world` | Fetches and builds anything `deps` hasn't yet, generates `binaries/build/web/build.ninja`, compiles the engine and links `binaries/web/spartan.html`, `.js`, `.wasm` and `.data`; without `--world` the package has no world |
| `python tools/web_build.py serve [--port 8080]` | Serves `binaries/web` with the cross-origin isolation headers |
| `python tools/web_build.py all` | `deps`, `engine`, then `serve` |

`--shader-cache <folder>` packages a WGSL cache from somewhere other than `binaries/cache/generated_cache/webgpu_shaders`.

From a fresh clone:

```powershell
generate_project_files.bat vs2026 webgpu        # dawn, data and the native project
# build release or development, then run the exe once (or open a world) so the shader cache fills
python tools/web_build.py engine --world pbr.world
python tools/web_build.py serve
```

Then open `http://localhost:8080/spartan.html` in a browser with WebGPU (Chrome, Edge and Brave on Windows have it on by default). The editor opens straight into the packaged world. `?world=worlds/<name>.world` picks another world if more than one was packaged. `binaries/web` also gets a copy of `serve.py`, so the folder can be zipped and served anywhere.

The first build of the dependencies and the engine takes a while; after that Ninja only rebuilds what changed.

### What gets packaged

Everything is preloaded into the browser's in-memory file system:

- `binaries/data` (without executables and symbols) as `/data`;
- the WGSL shader cache as `/cache/generated_cache/webgpu_shaders`, next to the executable, which is `/` in the browser;
- the chosen world, its asset folder and companion scripts as `/worlds`;
- every `project/...` asset that the world and its Lua scripts reference, as `/project`. Model files bring their whole folder so textures and buffers come along.

Native bake caches (meshlets, LODs, collision) aren't shipped; the browser bakes them on load. Nothing written at runtime persists between page loads.

### How the engine runs in a page

- **Toolchain**: wasm32, C++20, `-fwasm-exceptions`, SIMD128 (`-msimd128 -mavx2 -mfma`), with SDL3, FreeType and zlib from Emscripten ports.
- **Main loop**: the engine keeps its blocking loop. JSPI (`-sJSPI`) suspends the wasm stack at `emscripten_sleep(0)` once per frame (`Editor::Tick`), and wherever the backend waits on a WebGPU callback, so the browser can run its event loop and resolve promises.
- **Threads**: there are none, so the build doesn't need `SharedArrayBuffer`. The thread pool, the shader compiler queue and `ThreadPool::RunDetached` run work inline; PhysX uses a dispatcher with no workers and runs its tasks on the caller.
- **Window**: SDL3 maps the window to the `#canvas` element, and the swapchain is a WebGPU canvas surface. The splash screen is off because a second SDL window would claim the same canvas.
- **Logs**: the log is also printed to the browser's developer console, since the in-memory file system can't be opened from outside the page.
- **Compiled out**: the MCP server, DXC, Steam, OpenXR, RenderDoc, DLSS, XeSS and FSR, file dialogs and process spawning (Build Game).

## CI

The `build_webgpu` job in [`.github/workflows/workflow.yml`](../.github/workflows/workflow.yml) runs on Windows Server 2025:

1. It generates `vs2026 webgpu` (setup fetches and links Dawn), builds `release`, runs the GPU-free startup smoke test, and zips `spartan_webgpu_release.zip`.
2. It runs `spartan_webgpu.exe --software-gpu --bake-shaders` on WARP. `--bake-shaders` (any backend) starts the engine headless, waits until the shader queue has drained, and exits without rendering a frame. That fills the WGSL cache, about 130 modules in under half a minute on a desktop CPU. The job fails if the process hangs or writes fewer than 100 shaders.
3. It installs emsdk (pinned by `EMSDK_VERSION`), runs `python tools/web_build.py engine` without a world, and zips `spartan_web_release.zip`.

The release waits on this job like it does on the other platforms.

## Limitations

- No ray tracing: ReSTIR path tracing, ray-traced shadows and reflections are off. Worlds that rely on path tracing (`light_test.world` sets `r.restir_pt`) render dark.
- Bindless textures aren't implemented (see above).
- No tessellation stage.
- GPU BC1/BC3 texture compression fails in Tint, so those textures stay uncompressed.
- The browser build is single-threaded, so CPU-heavy worlds run slower than natively.
- wasm32 caps the heap at 4 GB; large worlds such as `plan.world` don't fit. Loading a second world in one page has run out of memory.
- Pointer warping isn't available in a browser, so camera controls that recentre the mouse log `SetMousePosition` errors.
