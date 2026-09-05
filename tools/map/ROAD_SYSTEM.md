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
triangles instead of independently simplifying the junction boundaries.

Asphalt U coordinates span only the driving deck. Sidewalks and embankments cannot
move its lane markings. The U tangent follows the cross-section and hard profile
corners keep separate normals. Longitudinal coordinates are rebased by full material
repeats before half-float packing. Both ends of each quad share the same UV origin.

## Existing Zakynthos map

`worlds/plan_roads_preview.world` is a copy of the existing edited world with recovered
OSM anchors. The migration recovered 301 node identities, inserted 255 control points,
and could not recover another 86 nodes within six metres of two retained routes.
These are topology counts, not a claim that all 301 junction polygons are supported.
`worlds/plan.world` was not overwritten by this change.

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
  approaches intact. Closely clustered junctions need a compound intersection builder.
- Junction asphalt currently samples the unmarked region of the existing road texture.
  This assumes the Zakynthos road atlas layout; a separate asphalt material and road
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

`node tools/map/test_road_runtime.mjs` loads a disposable T junction in an empty engine
started with `--mcp-control --mcp-port=47779`. It checks collision at the center and
mouths of roads with different widths and starting elevations. Do not run it in a
working editor containing a user scene. The built validation executable is
`binaries/spartan_road_validation.exe`.

Validation completed: the development build links successfully, all four importer
regressions pass, the C++ UV regressions pass, and all six live collision probes on
the unequal-width T junction returned the same shared height of 2.0 metres.
