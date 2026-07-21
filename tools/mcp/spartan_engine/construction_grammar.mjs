import { createHash } from "node:crypto";

function safe_name(value) {
  return String(value ?? "grammar")
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/^_+|_+$/g, "")
    .slice(0, 80) || "grammar";
}

function short_hash(value) {
  return createHash("sha256")
    .update(JSON.stringify(value))
    .digest("hex")
    .slice(0, 12);
}

function positive_size(value) {
  return (
    Array.isArray(value) &&
    value.length === 3 &&
    value.every((entry) =>
      Number.isFinite(entry) && entry > 0,
    )
  );
}

function valid_vector3(value) {
  return (
    Array.isArray(value) &&
    value.length === 3 &&
    value.every((entry) =>
      Number.isFinite(entry),
    )
  );
}

function distance_squared(a, b) {
  const x = a[0] - b[0];
  const y = a[1] - b[1];
  const z = a[2] - b[2];
  return x * x + y * y + z * z;
}

function validate_generated_part(part, index) {
  const label = `grammar part ${index + 1} ${part.name}`;
  if (
    part.position !== undefined &&
    !valid_vector3(part.position)
  )
  {
    throw new Error(`${label} has an invalid position`);
  }
  if (
    part.rotation_euler !== undefined &&
    !valid_vector3(part.rotation_euler)
  )
  {
    throw new Error(
      `${label} has an invalid rotation`,
    );
  }
  if (
    part.size !== undefined &&
    !positive_size(part.size)
  )
  {
    throw new Error(`${label} has an invalid size`);
  }
  if (
    part.bevel !== undefined &&
    (
      !Number.isFinite(part.bevel) ||
      part.bevel <= 0 ||
      !part.size ||
      part.bevel >= Math.min(...part.size) * 0.5
    )
  )
  {
    throw new Error(`${label} has an invalid bevel`);
  }
  if (part.shape === "inset_panel")
  {
    const panel_limit = Math.min(
      part.size[0],
      part.size[1],
    ) * 0.5;
    if (
      !Number.isFinite(part.border) ||
      part.border <= 0 ||
      part.border >= panel_limit ||
      !Number.isFinite(part.inset) ||
      part.inset <= 0 ||
      part.inset >= part.size[2]
    )
    {
      throw new Error(
        `${label} has invalid panel dimensions`,
      );
    }
  }
  if (part.shape === "rounded_cylinder")
  {
    if (
      !Number.isFinite(part.radius) ||
      part.radius <= 0 ||
      !Number.isFinite(part.height) ||
      part.height <= 0
    )
    {
      throw new Error(
        `${label} has invalid cylinder dimensions`,
      );
    }
  }
  if (part.shape === "torus")
  {
    if (
      !Number.isFinite(part.major_radius) ||
      part.major_radius <= 0 ||
      !Number.isFinite(part.minor_radius) ||
      part.minor_radius <= 0 ||
      part.minor_radius >= part.major_radius
    )
    {
      throw new Error(
        `${label} has invalid torus dimensions`,
      );
    }
  }
  if (part.path_points !== undefined)
  {
    if (
      !Array.isArray(part.path_points) ||
      part.path_points.length < 2 ||
      part.path_points.some((point) =>
        !valid_vector3(point),
      )
    )
    {
      throw new Error(`${label} has an invalid path`);
    }
    for (
      let point_index = 1;
      point_index < part.path_points.length;
      point_index++
    )
    {
      if (
        distance_squared(
          part.path_points[point_index - 1],
          part.path_points[point_index],
        ) <= 1e-12
      )
      {
        throw new Error(
          `${label} has consecutive duplicate path points`,
        );
      }
    }
  }
}

function clamp(value, minimum, maximum) {
  return Math.max(
    minimum,
    Math.min(maximum, value),
  );
}

function mesh_path(context, key, signature) {
  return [
    `${context.directory}/`,
    `${context.base_name}_${safe_name(key)}_`,
    `${short_hash(signature)}.mesh`,
  ].join("");
}

