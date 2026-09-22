import { execFile } from "node:child_process";
import { promisify } from "node:util";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const execute = promisify(execFile);
let running = false;

export function parse_summary(csv) {
  const records = []; let row = [], field = "", quoted = false;
  for (let i = 0; i < csv.length; ++i) {
    const c = csv[i];
    if (c === '"') {
      if (quoted && csv[i + 1] === '"') { field += '"'; ++i; }
      else quoted = !quoted;
    } else if (!quoted && (c === "," || c === "\n")) {
      row.push(field.replace(/\r$/, "")); field = "";
      if (c === "\n") { records.push(row); row = []; }
    } else field += c;
  }
  if (quoted) throw new Error("Unterminated CSV field");
  if (field || row.length) { row.push(field.replace(/\r$/, "")); records.push(row); }
  const [keys, ...rows] = records;
  if (!keys || !rows.length) throw new Error("Empty vehicle summary");
  return rows.map(values => {
    if (values.length !== keys.length) throw new Error("Invalid vehicle summary row");
    return Object.fromEntries(values.map((value, i) => {
      if (i && (value === "" || !Number.isFinite(Number(value)))) throw new Error("Non-finite vehicle measurement");
      return [keys[i], i === 0 ? value : Number(value)];
    }));
  });
}

// Runs the actual C++ vehicle model. The renderer and live engine are not involved.
export async function run_vehicle_validation(project_root, { hz = 200, tire_temperature_c = 20 } = {}) {
  if (running) throw new Error("A vehicle validation run is already active.");
  if (process.platform !== "win32") throw new Error("The supplied PhysX/MSVC libraries require Windows.");
  if (!Number.isInteger(hz) || hz < 100 || hz > 1000 || !Number.isFinite(tire_temperature_c) || tire_temperature_c < -20 || tire_temperature_c > 120)
    throw new Error("Invalid frequency or tire temperature.");
  running = true;
  let build, output, completed = false;
  try {
    build = await fs.mkdtemp(path.join(os.tmpdir(), "spartan-vehicle-build-"));
    output = await fs.mkdtemp(path.join(os.tmpdir(), "spartan-vehicle-results-"));
    // PowerShell caches executable extensions at startup. MCP's standard stdio
    // environment can omit PATHEXT even on Windows, preventing native tools running.
    const options = { cwd: project_root, windowsHide: true, timeout: 300000, maxBuffer: 4 * 1024 * 1024,
      env: { ...process.env, PATHEXT: process.env.PATHEXT || ".COM;.EXE;.BAT;.CMD" } };
    await execute("pwsh.exe", ["-NoProfile", "-File", path.join(project_root, "tools/car_validation/build.ps1"), "-OutputDirectory", build], options);
    const { stderr } = await execute(path.join(build, "car_validation.exe"), [path.join(project_root, "binaries/project/cars"), output, String(hz), String(tire_temperature_c)], options);
    const csv = await fs.readFile(path.join(output, "summary.csv"), "utf8");
    const results = parse_summary(csv);
    const references = JSON.parse(await fs.readFile(path.join(project_root, "tools/car_validation/references.json"), "utf8"));
    const report = {
      ok: true,
      calibrated: false,
      conditions: { hz, tire_temperature_c, surface: "flat dry asphalt", launch: "full pedal from idle; no rollout or launch control", brakes: "100 km/h initialized after acceleration; stop threshold 0.1 km/h", chassis: "production fallback box, full suspension assembly" },
      results: results.map(result => {
        const reference = references[result.car] ?? null;
        return { ...result, reference,
          completed: result.zero_to_100_s >= 0 && result.brake_100_to_0_1_m >= 0 && result.brake_70mph_to_0_1_m >= 0,
          net_power_difference_percent: reference?.net_engine_power_kw ? 100 * (result.dyno_net_peak_kw / reference.net_engine_power_kw - 1) : null,
          acceleration_difference_seconds: reference?.zero_to_100_seconds ? result.zero_to_100_s - reference.zero_to_100_seconds : null,
        };
      }),
      warnings: stderr.trim(),
      output_directory: output,
      limitations: ["A completed test is not a calibration pass.", "Manufacturer test tires, launch procedure, payload and atmosphere are not reproduced.", "Imported visual hulls are not used by this fixture.", "Missing manufacturer metrics remain unknown; the FWD reference is fictional."]
    };
    await fs.writeFile(path.join(output, "report.json"), JSON.stringify(report, null, 2) + "\n");
    completed = true;
    return report;
  } finally {
    if (build) await fs.rm(build, { recursive: true, force: true });
    if (output && !completed) await fs.rm(output, { recursive: true, force: true });
    running = false;
  }
}
