# Spartan Engine documentation

Spartan was documented by its code for twelve years. These pages are the map: they explain how the pieces fit, where each one lives and which file to open next. The code remains the source of truth, so every page links to it.

## Start here

| Page | Read it when you want to |
| ---- | ------------------------ |
| [Getting started](getting_started.md) | Build the engine, run it, and learn the command-line flags and the `binaries/` layout |
| [Architecture](architecture.md) | Understand the engine loop, the subsystems, threading, events, logging and profiling |
| [Worlds, entities and components](world.md) | Build scenes: the entity model, every component, the `.world` format, prefabs and resources |
| [Lua scripting](scripting.md) | Write gameplay: script lifecycle, world companion scripts and the full Lua API |
| [Visual scripting](visual_scripting.md) | Build gameplay as node graphs that compile to Lua, with a tutorial world |

## Systems

| Page | Covers |
| ---- | ------ |
| [Rendering](rendering.md) | Frame pass order, the bindless GPU-driven design, the RHI backends, shaders, lights, cameras and materials |
| [Physics and vehicles](physics_and_vehicles.md) | PhysX integration, the 200 Hz vehicle simulation, `.car` definitions, AI drivers and input |
| [Engine systems](systems.md) | Animation, audio, navigation, procedural geometry, weather, XR and Steam |
| [Console variables](console_variables.md) | The cvar system and every registered cvar, grouped by subsystem |

## Tools and workflow

| Page | Covers |
| ---- | ------ |
| [Editor](editor.md) | Panels, shortcuts, undo/redo, the sequencer and the game builder |
| [MCP bridge](mcp.md) | Driving the live engine from AI agents and scripts, and adding new bridge commands |
| [Testing and CI](testing_and_ci.md) | Smoke tests, the GPU test suite and the GitHub Actions pipeline |
| [Code style](code_style.md) | Naming, formatting and file conventions for contributions |

## Elsewhere in the repository

- [`worlds/README.md`](../worlds/README.md): where a world's files and assets live, and the rules for keeping them separate.
- [`source/navigation/README.md`](../source/navigation/README.md): the streamed navigation mesh.
- [`source/rendering/Decals.md`](../source/rendering/Decals.md): surface decals, vehicle dirt and paint scratches.
- [`tools/mcp/spartan_engine/README.md`](../tools/mcp/spartan_engine/README.md): the Node MCP server in full detail.
- [`plan.md`](../plan.md): the game being built on the engine.

## Reading the source

A good order for a first read:

1. [`source/core/main.cpp`](../source/core/main.cpp): process entry, working directory and crash handler.
2. [`source/editor/Editor.cpp`](../source/editor/Editor.cpp): the main loop.
3. [`source/core/Engine.cpp`](../source/core/Engine.cpp): subsystem start-up, the per-frame tick and shutdown.
4. [`source/world/World.cpp`](../source/world/World.cpp) and [`source/world/Entity.h`](../source/world/Entity.h): the scene.
5. [`source/rendering/Renderer.cpp`](../source/rendering/Renderer.cpp): `Renderer::ProduceFrame()` is the frame from top to bottom.