function add_reused_part(
  context,
  key,
  part,
) {
  const signature = {
    shape: part.shape,
    size: part.size,
    radius: part.radius,
    height: part.height,
    bevel: part.bevel,
    segments: part.segments,
    major_radius: part.major_radius,
    minor_radius: part.minor_radius,
    minor_segments: part.minor_segments,
    bevel_segments: part.bevel_segments,
    border: part.border,
    inset: part.inset,
    path_points: part.path_points,
    profile: part.profile,
    depth: part.depth,
    thickness: part.thickness,
    scale_start: part.scale_start,
    scale_end: part.scale_end,
  };
  context.parts.push({
    ...part,
    mesh_path: mesh_path(
      context,
      key,
      signature,
    ),
    reuse_existing: true,
  });
}

function add_box(
  context,
  {
    key,
    name,
    size,
    position,
    rotation_euler,
    material,
    bevel,
    shape = "beveled_box",
  },
) {
  const safe_bevel = clamp(
    bevel ?? Math.min(...size) * 0.08,
    0.0005,
    Math.min(...size) * 0.45,
  );
  add_reused_part(
    context,
    key,
    {
      name,
      shape,
      size,
      bevel: safe_bevel,
      position,
      rotation_euler,
      material,
    },
  );
}

function add_inset_panel(
  context,
  {
    key,
    name,
    size,
    position,
    material,
  },
) {
  const border = Math.min(
    size[0],
    size[1],
  ) * 0.08;
  add_reused_part(
    context,
    key,
    {
      name,
      shape: "inset_panel",
      size,
      border,
      inset: Math.min(size[2] * 0.2, border * 0.5),
      position,
      material,
    },
  );
}

function build_window_grid(context, args) {
  const [width, height, depth] = args.size;
  const rows = clamp(args.rows ?? 2, 1, 5);
  const columns = clamp(args.columns ?? 4, 1, 8);
  const frame = clamp(
    args.thickness ?? Math.min(width, height) * 0.025,
    0.015,
    Math.min(width / columns, height / rows) * 0.22,
  );
  const inner_width = width - frame * 2;
  const inner_height = height - frame * 2;
  const pane_width = inner_width / columns;
  const pane_height = inner_height / rows;
  const pane_depth = Math.max(0.008, depth * 0.22);

  for (let row = 0; row < rows; row++)
  {
    for (let column = 0; column < columns; column++)
    {
      add_box(context, {
        key: "window_pane",
        name: `window_pane_${row + 1}_${column + 1}`,
        size: [
          pane_width - frame * 1.4,
          pane_height - frame * 1.4,
          pane_depth,
        ],
        position: [
          -width * 0.5 +
            frame +
            pane_width * (column + 0.5),
          -height * 0.5 +
            frame +
            pane_height * (row + 0.5),
          0,
        ],
        material:
          args.glass_material ??
          args.secondary_material,
        bevel: pane_depth * 0.1,
      });
    }
  }

  for (let column = 0; column <= columns; column++)
  {
    add_box(context, {
      key: "window_vertical_frame",
      name: `window_vertical_frame_${column + 1}`,
      size: [frame, height + frame, depth],
      position: [
        column === 0
          ? -width * 0.5 + frame * 0.5
          : column === columns
            ? width * 0.5 - frame * 0.5
            : -width * 0.5 +
              frame +
              pane_width * column,
        0,
        0,
      ],
      material: args.primary_material,
    });
  }
  for (let row = 0; row <= rows; row++)
  {
    add_box(context, {
      key: "window_horizontal_frame",
      name: `window_horizontal_frame_${row + 1}`,
      size: [width, frame, depth],
      position: [
        0,
        row === 0
          ? -height * 0.5 + frame * 0.5
          : row === rows
            ? height * 0.5 - frame * 0.5
            : -height * 0.5 +
              frame +
              pane_height * row,
        0,
      ],
      material: args.primary_material,
    });
  }
}

