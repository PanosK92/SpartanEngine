// cheese grater side strake band, one loft with a comb cross section
const CONTROL = [
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
  [ 0.620, 0.895, 0.790, 0.160, 0.874, 0.842]
];

const SIDE = [
  [0.000, 0.880], [0.070, 0.935], [0.220, 0.972], [0.440, 0.996],
  [0.640, 1.000], [0.860, 0.990], [1.000, 0.952]
];

// z, band top, band bottom
const BAND = [
  [ 0.18, 0.640, 0.520],
  [ 0.05, 0.652, 0.508],
  [-0.12, 0.668, 0.494],
  [-0.30, 0.690, 0.481],
  [-0.48, 0.714, 0.472],
  [-0.64, 0.740, 0.470],
  [-0.78, 0.766, 0.488],
  [-0.88, 0.786, 0.528],
  [-0.96, 0.802, 0.582],
  [-1.02, 0.812, 0.628]
];

const FIN_COUNT = 6;
const FIN_FILL = 0.60;
const RECESS = 0.030;
const FIN_DEPTH = 0.028;

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

function body_x(z, y) {
  const [hw, fw, y0, ys, yc] = lerp_table(CONTROL, z);
  const t = Math.max(0, Math.min(1, (y - y0) / (ys - y0)));
  return side_fraction(t) * hw;
}

const path_points = [];
const loft_profiles = [];

for (const [z, top, bot] of BAND)
{
  const mid = 0.5 * (top + bot);
  const xs = body_x(z, mid);
  path_points.push([r(xs - RECESS), 0, r(z)]);

  const span = top - bot;
  const pitch = span / FIN_COUNT;
  const fin_h = pitch * FIN_FILL;

  const pts = [[0, r(bot - 0.012)]];
  for (let i = 0; i < FIN_COUNT; i++)
  {
    const fb = bot + i * pitch + (pitch - fin_h) * 0.5;
    const ft = fb + fin_h;
    pts.push([0, r(fb)]);
    pts.push([FIN_DEPTH, r(fb)]);
    pts.push([FIN_DEPTH, r(ft)]);
    pts.push([0, r(ft)]);
  }
  pts.push([0, r(top + 0.012)]);
  pts.push([-0.014, r(top + 0.012)]);
  pts.push([-0.014, r(bot - 0.012)]);
  loft_profiles.push(pts);
}

console.log(JSON.stringify({
  stations: path_points.length,
  points_per_profile: loft_profiles[0].length,
  path_points,
  loft_profiles
}));
