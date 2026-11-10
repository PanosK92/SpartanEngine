# Architecture

Spartan is a single C++20 executable. Everything lives in `namespace spartan`, most subsystems are static classes with `Initialize`, `Tick` and `Shutdown`, and there is no C++ game layer: gameplay is components and [Lua](scripting.md).

## Source layout

| Folder | Responsibility |
| ------ | -------------- |
| [`core`](../source/core) | `Engine`, `Window`, `Timer`, `Event`, `Settings`, `ThreadPool`, `ProgressTracker`, `GameManifest`, process entry |
| [`world`](../source/world) | `World`, `Entity`, all components, prefabs, terrain, weather, roads and world systems |
| [`rendering`](../source/rendering) | The renderer: passes, materials, the global geometry buffer, lights, IES profiles |
| [`rhi`](../source/rhi) | Render hardware interface with `d3d12/`, `vulkan/`, `metal/` and `webgpu/` backends, shader compilation, texture streaming, upscalers |
| [`physics`](../source/physics) | PhysX world, fixed-step scheduling, queries, contact reporting |
| [`car`](../source/car) | Vehicle simulation, AI drivers, car audio, HUD, telemetry, car weather effects |
| [`scripting`](../source/scripting) | Lua 5.4 state and all bindings (sol2) |
| [`resource`](../source/resource) | `ResourceCache`, model/image/font importers, animation I/O |
| [`geometry`](../source/geometry) | `Mesh`, procedural geometry, tree generator, generated cache |
| [`animation`](../source/animation) | Skeletons, clips, pose evaluation, IK |
| [`audio`](../source/audio) | Audio mixer groups (playback lives in the `AudioSource` component) |
| [`navigation`](../source/navigation) | Recast/Detour streamed navigation mesh |
| [`input`](../source/input) | Keyboard, mouse, gamepad, steering wheel, input injection |
| [`editor`](../source/editor) | ImGui editor, widgets, game builder, editor-only MCP commands |
| [`mcp`](../source/mcp) | In-engine TCP command bridge |
| [`commands`](../source/commands) | Console variables (`console/`) and the undo/redo command stack |
| [`ui`](../source/ui) | ImGui bootstrap and the in-game `hud` drawing API |
| [`profiling`](../source/profiling) | CPU/GPU profiler, metrics overlay, RenderDoc and GPU breadcrumbs |
| [`memory`](../source/memory) | mimalloc-backed tagged allocator, global `new`/`delete` overrides |
| [`logging`](../source/logging) | `Log` and the `SP_LOG_*` macros |
| [`error_handling`](../source/error_handling) | Crash handler, crash reporter process, fatal errors |
| [`math`](../source/math) | Vectors, matrices, quaternions, bounding volumes, SIMD |
| [`display`](../source/display) | Display modes and HDR output |
| [`font`](../source/font) | Font atlas generation |
| [`file_system`](../source/file_system) | Paths, file I/O, downloads |
| [`io`](../source/io) | Vendored pugixml |
| [`steam`](../source/steam) | Steamworks wrapper |
| [`xr`](../source/xr) | OpenXR session |
| [`testing`](../source/testing) | Smoke tests |
| [`runtime`](../source/runtime) | Minimal game loop (`RunRuntime`, compiled with `SP_RUNTIME`). Not part of the generated project |

HLSL shaders are in [`data/shaders`](../data/shaders), shared with C++ through `shared_buffers.h`.

## Engine lifecycle

[`source/core/main.cpp`](../source/core/main.cpp) installs the crash handler, sets the working directory to the executable's folder, creates `settings/`, `logs/` and `cache/`, then constructs the `Editor` ([`source/editor/Editor.cpp`](../source/editor/Editor.cpp)), which calls `Engine::Initialize` and runs the loop.

### Initialize

`Engine::Initialize(args)` in [`Engine.cpp`](../source/core/Engine.cpp), in order:

