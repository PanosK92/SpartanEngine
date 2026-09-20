# Island navigation

`plan.world` contains a **Navigation** entity with a `Navigation` component. The pedestrian
manager enables `use_navigation="true"` and references that entity through
`navigation_entity_id`. Press Play to
build nearby navigation tiles and spawn walkers on them. Existing road population sampling
still chooses spawn locations; DetourCrowd then chooses reachable wandering destinations,
follows polygon corridors, and provides acceleration, separation and local agent avoidance.
Both animation LODs use navigation. Ragdolls leave the crowd; recycled walkers get new agents.

The entire island is addressable as 64 m tiles, including negative coordinates. The 11 by 11
tile window follows the camera, and old tiles are removed outside a 15 by 15 window. Tiles
are built closest first, with one worker job in flight. This is runtime streaming, **not a
full offline island bake**; long-distance paths through unloaded tiles are not available.
Walkers wait for valid tiles rather than falling back to spline movement.

Recast uses a 0.4 m horizontal voxel, 0.2 m vertical voxel, 0.4 m agent radius, 1.8 m height,
0.4 m step and 45 degree maximum slope. Border overlap connects adjacent tiles. Terrain is
sampled on a 1 m grid from its current carved/sculpted heightfield, excluding submerged
samples. Static collision-enabled render geometry (including transformed instances and
physics inherited from a parent) supplies buildings, roads and props. Grass without
collision and dynamic/animated geometry are excluded. This uses render triangles rather
than PhysX's simplified collision hulls; very small terrain details can be lost at the
sampling resolution. Moving vehicles/doors do not dynamically carve the mesh.

Select **Navigation** in the hierarchy to configure **Enabled**, **Follow Camera**, and
**Debug Draw** in Properties. Debug Draw displays the detailed navmesh surface in the editor's
default light blue, at 35% opacity and 15 cm above its actual height to avoid z-fighting.
The overlay respects scene occlusion and works in edit mode, during play and while paused.
Edit-mode preview builds the nearby tiles without simulating agents. With Follow Camera disabled, the entity's world position
is the tile streaming focus. **Rebuild Navmesh** discards the old runtime and resnapshots
static geometry during play or debug preview. There is no persistent bake cache or automatic obstacle rebuild.

Navigation is registered with the component factory, XML serialization, cloning/undo attribute
system and **Add Component > Gameplay > Navigation**. Its Start/Stop/Tick lifecycle owns tile
streaming and advances the shared crowd exactly once per frame, independently of pedestrians.
The pedestrians inspector selects a Navigation entity, or **Automatic** for the first active
provider. Multiple groups may share the provider (256 agents total per Navigation component).
If the provider is disabled, deleted or rebuilt, consumers release their old agent slots and
wait for the selected provider; they do not create private navigation worlds or fall back to
splines. Agent slots are reacquired after the replacement tiles become available.

`NavigationMesh` is independent of engine systems and owns Recast/Detour resources.
`BuildTile` accepts copied triangle data and can execute on workers. Installation, queries
and crowd simulation run on the main thread. `MoveTo` rejects disconnected and truncated
paths; `Wander` samples reachable polygons and uses the same validation. `NavigationWorld`
extracts scene geometry and schedules tiles. Pending workers own their data, so stopping
play or unloading the world never leaves them holding pointers into a destroyed scene.
