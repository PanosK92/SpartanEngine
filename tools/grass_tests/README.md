# Grass and vegetation

GPU grass now bends under the selected car's grounded, loaded wheels during ordinary
rolling as well as sliding. Blades keep their roots, lean along the tire's travel
direction (including reverse), and lose wind motion as they flatten. Repeated wheel
passes accumulate pressure. No grass entities or per-blade CPU updates are needed.

The occupied car owns a 32 m square, 512 × 512 GPU history field. On foot, the nearest
drivable car is selected. This is one local car field, not simultaneous tracks for
every traffic vehicle. Two RGBA32F textures use 8 MiB and retain both current and
previous deformation for motion vectors. Integer scrolling avoids history blur and
world positions include the physics origin offset. Pressure-weighted contact height
prevents wheels on an elevated surface from pressing grass well below it.

Tracks stay pressed within **8 m** of the car. Outside that radius they recover
smoothly; the field edge fades out before storage is reused. Console controls:

- `r.grass_track_radius`: retention distance in metres, default 8, clamped to 1–12.
- `r.grass_track_recovery`: recovery rate in inverse seconds, default 1.5.

Unloaded/airborne wheels and moving receivers do not deposit. Contact sweeps break
on jumps, teleports, and long frame stalls. Pausing freezes pressure, and returning
to unpaused edit mode or unloading scatter clears the history. Keeping edit mode
paused allows inspection of a driving result without the chase camera.

## Car body contact

The selected car supplies current/previous rendered chassis poses and the planes of
its fitted convex collision shapes to the grass vertex shader (9,536 bytes total,
stored as float4 elements for both graphics APIs). Up to six hulls with 48 faces each
retain the body contours instead of filling their combined bounding rectangle.
Planes include shape pose and mesh scale; the rendered entity pose includes pitch,
roll and the world origin offset. This avoids fixed-step lag at speed. Box bounds
are used only to reject distant blades before reading the hull planes.

Blades that stay outside a separating face remain untouched. Contact blends the
supporting faces at lower body edges and bows vertices around their planted root,
preserving root-to-vertex length instead of compressing the grass into a thin sheet.
A two-centimetre skin covers ribbon width. Normals and tangents follow the rotation.
Nearby blades bow around the sills; blades underneath follow the floor clearance.
This uses the fitted physics hulls, not individual render triangles. Unusually complex
or non-mesh primitive shapes fall back to a box per shape.

This deformation follows the body directly and releases as it moves away. It never
writes to the persistent tire field. Pause holds the body pose; current/previous
poses feed motion vectors. Like the tire field, it covers the occupied car (or the
nearest drivable car on foot), rather than every traffic car simultaneously.

## Blade shape and camera facing

Grass uses independent blade placement. The experimental six/seven-blade grouping
has been removed, including its shared keep decisions and rounded dispatch counts.
Each blade keeps its own terrain height, wind, camera-facing, tire and body response.
Overall size was already independently randomized within the island's 0.8–1.2 range.

Within each scatter cell, grass now uses one blade per jittered subcell of a
scrambled quadtree. The sequence spreads arbitrary population counts across the
ground and uses a subdivision capacity independent of camera FOV. Existing roots
stay fixed when the chase camera changes FOV with speed. This prevents
coincident roots within a ring and spreads the existing blade budget across more
ground. Different LOD rings still overlap during their transitions; no global
minimum spacing is claimed between those separately seeded rings. Terrain rejection
can still leave gaps where grass is not eligible. Other scatter types are unchanged.

Grass ring budgets are now 4,718,592 / 3,145,728 / 1,572,864 blades: 8x / 4x / 2x the
previous budgets, concentrating density where ground gaps are most visible. Grass
instance storage is 144 MiB, an increase of 111 MiB. These same capacities drive
allocations, populate dispatches, ring offsets and indirect draw limits. Detail
budgets stay unchanged. Actual visible counts still depend on terrain and culling.
Cell spacing alone does not increase the population because the shader divides the
ring budget across its cells.

The island's grass coverage is now 1 rather than 0.5, removing deliberately bare
patches on eligible ground. The biome threshold is reduced from 0.34 to 0.2 to fill
thin meadow transitions; its grass/forest-floor ground mask, altitude and slope
limits still apply. Roots sit directly on the terrain instead of 5 cm above it.

Standing blades have a gentle 10–22 degree resting bend, varying in direction and
amount by world-space root position. The curve increases toward the tip and remains
present at zero wind. Wind adds to it within the existing slope limit; tire pressure
replaces the relaxed shape as a blade flattens. This adds no instances or vertices.
In wind, the arch also gently deepens and relaxes: stable per-blade phases vary the
motion, and shared gusts increase its amount, up to roughly 24% of the resting bend.
The modulation disappears at zero wind and uses the matching time for motion vectors.

The camera-facing response attracts both sides of a blade toward broadside. Small
camera movements change the visible blade angle slowly, while larger movements
progress smoothly toward a true edge-on view. The rotation is limited to about
26 degrees. A 45-degree view is reduced to roughly 19 degrees off broadside;
an exact 90-degree view still sees the edge. Each blade uses a single root-based
view direction and rotates around its terrain-aligned up axis. The effect fades
near overhead and on pressed grass, and uses the previous camera for motion vectors.

Grass albedo uses a darker, desaturated palette of mature and fresh greens, olive,
and occasional dry straw. Smooth root-to-tip gradients and stable tuft/per-blade
variation replace the bright lime tips and weak three-colour tint. Existing roughness
and light transmission remain in control of highlights and backlighting.

