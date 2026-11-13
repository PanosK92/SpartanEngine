# Editor

The editor is the same executable as the game. It is an ImGui interface over the live engine ([`source/editor`](../source/editor)); pressing play simulates the world in place, and stopping restores it exactly.

## Panels

All panels dock and are listed under **View**. They are registered in [`EditorWidgets.cpp`](../source/editor/EditorWidgets.cpp).

| Panel | Purpose |
| ----- | ------- |
| **Viewport** | The scene, with picking and the transform gizmo |
| **World** | Entity hierarchy: drag to reparent, right-click to create |
| **Properties** | Inspector for the selected entity's transform and components |
| **Environment** | Time of day, date, location, clouds, rain, wind, puddles |
| **Assets** | Asset browser. Right-click for new folders, Lua scripts and materials; **Import Model** brings in meshes |
| **Console** | Log output and the cvar prompt |
| **Renderer Options** | Every renderer feature, quality setting and debug view |
| **Profiler** | CPU, graphics and async compute timelines |
| **Memory** | CPU and GPU allocations and fragmentation |
| **Resource Viewer** | Everything in the resource cache |
| **Texture Viewer** | Render targets and textures, per mip |
| **Shader Editor** | Edit and hot-reload HLSL |
| **Script Editor** | Edit and reload Lua |
| **Visual Script** | Node graph editor for [visual scripts](visual_scripting.md); open it with **Edit graph** on a Script component |
| **Terrain** | Sculpt and paint terrain |
| **Sequencer** | Cinematic timeline (see [Engine systems](systems.md#cinematics)) |
| **Asset Viewer** | Inspect and edit a single asset in isolation |
| **Tree Creator** | Procedural tree species |
| **Build Game** | The game builder |
| **Spartan AI** | In-editor assistant over the [MCP bridge](mcp.md) |

The **World Launcher** opens at start-up, after the welcome window. It lists every `.world` in `project/` and in the repository's `worlds/` folder (`../worlds/` from the executable). If the project assets or the world files are missing, as in a fresh release download, it offers to download them: the assets come from the project package and the worlds from GitHub, placed in `../worlds/` exactly as a clone would have them.

## Shortcuts

**Ctrl+P** shows the full list in the editor.

| Shortcut | Action |
| -------- | ------ |
| **F5** / **F6** | Play / pause |
| **Ctrl+S** / **Ctrl+Shift+S** | Save world / save as |
| **Ctrl+O** | Load world |
| **Ctrl+Z** / **Ctrl+Shift+Z**, **Ctrl+Y** | Undo / redo |
| **Ctrl+D** | Duplicate the selection (viewport focused) |
| **F** | Focus the camera on the selection |
| **W** / **E** / **R** / **T** | Translate / rotate / scale / universal gizmo |
| **X** | Toggle local and world space |
| **Z** | Cycle the gizmo pivot (median, active, individual) |
| **Ctrl+Shift+B** | Build Game |
| **Alt+Enter** | Fullscreen |
| **F12** | Screenshot |
| **Ctrl+0** | Toggle VR |
| **Ctrl+F** | Filter in most panels |

### Camera

Hold the right mouse button to look, and move with **WASD**, **Q**/**E** for down/up and **Shift** to go faster. In play mode, a camera on a character controller walks: **Space** jumps, **Ctrl** crouches. The player can stand and bounce on cloth such as the trampoline in `physics_example.world`.

In play mode, the physics toys sit behind **Alt** so a plain click stays free for gameplay. **Alt + left click** grabs a dynamic body or a cloth at the point under the cursor and drags it until the button is released. Holding the right mouse button with **Alt** and left clicking shoots a box. [`physics_example.world`](physics_and_vehicles.md#example-world) is built for trying both, and uses the plain click for its experiment buttons.

## Undo and redo

Every edit is a `Command` with `OnApply` and `OnRevert` pushed onto the [`CommandStack`](../source/commands/CommandStack.h) (128 steps). Transform drags, entity deletion, component and material edits, terrain sculpting and sequencer edits are all undoable. New editor features should record their edits through the scopes in [`EditorHistory.h`](../source/editor/EditorHistory.h).

## Game builder

**Build Game** ([`GameBuild.cpp`](../source/editor/GameBuild.cpp)) packages one or more worlds into a standalone game:

1. Collects the executable, `third_party/`, the engine `data/`, and every asset the chosen worlds reference (worlds, materials, meshes, textures, Lua, `.car` files, prefabs). Alternatively, the entire `project/` folder.
2. Writes `game.xml` with the name, version, start world, window mode and Steam settings.
3. Copies everything to the output folder and optionally zips it.

The packaged executable sees `game.xml`, disables the editor and the MCP bridge, and plays the start world immediately. Options are kept in `settings/game_build.xml`.
