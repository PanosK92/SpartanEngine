# Terrain

The [`Terrain`](../source/world/components/Terrain.h) component is a heightfield with a hand sculpt layer, collision, eight rule driven ground materials and scattered vegetation and rocks. The **Terrain** editor panel is where all of it is authored. It opens from the Terrain button in the toolbar or from View > Widgets > Terrain.

The fastest way to learn it is [`worlds/terrain_example.world`](../worlds/terrain_example.world): an island sculpted from a flat square, with signs that explain every step. Open it, read the two boards at the start, then try the brush on it.

## Sculpting your first terrain

This works in edit mode, so stop the game first.

1. Open a world that has no terrain yet, then open the **Terrain** panel.
2. Press **Create flat terrain**. Flat resolution times cell size is the edge length in meters, the default 256 samples at 2 m is about 510 m. The ground appears after a few seconds.
3. Press **Sculpt** in the bar at the top of the panel. It opens the Shape tab and arms the brush.
4. Pick a mode: **Raise**, **Lower**, **Smooth** or **Flatten**.
5. Move the mouse over the ground in the viewport. A circle shows the brush.
6. Hold the left mouse button and drag to paint. The right mouse button still flies the camera with W A S D.
7. Ctrl+Z undoes a stroke. Ctrl+S saves the world.

The brush settings:

| Setting | What it does |
| ------- | ------------ |
| Radius | The brush size in meters |
| Strength | How fast the ground moves, in meters per second of holding the button |
| Falloff | 0 is a hard edged stamp, 1 fades smoothly from the center to the rim |
| Target Height | The height Flatten pulls the ground towards |

## When nothing seems to happen

- **The new ground is not visible.** A flat terrain is centered on the world origin at height 0. A floor at that height covers it, delete the floor.
- **The brush does nothing.** Check that the Sculpt button reads **Sculpting**, that the engine is in edit mode and that the mouse is over the ground. A brush smaller than one terrain cell moves nothing, the panel warns when the radius is too small.
- **The ground is bare.** Grass, flowers and trees only grow above sea level, each layer has its own minimum altitude. Flat ground at height 0 stays bare, raise it a meter or more.
- **There is no water.** The ocean is a separate entity with a [`Water`](../source/world/components/Water.h) component. It fills everything below sea level, ponds dug with Lower included.

## The panel

The summary at the top shows the size, the sample and tile counts and how many props are placed. Under it, **Generate** rebuilds the surface on a worker thread and **Sculpt** toggles the brush. Sculpt edits survive Generate. The four tabs:

| Tab | What it holds |
| --- | ------------- |
| **Shape** | The height map, elevation band and sea level, resolution, shoreline tools, the brush and the sculpt layer |
| **Ground** | The ground materials and the rules that decide where each one appears |
| **Life** | The scatter layers: trees, rocks, flowers, grass and pebbles |
| **Analysis** | Debug views of what the rules read, the baked maps and texture reloads |

### Shape

A terrain starts either flat or from a height map, an 8 bit grayscale image where black is the low end of the elevation band and white the high end. **Elevation** maps the image to world heights and sets the sea level. **Resolution** sets the density (extra samples per pixel), the scale (meters between samples), the tile count, smoothing passes and an optional sealed border. **Shoreline** bends a map into an island so the ocean clips it cleanly. **Start Over** throws the height map away and begins from a flat grid.

Brush work lives in the **sculpt layer**, apart from the generated ground. It is reapplied after every Generate and saved with the world as `terrain_sculpt.bin` in the world's resource folder (`binaries/project/<world>_resources/`). **Clear Sculpt Layer** removes all of it, and selecting a `tile_N` child in the hierarchy offers a reset for that one tile.

### Ground

Eight procedural materials share the surface, each one claims the ground its rule scores highest on. A rule has a slope band, an altitude band and a priority, plus optional responses to curvature, water flow, occlusion, sun exposure, wear, deposition and talus. A layer needs a folder of the same name under `project/materials/` holding albedo, normal, roughness, occlusion and height textures. The global Surface card controls snow, wetness and blend quality live.

### Life

Each scatter layer places one asset (with optional variants) by rule. The kinds are **Mesh** (real entities that can collide), **GPU Grass** and **GPU Detail** (free per instance, no entities). The Where card gates a layer by slope, altitude above sea level, biome and ground type. The other cards group instances into patches, size them from slope and altitude, seat them on the ground and limit render and shadow distance. The Result card shows how many instances the last scatter placed and how much of the ground the gates accepted, 0 means the gates are too tight.

The default layers are the Zakynthos island set (pines, olives, scrub, limestone, grass, flowers and pebbles). Switch off the mesh layers a world does not use, their models load with the first surface.

### Analysis

**Debug View** paints the surface with layer weights, the dominant layer or one analysis channel, the fastest way to see why a layer landed where it did. **Baked Maps** lists the maps Generate produces, without them the rules only see slope and altitude. **Reload Layer Textures** rereads the ground material folders and **Rebuild Biome Mask** refreshes the vegetation mask.

## Driving and physics

Every tile carries a physics heightfield, so characters and cars stand on what is sculpted. Tires deform soft terrain (`terrain.deformation*` cvars, see [Console variables](console_variables.md)) and vehicles push vegetation aside.

## From tools

The [MCP bridge](mcp.md) can do the same work without the editor: `terrain_create` builds a flat terrain, `terrain_get` reports its state, `terrain_sculpt` applies a brush at world positions, and `terrain_scatter_get` and `terrain_scatter_set` read and edit the scatter layers.

## Source

- [`source/world/components/Terrain.h`](../source/world/components/Terrain.h): the component, brush, sculpt layer, ground rules and scatter layers.
- [`source/world/TerrainSystem.cpp`](../source/world/TerrainSystem.cpp): heightfield generation, meshing and the brush math.
- [`source/world/TerrainDeformation.cpp`](../source/world/TerrainDeformation.cpp): tire ruts.
- [`source/editor/widgets/TerrainEditor.cpp`](../source/editor/widgets/TerrainEditor.cpp): the panel.
