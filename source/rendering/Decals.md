# Surface decals

Render components accept receiver-scoped decals through AddDecal and ClearDecals.
DecalParameters.world_to_decal maps world positions into a projector box [-1, 1].
Its +Z axis points out of the receiving surface. Color is linear RGBA; surface
contains roughness, relief in metres, random seed and grass-fibre fraction.
Kind 0 is a surface deposit; kind 1 is a procedural paint scratch with recessed
streaks and exposed metal. The kind occupies a previously reserved word, keeping
the 112-byte GPU record unchanged.
An optional source Material supplies albedo and normals. Alpha-tested source
materials use their alpha as an authored decal shape; other materials use the
procedural splatter mask. Use white tint and zero procedural relief for an authored
impact texture. Lua Render:AddDecal takes position, rotation, half_size, linear
RGBA Vector4 color, roughness, relief, seed, source_material (nil is allowed).
Lua Quaternion.FromEulerAngles takes three numbers: pitch, yaw, roll.

Projection is converted to receiver-local space at impact and transformed back
for rendering, independently of shared materials and mesh UVs. Depth and normal
rejection prevent opposite-side projection. Both raster geometry paths,
tessellation, ray-traced reflections and path tracing evaluate the deposits.
G-buffer material B stores deposit coverage, including clearcoat suppression.

## Vehicle spray

Every active drivable car automatically samples each grounded wheel's actual
contact actor. Terrain contacts use SampleSurface's dominant layer and the layer's
material. Roads do not sample terrain underneath them. Grass/moss, forest litter,
sand, rock/gravel, soil and snow produce different debris and dust. Terrain
wetness plus flow and deposition approximate damp hollows; standing-water depth
increases wetness.
This is a moisture proxy, not a rainfall or soil-water simulation.

Tread speed, contact load, hub velocity and sideways slip determine emission.
Particles leave the trailing tread arc across its width and inherit vehicle
motion. The existing GPU particle system renders the plume. A representative
subset follows ballistic trajectories against cached body-mesh triangle BVHs;
only intersections deposit dirt. Droplets stop at their local contact ground
plane. Those tracers and GPU particles share launch conditions, but are not a
one-to-one GPU collision readback. Motion-relative
segments account for the body moving or rotating into airborne spray.

## Vehicle paint scratches

Chassis contacts use PhysX pre-solver relative point velocity (including angular
motion and the other body's velocity). Wheel/suspension contacts are excluded.
Contacts must exceed 1.2 m/s into the surface, or 1.5 m/s sliding with a loaded
contact. Closing impacts also accept zero-impulse CCD reports. The strongest of
four reported manifold points locates the mark, stored in contact-time actor
space so later physics steps cannot move the body away from it. The existing
body-mesh BVH finds the nearest triangle within 45 cm of that point, rather than
casting along the collision hull's normal (which can miss the curved bodywork).
Distances are measured in world metres even on scaled imported panels. Glass and
trim participate in the query, but only opaque paint-preset materials receive
scratches, including matte paint. Unready BVHs or missed panels skip that contact.

Sliding sets streak direction and length; stronger impacts widen the patch.
Marks alter color, roughness, normals and metalness and suppress clearcoat through
the existing coverage channel. At most one mark is added every 60 ms per car,
within the same 60-metre effects range as spray. They attach in receiver-local
space, remain through teleports, and clear on play stop or ClearDecals. Damage is
visual only: no dents or changes to vehicle handling.

For live verification, vehicle_get reports scratch_count separately from total
decal_count and lists each scratch's receiver, world position and outward normal.
Pause play, exit the car to release its chase camera, then use camera_set_view
and screenshot_take to inspect those positions without clearing the damage.

## Motion and reconstruction

Billboards retain their previous position and size and write unjittered motion
into the shared velocity target, including when clouds supply that target.
Transparency coverage contributes to the same rejection mask consumed by DLSS,
XeSS and TAAU. Linear and FXAA paths use the same particle simulation and rendering.
Geometry rejection follows object motion and previous surface depth, including
thin moving silhouettes such as side mirrors.

Vehicle debris uses its birth contact plane and ballistic gravity instead of the
generic smoke collision probes. Launch lift is capped at 2.2 m/s before random
spread; debris lives for 0.55 seconds. Emission and deposit tracing run within
60 metres of the camera; accumulated dirt remains when the car leaves that range.

## Budgets and lifecycle

- 48 decals per Render, including up to 16 scratches; 8,192 GPU decals per view.
  New dirt replaces the oldest dirt, never a scratch. Scratches replace the oldest
  scratch at their limit, or the oldest dirt when the shared capacity is full.
- At most 96 ballistic tracers and eight small particle emitters per car.
- Body collision trees are built on a worker, shared by mesh/submesh, and retained
  across physics sleep/wake and teleports. Deposits wait for a ready tree; rendering
  never waits for its construction. Worker jobs retain assets, not scene entities.
- Repeated draws and LODs share uploaded decal records; GPU uploads use frame rings.
- Deposits are transient, cleared on play stop, and not saved into authored worlds.
- Teleports discard airborne tracers. Render:GetDecalCount and vehicle_get's
  decal_count expose accumulation; ClearDecals provides a washing/reset hook.
- Rigid, non-instanced receivers are supported. Skinned attachment, per-instance
  decals, terrain tracks, physical layer thickness and washing
  gameplay are not implemented.
