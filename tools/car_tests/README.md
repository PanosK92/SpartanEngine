# Car physics regression tests

Run from PowerShell on Windows with Visual Studio C++ tools and the engine's existing third-party libraries installed:

```powershell
.\tools\car_tests\run.cmd binaries/car_tests/validation.csv
binaries\car_tests\headless.exe binaries/car_tests/validation_400hz.csv 0.0025
```

The first command builds the production simulation, telemetry writer, and XML preset loader as a console executable with C++20, `/W3 /WX`, and the repository's PhysX libraries. It discovers Visual Studio with `vswhere`. Assimp supplies the same pugixml implementation used by the editor. Only engine logging and four filesystem entry points are replaced by console/filesystem adapters.

Arguments to the executable are optional telemetry output path, timestep in seconds (default 0.005), brake-force override in N, and traction-control slip-threshold override. Overrides affect the test instance only. Outputs go under the ignored `binaries/car_tests` directory. No editor process or user world is loaded or modified.

Checks cover COM point velocity, forward/reverse tire-force symmetry, dissipative force signs, combined grip, heat conservation, packer overtravel, airborne braking, external wake, support removal, brake-to-reverse wake, parking, assembled mass, motor power limits, valid driveline inertia, and the Ferrari's authored acceleration/braking targets. Every car XML preset is parsed and validated; dynamic performance is exercised on the Ferrari only.

The fixture uses gravity, a flat static plane, TGS, CCD, and the full suspension assembly. A small chassis box replaces the seed full-height box. It does **not** reproduce the rendered Ferrari's cooked collision hull, mesh-derived aerodynamic centres, the roads in `plan.world`, or driver manoeuvres. Passing these tests is a prerequisite for driving validation, not proof of vehicle fidelity.

To inspect an existing recording without modifying it, use Python with NumPy:

```powershell
python tools/car_tests/analyze_telemetry.py binaries/car_telemetry_8395611541522216465.csv --output binaries/car_tests/driving_summary.json
```

The analyzer rejects malformed rows, checks non-finite values, summarizes loads and temperatures, and groups extreme deceleration samples. Such samples are labelled events rather than being misreported as braking performance.

See [the audit](AUDIT.md) for findings, results, and remaining work.