function build_doorway(context, args) {
  const [width, height, depth] = args.size;
  const frame = clamp(
    args.thickness ?? width * 0.07,
    0.025,
    width * 0.18,
  );
  const leaf_width = width - frame * 2.4;
  const leaf_height = height - frame * 1.5;
  const leaf_depth = Math.max(0.025, depth * 0.32);

  for (const side of [-1, 1])
  {
    add_box(context, {
      key: "door_jamb",
      name: side < 0
        ? "door_jamb_left"
        : "door_jamb_right",
      size: [frame, height, depth],
      position: [
        side * (width * 0.5 - frame * 0.5),
        0,
        0,
      ],
      material: args.primary_material,
    });
  }
  add_box(context, {
    key: "door_header",
    name: "door_header",
    size: [width, frame, depth],
    position: [
      0,
      height * 0.5 - frame * 0.5,
      0,
    ],
    material: args.primary_material,
  });
  add_inset_panel(context, {
    key: "door_leaf",
    name: "door_leaf",
    size: [
      leaf_width,
      leaf_height,
      leaf_depth,
    ],
    position: [
      0,
      -frame * 0.25,
      depth * 0.12,
    ],
    material: args.secondary_material ??
      args.primary_material,
  });
  add_reused_part(
    context,
    "door_handle",
    {
      name: "door_handle",
      shape: "rounded_cylinder",
      size: [
        frame * 0.32,
        leaf_depth * 1.8,
        frame * 0.32,
      ],
      radius: frame * 0.16,
      height: leaf_depth * 1.8,
      bevel: frame * 0.03,
      segments: 12,
      position: [
        leaf_width * 0.34,
        -height * 0.02,
        depth * 0.36,
      ],
      rotation_euler: [90, 0, 0],
      material: args.accent_material ??
        args.primary_material,
    },
  );
  add_box(context, {
    key: "door_threshold",
    name: "door_threshold",
    size: [width, frame * 0.35, depth],
    position: [
      0,
      -height * 0.5 + frame * 0.18,
      0,
    ],
    material: args.accent_material ??
      args.primary_material,
  });
}

function build_gable_roof(context, args) {
  const [width, height, depth] = args.size;
  const thickness = clamp(
    args.thickness ?? height * 0.12,
    0.03,
    height * 0.35,
  );
  const half_run = width * 0.5;
  const target_rise = height - thickness;
  const fitted_pitch = Math.atan2(
    target_rise,
    half_run,
  ) * 180 / Math.PI;
  if (
    fitted_pitch <= 0 ||
    fitted_pitch >= 85
  )
  {
    throw new Error(
      "gable_roof size cannot produce a valid roof pitch",
    );
  }
  if (args.pitch_degrees !== undefined)
  {
    const requested_rise =
      half_run *
      Math.tan(args.pitch_degrees * Math.PI / 180);
    const tolerance = Math.max(
      0.02,
      target_rise * 0.05,
    );
    if (
      Math.abs(requested_rise - target_rise) >
      tolerance
    )
    {
      throw new Error(
        "gable_roof pitch_degrees conflicts with the requested height",
      );
    }
  }
  const pitch =
    args.pitch_degrees ?? fitted_pitch;
  const radians = pitch * Math.PI / 180;
  const slope_length = half_run / Math.cos(radians);
  const rise = half_run * Math.tan(radians);

  for (const side of [-1, 1])
  {
    add_box(context, {
      key: "gable_roof_slope",
      name: side < 0
        ? "roof_slope_left"
        : "roof_slope_right",
      size: [
        slope_length,
        thickness,
        depth,
      ],
      position: [
        side * half_run * 0.5,
        rise * 0.5,
        0,
      ],
      rotation_euler: [
        0,
        0,
        -side * pitch,
      ],
      material: args.primary_material,
    });
  }
  add_box(context, {
    key: "gable_roof_ridge",
    name: "roof_ridge_cap",
    size: [
      thickness * 1.5,
      thickness * 1.2,
      depth,
    ],
    position: [0, rise, 0],
    material: args.accent_material ??
      args.primary_material,
  });
  for (const side of [-1, 1])
  {
    add_box(context, {
      key: "gable_roof_fascia",
      name: side < 0
        ? "roof_fascia_left"
        : "roof_fascia_right",
      size: [
        thickness,
        thickness * 1.8,
        depth,
      ],
      position: [
        side * (half_run - thickness * 0.5),
        thickness * 0.25,
        0,
      ],
      material: args.accent_material ??
        args.primary_material,
    });
  }
}

