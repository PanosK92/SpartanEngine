# Road mesh repair

Splines remain the authoring paths. A road node is an explicit connection, identified by
the same `road_node_<id>` tag on control point entities in each participating spline.
The importer preserves OSM junction nodes through simplification and resampling and
emits these tags. Geometric crossings alone do not create connections.

After the splines sample and grade the terrain, `Spline::RebuildRoadJunctions` trims
their approaches, solves a shared flat junction elevation, constrains the connecting
grades, and adds a junction polygon to one road's mesh. Its mouth positions coincide
with the approach edges. Roads remain separate render/physics objects, suitable for
culling; they do not require one island-sized mesh. Road collision retains the mesh
triangles instead of independently simplifying the junction boundaries. Road renderers
also set `ExcludeFromTerrainBlend`, so ground blending/coating cannot paint dirt
over the deck; this is a per-object flag and leaves shared materials unchanged.
Car wheel hierarchies use the same flag.

Node edits compare the solved junction frames, cut segments, and patches with the
previous result. Only changed splines rebuild meshes/collision; unchanged roads retain
their GPU and physics resources. The shared grade solve still runs across the network
so connected roads remain consistent. Each changed spline is still one mesh.
Terrain carving retains the previous deck samples and invalidates only changed segments'
old/new footprints, including shoulders. Disconnected dirty regions stay separate;
overlapping roads are re-applied inside each region so moving/deleting a road restores
the original ground correctly. Junction height changes do not invalidate vegetation
along unrelated roads.

Asphalt U coordinates span only the driving deck. Sidewalks and embankments cannot
move its lane markings. The U tangent follows the cross-section and hard profile
corners keep separate normals. Longitudinal coordinates are rebased by full material
repeats before half-float packing. Both ends of each quad share the same UV origin.

## Existing Zakynthos map

`worlds/plan.world` contains the recovered OSM junction anchors. The migration
recovered 301 node identities, inserted 255 control points,
and could not recover another 86 nodes within six metres of two retained routes.
These are topology counts, not a claim that all 301 junction polygons are supported.
The previous map is backed up under `binaries/project/backups/`.
Do not remove `road_node_` tags when editing or regenerating its control points:
without those identities, roads are independently graded overlapping strips.
Junction corners follow rounded curves tangent to the approaching road edges;
mouth edges stay straight and share exact positions with the trimmed road meshes.

To repeat the migration on an existing world with the editor closed:

    python tools/map/upgrade_road_junctions.py --world path/to/map.world
    python tools/map/upgrade_road_junctions.py --world path/to/map.world --apply

The first command is a dry run. Applying creates an exclusive `.before_junctions.bak`
backup and changes only control point lines. It preserves road component settings,
materials, entity IDs, unrelated scene content, and existing handles away from nodes.
Migration proximity is only a recovery aid for this old import; inspect the preview
for ambiguous closely parallel roads. Future imports preserve actual graph identities.

## Current limits

- The junction pass supports open, unattached road strips without sidewalks. It reports
  missing space, displaced anchors, and acute/overlapping approaches and leaves those
  approaches intact. Nodes separated by less than a road width along a shared spline
  are combined into one compound junction, including staggered intersections and small islands.
  The current map builds 288 surfaces from 301 graph nodes (293 groups). Five groups
  still have overlapping approach mouths and remain unsupported: `1020823664`,
  `1020923060`, `273733874`, `926614157`, and `9755596181`. These need further
  road-layout/width repair; they are logged rather than silently generating invalid decks.
- Junction asphalt currently samples the unmarked region of the existing road texture.
  The paint-free band is mirrored at the approach texture scale, with geometry split
  at repeat boundaries to keep paint out of the junction. This assumes the Zakynthos road atlas layout; a separate asphalt material and road
  marking layer are still needed for arbitrary materials and detailed intersection markings.
- Shared grades take priority at connections; the network solve does not enforce each
  road's excavation budget. Inspect mountainous intersections for excessive cut/fill.
- Bridges, tunnels, banking, and tight hairpin offset self-intersections require additional
  authoring and meshing work. This is a repair foundation, not a finished road editor.
- A complete visual and driving pass of the 427 km island has not been completed.

## Checks

    python -m unittest discover -s tools/map -p test_roads.py

`test_spline_uv.cpp` is a standalone C++20 regression executable (include `source/core`).
It checks deck UV invariance with varying embankments/curbs and half-precision detail
at 30 km, including fractional material tiling.

`test_road_edit_regions.cpp` is a standalone C++20 regression executable. It checks
unchanged samples, endpoint/interior moves, inserted/removed samples, settings changes,
road removal, and incremental restoration with an overlapping unchanged road.

`node tools/map/test_road_runtime.mjs` loads disposable T, X, oblique, short-approach, compound, and overpass fixtures in an empty engine
started with `--mcp-control --mcp-port=47779`. It checks collision at the center and
mouths of roads with different widths and starting elevations, rounded corner cutouts,
separation of crossings that do not share a node, and a node edit that must preserve
a distant road without generating the edited mesh twice. Set `ROAD_TEST_PORT` and
`ROAD_TEST_RUNTIME_DIR` when using an isolated editor/runtime directory. Do not run it in a
working editor containing a user scene. The built validation executable is
`binaries/spartan_vulkan_development.exe` (use a separate copied executable and data/project
links for testing). Copy changed shader sources to `binaries/data/shaders` before
restarting a build that uses the local runtime data copy. Set `ROAD_SCREENSHOT=1` to also request
a T-junction screenshot when the test editor is rendering.

Validation completed: the development build links successfully, all four importer
regressions pass, the C++ UV regressions pass, and six live runtime fixtures verify continuous collision at unequal-width and
unequal-height connections, rounded corners, short approaches, compound intersections,
and unconnected overpasses. The island was also loaded and nearby junctions inspected;
the five unsupported groups above remain explicit limitations.
