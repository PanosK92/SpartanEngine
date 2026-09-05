# Car physics validation

Run from the repository root with Visual Studio C++ tools and the engine's existing libraries:

```powershell
.\tools\car_tests\run.cmd binaries/car_tests/warm_200.csv
binaries\car_tests\headless.exe binaries/car_tests/warm_400.csv 0.0025
binaries\car_tests\headless.exe binaries/car_tests/cold_200.csv 0.005 24000 0.1 cold
```

Arguments: telemetry path, scene timestep (seconds, default 0.005), optional brake-force and TC-threshold overrides, `warm`/`cold`, optional exported chassis hull CSV. Overrides affect only the test instance. The Ferrari's warm envelope comes from its XML validation targets. The cold launch check permits 1.4 times the warm upper bound: an engineering regression bound, not a manufacturer claim. Warm means 80 C tread/40 C core after settling; cold means the preset's 20 C initial tire state. Handling fixtures always start warm.

The harness compiles production C++ simulation, XML loading, and telemetry with C++20 and `/W3 /WX`. Only engine logging and filesystem adapters are replaced. All five presets are parsed; dynamic tests exercise the Ferrari. Outputs live in ignored `binaries/car_tests`.

## Actual plan car collision hull

The in-editor Bench now writes `car_bench_hulls.csv`. The MCP `vehicle_export_hull` tool writes `car_validation_hulls.csv` for the selected car. It exports the actual cooked convex vertices, including each shape's local transform. Use that capture in the same fixtures:

```powershell
binaries\car_tests\headless.exe binaries/car_tests/hull_200.csv 0.005 24000 0.1 warm binaries/car_validation_hulls.csv
binaries\car_tests\headless.exe binaries/car_tests/hull_400.csv 0.0025 24000 0.1 warm binaries/car_validation_hulls.csv
```

Without a capture, a small chassis box is used. With a capture, the actual compound is recooked from its convex vertices; aerodynamic application points are inferred from those hull vertices. The supplied acceptance capture came from the `player_car` prefab and overrides extracted from `worlds/plan.world`, loaded in a separate editor instance. Roads, traffic, rendering, driver inputs, and the rest of the world are deliberately outside the flat-surface fixture. The source world and supplied driving recording are not rewritten by these tests.

## Chassis collision fitting

```powershell
.\tools\car_tests\run_collision.cmd binaries/project/models/ferrari_laferrari/scene.gltf
```

This standalone fixture compiles the production `CarChassisCollision.h` with `/W3 /WX` and the bundled PhysX/Assimp libraries. It checks convex and concave bodies, sparse triangles crossing split planes, a planar wing, reversed input ordering, invalid/empty geometry, hull budgets, and cache fidelity. The optional asset check reads the Ferrari mesh, excludes the definition's baked wheel parts, and normalizes its longest axis to 4.702 m. It tests geometry fitting, not the editor's final transforms, suspension clearance, or driving dynamics.

The fitter replaces the fixed overlapping regions and index-stride vertex sampling. Each candidate splits actual triangles; the next hull is spent on the greatest reduction in combined hull volume, stopping below 1% of the initial volume or at six hulls. Each hull has at most 24 vertices. Directional support refinement checks every input vertex and avoids PhysX's intermediate 255-polygon limit. Numerical failures retain the parent or use a conservative box. A bounded cache retains plain hull vertices, so repeated instances reuse the fit without keeping PhysX objects alive across shutdown.

The normalized Ferrari fixture has 246,638 triangles. The measured result is six hulls / 144 vertices, approximately 2.0 seconds for the first fit and 2.1 ms for cached fitting (geometry import and editor mesh extraction excluded). Source vertices and triangle centroids have at most 0.469 mm sampled undercoverage; the sparse fixtures also sample triangle interiors densely. The regression limit is 1 mm. These are geometry/cooking measurements, not a frame-time benchmark. Chassis vertices now use local hierarchy transforms, avoiding the previous duplicate chassis scaling and world-position cancellation. Existing wheel exclusions and suspension/floor clearance rules still apply.

## Coverage

Checks include assembled mass and full inertia, COM point velocities, left/right kinematic sweeps over +/-80 mm, motion ratios and wheel frequencies, packers, brake torque signs and handbrake heat, natural engine stall/restart, tire combined grip and reverse symmetry, pressure and water limits, battery energy and regeneration bounds, parking/wake and support removal, acceleration/stopping envelopes, moving support momentum exchange, and 50 mm kerbs. Handling includes left/right skidpads, step steering, slalom, lift-off, power-on, and split-friction braking. CSVs and kinematic sweep results are retained.

