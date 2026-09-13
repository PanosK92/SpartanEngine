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

- The junction pass supports open, unattached road strips, including localized
  sidewalks. Nearby graph nodes on a shared spline can form a compound junction.
  The current island builds 364 junction surfaces; the previously unsupported
  overlapping approaches were corrected in the road overhaul. Arbitrary future
  layouts can still exceed this simple junction solver's geometric limits.
- Island roads use separate asphalt, paint, and gravel shoulder materials. Generic
  spline fixtures retain the legacy atlas UV mapping and paint-free junction band.
  Detailed intersection markings still require authoring.
- Shared grades take priority at connections; the network solve does not enforce each
  road's excavation budget. Inspect mountainous intersections for excessive cut/fill.
- Bridges, tunnels, banking, and tight hairpin offset self-intersections require additional
  authoring and meshing work. This is a repair foundation, not a finished road editor.
- A complete visual and driving pass of the 450 km island has not been completed.

## Checks

### Island road presentation

The racing cross-sections are authored with `python -B tools/map/tune_racing_roads.py --apply`
(omit `--apply` to inspect). Main routes and their matching airport links
use 15 m of continuous asphalt: four 3.5 m lanes and 0.5 m paved margins. The
remaining technical routes use 8 m. Tags `racing_main` and `racing_technical`
identify the groups for future track layouts; they do not create race events.
The importer uses the same main-road width for future primary/secondary routes.

`RoadCrossSection.h` keeps lane paint and ambient traffic offsets consistent.
Wide roads have two same-direction dashed dividers and a double painted centre,
without a physical median. Ambient traffic follows the outer lane in each
direction, preserving its paired routing edges; this does not implement racing
opponents or lane-changing AI. Junctions, shoulders, sidewalks, vegetation
clearance and furniture rebuild from the authored road widths.
Closely staggered four-lane connections share a junction deck when their mouths
would overlap. Connected roads upload geometry after the shared junction solve,
including during loading, avoiding duplicate provisional decks in scene buffers.

`python -B tools/map/fetch_road_materials.py` installs the unmodified CC0
Poly Haven Asphalt Track (4K) and Gravel Road (2K) color, normal, roughness and
occlusion maps. `road_surface_assets.json` records URLs, licences and checksums.
The downloaded maps belong in `binaries/project/materials/island_roads`, alongside
the island's other external assets. Run this installer on a fresh checkout.

`IslandRoadSurface.h` opts the `plan.world` roads hierarchy into separate layers:
three-metre asphalt repeats, fixed-width off-white paint, and two-metre gravel
repeats on wider, gently irregular shoulders. Paint is clipped against solved
junction segments and does not affect collision. Shoulders retain their own mesh
collision and allow terrain blending; the asphalt stays free of terrain coating.
The world uses 16 samples per control-point span for smoother road silhouettes.

`common_road.hlsl` adds low-contrast weathering in world coordinates and fine paint
wear. Raster and reflection/GI ray hits use the same evaluator; subpixel paint
grain fades with footprint to avoid distant shimmer. This is dry asphalt, without
an artificial clearcoat. Surface flags default off for all other materials.

Road furniture still follows the resulting spline frames. Supporting render
layers are transient and regenerated after edits; the saved road graph is unchanged.

### Local sidewalks

`plan.world` contains 93 sidewalk intervals on 73 roads around 40 town/service markers.
`python tools/map/add_populated_sidewalks.py --apply` authors these intervals; the marker
list and radii live in that script and exclude scenic and event-only locations.
Each `<sidewalk_range start="0.2" end="0.4" />` is a normalized spline interval.
Without ranges, the existing sidewalk toggle still covers the entire spline.
Ranges taper at their ends, and explicit junction cutouts remain open to vehicles.
The sidewalk is a transient child with its own paving material and exact collision mesh;
it is regenerated on load and follows the road transform. Attached outer-edge splines
and terrain carving use the local sidewalk width. Pedestrians follow continuous raised
sections and turn back on the same side at gaps, without inferred road crossings.

### Regression commands

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
`tools/map/remove_road_blocking_buildings.py --apply` removes city building boxes that overlap sampled road and localized paving corridors (including the box-shaped city grid streets). It preserves unrelated XML and leaves 0.5 m clearance; 10 conflicting buildings were removed from plan.world.


### Island road overhaul

The old end-return migration is superseded by `overhaul_roads.py`. The initial
island contained 282 splines in nine disconnected networks, including 69 artificial
return loops. The revised roads hierarchy has 204 splines in one connected network,
with no unconnected endpoints or artificial return loops. Simple joins are merged
into continuous splines; short duplicate strips and reversing junction hooks are
removed. Shared node identities still define traffic and mesh connections.

The migration uses the local atlas coastline as well as elevation. Zero-valued
heightmap cells occur inside the coastal plain; treating every such cell as open
sea was preventing legitimate connections. New connections use checked straight
corridors or terrain-cost pathfinding. All new at-grade crossings receive shared
anchors. Tight junction approaches have explicit space for their road mouths.

Roads covered by the raised airport ground are removed or clipped back to a new
perimeter route. A graded, two-way terminal access loop connects two points of
that route and meets the airport service road between the runways. Its heights
are explicit to match the airport platform. The east fence has an opening for
both access lanes. The airport buildings and runway transforms are preserved.

Run from the repository root:

    python -B tools/map/overhaul_roads.py
    python -B tools/map/overhaul_roads.py --apply
    python -B tools/map/add_populated_sidewalks.py --apply

Dry-run validates and reports without writing. The current migration is
idempotent. It changes the roads hierarchy and the airport fence opening; other
world content is preserved. `road_overhaul.json` records the current topology
and continuous routes. The earlier `road_end_repairs.json` is a historical record
of the superseded return-loop pass.

`source/world/IslandRoadDetails.h` builds road furniture from the final generated
road frames in edit and play modes: yield signs on smaller-road approaches,
chevrons on sharp bends, delineator posts, and short outside guardrails. Junction
clearances keep the repeated furniture away from crossing mouths. Posts and
rails use merged meshes per road and material; signs remain individually selectable.
Everything is transient, with shared meshes/materials and bounded render/shadow
ranges. One road is processed per frame; frame/transform changes rebuild its
furniture and deleted roads relinquish theirs.

Validation: ten topology/airport/width tests and four importer regressions pass.
The C++ traffic suite checks the four-lane offsets and 100,000 route traversal
steps. A fresh widened-island load builds all 360 junction surfaces without
unsupported-junction warnings. Four current lane-centre collision probes on
`r000_zakynthos_keri` hit the road. One unidentified PhysX mesh-cooking warning
remains in the scene log; scene-wide collision is not fully verified.
The earlier 99 collision probes and four sidewalk
regressions covered the pre-widening network; they are not a driving survey of
every metre of the widened island.
