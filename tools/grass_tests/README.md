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

## Blade shape and camera facing

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