### Suspension attachment regression

The engine and harness now share `ConfigurePhysicsScene`: TGS, CCD, and four CCD passes. Previously only the harness selected TGS; the engine used the bundled SDK's default PGS. The stiff suspension loops did not converge sufficiently with the previous 16 position iterations. Car bodies now request 64 position and four velocity iterations. Rear toe still follows the physical links, including bump steer and bushing compliance; no extra yaw lock or visual smoothing was added. TGS applies scene-wide, and the increased iteration budget applies to islands containing the car, so physics CPU cost can increase.

Every full run also performs a warm Ferrari launch on a flat plane after four seconds of settling. It measures rear wheel/hub separation, bearing-axis alignment, and the spherical joints attached to the rear uprights **after** each PhysX solve through eight seconds of acceleration and at least three upshifts. Limits are 1 mm for hub and joint separation and 0.25 degrees for bearing alignment. Toe angular rate is reported separately because genuine suspension travel changes toe.

Run the attachment and assembly checks without the full driving suite with:

```powershell
.\tools\car_tests\run.cmd --suspension-check 0.005
binaries\car_tests\headless.exe --suspension-check 0.0025
```

An optional final `pgs` argument deliberately overrides the shared solver for diagnostic comparisons and can fail the assertions. CSVs are saved as `suspension_{solver}_{frequency}.csv` in `binaries/car_tests`.

The original engine settings (PGS, 16/4 iterations) produced 17.843 mm peak rear joint separation and 251.641 degrees/s peak toe rate in this fixture. TGS alone at 16/4 still produced 4.249 mm separation. With the fix, the 200 Hz run completes four upshifts with 0.642 mm joint separation, 0.015 mm hub separation, and 0.095 degrees bearing error; at 400 Hz these are 0.824 mm, 0.023 mm, and 0.018 degrees. The full regression suites pass at both rates, including approximately 3.72/3.68 s to 100 km/h and 31.55/31.52 m stopping distances. These are reproducible numerical attachment checks, not measured Ferrari validation or a replay of the exact road/video sequence.

The editor's launch and braking Bench scenarios also enforce the warm XML performance envelopes. They include two seconds of settling and use a repeatable 100 km/h braking seed with consistent wheel/engine/gearbox speeds. Braking uses manual gear selection to keep the brake-to-reverse convenience feature out of the stopping test. The original setting is restored afterward.

## Assembly and debug accuracy

`assembly.h` checks a suspension rebuild at identity, 90-degree yaw, and an arbitrary 3D rotation. Wishbone dimensions and inertia must match in chassis coordinates. The old world-axis construction failed this check. Another fixture isolates neutral engine acceleration and verifies the equal/opposite chassis angular momentum; the former clutch-gated reaction failed this check. Both corrections preserve the existing estimated mass proxies and preset parameters.

The contact/debug fixture deliberately separates and yaws a rear wheel while leaving its upright untouched. Both bearing anchors must remain visible as distinct transforms, and tread rows must follow the wheel's actual plane. The renderer now uses the actual wheel pose for its rings and the upright pose for its caliper. Ball/fixed/hinge joints show both solved anchors, with red markers above 1 mm separation; hinge axes are drawn on both sides. Bushing markers likewise distinguish the chassis anchor from the deflected anchor. Bump-stop and packer markers use shock travel including motion ratio, and damper intensity uses shock velocity.

Longitudinal, lateral, and rolling-resistance arrows use captured world forces and their application points from the force step. The aggregate normal arrow sums the actual row forces instead of using the filtered contact normal. Caliper actuation uses the applied braking torque after the stopping clamp, which already includes handbraking. Tests check that a stationary unloaded wheel reports zero applied braking torque and clears stale tire-force arrows. Physical shaft outlines and stripes follow actor geometry/orientation rather than invented torque twist or wheel-speed animation.

The skeleton is a diagnostic view, not a CAD model: inertia/collision proxies are approximations, frame/engine internals remain schematic, and thermal colours/force arrows are scaled indicators. The HUD states these distinctions and arrow scales. PhysX validates rigid-body integration and constraints; it does not establish that the estimated hardpoints, tires, dampers, aero, or reduced drivetrain match a measured car. These changes have automated geometry/data checks and an editor build check, not screenshot-based rendering validation.

