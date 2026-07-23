export const advanced_scene_tool_names = [
  "mesh_generate",
  "mesh_generate_batch",
  "mesh_geometry_capabilities",
  "mesh_physics_bind",
  "render_set_mesh",
  "compound_create",
  "construction_grammar_suggest",
  "construction_grammar_create",
  "detail_pattern_create",
  "material_apply_preset",
  "material_semantic_create",
  "material_palette_create",
  "entity_snap",
  "entity_spatial_snapshot",
  "scene_plan_suggest",
  "design_brief_create",
  "scene_layout_audit",
  "scene_visual_review",
  "scene_quality_audit",
  "scene_benchmark_list",
  "scene_benchmark_get",
  "scene_benchmark_capture",
  "scene_benchmark_score",
  "scene_benchmark_baseline_save",
  "scene_benchmark_baseline_get",
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
  "entity_create_light_batch",
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

const quality_profiles = {
  generic: {
    min_entities: 12,
    min_unique_materials: 3,
    min_advanced_mesh_ratio: 0.12,
  },
  room: {
    min_entities: 10,
    min_unique_materials: 3,
    min_advanced_mesh_ratio: 0.15,
  },
  storefront: {
    min_entities: 18,
    min_unique_materials: 4,
    min_advanced_mesh_ratio: 0.18,
  },
  gas_station: {
    min_entities: 24,
    min_unique_materials: 5,
    min_advanced_mesh_ratio: 0.2,
  },
  airport: {
    min_entities: 32,
    min_unique_materials: 6,
    min_advanced_mesh_ratio: 0.16,
  },
  warehouse: {
    min_entities: 18,
    min_unique_materials: 4,
    min_advanced_mesh_ratio: 0.14,
  },
  road: {
    min_entities: 14,
    min_unique_materials: 3,
    min_advanced_mesh_ratio: 0.08,
  },
};

function normalized(value) {
  return String(value ?? "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "");
}

function rounded(value, precision = 10) {
  return Math.round(Number(value ?? 0) * precision) /
    precision;
}

function geometry_signature(entity) {
  const box = entity.bounding_box;
  if (!box?.min || !box?.max)
  {
    return "";
  }
  return [
    ...(box.min ?? []),
    ...(box.max ?? []),
  ].map((value) => rounded(value)).join(":");
}

function duplicate_geometry(entities) {
  const groups = new Map();
  for (const entity of entities)
  {
    if (!entity.has_render_bounds)
    {
      continue;
    }
    const signature = geometry_signature(entity);
    if (!signature)
    {
      continue;
    }
    const group = groups.get(signature) ?? [];
    group.push(entity);
    groups.set(signature, group);
  }
  const duplicates = [];
  for (const group of groups.values())
  {
    if (group.length < 2)
    {
      continue;
    }
    for (let index = 1; index < group.length; index++)
    {
      duplicates.push({
        entity: {
          id: group[index].id,
          name: group[index].name,
        },
        overlaps: {
          id: group[0].id,
          name: group[0].name,
        },
        bounds: group[index].bounding_box,
      });
    }
  }
  return duplicates;
}

function tag_value(entity, key) {
  const prefix = `${key}=`;
  const tags = Array.isArray(entity?.tags)
    ? entity.tags
    : [];
  return String(
    tags.find((tag) =>
      String(tag).startsWith(prefix),
    ) ?? "",
  ).slice(prefix.length);
}

function box_volume(box) {
  const size = box?.size;
  if (
    !Array.isArray(size) ||
    size.length !== 3
  )
  {
    return 0;
  }
  return Math.max(0, size[0]) *
    Math.max(0, size[1]) *
    Math.max(0, size[2]);
}

function repetition_metrics(spatial_entities, materials) {
  const material_by_id = new Map(
    materials.map((entry) => [
      String(entry.id),
      normalized(entry.mesh),
    ]),
  );
  const groups = new Map();
  let renderable_count = 0;
  for (const entity of spatial_entities)
  {
    if (!entity.bounding_box)
    {
      continue;
    }
    renderable_count++;
    const size = entity.bounding_box.size ?? [];
    const signature = [
      material_by_id.get(String(entity.id)) ?? "",
      ...size.map((value) => rounded(value)),
    ].join(":");
    groups.set(
      signature,
      (groups.get(signature) ?? 0) + 1,
    );
  }
  const largest_group = Math.max(
    0,
    ...groups.values(),
  );
  return {
    repeated_group_count: groups.size,
    largest_repeated_group: largest_group,
    repetition_ratio: renderable_count > 0
      ? largest_group / renderable_count
      : 0,
  };
}

function resolved_quality_options(options) {
  const profile =
    quality_profiles[normalized(options.scene_type)] ??
    quality_profiles.generic;
  const planned_min = Number.isInteger(
    options.planned_element_count,
  )
    ? Math.ceil(options.planned_element_count * 1.5)
    : 0;
  return {
    required_features: [],
    required_roles: [],
    max_default_material_ratio: 0.2,
    require_light: true,
    max_duplicate_geometry: 0,
    min_collision_ratio: 1,
    max_repetition_ratio: 0.4,
    max_dominant_geometry_ratio: 0.8,
    ...profile,
    ...options,
    min_entities:
      options.min_entities ??
      Math.max(profile.min_entities, planned_min),
  };
}

async function list_spatial_entities(send_command, id) {
  const entities = [];
  const seen_ids = new Set();
  const page_size = 5000;
  let offset = 0;
  while (offset < 100000)
  {
    const page = await send_command(
      "entity_spatial_snapshot",
      {
        id,
        include_descendants: true,
        limit: page_size,
        offset,
      },
    );
    if (!page.ok)
    {
      return page;
    }
    const page_entities = page.entities ?? [];
    let added = 0;
    for (const entity of page_entities)
    {
      const entity_id = String(entity.id);
      if (seen_ids.has(entity_id))
      {
        continue;
      }
      seen_ids.add(entity_id);
      entities.push(entity);
      added++;
    }
    if (!page.truncated)
    {
      return {
        ...page,
        entities,
        count: entities.length,
        truncated: false,
      };
    }
    if (page_entities.length === 0 || added === 0)
    {
      return {
        ok: false,
        entities,
        error: "spatial snapshot pagination made no progress",
      };
    }
    offset += page_entities.length;
  }
  return {
    ok: false,
    entities,
    truncated: true,
    error: "scene exceeds the 100000 entity quality budget",
  };
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
    "Treat the user's request as a creative brief. Infer the ordinary supporting architecture, materials, props, environmental context, circulation, and detail needed for the requested place to feel complete; the user does not need to enumerate standard quality requirements.",
    "Scale the work into bounded internal passes by zone or discipline, preserving good existing work during refinement. Finish each pass coherently instead of scattering shallow edits across the whole scene.",
    "Use context-sensitive design judgment rather than a fixed prop checklist. Details must explain how the place is built, used, accessed, maintained, and lit.",
    "Before creating entities, use the fresh prepared design context when present. Otherwise call scene_plan_suggest from the current request. Never search for or reuse a stored design.",
    "Complete layout and circulation before structure, complete structure before functional objects, and complete functional objects before decoration. Do not hide an incoherent layout under detail.",
    "As soon as the quality root exists or is resolved, call viewport_frame on its id with the perspective view so the editor camera follows the build location. Frame it again after major layout changes.",
    "The planner is environment-agnostic. Infer suitable roles and dimensions for the current request; never force car, room, city, or other domain-specific structure onto unrelated scenes.",
    "Build the scene to match the validated plan. Entity and compound names must match semantic element names so spatial evidence can resolve them.",
    "Attach semantic tags such as entrance, support, walkable, focal_point, and service_route to entities where the role applies.",
    "For planned elements with count greater than one, name instance roots semantic_name_1, semantic_name_2, and so on. Put detail parts below each instance root.",
    "Optimize tool calls for completeness and visual quality, not for the smallest call count.",
    "Unless the user explicitly asks for an uncolored greybox, create a coordinated material_palette_create palette and assign non-default semantic materials to nearly every visible renderable.",
    "Call construction_grammar_suggest for each major architectural purpose, then use construction_grammar_create where a ranked assembly fits. Prefer layered assemblies over hand-authored primitive repetition.",
    "Read spartan://engine/construction-grammars when choosing grammar dimensions, material roles, or combinations.",
    "Use mesh_generate, compound_create, and detail_pattern_create for custom silhouettes and smaller details that construction grammars do not cover.",
    "Repeated objects may share a mesh, but vary their grouping, orientation, state, or restrained material accents where repetition would otherwise look stamped.",
    "Give requested features explicit descriptive entity names so scene_quality_audit can verify them.",
    "Use entity_snap for floor, wall, ceiling, or surface placement where applicable.",
    "Create purposeful calibrated lighting under the scene root. Use the plan's lighting intent to choose type, placement, color, intensity, range, and shadows.",
    "Call scene_layout_audit after construction and fix implausible scale, objects outside zones, floating or deeply intersecting supports, broken relationships, weak light coverage, and uncalibrated lights.",
    "Before reporting completion, call scene_quality_audit on the requested root and fix every failed requirement.",
    "Then call scene_visual_review with perspective and top views, inspect both returned images, and perform at least one targeted correction pass for composition, scale, intersections, materials, lighting, or geometric repetition. Add a front or side view when vertical alignment or facade readability is uncertain.",
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
  else if (intent?.use_selected)
  {
    lines.push(
      "Treat the selected entity as the quality root. Preserve its good existing content and keep refinements inside that hierarchy.",
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
  options = {},
) {
  const {
    id,
    required_features,
    required_roles,
    min_entities,
    max_default_material_ratio,
    min_unique_materials,
    min_advanced_mesh_ratio,
    require_light,
    max_duplicate_geometry,
    min_collision_ratio,
    max_repetition_ratio,
    max_dominant_geometry_ratio,
  } = resolved_quality_options(options);
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
  const spatial_snapshot = await list_spatial_entities(
    send_command,
    id,
  );

  const entities = hierarchy_entities(
    world_entities.entities,
    id,
  );
  const descendants = entities.filter(
    (entity) => String(entity.id) !== String(id),
  );
  const names = entities.map((entity) =>
    normalized(entity.name),
  );
  const tags = new Set(
    entities.flatMap((entity) =>
      Array.isArray(entity.tags)
        ? entity.tags.map(normalized)
        : [],
    ),
  );
  const materials = Array.isArray(material_snapshot.materials)
    ? material_snapshot.materials
    : [];
  names.push(
    ...materials.map((entry) =>
      normalized(entry.name),
    ),
  );
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
  const entity_by_id = new Map(
    entities.map((entity) => [
      String(entity.id),
      entity,
    ]),
  );
  const collision_missing = materials.filter((entry) => {
    const entity = entity_by_id.get(String(entry.id));
    return !entity?.components?.some(
      (component) => normalized(component) === "physics",
    );
  });
  const collision_count =
    materials.length - collision_missing.length;
  const collision_ratio = materials.length > 0
    ? collision_count / materials.length
    : 0;

  const feature_results = required_features.map((feature) => {
    const matches = feature_match(normalized(feature), names);
    if (tags.has(normalized(feature)))
    {
      matches.push(`tag:${normalized(feature)}`);
    }
    return {
      feature: normalized(feature),
      pass: matches.length > 0,
      matches: matches.slice(0, 12),
    };
  });
  const role_results = required_roles.map((role) => ({
    role: normalized(role),
    pass: tags.has(normalized(role)),
  }));
  const duplicates = spatial_snapshot.ok
    ? duplicate_geometry(spatial_snapshot.entities ?? [])
    : [];
  const spatial_entities =
    spatial_snapshot.entities ?? [];
  const spatial_by_id = new Map(
    spatial_entities.map((entity) => [
      String(entity.id),
      entity,
    ]),
  );
  const repetition = repetition_metrics(
    spatial_entities,
    materials,
  );
  const scene_bounds =
    spatial_entities[0]?.subtree_bounding_box;
  const scene_volume = box_volume(scene_bounds);
  const largest_entity_volume = Math.max(
    0,
    ...spatial_entities.map((entity) =>
      box_volume(entity.bounding_box),
    ),
  );
  const dominant_geometry_ratio = scene_volume > 0
    ? largest_entity_volume / scene_volume
    : 0;

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
    {
      name: "collision_coverage",
      pass:
        materials.length > 0 &&
        collision_ratio >= min_collision_ratio,
      actual: collision_ratio,
      required: min_collision_ratio,
      missing: collision_missing.slice(0, 64).map(
        (entry) => ({
          id: entry.id,
          name: entry.name,
          mesh: entry.mesh,
        }),
      ),
    },
    {
      name: "duplicate_geometry",
      pass:
        spatial_snapshot.ok &&
        !spatial_snapshot.truncated &&
        duplicates.length <= max_duplicate_geometry,
      actual: duplicates.length,
      required_max: max_duplicate_geometry,
    },
    {
      name: "repetition_ratio",
      pass:
        materials.length < 8 ||
        repetition.repetition_ratio <=
          max_repetition_ratio,
      actual: repetition.repetition_ratio,
      required_max: max_repetition_ratio,
      largest_group:
        repetition.largest_repeated_group,
    },
    {
      name: "dominant_geometry_ratio",
      pass:
        scene_volume > 0 &&
        dominant_geometry_ratio <=
          max_dominant_geometry_ratio,
      actual: dominant_geometry_ratio,
      required_max: max_dominant_geometry_ratio,
    },
    ...feature_results.map((entry) => ({
      name: `feature_${entry.feature}`,
      pass: entry.pass,
      matches: entry.matches,
      required: 1,
    })),
    ...role_results.map((entry) => ({
      name: `role_${entry.role}`,
      pass: entry.pass,
      required: 1,
    })),
  ];
  const failed_checks = checks.filter((check) => !check.pass);
  const default_material_semantic_ids = materials
    .filter((entry) => entry.default_material)
    .map((entry) =>
      tag_value(
        spatial_by_id.get(String(entry.id)),
        "semantic_id",
      ),
    )
    .filter(Boolean);
  const collision_semantic_ids = collision_missing
    .map((entry) =>
      tag_value(
        spatial_by_id.get(String(entry.id)),
        "semantic_id",
      ),
    )
    .filter(Boolean);
  const issues = failed_checks.flatMap((check) => {
    if (
      check.name === "duplicate_geometry" &&
      duplicates.length > 0
    )
    {
      return duplicates.map((duplicate) => ({
        severity: "error",
        code: "duplicate_geometry",
        message: "near identical generated geometry overlaps",
        semantic_ids: [
          tag_value(
            spatial_by_id.get(
              String(duplicate.entity.id),
            ),
            "semantic_id",
          ),
          tag_value(
            spatial_by_id.get(
              String(duplicate.overlaps.id),
            ),
            "semantic_id",
          ),
        ].filter(Boolean),
        evidence: duplicate,
      }));
    }
    return [{
      severity: "error",
      code: check.name,
      message: `quality check ${check.name} failed`,
      semantic_ids:
        check.name === "default_material_ratio"
          ? default_material_semantic_ids
          : check.name === "collision_coverage"
            ? collision_semantic_ids
            : [],
      evidence: check,
    }];
  });
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
      collision_count,
      collision_missing_count: collision_missing.length,
      collision_ratio,
      duplicate_geometry_count: duplicates.length,
      duplicate_geometry: duplicates.slice(0, 32),
      semantic_tags: [...tags],
      spatial_snapshot_complete:
        spatial_snapshot.ok &&
        !spatial_snapshot.truncated,
      repetition,
      scene_bounds,
      dominant_geometry_ratio,
    },
    feature_results,
    role_results,
    checks,
    failed_checks,
    issues,
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
      if (check.name === "duplicate_geometry")
      {
        return "remove duplicate shells and overlapping generated siblings";
      }
      if (check.name === "repetition_ratio")
      {
        return "vary repeated silhouettes, grouped details, orientation, or restrained material accents";
      }
      if (check.name === "dominant_geometry_ratio")
      {
        return "break the scene into purposeful structural and functional forms instead of one dominant shell";
      }
      if (check.name === "collision_coverage")
      {
        return "add a static convex compatible physics collider to every renderable";
      }
      if (check.name.startsWith("role_"))
      {
        return `tag geometry with semantic role ${check.name.slice(5)}`;
      }
      if (check.name.startsWith("feature_"))
      {
        return `add and descriptively name ${check.name.slice(8)}`;
      }
      return "add more purposeful scene detail";
    }),
  };
}
