"""Fit the simulator's thermal coefficients to independently measured test data.

CSV: time_s,surface_c,core_c,ambient_c,speed_ms,slip_power_w,rolling_power_w.
Surface is the equal-area mean of three tread zones. Use a controlled heat/cool
test with known power; driving telemetry's estimated forces are not ground truth.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.optimize import lsq_linear

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("measurements", type=Path)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
data = pd.read_csv(args.measurements)
names = ["time_s", "surface_c", "core_c", "ambient_c", "speed_ms", "slip_power_w", "rolling_power_w"]
values = data[names].to_numpy()
if len(data) < 30 or not np.isfinite(values).all() or (np.diff(data.time_s) <= 0).any():
    raise ValueError("Need at least 30 finite samples at increasing times")
t, surface, core, ambient, speed, slip, rolling = values.T
ds, dc = np.gradient(surface, t), np.gradient(core, t)
delta = surface - core
zero = np.zeros(len(t))
matrix = np.vstack([np.column_stack([ds, zero, delta, surface-ambient, np.abs(speed)*(surface-ambient)]), np.column_stack([zero, dc, -delta, 0.1*(core-ambient), 0.1*np.abs(speed)*(core-ambient)])])
power = np.concatenate([0.9*slip, rolling])
scale = np.linalg.norm(matrix, axis=0)
if (scale < 1e-9).any() or np.linalg.matrix_rank(matrix/scale) < 5:
    raise ValueError("Heat/cool/airspeed excitation is insufficient to identify all five coefficients")
fit = lsq_linear(matrix/scale, power, bounds=(np.array([100, 100, 0, 0, 0])*scale, np.array([1e6, 1e6, 1e4, 1e4, 1e3])*scale))
coefficients = fit.x/scale
report = {"source": str(args.measurements.resolve()), "samples": len(data), "converged": bool(fit.success), "power_rmse_w": float(np.sqrt(np.mean((matrix@coefficients-power)**2))), "condition_number": float(np.linalg.cond(matrix/scale)), "parameters": dict(zip(["tire_surface_heat_capacity", "tire_core_heat_capacity", "tire_surface_core_conductance", "tire_heat_transfer_static", "tire_heat_transfer_airflow"], map(float, coefficients))), "preset_modified": False}
args.output.write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
