export const advanced_scene_tool_names = [
  "mesh_generate",
  "mesh_generate_batch",
  "render_set_mesh",
  "compound_create",
  "detail_pattern_create",
  "material_apply_preset",
  "material_semantic_create",
  "material_palette_create",
  "entity_snap",
  "entity_spatial_snapshot",
  "scene_plan_create",
  "scene_plan_get",
  "scene_layout_audit",
  "scene_visual_review",
  "scene_quality_audit",
  "resource_list",
  "resource_load",
  "resource_save",
  "material_get",
  "material_create",
  "material_set_property",
  "material_set_texture",
  "prefab_save",
  "prefab_load",
  "component_set_batch",
  "entity_set_transform_batch",
  "entity_find_by_component",
  "viewport_frame",
  "screenshot_take",
];

const feature_aliases = {
  bumps: ["bump", "bumps", "rumble", "suspension", "whoop", "whoops"],
  jumps: ["jump", "jumps", "launch", "takeoff", "landing"],
  slalom_cones: ["slalom", "cone", "cones", "weave"],
  ramps: ["ramp", "ramps", "incline"],
  banked_turn: ["bank", "berm", "turn"],
  braking_lane: ["brake", "braking", "stop"],
  obstacles: ["obstacle", "barrier", "chicane"],
  lighting: ["light", "lamp", "flood"],
};

const primitive_mesh_names = new Set([
  "cube",
  "quad",
  "sphere",
  "cylinder",
  "cone",
  "capsule",
  "triangle",
]);