1. Load `game.xml` if present; set the engine mode flags.
2. `Log`, then `Settings::LoadPreInitSettings()` so persisted cvars apply before the GPU exists.
3. Font and image importers, `Window` (SDL3), `Display`, `Timer`, `Input`, `ThreadPool`, `ResourceCache`, `Profiler`.
4. On worker threads: `PhysicsWorld::Initialize()` and editor icon decoding. In parallel, on the main thread: `Renderer::Initialize()`.
5. `World::Initialize()` (which creates the Lua state), `Settings::Initialize()`, `SmokeTest::Initialize()`.
6. `McpServer::Initialize(args)` when `--mcp-control` is passed (never in packaged games).
7. `Steam::Initialize()` unless `--no-steam` or headless.
8. Join the worker tasks, apply GPU-based default cvars on first run, load default resources.

The editor then installs the world lifecycle hooks (`world_systems::Initialize()`) and registers MCP commands and widgets.

### The frame

The loop in `Editor::Tick()`:

```
while window is open
    Profiler::FrameStart
    World::ProcessPendingLoad       start a queued world load
    ImGui new frame
    Engine::Tick
    editor widgets                   only if EditorVisible and not headless
    Timer::PostTick, Profiler::PostTick
```

`Engine::Tick()`:

| Step | Work |
| ---- | ---- |
| 1 | `Input::PreTick`, `McpServer::Tick` (runs queued bridge commands on the main thread), `Steam::Tick` |
| 2 | Global keys: **F12** screenshot, **Ctrl+0** toggles OpenXR |
| 3 | `Window::Tick`, `Input::Tick`, fire `InputTicked` |
| 4 | `PhysicsWorld::Tick` decides how many 5 ms physics steps this frame needs |
| 5 | `World::Tick` runs entities and components, then fires `WorldTicked` |
| 6 | `PhysicsWorld::Simulate` starts the physics steps on a worker |
| 7 | `Xr::Tick`, `Renderer::Tick` render while physics runs |
| 8 | `PhysicsWorld::WaitForSimulation`, `Allocator::Tick` |

Physics overlapping rendering is the reason component state written in `Tick` is read by the renderer of the same frame while PhysX integrates the next step. See [Physics and vehicles](physics_and_vehicles.md).

### Shutdown

Reverse dependency order: Steam, MCP, profiler, flush the thread pool, `World`, thread pool, default resources, physics, XR, renderer, events, window, importers, and finally `Settings::Shutdown()`, which saves cvars to `settings/spartan.xml` (skipped when headless).

## Engine modes

`EngineMode` flags in [`Engine.h`](../source/core/Engine.h):

| Flag | Meaning |
| ---- | ------- |
| `EditorVisible` | Editor panels drawn. Off with `-game` or in packaged games |
| `Playing` | Simulation running: scripts, physics, vehicles |
| `Paused` | With `Playing`, freezes simulation while keeping the play state |

Query with `Engine::IsFlagSet(EngineMode::Playing)`; change with `SetFlag`/`ToggleFlag`. Starting play captures every authored transform so stopping restores the scene exactly; starting components is spread across frames by `PlaySession`.

## Events

[`source/core/Event.h`](../source/core/Event.h) is a synchronous, thread-safe event bus.

```cpp
SP_SUBSCRIBE_TO_EVENT(EventType::WorldLoaded, SP_EVENT_HANDLER_STATIC(on_world_loaded));
SP_FIRE_EVENT(EventType::WorldLoaded);
SP_FIRE_EVENT_DATA(EventType::EntityRemoving, entity);
```

| Event | Fired when |
| ----- | ---------- |
| `RendererOnFirstFrameCompleted` | The first frame has been presented |
| `RendererOnShutdown` | The renderer is shutting down |
| `Sdl` | An SDL event arrives (payload: the event) |
| `WindowResized`, `WindowFullScreenToggled` | Window changes |
| `HdrToggled` | HDR output is switched |
| `InputTicked` | Input is current; world controls have not run yet |
| `EntityRemoving` | An entity is about to be removed (payload: `Entity*`) |
| `WorldUnloading` | Before the resource cache drops the old world |
| `WorldLoaded` | A world finished loading |
| `WorldTicked` | After entity and component ticks, before rendering |