function build_flat_roof(context, args) {
  const [width, height, depth] = args.size;
  const slab_height = clamp(
    height * 0.24,
    Math.min(0.04, height * 0.5),
    height * 0.5,
  );
  const parapet = clamp(
    args.thickness ?? Math.min(width, depth) * 0.025,
    0.04,
    Math.min(width, depth) * 0.12,
  );
  const parapet_height = height - slab_height;
  add_box(context, {
    key: "flat_roof_slab",
    name: "roof_slab",
    size: [width, slab_height, depth],
    position: [
      0,
      -height * 0.5 + slab_height * 0.5,
      0,
    ],
    material: args.primary_material,
  });
  for (const side of [-1, 1])
  {
    add_box(context, {
      key: "flat_roof_side_parapet",
      name: side < 0
        ? "roof_parapet_left"
        : "roof_parapet_right",
      size: [parapet, parapet_height, depth],
      position: [
        side * (width * 0.5 - parapet * 0.5),
        -height * 0.5 +
          slab_height +
          parapet_height * 0.5,
        0,
      ],
      material: args.secondary_material ??
        args.primary_material,
    });
    add_box(context, {
      key: "flat_roof_end_parapet",
      name: side < 0
        ? "roof_parapet_front"
        : "roof_parapet_back",
      size: [
        width - parapet * 2,
        parapet_height,
        parapet,
      ],
      position: [
        0,
        -height * 0.5 +
          slab_height +
          parapet_height * 0.5,
        side * (depth * 0.5 - parapet * 0.5),
      ],
      material: args.secondary_material ??
        args.primary_material,
    });
  }
}

function build_stairs(context, args) {
  const [width, height, depth] = args.size;
  const steps = clamp(args.step_count ?? 10, 2, 32);
  const riser = height / steps;
  const tread = depth / steps;
  const side_width = clamp(
    args.thickness ?? width * 0.06,
    0.025,
    width * 0.18,
  );

  for (let index = 0; index < steps; index++)
  {
    const step_height = riser * (index + 1);
    add_box(context, {
      key: `stair_step_${index}`,
      name: `stair_step_${index + 1}`,
      size: [
        width - side_width * 2,
        step_height,
        tread,
      ],
      position: [
        0,
        -height * 0.5 + step_height * 0.5,
        -depth * 0.5 + tread * (index + 0.5),
      ],
      material: args.primary_material,
      bevel: Math.min(riser, tread) * 0.08,
    });
  }
  for (const side of [-1, 1])
  {
    add_reused_part(
      context,
      "stair_stringer",
      {
        name: side < 0
          ? "stair_stringer_left"
          : "stair_stringer_right",
        shape: "wedge",
        size: [side_width, height, depth],
        position: [
          side * (width * 0.5 - side_width * 0.5),
          0,
          0,
        ],
        material: args.secondary_material ??
          args.primary_material,
      },
    );
  }
}