function normalized(value) {
  return String(value ?? "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "");
}

export function infer_required_features(prompt) {
  const value = String(prompt ?? "").toLowerCase();
  const required = [];
  const add = (feature) => {
    if (!required.includes(feature))
    {
      required.push(feature);
    }
  };

  if (/\b(bump|bumps|suspension|rumble|whoops?)\b/.test(value))
  {
    add("bumps");
  }
  if (/\b(jump|jumps|launch|takeoff|landing)\b/.test(value))
  {
    add("jumps");
  }
  if (/\b(slalom|cones?|weave)\b/.test(value))
  {
    add("slalom_cones");
  }
  if (/\b(ramp|ramps|incline)\b/.test(value))
  {
    add("ramps");
  }
  if (/\b(bank|banked|berm)\b/.test(value))
  {
    add("banked_turn");
  }
  if (/\b(brake|braking|stopping)\b/.test(value))
  {
    add("braking_lane");
  }
  if (/\b(obstacles?|barriers?|chicanes?)\b/.test(value))
  {
    add("obstacles");
  }

  return required;
}

export function scene_quality_prompt_lines(prompt, intent) {
  const required = infer_required_features(prompt);
  const lines = [
    "This is a finished scene-construction task, not a minimal greybox task.",
    "Before creating entities, call scene_plan_create with a generic semantic plan derived from the request: choose a credible scale reference, purposeful zones, descriptive elements with realistic expected dimensions, support modes, spatial relationships, and a lighting intent.",
    "The planner is environment-agnostic. Infer suitable roles and dimensions for the current request; never force car, room, city, or other domain-specific structure onto unrelated scenes.",
    "Build the scene to match the validated plan. Entity and compound names must match semantic element names so spatial evidence can resolve them.",
    "For planned elements with count greater than one, name instance roots semantic_name_1, semantic_name_2, and so on. Put detail parts below each instance root.",
    "Optimize tool calls for completeness and visual quality, not for the smallest call count.",
    "Unless the user explicitly asks for an uncolored greybox, create a coordinated material_palette_create palette and assign non-default semantic materials to nearly every visible renderable.",
    "Use mesh_generate, compound_create, and detail_pattern_create when they produce more descriptive geometry than cubes and cylinders.",
    "Give requested features explicit descriptive entity names so scene_quality_audit can verify them.",
    "Use entity_snap for floor, wall, ceiling, or surface placement where applicable.",
    "Create purposeful calibrated lighting under the scene root. Use the plan's lighting intent to choose type, placement, color, intensity, range, and shadows.",
    "Call scene_layout_audit after construction and fix implausible scale, objects outside zones, floating or deeply intersecting supports, broken relationships, weak light coverage, and uncalibrated lights.",
    "Before reporting completion, call scene_quality_audit on the requested root and fix every failed requirement.",
    "Then call scene_visual_review, inspect the returned image, and perform at least one targeted correction pass for composition, scale, intersections, materials, lighting, or geometric repetition.",
    "Run scene_quality_audit again after corrections. Do not report completion while pass is false.",
    "Default materials, missing requested features, one-shape repetition, floating geometry, and an unreviewed screenshot are incomplete work.",
  ];

  if (required.length > 0)
  {
    lines.push(
      `Required feature audit tokens inferred from the request: ${required.join(", ")}.`,
    );
  }
  if (intent?.target_name)
  {
    lines.push(
      `The quality audit root is ${intent.target_name}. Keep all created content under that root.`,
    );
  }
  return lines;
}

async function list_all_entities(send_command) {
  const entities = [];
  let offset = 0;
  while (true)
  {
    const page = await send_command(
      "entity_list",
      {
        offset,
        limit: 1000,
      },
    );
    if (!page.ok)
    {
      return page;
    }
    const page_entities = page.entities ?? [];
    const page_count = page.count ?? page_entities.length;
    entities.push(...page_entities);
    offset += page_count;
    if (!page.truncated || page_count === 0)
    {
      return {
        ok: true,
        entities,
      };
    }
  }
}

function hierarchy_entities(all_entities, root_id) {
  const hierarchy_ids = new Set([String(root_id)]);
  let changed = true;
  while (changed)
  {
    changed = false;
    for (const entity of all_entities)
    {
      if (
        entity.parent_id &&
        hierarchy_ids.has(String(entity.parent_id)) &&
        !hierarchy_ids.has(String(entity.id))
      )
      {
        hierarchy_ids.add(String(entity.id));
        changed = true;
      }
    }
  }

  return all_entities.filter((entity) =>
    hierarchy_ids.has(String(entity.id)),
  );
}

function feature_match(feature, names) {
  const aliases = feature_aliases[feature] ?? [normalized(feature)];
  return names.filter((name) => {
    const tokens = new Set(name.split("_").filter(Boolean));
    return aliases.some((alias) =>
      tokens.has(normalized(alias)),
    );
  });
}

export async function audit_scene_quality(
  send_command,
  {
    id,
    required_features = [],
    min_entities = 12,
    max_default_material_ratio = 0.2,
    min_unique_materials = 3,
    min_advanced_mesh_ratio = 0.12,
    require_light = true,
  },
) {
  const hierarchy = await send_command("entity_get", { id });
  if (!hierarchy.ok || !hierarchy.entity)
  {
    return {
      ok: false,
      pass: false,
      error: hierarchy.error ?? "quality root was not found",
    };
  }

  const world_entities = await list_all_entities(send_command);
  if (!world_entities.ok)
  {
    return {
      ok: false,
      pass: false,
      error: world_entities.error ?? "entity hierarchy audit failed",
    };
  }

  const material_snapshot = await send_command(
    "entity_render_materials",
    {
      id,
      include_descendants: true,
    },
  );
  if (!material_snapshot.ok)
  {
    return {
      ok: false,
      pass: false,
      error: material_snapshot.error ??
        "material audit failed",
    };
  }

  const entities = hierarchy_entities(
    world_entities.entities,
    id,
  );
  const descendants = entities.filter(
    (entity) => String(entity.id) !== String(id),
  );
  const names = descendants.map((entity) =>
    normalized(entity.name),
  );
  const materials = Array.isArray(material_snapshot.materials)
    ? material_snapshot.materials
    : [];
  const default_material_count = materials.filter(
    (entry) => entry.default_material,
  ).length;
  const default_material_ratio = materials.length > 0
    ? default_material_count / materials.length
    : 1;
  const unique_materials = new Set(
    materials
      .filter((entry) => !entry.default_material)
      .map((entry) => entry.material)
      .filter(Boolean),
  );
  const advanced_mesh_count = materials.filter((entry) => {
    const mesh_name = normalized(entry.mesh);
    return (
      mesh_name &&
      !mesh_name.startsWith("standard_") &&
      !primitive_mesh_names.has(mesh_name)
    );
  }).length;
  const advanced_mesh_ratio = materials.length > 0
    ? advanced_mesh_count / materials.length
    : 0;
  const light_count = entities.filter((entity) =>
    Array.isArray(entity.components) &&
    entity.components.some(
      (component) => normalized(component) === "light",
    ),
  ).length;

  const feature_results = required_features.map((feature) => {
    const matches = feature_match(normalized(feature), names);
    return {
      feature: normalized(feature),
      pass: matches.length > 0,
      matches: matches.slice(0, 12),
    };
  });

  const checks = [
    {
      name: "entity_count",
      pass: descendants.length >= min_entities,
      actual: descendants.length,
      required: min_entities,
    },
    {
      name: "renderable_detail_count",
      pass: materials.length >= Math.ceil(min_entities * 0.6),
      actual: materials.length,
      required: Math.ceil(min_entities * 0.6),
    },
    {
      name: "default_material_ratio",
      pass:
        materials.length > 0 &&
        default_material_ratio <= max_default_material_ratio,
      actual: default_material_ratio,
      required_max: max_default_material_ratio,
    },
    {
      name: "unique_materials",
      pass: unique_materials.size >= min_unique_materials,
      actual: unique_materials.size,
      required: min_unique_materials,
    },
    {
      name: "advanced_mesh_ratio",
      pass: advanced_mesh_ratio >= min_advanced_mesh_ratio,
      actual: advanced_mesh_ratio,
      required: min_advanced_mesh_ratio,
    },
    {
      name: "lighting",
      pass: !require_light || light_count > 0,
      actual: light_count,
      required: require_light ? 1 : 0,
    },
    ...feature_results.map((entry) => ({
      name: `feature_${entry.feature}`,
      pass: entry.pass,
      matches: entry.matches,
      required: 1,
    })),
  ];
  const failed_checks = checks.filter((check) => !check.pass);
  const score = checks.length > 0
    ? Math.round(
        (
          checks.length -
          failed_checks.length
        ) /
        checks.length *
        100
      )
    : 0;

  return {
    ok: true,
    pass: failed_checks.length === 0,
    score,
    root: {
      id: hierarchy.entity.id,
      name: hierarchy.entity.name,
    },
    metrics: {
      entity_count: descendants.length,
      renderable_count: materials.length,
      default_material_count,
      default_material_ratio,
      unique_materials: [...unique_materials],
      advanced_mesh_count,
      advanced_mesh_ratio,
      light_count,
    },
    feature_results,
    checks,
    failed_checks,
    recommendations: failed_checks.map((check) => {
      if (check.name === "default_material_ratio")
      {
        return "create a semantic palette and replace default materials";
      }
      if (check.name === "unique_materials")
      {
        return "use at least three coordinated semantic materials";
      }
      if (check.name === "advanced_mesh_ratio")
      {
        return "replace repeated primitives with generated or compound geometry";
      }
      if (check.name === "lighting")
      {
        return "add at least one calibrated light under the root";
      }
      if (check.name.startsWith("feature_"))
      {
        return `add and descriptively name ${check.name.slice(8)}`;
      }
      return "add more purposeful scene detail";
    }),
  };
}
