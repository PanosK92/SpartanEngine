# Visual scripting

Visual scripts are node graphs that compile to Lua. You build behavior by wiring events, flow control and engine calls together in the editor's **Visual Script** window, and the result runs through the same Script component, with the same speed, as a hand-written [Lua script](scripting.md). There is no graph interpreter at runtime.

The fastest way to learn it is the tutorial world, [`worlds/visual_scripting.world`](../worlds/visual_scripting.world). It has four stations, each an object with a small commented graph:

| Station | Graph | Teaches |
| ------- | ----- | ------- |
| 1, On Tick | `visual_scripting_spin.lua` | Per-frame events and frame-rate independent motion (`speed × delta`) |
| 2, Variables | `visual_scripting_bob.lua` | Variables, On Start, reading and writing the entity's position |
| 3, Input | `visual_scripting_input.lua` | On Key and Flip Flop: Space toggles a cube between two sizes (play mode) |
| 4, Branch | `visual_scripting_branch.lua` | Branch (if / else) and reading a component: a lamp blinks |

The graphs ship in the project package, in `binaries/project/visual_scripting_resources/scripts/`. Load the world, select an object, find its **Script** component in the inspector and press **Edit graph**. Press play to see the graphs run.

## How it works

A visual script is a normal `.lua` file with two parts:

```lua
-- A visual script. Spartan generated this file from the graph below, ...
--[==[spartan-visual-script
<visual_script version="1" next_id="7">
  <variables>
    <variable name="speed" type="number" value="90" />
  </variables>
  <nodes> ... </nodes>
  <links> ... </links>
</visual_script>
]==]

local script = {}
script.speed = 90

function script:Tick(entity)
    vs.delta = Timer.GetDeltaTimeSec()
    -- Rotate [3]
    entity:Rotate(Quaternion.FromEulerAngles(0, (self.speed * vs.delta), 0))
end

return script
```

- **The graph** is XML inside a Lua block comment, so Lua ignores it.
- **The generated code** follows it. Each statement carries a `-- Title [node id]` comment that points back to its node.

Saving the graph recompiles it and rewrites the code. Script components that use the file reload automatically. The graph is the source of truth: hand edits to the generated part are replaced on the next save.

Because the output is plain Lua, everything in the [Lua scripting guide](scripting.md) applies: the callbacks, saved values, reloading and error reporting.

## The editor

Open it from an entity's Script component (**Edit graph**), or from the window's **New** and **Open** buttons. The window has three parts:

- **Toolbar.** Save, Attach to selected, New, Open, Undo, Redo, Frame, plus toggles for the side panel and the generated Lua. A status pill on the right shows whether the graph compiles, or how many problems it has.
- **Canvas.** The graph itself. Wires are colored by type, and the wires touching the selection light up.
- **Side panel.** The selected node's description, the graph's variables, and a **Problems** list. Clicking a problem focuses its node.

| Action | How |
| ------ | --- |
| Add a node | Right-click the canvas, or press Tab or Space over it. Type to search, use the arrow keys, Enter to place |
| Connect | Drag from an output pin to an input pin |
| Add a connected node | Drop a wire on empty canvas: the menu lists only nodes that fit |
| Node and pin actions | Right-click a node or a pin |
| Pan / zoom | Right or middle drag / mouse wheel |
| Select | Click, Ctrl or Shift click to add, drag a box on the canvas |
| Frame the graph | F |
| Delete | Delete or Backspace |
| Duplicate, copy, cut, paste | Ctrl+D, Ctrl+C, Ctrl+X, Ctrl+V |
| Select all | Ctrl+A |
| Undo, redo | Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z |
| Save | Ctrl+S |

Unconnected inputs show an inline field for their value. An unconnected `target` input means the script's own entity.

## Nodes

The library has about 740 node types. Most are generated from the Lua bindings in [`source/scripting`](../source/scripting), so every function the [Lua API](scripting.md#api-reference) exposes is also a node and new bindings appear without extra work.

| Category | Nodes |
| -------- | ----- |
| **Events** | On Initialize, On Start, On Tick (gives `delta`), On Pre Tick, On Stop, On Remove, On Key (pressed / released for a chosen key, play mode only) and Custom Event |
| **Flow** | Branch, Sequence, For Loop, For Each, While Loop, Do Once, Flip Flop, Delay (seconds, driven by On Tick) |
| **Math** | Number operations (add, multiply, sin, abs, lerp, clamp ...), comparisons and logic |
| **Values** | Boolean, Number, Integer, String constants and every engine enum (`KeyCode`, `LightType`, ...) |
| **Engine API** | Entity, World, Timer, Input, Physics, Light, Render, Camera, AudioSource, Animator, ParticleSystem, Text3D, Spline, Hud, Sequencer, CarTuning and the math types (Vector2/3/4, Quaternion, BoundingBox) |
| **Components** | Get Light, Get Physics and so on: an entity's component, or nil when it has none |
| **Utilities** | Self, Is Valid, Select, To String, To Number, Type Of, Comment, and `print` |

Execution follows the white **exec** wires from an event. Value nodes without exec pins (math, getters, constants) are evaluated where their result is used.

A **Custom Event** is a named function of the script. **Call** nodes run it from the same graph, and other scripts can call it as `script:Event_<name>(entity)`.

A **Comment** frames a group of nodes. Its first line is the title, and the remaining lines are shown as a note inside the frame. Dragging the header moves the nodes inside.

## Variables

Add variables in the side panel. A variable has a name, a type and a starting value. Types are `boolean`, `number`, `integer`, `string`, vectors, `Quaternion`, `Entity`, `table`, `any` and every engine type.

- **Get** and **Set** nodes read and write a variable; drag the Get or Set button from the panel, or pick them in the add menu.
- Variables are fields of the script table (`self.speed`). Numbers, booleans and strings also show up in the entity's inspector and are saved with the world, so the same graph can be tuned per entity without editing it.

## Using it from tools

The [MCP bridge](mcp.md) can build and edit graphs in a running editor through `engine_command`: `visual_script_open` (`path`, `create`), `visual_script_set_graph` (`xml`), `visual_script_add_node`, `visual_script_remove_node`, `visual_script_connect`, `visual_script_disconnect`, `visual_script_set_value`, `visual_script_variable`, `visual_script_compile`, `visual_script_save`, `visual_script_attach` (`entity`), `visual_script_view` (shows the window for screenshots), `visual_script_state` and `visual_script_api`, which lists node types, categories and pins.

## Where the code lives

| Path | Contents |
| ---- | -------- |
| [`source/scripting/visual/VisualGraph.*`](../source/scripting/visual) | The graph model and its XML format |
| [`source/scripting/visual/VisualApi*`](../source/scripting/visual) | Reflection of the Lua bindings: every binding records its C++ signature |
| [`source/scripting/visual/VisualNodes.*`](../source/scripting/visual) | Node definitions, built from the reflected Lua API |
| [`source/scripting/visual/VisualCompiler.*`](../source/scripting/visual) | Graph to Lua compilation and diagnostics |
| [`source/scripting/visual/VisualEdit.*`](../source/scripting/visual) | Graph edits shared by the editor and the MCP bridge |
| [`source/scripting/visual/VisualScript.*`](../source/scripting/visual) | Reading and writing visual script files |
| [`source/editor/widgets/VisualScriptEditor.*`](../source/editor/widgets/VisualScriptEditor.cpp) | The editor window |
| [`source/editor/mcp/McpVisualScriptCommands.cpp`](../source/editor/mcp/McpVisualScriptCommands.cpp) | The bridge commands |
