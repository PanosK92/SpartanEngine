# Surface decals

Render components accept receiver-scoped decals through AddDecal and ClearDecals.
DecalParameters.world_to_decal maps world positions into a projector box [-1, 1].
Its +Z axis points out of the receiving surface. Color is linear RGBA; surface
contains roughness, relief in metres, random seed and grass-fibre fraction.
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

- 48 deposits per Render, oldest replaced first; 8,192 GPU deposits per view.
- At most 96 ballistic tracers and eight small particle emitters per car.
- Body collision trees are built on a worker, shared by mesh/submesh, and retained
  across physics sleep/wake and teleports. Deposits wait for a ready tree; rendering
  never waits for its construction. Worker jobs retain assets, not scene entities.
- Repeated draws and LODs share uploaded decal records; GPU uploads use frame rings.
- Deposits are transient, cleared on play stop, and not saved into authored worlds.
- Teleports discard airborne tracers. Render:GetDecalCount and vehicle_get's
  decal_count expose accumulation; ClearDecals provides a washing/reset hook.
- Rigid, non-instanced receivers are supported. Skinned attachment, per-instance
  decals, terrain tracks, paint damage, physical layer thickness and washing
  gameplay are not implemented.