function build_railing(context, args) {
  const [width, height, depth] = args.size;
  const count = clamp(args.count ?? 8, 2, 32);
  const maximum_radius =
    Math.min(width, height, depth) * 0.12;
  const radius = clamp(
    args.thickness ?? Math.min(width, height) * 0.025,
    Math.min(0.01, maximum_radius * 0.5),
    maximum_radius,
  );
  const span_axis = width >= depth ? 0 : 2;
  const span = span_axis === 0 ? width : depth;
  const fixed = span_axis === 0 ? depth : width;
  const usable_span = span - radius * 2;
  const post_height = height - radius * 2;

  for (let index = 0; index < count; index++)
  {
    const coordinate =
      -usable_span * 0.5 +
      usable_span * index / (count - 1);
    const position = [0, 0, 0];
    position[span_axis] = coordinate;
    add_reused_part(
      context,
      "railing_post",
      {
        name: `railing_post_${index + 1}`,
        shape: "rounded_cylinder",
        size: [
          radius * 2,
          post_height,
          radius * 2,
        ],
        radius,
        height: post_height,
        bevel: radius * 0.15,
        segments: 12,
        position,
        material: args.primary_material,
      },
    );
  }
  for (const level of [0.55, 1])
  {
    const rail_size = span_axis === 0
      ? [span, radius * 2, Math.max(radius * 2, fixed)]
      : [Math.max(radius * 2, fixed), radius * 2, span];
    add_box(context, {
      key: `railing_rail_${span_axis}`,
      name: level === 1
        ? "railing_top_rail"
        : "railing_mid_rail",
      size: rail_size,
      position: [
        0,
        level === 1
          ? height * 0.5 - radius
          : -height * 0.5 + height * level,
        0,
      ],
      material: args.secondary_material ??
        args.primary_material,
      bevel: radius * 0.5,
    });
  }
}

function build_structural_frame(context, args) {
  const [width, height, depth] = args.size;
  const columns = clamp(args.columns ?? 4, 2, 8);
  const rows = clamp(args.rows ?? 2, 1, 5);
  const post = clamp(
    args.thickness ?? Math.min(width, depth) * 0.04,
    0.04,
    Math.min(width, depth) * 0.18,
  );

  for (const z_side of [-1, 1])
  {
    for (let column = 0; column < columns; column++)
    {
      add_box(context, {
        key: "frame_column",
        name: `frame_column_${z_side < 0 ? "front" : "back"}_${column + 1}`,
        size: [post, height, post],
        position: [
          -width * 0.5 +
            post * 0.5 +
            (width - post) *
            column /
            (columns - 1),
          0,
          z_side * (depth * 0.5 - post * 0.5),
        ],
        material: args.primary_material,
      });
    }
  }
  for (let row = 0; row <= rows; row++)
  {
    const y =
      -height * 0.5 +
      post * 0.5 +
      (height - post) * row / rows;
    for (const z_side of [-1, 1])
    {
      add_box(context, {
        key: "frame_long_beam",
        name: `frame_beam_${z_side < 0 ? "front" : "back"}_${row + 1}`,
        size: [width, post, post],
        position: [
          0,
          y,
          z_side * (depth * 0.5 - post * 0.5),
        ],
        material: args.secondary_material ??
          args.primary_material,
      });
    }
  }
  for (const x_side of [-1, 1])
  {
    add_box(context, {
      key: "frame_cross_beam",
      name: x_side < 0
        ? "frame_cross_beam_left"
        : "frame_cross_beam_right",
      size: [post, post, depth],
      position: [
        x_side * (width * 0.5 - post * 0.5),
        height * 0.5 - post * 0.5,
        0,
      ],
      material: args.secondary_material ??
        args.primary_material,
    });
  }
}

function build_panel_wall(context, args) {
  const [width, height, depth] = args.size;
  const rows = clamp(args.rows ?? 3, 1, 5);
  const columns = clamp(args.columns ?? 5, 1, 8);
  const gap = clamp(
    args.spacing ?? Math.min(width, height) * 0.012,
    0.005,
    Math.min(width / columns, height / rows) * 0.2,
  );
  const panel_width = width / columns;
  const panel_height = height / rows;

  for (let row = 0; row < rows; row++)
  {
    for (let column = 0; column < columns; column++)
    {
      add_inset_panel(context, {
        key: "wall_panel",
        name: `wall_panel_${row + 1}_${column + 1}`,
        size: [
          panel_width - gap,
          panel_height - gap,
          depth,
        ],
        position: [
          -width * 0.5 +
            panel_width * (column + 0.5),
          -height * 0.5 +
            panel_height * (row + 0.5),
          0,
        ],
        material:
          (row + column) % 3 === 0
            ? args.secondary_material ??
              args.primary_material
            : args.primary_material,
      });
    }
  }
}

