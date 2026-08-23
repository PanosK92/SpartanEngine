// testarossa body shell with wheel arch openings cut into the loft profiles
const CONTROL = [
  // z,      hw,    fw,     y0,    ys,    yc
  [-2.245, 0.860, 0.700, 0.440, 0.980, 1.000],
  [-2.185, 0.925, 0.790, 0.375, 1.005, 1.015],
  [-2.050, 0.958, 0.830, 0.290, 1.020, 1.020],
  [-1.850, 0.976, 0.855, 0.215, 1.030, 1.015],
  [-1.600, 0.986, 0.868, 0.170, 1.035, 1.005],
  [-1.275, 0.988, 0.870, 0.155, 1.030, 0.995],
  [-0.950, 0.984, 0.865, 0.150, 1.010, 0.985],
  [-0.700, 0.972, 0.855, 0.150, 0.975, 0.968],
  [-0.450, 0.952, 0.840, 0.150, 0.945, 0.940],
  [-0.200, 0.928, 0.822, 0.150, 0.915, 0.905],
  [ 0.050, 0.908, 0.805, 0.155, 0.900, 0.895],
  [ 0.350, 0.898, 0.796, 0.160, 0.885, 0.878],
  [ 0.620, 0.895, 0.790, 0.160, 0.872, 0.858],
  [ 0.900, 0.899, 0.786, 0.160, 0.828, 0.775],
  [ 1.150, 0.904, 0.781, 0.165, 0.805, 0.735],
  [ 1.400, 0.906, 0.776, 0.175, 0.788, 0.705],
  [ 1.650, 0.899, 0.766, 0.190, 0.755, 0.665],
  [ 1.880, 0.879, 0.742, 0.215, 0.695, 0.615],
  [ 2.060, 0.846, 0.702, 0.250, 0.625, 0.555],
  [ 2.180, 0.802, 0.652, 0.290, 0.565, 0.510],
  [ 2.245, 0.742, 0.600, 0.335, 0.500, 0.465]
];

const STATIONS = [
  -2.245, -2.185, -2.05, -1.90, -1.79,
  -1.72, -1.64, -1.55, -1.45, -1.36, -1.275, -1.19, -1.10, -1.01, -0.92, -0.85,
  -0.78, -0.62, -0.45, -0.25, -0.05, 0.18, 0.40, 0.62,
  0.80, 0.87, 0.96, 1.06, 1.17, 1.275, 1.38, 1.48, 1.58, 1.66,
  1.72, 1.80, 1.88, 2.00, 2.10, 2.18, 2.245
];

// side curve, height fraction of the floor to crown span against half width fraction
const SIDE = [
  [0.000, 0.870],
  [0.055, 0.900],
  [0.200, 0.930],
  [0.420, 0.995],
  [0.620, 1.000],
  [0.820, 0.982],
  [1.000, 0.930]
];

const ARCHES = [
  { center: 1.275, half: 0.44, top: 0.658, well: 0.632 },
  { center: -1.275, half: 0.47, top: 0.700, well: 0.688 }
];

const r = (v) => Math.round(v * 10000) / 10000;

function interpolate(z) {
  if (z <= CONTROL[0][0]) { return CONTROL[0].slice(1); }
  if (z >= CONTROL.at(-1)[0]) { return CONTROL.at(-1).slice(1); }
  for (let i = 0; i < CONTROL.length - 1; i++)
  {
    const a = CONTROL[i];
    const b = CONTROL[i + 1];
    if (z >= a[0] && z <= b[0])
    {
      const t = (z - a[0]) / (b[0] - a[0]);
      return [1, 2, 3, 4, 5].map((k) => a[k] + (b[k] - a[k]) * t);
    }
  }
  return CONTROL.at(-1).slice(1);
}

function side_fraction(t) {
  if (t <= SIDE[0][0]) { return SIDE[0][1]; }
  if (t >= SIDE.at(-1)[0]) { return SIDE.at(-1)[1]; }
  for (let i = 0; i < SIDE.length - 1; i++)
  {
    const a = SIDE[i];
    const b = SIDE[i + 1];
    if (t >= a[0] && t <= b[0])
    {
      const k = (t - a[0]) / (b[0] - a[0]);
      return a[1] + (b[1] - a[1]) * k;
    }
  }
  return SIDE.at(-1)[1];
}

function arch_at(z, y0, fw) {
  for (const arch of ARCHES)
  {
    const dz = z - arch.center;
    if (Math.abs(dz) < arch.half)
    {
      const bottom = y0 + 0.05;
      const k = Math.sqrt(
        Math.max(0, 1 - (dz / arch.half) * (dz / arch.half))
      );
      return {
        height: bottom + (arch.top - bottom) * k - y0,
        well: fw + (arch.well - fw) * Math.min(1, k * 2.4)
      };
    }
  }
  return { height: 0.022, well: fw };
}

const path_points = STATIONS.map((z) => [0, 0, r(z)]);

const loft_profiles = STATIONS.map((z) => {
  const [hw, fw, y0, ys, yc] = interpolate(z);
  const h = ys - y0;
  const d = ys - yc;
  const { height: ah, well: xw } = arch_at(z, y0, fw);
  const t_arch = ah / h;
  const lip_x = Math.max(side_fraction(t_arch) * hw, xw + 0.026);

  const half = [
    [xw, y0],
    [xw, y0 + 0.35 * ah],
    [xw, y0 + 0.75 * ah],
    [xw + 0.012, y0 + ah],
    [lip_x, y0 + ah]
  ];
  for (const f of [0.22, 0.48, 0.72, 0.88])
  {
    const t = t_arch + (1 - t_arch) * f;
    half.push([side_fraction(t) * hw, y0 + t * h]);
  }
  half.push([side_fraction(1) * hw, ys]);
  half.push([0.760 * hw, ys - 0.350 * d - 0.004]);
  half.push([0.420 * hw, yc + 0.100 * d]);

  const pts = [[0, y0]];
  for (const p of half) { pts.push(p); }
  pts.push([0, yc]);
  for (let i = half.length - 1; i >= 0; i--)
  {
    pts.push([-half[i][0], half[i][1]]);
  }
  return pts.map(([x, y]) => [r(x), r(y)]);
});

console.log(JSON.stringify({
  stations: STATIONS.length,
  points_per_profile: loft_profiles[0].length,
  path_points,
  loft_profiles
}));
