# Dyno drivetrain laboratory

Open `dyno.world` in the world selector, then press Play. Choose any car from the
same `.car` catalog used by Plan, choose a forward gear and throttle, and run an
RPM sweep or a fixed-speed hold. No preset tuning is changed or saved.

The car is secured on a speed-controlled **hub** fixture. The primitive rollers
and garage are presentation geometry. The fixture imposes hub speed and runs the
production crank, clutch, shaft, gearbox, boost and hybrid integrator at at most
0.5 ms per step. Engine RPM is measured, not forced; clutch slip is visible.
The garage is near the origin to avoid large-world coordinate precision effects.

Each run resets drivetrain transients and the preset's initial battery charge.
Two seconds of conditioning precede the sweep/hold duration. The graphs exclude
conditioning from RPM curves but retain it in the time trace and CSV. Hover a
graph for values. Purple power traces preserve the previous run for comparison.
The selected gear stays locked during the test. Stop retains a partial capture.

- **Combustion Nm/kW:** gross simulated combustion plus idle controller output,
  before engine friction and rotor acceleration. This is not certified net crank power.
- **Axle Nm/kW:** combined drivetrain output after clutch and gearbox losses,
  including hybrid assist. Power uses hub angular speed, not engine RPM.
- **Motor Nm/kW:** measured at the electric motor shaft, which runs at hub speed
  times final drive, independently of the selected engine gear ratio.
- hp means mechanical horsepower (`kW * 1.34102209`).

Tire contact losses, roller inertia, road/aero load and atmospheric corrections
are excluded. Compare like-for-like measurement locations and units when using
real-car reference data. A speed controller can motor the drivetrain during a
low-throttle sweep; negative axle output is retained. This tool tests the model,
not the accuracy of its preset calibration.

Completed and stopped runs automatically save `car_dyno_<timestamp>.csv` in the
engine working directory (normally `binaries`). The panel shows the saved name.
Files include car, fixture type, gear, ratios, conditioning flag, time, target and
actual RPM, hub RPM, throttle, torque/power, boost, SOC and clutch slip.

Agents can use the `vehicle_dyno` MCP tool with `action: status`, `start`, `stop` or
`select` (with `car` set to an exact catalog name, while mounted and stopped).
Start accepts `gear` (1-based), `start_rpm`, `end_rpm`, `seconds`, `throttle`, and
`sweep` (false selects hold). Load this world and enable play first. Status returns
progress, latest output and the CSV filename. The car selector is in the panel.

Run the production regression fixture from the repository root:

```powershell
.\tools\car_tests\run.cmd --dyno-check
```

It tests all registered cars at 200/400 Hz scene rates, repeatability, fixed gear
speed conversion, torque/power units, battery bounds, hold/stop, invalid inputs,
CSV capture and restraint of the chassis and multibody mechanisms. Test exports
are retained in `binaries/car_tests`.