function build_support_array(context, args) {
  const [width, height, depth] = args.size;
  const count = clamp(args.count ?? 6, 1, 32);
  const support_width = clamp(
    args.thickness ?? Math.min(width, depth) * 0.08,
    0.03,
    Math.max(0.03, width / Math.max(1, count) * 0.6),
  );
  const usable_width = Math.max(
    0,
    width - support_width,
  );
  const spacing = count > 1
    ? usable_width / (count - 1)
    : 0;
  for (let index = 0; index < count; index++)
  {
    const position = [
      count > 1
        ? -usable_width * 0.5 + spacing * index
        : 0,
      0,
      0,
    ];
    if (args.support_style === "round")
    {
      add_reused_part(
        context,
        "round_support",
        {
          name: `support_${index + 1}`,
          shape: "rounded_cylinder",
          size: [
            support_width,
            height,
            support_width,
          ],
          radius: support_width * 0.5,
          height,
          bevel: support_width * 0.08,
          segments: 16,
          position,
          material: args.primary_material,
        },
      );
    }
    else
    {
      add_box(context, {
        key: "square_support",
        name: `support_${index + 1}`,
        size: [
          support_width,
          height,
          depth,
        ],
        position,
        material: args.primary_material,
      });
    }
  }
}

function build_sign(context, args) {
  const [width, height, depth] = args.size;
  const post_height = height * 0.72;
  const panel_height = height * 0.42;
  const post_width = clamp(
    args.thickness ?? width * 0.04,
    0.025,
    width * 0.15,
  );

  for (const side of [-1, 1])
  {
    add_box(context, {
      key: "sign_post",
      name: side < 0
        ? "sign_post_left"
        : "sign_post_right",
      size: [post_width, post_height, post_width],
      position: [
        side * width * 0.34,
        -height * 0.5 + post_height * 0.5,
        0,
      ],
      material: args.primary_material,
    });
  }
  add_inset_panel(context, {
    key: "sign_panel",
    name: "sign_panel",
    size: [width, panel_height, depth],
    position: [
      0,
      height * 0.5 - panel_height * 0.5,
      0,
    ],
    material: args.secondary_material ??
      args.primary_material,
  });
  add_box(context, {
    key: "sign_face",
    name: "sign_face",
    size: [
      width * 0.88,
      panel_height * 0.72,
      Math.max(0.006, depth * 0.18),
    ],
    position: [
      0,
      height * 0.5 - panel_height * 0.5,
      depth * 0.5 -
        Math.max(0.006, depth * 0.18) * 0.5,
    ],
    material:
      args.emissive_material ??
      args.accent_material ??
      args.secondary_material,
    bevel: Math.max(0.001, depth * 0.02),
  });
}

function build_cable_run(context, args) {
  if (
    !Array.isArray(args.path_points) ||
    args.path_points.length < 2
  )
  {
    throw new Error(
      "cable_run requires at least two path_points",
    );
  }
  const radius = clamp(
    args.thickness ?? 0.018,
    0.003,
    0.25,
  );
  add_reused_part(
    context,
    "cable_run",
    {
      name: "cable_run",
      shape: "pipe",
      path_points: args.path_points,
      radius,
      segments: 10,
      material: args.primary_material,
    },
  );
  for (
    let index = 0;
    index < args.path_points.length;
    index++
  )
  {
    add_reused_part(
      context,
      "cable_clip",
      {
        name: `cable_clip_${index + 1}`,
        shape: "torus",
        major_radius: radius * 1.35,
        minor_radius: radius * 0.28,
        segments: 12,
        minor_segments: 6,
        position: args.path_points[index],
        rotation_euler: [90, 0, 0],
        material: args.accent_material ??
          args.primary_material,
      },
    );
  }
}

