// builds the compound_create payload for the testarossa greenhouse
const r = (v) => Math.round(v * 10000) / 10000;
const pt = (x, y) => [r(x), r(y)];

const PAINT = "project/mcp/blockout/materials/testarossa_paint_red.xml";
const GLASS = "project/mcp/blockout/materials/tr_glass.xml";
const BLACK = "project/mcp/blockout/materials/tr_black_plastic.xml";

// roof panel, shallow crowned slab
const roof = [
  [ 0.060, 0.668],
  [-0.100, 0.678],
  [-0.300, 0.686],
  [-0.450, 0.688],
  [-0.600, 0.684]
];
const roof_path = roof.map(([z]) => [0, 0, z]);
const roof_profiles = roof.map(([z, hw]) => [
  pt(-hw, 1.088),
  pt(-hw * 0.55, 1.083),
  pt(0, 1.081),
  pt(hw * 0.55, 1.083),
  pt(hw, 1.088),
  pt(hw, 1.112),
  pt(hw * 0.72, 1.126),
  pt(0, 1.133),
  pt(-hw * 0.72, 1.126),
  pt(-hw, 1.112)
]);

// a pillar, one side, mirrored on x
const pillar = [
  [0.615, 0.792, 0.845, 0.912, 0.040],
  [0.500, 0.783, 0.900, 0.965, 0.038],
  [0.380, 0.766, 0.958, 1.022, 0.037],
  [0.260, 0.742, 1.018, 1.078, 0.036],
  [0.150, 0.712, 1.070, 1.122, 0.035],
  [0.060, 0.678, 1.098, 1.135, 0.034]
];
const pillar_path = pillar.map(([z]) => [0, 0, z]);
const pillar_profiles = pillar.map(([z, xc, yb, yt, ht]) => [
  pt(xc - ht, yb + 0.012),
  pt(xc, yb),
  pt(xc + ht, yb + 0.012),
  pt(xc + ht, yt - 0.012),
  pt(xc, yt),
  pt(xc - ht, yt - 0.012)
]);

// windscreen, thin curved pane
const screen = [
  [0.615, 0.755, 0.862],
  [0.460, 0.748, 0.935],
  [0.300, 0.732, 1.008],
  [0.160, 0.702, 1.072],
  [0.060, 0.672, 1.115]
];
const screen_path = screen.map(([z]) => [0, 0, z]);
const screen_profiles = screen.map(([z, hw, yb]) => {
  const xs = [-1, -0.6, 0, 0.6, 1].map((f) => f * hw);
  const y = (x) => yb - 0.028 * (1 - (x / hw) * (x / hw));
  const lower = xs.map((x) => pt(x, y(x) - 0.008));
  const upper = xs.slice().reverse().map((x) => pt(x, y(x)));
  return [...lower, ...upper];
});

// side glass, one side, mirrored on x
const side = [
  [ 0.520, 0.812, 0.876, 0.780, 0.925],
  [ 0.350, 0.826, 0.874, 0.752, 1.030],
  [ 0.150, 0.836, 0.878, 0.716, 1.096],
  [-0.050, 0.842, 0.886, 0.700, 1.114],
  [-0.250, 0.848, 0.900, 0.700, 1.118],
  [-0.450, 0.852, 0.936, 0.702, 1.118],
  [-0.560, 0.852, 0.958, 0.704, 1.112]
];
const side_path = side.map(([z]) => [0, 0, z]);
const side_profiles = side.map(([z, xo, yb, xi, yt]) => [
  pt(xo, yb),
  pt(xi, yt),
  pt(xi - 0.008, yt),
  pt(xo - 0.008, yb)
]);

// flying buttress sail panel, one side, mirrored on x
const sail = [
  [-0.550, 0.860, 0.700, 0.960, 1.128],
  [-0.750, 0.882, 0.720, 1.000, 1.120],
  [-0.950, 0.900, 0.752, 1.010, 1.106],
  [-1.150, 0.910, 0.790, 1.020, 1.086],
  [-1.350, 0.912, 0.828, 1.028, 1.062],
  [-1.520, 0.906, 0.860, 1.030, 1.044]
];
const sail_path = sail.map(([z]) => [0, 0, z]);
const sail_profiles = sail.map(([z, xo, xi, ylow, ytop]) => [
  pt(xi, ytop - 0.058),
  pt(xo, ylow - 0.058),
  pt(xo, ylow),
  pt(xi, ytop)
]);

// rear window between the buttresses
const rear = [
  [-0.600, 0.715, 1.120],
  [-0.800, 0.740, 1.088],
  [-1.000, 0.775, 1.048],
  [-1.120, 0.795, 1.016]
];
const rear_path = rear.map(([z]) => [0, 0, z]);
const rear_profiles = rear.map(([z, hw, yb]) => {
  const xs = [-1, -0.6, 0, 0.6, 1].map((f) => f * hw);
  const y = (x) => yb + 0.014 * (1 - (x / hw) * (x / hw));
  const lower = xs.map((x) => pt(x, y(x) - 0.008));
  const upper = xs.slice().reverse().map((x) => pt(x, y(x)));
  return [...lower, ...upper];
});

const payload = {
  command: "compound_create",
  arguments: {
    name: "greenhouse",
    parent_id: "9304572436000742030",
    position: [0, 0, 0],
    asset_directory: "project/mcp/blockout/meshes",
    parts: [
      {
        name: "roof_panel",
        shape: "loft",
        mesh_path: "project/mcp/blockout/meshes/tr_roof_panel.mesh",
        path_points: roof_path,
        loft_profiles: roof_profiles,
        material: PAINT,
        with_physics: false
      },
      {
        name: "a_pillars",
        shape: "loft",
        mesh_path: "project/mcp/blockout/meshes/tr_a_pillars.mesh",
        path_points: pillar_path,
        loft_profiles: pillar_profiles,
        mirror_axis: "x",
        mirror_plane: 0,
        material: PAINT,
        with_physics: false
      },
      {
        name: "buttresses",
        shape: "loft",
        mesh_path: "project/mcp/blockout/meshes/tr_buttresses.mesh",
        path_points: sail_path,
        loft_profiles: sail_profiles,
        mirror_axis: "x",
        mirror_plane: 0,
        material: PAINT,
        with_physics: false
      },
      {
        name: "windscreen",
        shape: "loft",
        mesh_path: "project/mcp/blockout/meshes/tr_windscreen.mesh",
        path_points: screen_path,
        loft_profiles: screen_profiles,
        material: GLASS,
        with_physics: false
      },
      {
        name: "side_glass",
        shape: "loft",
        mesh_path: "project/mcp/blockout/meshes/tr_side_glass.mesh",
        path_points: side_path,
        loft_profiles: side_profiles,
        mirror_axis: "x",
        mirror_plane: 0,
        material: GLASS,
        with_physics: false
      },
      {
        name: "rear_window",
        shape: "loft",
        mesh_path: "project/mcp/blockout/meshes/tr_rear_window.mesh",
        path_points: rear_path,
        loft_profiles: rear_profiles,
        material: GLASS,
        with_physics: false
      }
    ]
  }
};

console.log(JSON.stringify(payload));