## Threading

[`ThreadPool`](../source/core/ThreadPool.h) sizes itself to the hardware.

```cpp
std::future<void> done = ThreadPool::AddTask([]() { /* work */ });

ThreadPool::ParallelLoop([&](uint32_t start, uint32_t end)
{
    for (uint32_t i = start; i < end; i++) { /* item i */ }
}, item_count);
```

`ParallelLoop` called from a worker runs inline, so nested parallelism is safe. `ThreadPool::Flush()` waits for all tasks. Entities created on a worker must be wrapped in a `World::EntityBatch` and published with `Commit()`; see [World](world.md#creating-entities-off-the-main-thread).

The main thread owns the world, the renderer and the Lua state. Bridge commands are marshalled onto it by `McpQueue`.

## Timing

[`Timer`](../source/core/Timer.h): `GetDeltaTimeSec()`, `GetTimeSec()`, smoothed deltas, `SetTimeScale()` (slow motion also scales physics steps), `SetFpsLimit()`. Physics runs on its own fixed step regardless of frame rate.

## Settings

All console variables are persisted to `binaries/settings/spartan.xml` on shutdown and loaded before the GPU is created. Worlds can override cvars for themselves in their `<ConsoleVariables>` block. See [Console variables](console_variables.md).

## Logging

```cpp
SP_LOG_INFO("loaded %s in %.2f ms", name.c_str(), ms);
SP_LOG_WARNING("...");
SP_LOG_ERROR("...");
```

Messages go to the editor console, and to `logs/log.txt` when `debug.log_to_file` is on. Lua `print` routes to `SP_LOG_INFO` with a `[Lua]` prefix.

## Profiling

```cpp
void Something::Tick()
{
    SP_PROFILE_CPU();                    // scope named after the function
    SP_PROFILE_CPU_START("sub step");
    // ...
    SP_PROFILE_CPU_END();
}
```

GPU timings are recorded by the RHI command lists around each pass. The **Profiler** panel shows CPU, graphics and async compute lanes on a timeline; the **Memory** panel shows CPU and GPU allocations and fragmentation. RenderDoc (`debug.renderdoc`) and GPU breadcrumbs for crash tracing (`debug.breadcrumbs`) are opt-in cvars that take effect on restart.

## Memory

[`source/memory/Allocator.cpp`](../source/memory/Allocator.cpp) wraps mimalloc and tags allocations by subsystem; global `new`/`delete` route through it. Hot types use `PooledObject` ([`core/PooledObject.h`](../source/core/PooledObject.h)).

## Crashes and fatal errors

On Windows, [`CrashHandler`](../source/error_handling/CrashHandler.cpp) catches the crash and starts a second copy of the executable (`--spartan-crash-report`) that writes the dump to `crashes/` and reports it. Headless and smoke-test runs skip the interactive dialog. `ErrorHandler::Fatal` ends the process with a categorized reason (out of GPU memory, device lost, and so on).

## Engine and world boundary

- [`source/world/WorldSystems.cpp`](../source/world/WorldSystems.cpp) installs the `WorldCallbacks` hooks (load, prepare, tick, stop play) that connect cars, camera control, terrain preparation and the optional world systems to the generic `World`.
- Vehicle physics lives in `source/car`, the vehicle MCP commands in `source/mcp/McpCommandsCar.cpp`, and traffic, race drivers, route drivers, car resets and skid marks are ordinary components in `source/world/components`.
- Physics stepping and contacts use game-independent callbacks; the car code registers its own.
- Component attributes exposed for editing go through the component's setters; derived fields are read-only.
