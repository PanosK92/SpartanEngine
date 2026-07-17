# Car preset contract

Every `.car` file is data for the shared vehicle assembly and simulation. Adding a car must not add car-specific code.

## Measured inputs

These values should come from manufacturer specifications or defensible public references:

- chassis mass and dimensions
- wheelbase and front and rear track
- wheel and tire radius and width
- gear ratios and final drive
- engine speed and torque limits
- brake bias
- center of mass estimate
- aerodynamic area and coefficients when available

## Physical model inputs

These values describe components that are rarely published and may require estimation:

- suspension hardpoints and mechanism
- upright wheel link and rack masses
- spring frequency damping ratio and anti-roll stiffness
- tire Pacejka coefficients relaxation length load sensitivity and vertical stiffness
- differential preload locking and viscous behavior
- aero balance ground effect and sensitivity maps

Physical model inputs must remain inside the ranges enforced by `validate_preset`.

## Accessibility settings

The `<assists>` section modifies driver commands or controller intervention. It must not change mass geometry tire grip or other physical limits.

- `steering_speed_reduction`
- `steering_speed_reference`
- `abs_level`
- `traction_control_level`

## Validation targets

The `<validation>` section defines broad public evidence envelopes rather than simulation tuning:

- `settle_speed_max`
- `zero_to_100_min` and `zero_to_100_max`
- `braking_distance_min` and `braking_distance_max`
- `skidpad_g_min` and `skidpad_g_max`

Use **Run validation** in the telemetry window from a flat vehicle test world. Results are written to `car_validation_report.csv`.

## Acceptance

A preset is acceptable when it loads without validation errors and passes settle acceleration braking coastdown skidpad step steer and slalom scenarios. Subjective driving is a final review and not a replacement for telemetry.
