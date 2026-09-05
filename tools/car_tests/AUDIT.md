# Ferrari simulation audit � first pass

**Historical first-pass report.** The follow-up requested for every realism category is documented in [REALISM.md](REALISM.md), which supersedes the remaining-work list and test results below.

Audit date: 5 September 2026. Scope: the Ferrari LaFerrari prefab selected by `worlds/plan.world`, `worlds/cars/ferrari_laferrari.car`, the production car simulation, preset loading, tire model, telemetry, relevant physics integration, and the existing bench. The supplied driving CSV was treated as data and was not edited. Concurrent world, road, and spline changes were preserved.

## Assessment

This is a substantial custom multibody simulation, with several sound foundations, but it is not yet a validated representation of a real LaFerrari. The assembly is more detailed than the measured data that supports it. Adding more rigid bodies alone would not close that gap.

The useful foundations are a fixed 200 Hz scene step, forces evaluated immediately before PhysX advances, wheel/upright bodies connected by revolute bearings, suspension links and coilovers, contact-plane tire forces, equal/opposite road reactions, load-sensitive grip, transient slip, combined friction limits, and separate sprung/unsprung mass accounting. The Ferrari uses front double wishbones, a rear multi-link approximation, and the Full simulation mode by default. The test assembly contains 47 rigid actors including the chassis.

The changes in this audit correct reproducible mathematical and integration defects and improve the Ferrari's straight-line calibration. They do not establish measured suspension kinematics, tire coefficients, skidpad performance, or collision fidelity.

## Implemented corrections

