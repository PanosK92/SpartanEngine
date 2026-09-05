"""Fit reference-condition laboratory tire data against the production C++ model.

Columns: kappa,alpha_rad,camber_rad,fz_n,radius_m,width_m,fx_n,fy_n.
kappa = (R*omega-vx)/max(abs(vx),abs(R*omega)); alpha=atan2(vy,abs(vx)).
Positive Fx follows positive kappa, positive alpha produces negative Fy.
Use steady, forward-running samples at the calibrated pressure/temperature.
Driving telemetry is not a substitute for independently measured tire forces.
"""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.optimize import least_squares

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("measurements", type=Path)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--exe", type=Path, default=Path("binaries/car_tests/headless.exe"))
args = parser.parse_args()
data = pd.read_csv(args.measurements)
inputs = ["kappa", "alpha_rad", "camber_rad", "fz_n", "radius_m", "width_m"]
values = data[inputs + ["fx_n", "fy_n"]].to_numpy()
if len(values) < 20 or not np.isfinite(values).all() or (data.fz_n <= 0).any():
    raise ValueError("Need at least 20 finite positive-load samples")
if data.kappa.abs().max() < 0.1 or data.alpha_rad.abs().max() < 0.1:
    raise ValueError("Both axes need linear and sliding-region measurements to identify stiffness and friction")
with tempfile.TemporaryDirectory(prefix="spartan_tire_fit_") as directory:
    source, prediction = Path(directory) / "input.csv", Path(directory) / "prediction.csv"
    data[inputs].to_csv(source, index=False, lineterminator="\n")

    def residual(log_parameters):
        parameters = np.exp(log_parameters)
        subprocess.run([str(args.exe.resolve()), "--tire-evaluate", str(source), str(prediction), *map(str, parameters)], check=True, capture_output=True)
        predicted = pd.read_csv(prediction).to_numpy()
        return ((predicted - data[["fx_n", "fy_n"]].to_numpy()) / data.fz_n.to_numpy()[:, None]).ravel()

    result = least_squares(residual, np.log([1.8e7, 2.4e7, 1.5]), bounds=(np.log([1e6, 1e6, 0.2]), np.log([1e8, 1e8, 3])), diff_step=0.001, loss="soft_l1", max_nfev=120)
    parameters = dict(zip(["tread_stiffness_long", "tread_stiffness_lat", "tire_friction"], map(float, np.exp(result.x))))
    report = {"source": str(args.measurements.resolve()), "samples": len(data), "converged": bool(result.success), "normalized_rmse": float(np.sqrt(np.mean(result.fun**2))), "jacobian_rank": int(np.linalg.matrix_rank(result.jac)), "parameters": parameters, "preset_modified": False}
    args.output.write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    if not result.success or report["jacobian_rank"] < 3:
        raise SystemExit("Fit is not identifiable/converged; do not use these parameters")
