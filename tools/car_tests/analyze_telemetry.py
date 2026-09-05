"""Read-only telemetry checks. Run from the repository root with bundled Python."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("csv", type=Path)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
with args.csv.open(newline="") as stream:
    reader = csv.DictReader(stream)
    rows = list(reader)
    columns = reader.fieldnames
if not rows or any(None in row or any(value is None for value in row.values()) for row in rows):
    raise ValueError("Empty or malformed telemetry")
data = {key: np.asarray([float(row[key]) for row in rows]) for key in columns if key not in ("car_name", "calibration_id")}
wheels = ["fl", "fr", "rl", "rr"]
summary = {
    "source": str(args.csv), "rows": len(rows), "columns": len(columns),
    "cars": sorted({row["car_name"] for row in rows}),
    "duration_seconds": float(data["dt"].sum()),
    "nonfinite_values": sum(int((~np.isfinite(v)).sum()) for v in data.values()),
    "dt_seconds": [float(data["dt"].min()), float(data["dt"].max())],
    "ranges": {}, "wheels": {}, "deceleration_events": [],
}
for name in ["speed_kmh", "long_accel", "lat_accel", "vy", "ang_vel_mag", "mass", "brake_force", "engine_peak_tq"]:
    summary["ranges"][name] = dict(zip(["min", "median", "p95", "max"], map(float, np.percentile(data[name], [0, 50, 95, 100]))))
for wheel in wheels:
    summary["wheels"][wheel] = {
        "grounded_fraction": float(data[wheel + "_grounded"].mean()),
        "maximum_surface_celsius": float(data[wheel + "_surf_temp"].max()),
        "maximum_core_celsius": float(data[wheel + "_core_temp"].max()),
        "minimum_camber_degrees": float(np.rad2deg(data[wheel + "_dyn_camb"]).min()),
        "maximum_camber_degrees": float(np.rad2deg(data[wheel + "_dyn_camb"]).max()),
        "maximum_load_newtons": float(data[wheel + "_tire_load"].max()),
        "maximum_compression": float(data[wheel + "_comp"].max()),
    }
# Group extreme deceleration samples. These are events to inspect, not braking tests.
indices = np.flatnonzero(data["long_accel"] < -20)
for group in np.split(indices, np.where(np.diff(indices) > 1)[0] + 1):
    if not len(group):
        continue
    first, last = int(group[0]), int(group[-1])
    summary["deceleration_events"].append({
        "first_time": float(data["time"][first]), "last_time": float(data["time"][last]),
        "minimum_acceleration": float(data["long_accel"][group].min()),
        "speed_before_kmh": float(data["speed_kmh"][max(first - 1, 0)]),
        "speed_after_kmh": float(data["speed_kmh"][last]),
        "maximum_brake_input": float(data["brake"][group].max()),
    })
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))