1. **Point velocities use the center of mass.** PhysX reports linear velocity at the COM. Chassis and moving-ground calculations previously used `v + omega cross (point - actor_origin)`. They now use the world COM. This affects shock velocity, ground-relative contact damping, and the chassis fallback tire velocity. The convention is explicitly documented by [NVIDIA's rigid-body API](https://nvidia-omniverse.github.io/PhysX/physx/5.8.0/_api_build/classPxRigidBody.html).

2. **Assembled mass matches the preset.** The estimated mechanism mass included an 8.5 kg propshaft that the Ferrari assembly did not actually create because its endpoints were too close. The physical assembly weighed about 1421.5 kg while telemetry said 1430 kg. Chassis mass now reconciles against the actors actually created. COM reconciliation is performed in chassis coordinates to reduce cancellation at large world positions.

3. **Packers can respond to overtravel.** Shock compression was clamped at 100% before evaluating a packer that starts at 105%. Overtravel now reaches the bump-stop/packer calculation, while force limits remain. The mechanical travel constraint still handles nominal travel; this fix allows the compliant force law to respond when that limit is exceeded.

4. **Low-speed tire stabilization uses unsprung mass.** The previous static lateral correction used `Fz/g`, despite applying the force to a wheel body before the suspension constraints solve. In the isolated test this excited large opposing lateral forces at rest. The correction now uses wheel mass when a wheel actor exists. This is a conservative stabilization approximation, not an exact articulated effective-mass solve.

5. **Tire substeps match the actual contact torque.** Effective rolling radius is used to calculate slip. The torque integrated internally now uses the actual contact lever arm, including lateral-force contribution when relevant, matching the force applied to PhysX. Bearing torque is averaged across the substeps and reacts on the upright.

6. **Reverse behavior is consistent.** Brush traction/braking conversion now accounts for direction of travel; reverse acceleration no longer follows the forward-braking branch. LSD acceleration/coast locking uses torque relative to axle travel direction. Pneumatic trail reverses with rolling direction and fades through zero speed.

7. **Service brakes cannot reverse an airborne wheel.** Both supported and airborne paths cap brake torque at the value needed to arrest the predicted wheel motion, including holding against drive torque at zero spin. Brake heating uses the work actually performed by the limited brake torque.

8. **Hybrid power uses the correct shaft speed.** Electric torque is routed after the selected gear and before the final drive in this implementation. Its limiter and displayed power now use that shaft's speed rather than engine RPM. Smoothing cannot retain torque above the instantaneous motor envelope. The separate battery/energy-management omissions remain below.

9. **Gearbox acceleration transforms correctly.** With `omega_input = ratio * omega_output`, acceleration must also multiply by the ratio. The previous division corrupted the drivetrain reaction calculation. The proxy gearbox flange's transverse inertia now also satisfies the principal-moment triangle bound.

10. **Parking can wake safely.** Brake input in a forward gear, external body wake, moving query supports, and loss of support can wake the assembly. A jump apex or wheelspin cannot trigger parking solely because chassis translation is small. An unchanged handbrake no longer calls the PhysX drive setter with automatic wake every tick. Handbrake limits explicitly use torque units. Parking still uses a numerical low-speed sleep tolerance; it is not a general static-friction constraint solver.

11. **Thermal evolution uses energy and heat capacity.** Tire sliding power is `max(Fx * (wheel_surface_speed - vx) - Fy * vy, 0)`. Surface/core exchange is equal and opposite, and rolling losses heat the core. Brake temperature uses work divided by disc mass and specific heat; cooling uses the same capacity. A stationary burnout can heat the tread. Temperatures continue evolving while parked.

12. **Telemetry and timestep safeguards.** Sleeping samples retain elapsed time, parked acceleration is cleared, and the opening log reports the actual telemetry path. Invalid/nonpositive tick durations are rejected. Rolling resistance cannot become propulsive through an extreme pressure multiplier.

13. **Ferrari brake and TC calibration.** Brake capacity increased from 12,000 to 24,000 N and the TC threshold from 0.05 to 0.10. At 1430 kg, the old cold brake capacity was only about 0.68 g before other resistance, inconsistent with the preset's 28–38 m stopping target. The old TC threshold also cut substantial drive torque before the current brush model reached useful longitudinal grip. The new values were checked at two timesteps against the existing target ranges; they are simulation calibration values, not measured Ferrari actuator specifications.

## Supplied telemetry

The recording contains 9,374 rows and 315 columns, representing 46.87 seconds at 0.005 seconds per sample. No numeric field contained NaN or infinity. Peak speed was 291.36 km/h. The configuration stayed at 1430 kg, 12,000 N brake capacity, and 700 Nm engine peak torque.

Rear surface temperatures reached 225.1/226.1 C and rear core temperatures 210.3/210.4 C. Front surfaces reached 188.2/189.3 C. These are outputs of the old thermal model, not evidence that real tires reached those temperatures. The old rolling-heating term alone was proportional to wheel speed and was multiplied directly into a temperature rate, without a heat capacity. The revised implementation replaces that formulation rather than hiding it with a lower temperature clamp.

Wheel contact was present for about 97.5–98.3% of samples. Maximum front loads were approximately 12.9/12.5 kN, and rear loads 20.2/18.9 kN. Those peaks require event context; they are not steady corner weights.

The strongest filtered longitudinal deceleration was -177.837 m/s2, approximately -18.1 g, during 8.775–8.885 seconds. Brake input was zero throughout that event. Smaller extreme events also occurred around 5.675–5.710 seconds with zero brake. These cannot be used as ordinary braking measurements. Collision, road discontinuity, reset, or constraint impulses are possibilities; the recording lacks the contact-impulse/event information needed to distinguish them conclusively.

## Validation results

The unmodified production simulation on the isolated plane took **4.415 s from 0–100 km/h** and **50.88 m from 100–0 km/h**. After the fixes, thermal changes, and Ferrari calibration, the 200 Hz run is approximately **3.77 s** and **31.1 m**. The 400 Hz check is approximately **3.76 s** and **31.2 m**. Both meet the preset's 2.5–4.0 s acceleration and 28–38 m braking envelopes. These figures are rounded; executable output and CSVs contain the individual run results.

The final assembly mass is 1429.999 kg within float precision. Parking remains asleep with zero speed. In the launch/braking fixture, final temperatures are about 63 C at the front tread, 99 C at the rear tread, 53 C in the rear core, and 77 C at a front brake. This is a different maneuver from the supplied driving recording and must not be presented as a direct replay comparison.

The regression executable compiles the real simulation, preset loader, and telemetry implementation with `/W3 /WX`. Checks include reverse force symmetry across combined-slip cases, dissipative signs, friction limits, heat conservation, mass reconciliation, packer engagement, airborne braking, support removal, parking/wake behavior, motor power, inertia bounds, and Ferrari performance targets. All five car presets pass parsing/validation. Other cars have not undergone full dynamic regression.

The full editor was not launched, and the exact Ferrari mesh-derived hull and aerodynamic application points were not exercised. The existing in-editor bench also does not enforce the XML acceleration, stopping-distance, or skidpad ranges: its launch/braking scenarios primarily check other failure symptoms. The new headless tests explicitly enforce the first two ranges; skidpad remains unvalidated.

## Original follow-up work list (see REALISM.md for implementation and current limits)

**Suspension geometry and wheel rates.** The hardpoints are generated from a small number of offsets and fractions of track width, not measured 3D pickup coordinates. The front upper/lower outer points share longitudinal position, so meaningful authored caster and anti-dive geometry are absent from this construction. Rear yaw is restrained by a chassis-relative angular lock rather than a physical toe link. That limits toe compliance and bump-steer realism. Replace these approximations with explicit pickup coordinates and validate kinematics over bump, droop, roll, and steering before adding more mechanisms.

`compute_constants` derives rates as `k = m * (2*pi*f)^2`, but those wheel-rate targets are applied along an inclined shock. The measured motion ratio is largely telemetry; it does not convert the authored wheel rate into shock rate. For motion ratio `r = d(shock travel)/d(wheel travel)`, the local conversion is `k_shock = k_wheel/r^2`, likewise for damping, with preload solved from the actual force directions. That correction should follow a kinematic sweep so the ratio includes linkage motion rather than only shock inclination. The stated suspension frequencies are therefore not yet verified modal frequencies.

**Mass distribution and inertia provenance.** The engine coordinates are X lateral, Y vertical, Z forward; `Ixx` is pitch, `Iyy` yaw, and `Izz` roll. The Ferrari values 520/2150/1720 kg m2 deserve checking against that convention. They are assigned to the chassis while mechanism inertias are additional. Establish whether the numbers represent the sprung chassis or the complete car before changing them or subtracting mechanism inertia with the parallel-axis theorem. Mass and COM now reconcile, but complete-car inertia has not been measured.

**Contact and collisions.** Wheel mechanism shapes have simulation/query collision disabled; contact comes from tread rays. A credible patch on a smooth road does not imply realistic tire sidewall, rim, kerb-edge, wall, or deep-penetration contact. The probe excludes surfaces whose normal is too far from chassis-up, and averages samples that can belong to different surfaces. Normal reactions are distributed per row, while tangential reaction is assigned to one selected ground actor. Split moving supports are consequently approximate. Shape sweeps or a dedicated contact envelope, plus wheel/rim collision guards and per-contact material/reaction handling, are needed for robust kerb and obstacle behavior.

**Tires and handling.** Neither the brush parameters nor the alternative Magic Formula coefficients have been fitted to measured force/slip/load/camber curves. The implementation's symmetric slip denominator must be accounted for when importing conventional coefficients. Tire wear, temperature-to-grip loss, pressure effects, and sliding friction remain empirical. There is no pressure evolution from gas temperature, structural tire damage, aquaplaning, or validated road roughness response. Run constant-radius skidpads in both directions, step steer, slalom, split-friction braking, and lift-off/power-oversteer tests. A plausible 0–100 time is not a handling validation. [NVIDIA's vehicle guide](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html) discusses the sensitivity of tire/suspension behavior to tuning and substeps; [MathWorks' tire model documentation](https://www.mathworks.com/help/sdl/ref/tireroadinteractionmagicformula.html) provides explicit slip conventions for coefficient-based models.

**Powertrain.** The engine curve is a generated shape around peak torque, not a dyno curve. Engine idle/max RPM clamps, no-stall behavior, coast-torque clipping, and shaft-twist clearing are deliberate simplifications with energy consequences. Several drivetrain actors have prescribed velocities while wheel torque is applied separately; they are not a wholly constraint-driven differential and transmission. The hybrid motor has a power envelope but no battery state of charge, regeneration, efficiency map, or thermal derating. Use one clearly defined reduced rotational drivetrain model with explicit energy accounting, or a fully coupled mechanical formulation; avoid adding more inertia-carrying visual shaft actors without a consistent momentum budget.

**Aerodynamics.** Scalar drag/downforce, mesh-derived application points, and heuristic yaw/pitch/ground-effect factors are not an aerodynamic map. Ride height is estimated from suspension compression rather than measured underfloor clearance. Use measured or explicitly estimated front/rear aero maps versus speed, ride height, pitch, and yaw. No CFD, wind-tunnel, or high-speed balance validation was performed here.

**Thermal calibration.** The new equations conserve exchanged energy, but their default capacities, transfer coefficients, and 90% friction heat partition are engineering estimates. They require fitting to tire/brake measurements. The Ferrari still uses 50 C as tire ambient, effectively a warm environment. Handbrake drive work is not separately instrumented for brake heating. Legacy `tire_heat_from_slip`, `tire_heat_from_rolling`, `tire_cooling_rate`, `tire_cooling_airflow`, `tire_core_transfer_rate`, `tire_surface_response`, and `brake_heat_coefficient` remain readable for XML compatibility but no longer control heating. Use the new heat-capacity/conductance fields and `brake_specific_heat` instead.

**Validation and observability.** Add contact impulses, reset/collision markers, accumulated distance, actual assembled inertia, and simulation/preset version information to recordings. Extend the bench with performance envelopes and repeatable maneuver definitions. The next acceptance run should use the actual `plan.world` car, the cooked hull, a level measured test surface, warm/cold conditions, both turn directions, and the same scenarios at two scene timesteps. Preserve that baseline before further tuning.