## Large-world acceleration regression

The September 5 recording `binaries/car_telemetry_15333242640187095602.csv` exposed a missing condition in the earlier suspension tests: the car was at approximately X=6276 m, Z=-2823 m. At those coordinates, 64 TGS position iterations made small per-iteration translations round away. Position stopped advancing along one map axis despite nonzero velocity, loading the suspension asymmetrically and turning the car under straight throttle. Increasing solver iterations alone was an incomplete fix.

`PhysicsSceneOrigin` now rebases the horizontal physics coordinates in 64 m increments when the active area leaves a 64 m radius along either map axis. It uses `PxScene::shiftOrigin` to retain contacts and constraint caches, rather than teleporting the assembly. Map/entity coordinates and elevation stay unchanged. The engine updates the character controller manager, inactive streamed actors, vehicle contacts, force/debug points, and odometer history. Rendering, queries, rigid-body creation/teleports, ragdolls, buoyancy and editor bench measurements convert at the physics/world boundary. The active area follows the camera during play and the car during bench runs and vehicle placement. This improves local precision; it does not make all distant, simultaneously active objects double precision.

The shared regression runs a cold Ferrari at the origin and at the recording's coordinates with a 0.63 rad heading. Five seconds at half throttle previously produced a 5.007 m position/integrated-velocity mismatch and a -0.222 rad unwanted heading change at the distant location. With rebasing, the mismatch is 0.029 m at 200 Hz (0.014 m at 400 Hz), matching the near-origin control. It then accelerates, steers and brakes over another 214 m, crossing three origin boundaries. Maximum near/far trajectory differences are 0.067 m at 200 Hz and 0.443 m at 400 Hz; both stop below 0.5 km/h. These are flat-road reproductions using the production force model and benchmark chassis proxy, not an exact replay of the recorded road.

```powershell
.\tools\car_tests\run.cmd --large-world-check
.\binaries\car_tests\headless.exe --large-world-check 0.0025
# Deliberately disable the fix; this must fail the map-coordinate motion check:
.\binaries\car_tests\headless.exe --large-world-check 0.005 unrebased
```

The full suite includes this regression. Additional checks cover finite road queries, a pending kinematic target, a world-anchored joint, character controller movement, and cached vehicle force points across a shift. Generated map-fixture CSVs live under `binaries/car_tests/large_world_map_*.csv`. Telemetry body, contact and hub positions remain map coordinates and now retain six decimal places; summation with the physics origin occurs in double precision so CSV rounding does not hide the small movement being diagnosed. The supplied recording is preserved.

## Telemetry and calibration

Python tools need NumPy and pandas. No SciPy dependency or network access is required.

```powershell
python tools/car_tests/analyze_telemetry.py binaries/car_telemetry_8395611541522216465.csv --output binaries/car_tests/driving_summary.json
python tools/car_tests/fit_tires.py measured_tire_lab.csv --output tire_fit.json
python tools/car_tests/fit_thermal.py measured_thermal_lab.csv --output thermal_fit.json
```

Run either fitter with `--help` for its measurement schema. Tire fitting calls the production C++ brush model through `headless --tire-evaluate`; it does not maintain a duplicate Python tire model. Thermal fitting solves the surface/core heat equations with bounded least squares. Fits report residual error and identification diagnostics and never modify a preset automatically. Driving telemetry is not independent laboratory force or heat data. Synthetic recovery checks verify the tools, not the Ferrari's calibration.

Telemetry schema version 2 appends calibration ID, event bits, reset count, accumulated distance, collision impulse (N s), assembled tensor (kg m2), usable battery SOC/temperature/electrical power/loss, engine-running state, clutch/gearbox losses (J), and per-wheel gauge pressure/damage/water depth/slip energy. Event bits: 1 reset, 2 validation velocity seed, 4 reported rigid contact, 8 numerical repair, 16 detected position discontinuity. Rigid-contact impulses belong to the preceding PhysX solve; tread-query forces remain separate force channels. Zero impulse is not proof that no road discontinuity occurred.

See [the follow-up report](REALISM.md) for current results, assumptions, and measured-data requirements. [The original audit](AUDIT.md) preserves the initial findings and supplied telemetry analysis.
