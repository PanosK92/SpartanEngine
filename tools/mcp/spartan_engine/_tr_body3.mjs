// testarossa body shell, flat wedge surfaces, crisp shoulder crease, wheel arch openings
const CONTROL = [
  // z,      hw,    fw,     y0,    ys,    yc
  [-2.245, 0.860, 0.700, 0.440, 0.985, 0.995],
  [-2.185, 0.925, 0.790, 0.375, 1.008, 1.012],
  [-2.050, 0.958, 0.830, 0.290, 1.020, 1.018],
  [-1.850, 0.976, 0.855, 0.215, 1.028, 1.018],
  [-1.600, 0.986, 0.868, 0.170, 1.032, 1.014],
  [-1.275, 0.988, 0.870, 0.155, 1.030, 1.008],
  [-0.950, 0.984, 0.865, 0.150, 1.012, 0.995],
  [-0.700, 0.972, 0.855, 0.150, 0.978, 0.950],
  [-0.450, 0.952, 0.840, 0.150, 0.948, 0.900],
  [-0.200, 0.928, 0.822, 0.150, 0.918, 0.870],
  [ 0.050, 0.908, 0.805, 0.155, 0.902, 0.855],
  [ 0.350, 0.898, 0.796, 0.160, 0.888, 0.845],
  [ 0.620, 0.895, 0.790, 0.160, 0.874, 0.842],
  [ 0.900, 0.899, 0.786, 0.160, 0.836, 0.792],
  [ 1.150, 0.904, 0.781, 0.165, 0.812, 0.762],
  [ 1.400, 0.906, 0.776, 0.175, 0.795, 0.742],
  [ 1.650, 0.899, 0.766, 0.190, 0.772, 0.716],
  [ 1.880, 0.879, 0.742, 0.215, 0.735, 0.680],
  [ 2.060, 0.846, 0.702, 0.250, 0.690, 0.640],
  [ 2.180, 0.802, 0.652, 0.290, 0.640, 0.600],
  [ 2.245, 0.742, 0.600, 0.320, 0.590, 0.560]
];

const STATIONS = [
  -2.245, -2.185, -2.05, -1.90, -1.79,
  -1.72, -1.64, -1.55, -1.45, -1.36, -1.275, -1.19, -1.10, -1.01, -0.92, -0.85,
  -0.78, -0.62, -0.45, -0.25, -0.05, 0.18, 0.40, 0.62,
  0.80, 0.87, 0.96, 1.06, 1.17, 1.275, 1.38, 1.48, 1.58, 1.66,
  1.72, 1.80, 1.88, 2.00, 2.10, 2.18, 2.245
];

// slab side with a hard crease, height fraction against half width fraction
const SIDE = [
  [0.000, 0.880],
  [0.070, 0.935],
  [0.220, 0.972],
  [0.440, 0.996],
  [0.640, 1.000],
  [0.860, 0.990],
  [1.000, 0.952]
];

const ARCHES = [
  { center: 1.275, half: 0.45, top: 0.652, well: 0.632 },
  { center: -1.275, half: 0.48, top: 0.690, well: 0.688 }
];

const r = (v) => Math.round(v * 10000) / 10000;

function lerp_table(table, key) {
  if (key <= table[0][0]) { return table[0].slice(1); }
  if (key >= table.at(-1)[0]) { return table.at(-1).slice(1); }
  for (let i = 0; i < table.length - 1; i++)
  {
    const a = table[i];
    const b = table[i + 1];
    if (key >= a[0] && key <= b[0])
    {
      const t = (key - a[0]) / (b[0] - a[0]);
      return a.slice(1).map((v, k) => v + (b[k + 1] - v) * t);
    }
  }
  return table.at(-1).slice(1);
}

const side_fraction = (t) => lerp_table(SIDE, t)[0];

function arch_at(z, y0, fw) {
  for (const arch of ARCHES)
  {
    const dz = z - arch.center;
    if (Math.abs(dz) < arch.half)
    {
      const bottom = y0 + 0.045;
      const k = Math.sqrt(
        Math.max(0, 1 - (dz / arch.half) * (dz / arch.half))
      );
      return {
        height: bottom + (arch.top - bottom) * k - y0,
        well: fw + (arch.well - fw) * Math.min(1, k * 2.6)
      };
    }
  }
  return { height: 0.022, well: fw };
}

const path_points = STATIONS.map((z) => [0, 0, r(z)]);

const loft_profiles = STATIONS.map((z) => {
  const [hw, fw, y0, ys, yc] = lerp_table(CONTROL, z);
  const h = ys - y0;
  const d = ys - yc;
  const { height: ah, well: xw } = arch_at(z, y0, fw);
  const t_arch = ah / h;
  const lip_x = Math.max(side_fraction(t_arch) * hw, xw + 0.026);

  const half = [
    [xw, y0],
    [xw, y0 + 0.35 * ah],
    [xw, y0 + 0.78 * ah],
    [xw + 0.012, y0 + ah],
    [lip_x, y0 + ah]
  ];
  for (const f of [0.24, 0.50, 0.74, 0.90])
  {
    const t = t_arch + (1 - t_arch) * f;
    half.push([side_fraction(t) * hw, y0 + t * h]);
  }
  // shoulder crease, two close points hold a hard highlight line
  half.push([0.952 * hw, ys - 0.016]);
  half.push([0.900 * hw, ys]);
  // flat top deck
  half.push([0.780 * hw, ys - 0.62 * d]);
  half.push([0.480 * hw, yc + 0.06 * d]);

  const pts = [[0, y0]];
  for (const p of half) { pts.push(p); }
  pts.push([0, yc]);
  for (let i = half.length - 1; i >= 0; i--)
  {
    pts.push([-half[i][0], half[i][1]]);
  }
  return pts.map(([x, y]) => [r(x), r(y)]);
});

// side surface x at the strake band, used to place the cheese grater fins
const strake_stations = [
  0.06, -0.15, -0.35, -0.55, -0.75, -0.95, -1.06
];
const strake_x = strake_stations.map((z) => {
  const [hw, fw, y0, ys, yc] = lerp_table(CONTROL, z);
  const t = (0.60 - y0) / (ys - y0);
  return [z, r(side_fraction(t) * hw)];
});

console.log(JSON.stringify({
  stations: STATIONS.length,
  points_per_profile: loft_profiles[0].length,
  strake_x,
  path_points,
  loft_profiles
}));