function build_facade(context, args) {
  const [width, height, depth] = args.size;
  const rows = clamp(args.rows ?? 3, 1, 5);
  const columns = clamp(args.columns ?? 6, 1, 8);
  const trim = clamp(
    args.thickness ?? Math.min(width, height) * 0.018,
    0.015,
    Math.min(width / columns, height / rows) * 0.16,
  );
  const cell_width = width / columns;
  const cell_height = height / rows;

  for (let row = 0; row < rows; row++)
  {
    for (let column = 0; column < columns; column++)
    {
      const is_solid =
        row === 0 &&
        column % 3 === 0;
      if (is_solid)
      {
        add_inset_panel(context, {
          key: "facade_solid_panel",
          name: `facade_panel_${row + 1}_${column + 1}`,
          size: [
            cell_width - trim * 1.2,
            cell_height - trim * 1.2,
            depth,
          ],
          position: [
            -width * 0.5 +
              cell_width * (column + 0.5),
            -height * 0.5 +
              cell_height * (row + 0.5),
            0,
          ],
          material: args.primary_material,
        });
      }
      else
      {
        add_box(context, {
          key: "facade_window",
          name: `facade_window_${row + 1}_${column + 1}`,
          size: [
            cell_width - trim * 1.4,
            cell_height - trim * 1.4,
            Math.max(0.008, depth * 0.22),
          ],
          position: [
            -width * 0.5 +
              cell_width * (column + 0.5),
            -height * 0.5 +
              cell_height * (row + 0.5),
            depth * 0.28,
          ],
          material:
            args.glass_material ??
            args.secondary_material,
          bevel: Math.max(0.001, depth * 0.025),
        });
      }
    }
  }
  for (let column = 0; column <= columns; column++)
  {
    add_box(context, {
      key: "facade_vertical_trim",
      name: `facade_vertical_trim_${column + 1}`,
      size: [trim, height, depth * 1.2],
      position: [
        column === 0
          ? -width * 0.5 + trim * 0.5
          : column === columns
            ? width * 0.5 - trim * 0.5
            : -width * 0.5 +
              cell_width * column,
        0,
        0,
      ],
      material: args.accent_material ??
        args.primary_material,
    });
  }
  for (let row = 0; row <= rows; row++)
  {
    add_box(context, {
      key: "facade_horizontal_trim",
      name: `facade_horizontal_trim_${row + 1}`,
      size: [width, trim, depth * 1.2],
      position: [
        0,
        row === 0
          ? -height * 0.5 + trim * 0.5
          : row === rows
            ? height * 0.5 - trim * 0.5
            : -height * 0.5 +
              cell_height * row,
        0,
      ],
      material: args.accent_material ??
        args.primary_material,
    });
  }
}

const builders = {
  window_grid: build_window_grid,
  doorway: build_doorway,
  gable_roof: build_gable_roof,
  flat_roof: build_flat_roof,
  stairs: build_stairs,
  railing: build_railing,
  structural_frame: build_structural_frame,
  panel_wall: build_panel_wall,
  support_array: build_support_array,
  sign: build_sign,
  cable_run: build_cable_run,
  facade: build_facade,
};

export const construction_grammar_names =
  Object.freeze(Object.keys(builders));

export function build_construction_grammar(args) {
  const builder = builders[args.grammar];
  if (!builder)
  {
    throw new Error(
      `unknown construction grammar ${args.grammar}`,
    );
  }
  if (
    args.grammar !== "cable_run" &&
    !positive_size(args.size)
  )
  {
    throw new Error(
      "construction grammar requires a positive size",
    );
  }

  const context = {
    parts: [],
    directory: String(
      args.asset_directory ??
      "project/generated/mcp/grammar",
    ).replace(/[\\/]+$/g, ""),
    base_name: safe_name(args.name),
  };
  builder(context, args);
  if (
    context.parts.length === 0 ||
    context.parts.length > 64
  )
  {
    throw new Error(
      `construction grammar produced ${context.parts.length} parts, expected 1 to 64`,
    );
  }
  for (
    let index = 0;
    index < context.parts.length;
    index++
  )
  {
    validate_generated_part(
      context.parts[index],
      index,
    );
  }

  return {
    parts: context.parts,
    metadata: {
      grammar: args.grammar,
      part_count: context.parts.length,
      size: args.size,
      rows: args.rows,
      columns: args.columns,
      count: args.count,
    },
  };
}