## Vegetation scattering

Thin foliage now uses a normalized wrapped diffuse response with a broad forward
transmission lobe. Scattering replaces a fraction of ordinary diffuse reflection;
it no longer adds a minimum glow or invents thickness from the camera angle.
Composition applies the leaf/blade albedo once, keeping backlighting in the authored
palette. Primary and nearby contact shadows both occlude scattering.

Grass, flowers, and terrain-scattered cutout or explicitly named foliage materials
enable the `IsFoliage` / `is_foliage` property independently of wind. Imported
`_foliage` materials also enable it for individual placements. Bark stays solid.
Older world materials named `_foliage` or `_flower` acquire the defaults on load
when the new flag is absent; an explicitly saved flag takes precedence.
The existing `SubsurfaceScattering` scalar is the scattering fraction (default
0.35 for vegetation); `IsFoliage` distinguishes a thin sheet from bulk materials.
This is a local lighting approximation, not measured thickness, volumetric transport,
or transmission through multiple overlapping leaf layers.

Backlit receivers now get sun visibility rays, and thin foliage starts its shadow
test on the light-facing side in both ray traced and shadow-map paths. Procedural
grass/petal back-face normals flip exactly once, after curvature is applied.

## Validation

Run `tools\grass_tests\run.cmd` from the repository root with Visual Studio C++,
Node, and `VULKAN_SDK` configured. It compiles the production shaders for Vulkan and
D3D12 (including lighting and ray-shadow entries), checks the production scattering
math for normalized/bounded energy, continuity, and thin/solid behavior across camera
angles, then executes the actual deformation compute shader on Vulkan and reads its
output back. Checks cover fast sweep continuity, tire width, bend direction,
large world coordinates and height, nearby persistence, exact grid scrolling,
newly exposed cells, smooth reversal without losing pressure, distant recovery,
reset, resting pressure at 30/60/144 Hz, and pause.

The body fixture runs the production vertex-deformation helper on the GPU and checks
chassis clearance, preserved root-to-vertex length, side/bumper response, undertray clearance, planted roots,
continuity along blades and across the lower body edge, large world coordinates,
rotated/sloped chassis, shading basis stability, release, and unaffected blades
above/far from the car, beside its bumper, inside empty hull corners, or already low
beneath its floor. Placement tests execute the production stratification helper and
check separation, balanced coverage, arbitrary counts, scrambled cells,
and repeatability across frames and changing FOV.

The rectangular clearing was reproduced in a chase view before the hull change.
Afterwards the same short drive shows grass surrounding the car in chase and side
views, with no rectangular clearing. Captures are retained as
`binaries/project/mcp/blockout/thumbnails/grass_contact_before_car_play.png` and
`binaries/project/mcp/blockout/thumbnails/grass_contact_final_car_play.png`, with the
side check at `binaries/project/mcp/blockout/thumbnails/grass_contact_final_car_side.png`.
The elevated overview is `binaries/project/mcp/blockout/thumbnails/grass_contact_final_overview.png`.
The development engine build, Vulkan/D3D12 shader compilation, and GPU tests passed.
One island-load attempt hit the existing Text3D meshlet 25-bit offset assertion;
a fresh process loaded successfully. Runtime test world edits were not saved.

The increased independent-blade density was inspected in the island from overhead,
at driving height, and beside the car after a short drive. Coverage is substantially
fuller, and body clearance and pressed tire trails still work. Local captures:
`binaries/project/mcp/blockout/thumbnails/grass_dense_overhead.png`,
`binaries/project/mcp/blockout/thumbnails/grass_dense_driving_height.png`, and
`binaries/project/mcp/blockout/thumbnails/grass_dense_car_side.png`.
The dense driving-height view averaged about 45 FPS across 16 status samples in the
development build under a debugger; this is not a comparative or release benchmark.
The development build and Vulkan/D3D12 shader compilation passed after removing
the blade grouping and raising the budgets. No runtime shader or validation errors
were logged in this check. The runtime test world was not saved, and editor settings
were restored after testing; the authored grass coverage changes remain in the world.

The development engine build passed (existing third-party PDB linker warnings).
All shader and GPU checks passed on an RTX 5070 Ti. A disposable instance of
`worlds/plan.world` was used to move the player car through nearby grass and inspect
the resulting two continuous trails of flattened blades. The world was not saved.
The runtime log had no shader or validation errors. The capture is retained locally
at `binaries/project/mcp/blockout/thumbnails/grass_tire_tracks.png`.

The combined shape, wind, palette and scattering changes were also inspected in
the island using shadow maps and ray traced shadows. Runtime material checks covered
grass, flowers, pines, olive trees, scrub and legacy placed foliage; bark retained
zero scattering. A final drive of about five metres left two pressed trails,
captured at `binaries/project/mcp/blockout/thumbnails/grass_final_tire_tracks.png`.
There were intermittent island-load exits during validation; subsequent runs,
including the final drive under a debugger, completed without a captured exception.
Their cause remains unconfirmed.

D3D12 was shader-compiled only; its runtime and XR were not visually tested.
For driving checks keep the editor UI active: hiding it skips ImGui's frame setup,
while the existing car HUD still attempts to draw and asserts on entering a vehicle.
For runtime testing, synchronize the changed shader sources and headers into
`binaries/data/shaders` when root shader loading is disabled.
