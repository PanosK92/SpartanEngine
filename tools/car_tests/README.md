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

## Coverage

Checks include assembled mass and full inertia, COM point velocities, left/right kinematic sweeps over +/-80 mm, motion ratios and wheel frequencies, packers, brake torque signs and handbrake heat, natural engine stall/restart, tire combined grip and reverse symmetry, pressure and water limits, battery energy and regeneration bounds, parking/wake and support removal, acceleration/stopping envelopes, moving support momentum exchange, and 50 mm kerbs. Handling includes left/right skidpads, step steering, slalom, lift-off, power-on, and split-friction braking. CSVs and kinematic sweep results are retained.

The editor's launch and braking Bench scenarios also enforce the warm XML performance envelopes. They include two seconds of settling and use a repeatable 100 km/h braking seed with consistent wheel/engine/gearbox speeds. Braking uses manual gear selection to keep the brake-to-reverse convenience feature out of the stopping test. The original setting is restored afterward.

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
