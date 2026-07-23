import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { createHash } from "node:crypto";

const supported_roles = new Set([
  "surface",
  "structure",
  "route",
  "functional",
  "furnishing",
  "prop",
  "detail",
  "light",
]);

const supported_support_modes = new Set([
  "ground",
  "surface",
  "wall",
  "ceiling",
  "suspended",
  "none",
]);

const supported_relations = new Set([
  "on",
  "inside",
  "connected_to",
  "separated_from",
  "aligned_with",
  "beside",
]);
const scene_plan_directories = new Map();

function normalized(value) {
  return String(value ?? "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "");
}

function safe_namespace(value) {
  return normalized(value).slice(0, 80) || "default";
}

export function make_scene_plan_namespace({
  project_root,
  engine_host,
  engine_port,
  world,
}) {
  const world_path =
    world?.file_path ??
    world?.path ??
    "";
  const identity = JSON.stringify({
    project_root,
    engine_host,
    engine_port,
    world_path,
    world_name: world?.name ?? "",
  });
  const hash = createHash("sha256")
    .update(identity)
    .digest("hex")
    .slice(0, 20);
  const namespace =
    `${safe_namespace(path.basename(project_root))}_${hash}`;
  const storage_root = world_path
    ? path.dirname(path.resolve(world_path))
    : path.resolve(project_root);
  scene_plan_directories.set(
    namespace,
    path.join(
      storage_root,
      ".spartan",
      "scene_plans",
      namespace,
    ),
  );
  return namespace;
}

function plan_path(namespace, root_name) {
  const persistent_directory =
    scene_plan_directories.get(namespace);
  return path.join(
    persistent_directory ??
      path.join(
        os.tmpdir(),
        "spartan_engine_scene_plans",
        safe_namespace(namespace),
      ),
    `${safe_namespace(root_name)}.json`,
  );
}

function valid_vector3(value, positive = false) {
  return (
    Array.isArray(value) &&
    value.length === 3 &&
    value.every((entry) =>
      Number.isFinite(entry) &&
      (!positive || entry > 0),
    )
  );
}

function add_issue(issues, severity, code, message, target) {
  issues.push({
    severity,
    code,
    message,
    target,
  });
}

export async function create_scene_plan(plan, namespace) {
  const issues = [];
  const root_name = normalized(plan.root_name);
  if (!root_name)
  {
    add_issue(
      issues,
      "error",
      "missing_root",
      "root_name is required",
    );
  }
  if (!String(plan.purpose ?? "").trim())
  {
    add_issue(
      issues,
      "error",
      "missing_purpose",
      "purpose is required",
    );
  }

  const reference = plan.scale_reference;
  if (
    !reference?.name ||
    !valid_vector3(reference.size, true)
  )
  {
    add_issue(
      issues,
      "error",
      "invalid_scale_reference",
      "scale_reference requires a name and positive size",
    );
  }

  const zones = Array.isArray(plan.zones)
    ? plan.zones
    : [];
  if (zones.length === 0)
  {
    add_issue(
      issues,
      "error",
      "missing_zones",
      "at least one purposeful zone is required",
    );
  }
  const zone_names = new Set();
  for (const zone of zones)
  {
    const name = normalized(zone.name);
    if (
      !name ||
      zone_names.has(name) ||
      !valid_vector3(zone.center) ||
      !valid_vector3(zone.size, true)
    )
    {
      add_issue(
        issues,
        "error",
        "invalid_zone",
        "zones require unique names, centers, and positive sizes",
        zone.name,
      );
      continue;
    }
    zone_names.add(name);
  }

  const elements = Array.isArray(plan.elements)
    ? plan.elements
    : [];
  if (elements.length < 5)
  {
    add_issue(
      issues,
      "error",
      "insufficient_elements",
      "a finished environment plan requires at least five semantic elements",
    );
  }
  const element_names = new Set();
  for (const element of elements)
  {
    const name = normalized(element.name);
    if (!name || element_names.has(name))
    {
      add_issue(
        issues,
        "error",
        "invalid_element_name",
        "elements require unique descriptive names",
        element.name,
      );
    }
    element_names.add(name);

    if (!supported_roles.has(element.role))
    {
      add_issue(
        issues,
        "error",
        "invalid_role",
        `unsupported semantic role ${element.role}`,
        element.name,
      );
    }
    if (
      element.role !== "light" &&
      !valid_vector3(element.expected_size, true)
    )
    {
      add_issue(
        issues,
        "error",
        "invalid_expected_size",
        "non-light elements require positive expected_size",
        element.name,
      );
    }
    if (
      element.zone &&
      !zone_names.has(normalized(element.zone))
    )
    {
      add_issue(
        issues,
        "error",
        "unknown_zone",
        `unknown zone ${element.zone}`,
        element.name,
      );
    }
    if (!supported_support_modes.has(element.support))
    {
      add_issue(
        issues,
        "error",
        "invalid_support",
        `unsupported support mode ${element.support}`,
        element.name,
      );
    }
  }

  const relationships = Array.isArray(plan.relationships)
    ? plan.relationships
    : [];
  if (relationships.length < 2)
  {
    add_issue(
      issues,
      "error",
      "insufficient_relationships",
      "a finished environment plan requires at least two meaningful spatial relationships",
    );
  }
  for (const relationship of relationships)
  {
    if (
      !element_names.has(normalized(relationship.subject)) ||
      !element_names.has(normalized(relationship.object)) ||
      !supported_relations.has(relationship.relation) ||
      normalized(relationship.subject) ===
        normalized(relationship.object)
    )
    {
      add_issue(
        issues,
        "error",
        "invalid_relationship",
        "relationships require known elements and a supported relation",
        relationship.subject,
      );
    }
  }
  for (const element of elements)
  {
    if (
      element.support === "suspended" &&
      !relationships.some(
        (relationship) =>
          normalized(relationship.subject) ===
            normalized(element.name) &&
          relationship.relation === "connected_to",
      )
    )
    {
      add_issue(
        issues,
        "error",
        "missing_suspension_relationship",
        "suspended elements require an explicit support relationship",
        element.name,
      );
    }
  }

  const lighting = plan.lighting ?? {};
  if (
    !Number.isInteger(lighting.min_lights) ||
    lighting.min_lights < 1
  )
  {
    add_issue(
      issues,
      "error",
      "missing_lighting_plan",
      "lighting.min_lights must be at least one",
    );
  }
  if (
    Number.isInteger(lighting.max_lights) &&
    Number.isInteger(lighting.min_lights) &&
    lighting.max_lights < lighting.min_lights
  )
  {
    add_issue(
      issues,
      "error",
      "invalid_lighting_range",
      "lighting.max_lights cannot be less than min_lights",
    );
  }
  if (!String(lighting.intent ?? "").trim())
  {
    add_issue(
      issues,
      "error",
      "missing_lighting_intent",
      "lighting intent is required",
    );
  }
  const planned_light_count = elements
    .filter((element) => element.role === "light")
    .reduce(
      (total, element) =>
        total + (element.count ?? 1),
      0,
    );
  if (
    Number.isInteger(lighting.min_lights) &&
    planned_light_count < lighting.min_lights
  )
  {
    add_issue(
      issues,
      "error",
      "insufficient_planned_lights",
      `planned ${planned_light_count} lights, expected at least ${lighting.min_lights}`,
    );
  }

  const error_count = issues.filter(
    (issue) => issue.severity === "error",
  ).length;
  const normalized_plan = {
    ...plan,
    root_name,
    zones: zones.map((zone) => ({
      ...zone,
      name: normalized(zone.name),
    })),
    elements: elements.map((element) => ({
      size_tolerance: 0.4,
      size_mode:
        (element.count ?? 1) > 1
          ? "individual"
          : "aggregate",
      allow_axis_permutation: false,
      max_support_gap: 0.08,
      max_intersection_depth: 0.05,
      count: 1,
      clearance: 0,
      ...element,
      name: normalized(element.name),
      zone: normalized(element.zone),
      semantic_tags: [
        ...new Set(
          (element.semantic_tags ?? [])
            .map(normalized)
            .filter(Boolean),
        ),
      ],
    })),
  };

  const target_path = plan_path(
    namespace,
    root_name || "invalid_plan",
  );
  if (error_count === 0)
  {
    const now = new Date().toISOString();
    let created_at = now;
    try
    {
      const previous = JSON.parse(
        await fs.readFile(target_path, "utf8"),
      );
      created_at = previous.created_at ?? now;
    }
    catch
    {
      created_at = now;
    }
    await fs.mkdir(path.dirname(target_path), {
      recursive: true,
    });
    await fs.writeFile(
      target_path,
      JSON.stringify(
        {
          version: 2,
          created_at,
          updated_at: now,
          plan: normalized_plan,
        },
        null,
        2,
      ),
      "utf8",
    );
  }
  else
  {
    await fs.rm(target_path, {
      force: true,
    });
  }

  return {
    ok: true,
    pass: error_count === 0,
    error_count,
    issues,
    plan: normalized_plan,
  };
}

export async function read_scene_plan(
  namespace,
  root_name,
) {
  try
  {
    const value = JSON.parse(
      await fs.readFile(
        plan_path(namespace, root_name),
        "utf8",
      ),
    );
    return {
      ok: true,
      ...value,
    };
  }
  catch
  {
    return {
      ok: false,
      error: `scene plan not found for ${root_name}`,
    };
  }
}

function box_for(entity) {
  return (
    entity?.subtree_bounding_box ??
    entity?.bounding_box ??
    null
  );
}

function sorted_size(box) {
  return [...(box?.size ?? [])].sort(
    (left, right) => left - right,
  );
}

function box_distance(left, right) {
  if (!left || !right)
  {
    return Number.POSITIVE_INFINITY;
  }
  let squared = 0;
  for (let axis = 0; axis < 3; axis++)
  {
    const gap = Math.max(
      0,
      left.min[axis] - right.max[axis],
      right.min[axis] - left.max[axis],
    );
    squared += gap * gap;
  }
  return Math.sqrt(squared);
}

function horizontal_overlap(left, right) {
  return (
    left.min[0] <= right.max[0] &&
    left.max[0] >= right.min[0] &&
    left.min[2] <= right.max[2] &&
    left.max[2] >= right.min[2]
  );
}

function interval_overlap(
  left_min,
  left_max,
  right_min,
  right_max,
) {
  return (
    left_min <= right_max &&
    left_max >= right_min
  );
}

function has_geometric_support(
  entity,
  mode,
  entities,
  excluded_ids,
  maximum_gap,
  maximum_depth,
) {
  const own = entity.bounding_box;
  if (!own)
  {
    return false;
  }
  return entities.some((candidate) => {
    if (
      excluded_ids.has(String(candidate.id)) ||
      !candidate.bounding_box
    )
    {
      return false;
    }
    const support = candidate.bounding_box;
    if (mode === "ground" || mode === "surface")
    {
      const gap = own.min[1] - support.max[1];
      return (
        gap <= maximum_gap &&
        gap >= -maximum_depth &&
        horizontal_overlap(own, support)
      );
    }
    if (mode === "ceiling")
    {
      const gap = support.min[1] - own.max[1];
      return (
        gap <= maximum_gap &&
        gap >= -maximum_depth &&
        horizontal_overlap(own, support)
      );
    }
    if (mode === "wall")
    {
      const vertical_overlap = interval_overlap(
        own.min[1],
        own.max[1],
        support.min[1],
        support.max[1],
      );
      const x_gap = Math.max(
        support.min[0] - own.max[0],
        own.min[0] - support.max[0],
      );
      const z_gap = Math.max(
        support.min[2] - own.max[2],
        own.min[2] - support.max[2],
      );
      const overlaps_x = interval_overlap(
        own.min[0],
        own.max[0],
        support.min[0],
        support.max[0],
      );
      const overlaps_z = interval_overlap(
        own.min[2],
        own.max[2],
        support.min[2],
        support.max[2],
      );
      return (
        vertical_overlap &&
        (
          (
            overlaps_z &&
            x_gap <= maximum_gap &&
            x_gap >= -maximum_depth
          ) ||
          (
            overlaps_x &&
            z_gap <= maximum_gap &&
            z_gap >= -maximum_depth
          )
        )
      );
    }
    return false;
  });
}

function contains_box(outer, inner, tolerance) {
  return [0, 1, 2].every((axis) =>
    inner.min[axis] >= outer.min[axis] - tolerance &&
    inner.max[axis] <= outer.max[axis] + tolerance,
  );
}

function contains_point(box, point, tolerance) {
  return [0, 1, 2].every((axis) =>
    point[axis] >= box.min[axis] - tolerance &&
    point[axis] <= box.max[axis] + tolerance,
  );
}

function zone_box(zone) {
  const half = zone.size.map((value) => value * 0.5);
  return {
    min: zone.center.map(
      (value, axis) => value - half[axis],
    ),
    max: zone.center.map(
      (value, axis) => value + half[axis],
    ),
    center: zone.center,
    size: zone.size,
  };
}

function hierarchy_ids(entities, root_id) {
  const ids = new Set([String(root_id)]);
  let changed = true;
  while (changed)
  {
    changed = false;
    for (const entity of entities)
    {
      if (
        entity.parent_id &&
        ids.has(String(entity.parent_id)) &&
        !ids.has(String(entity.id))
      )
      {
        ids.add(String(entity.id));
        changed = true;
      }
    }
  }
  return ids;
}

function matching_entities(
  element,
  entities,
  all_elements,
) {
  const name = normalized(element.name);
  const semantic_names = all_elements.map((entry) =>
    normalized(entry.name),
  );
  return entities.filter((entity) => {
    const tags = Array.isArray(entity.tags)
      ? entity.tags.map(normalized)
      : String(entity.tags ?? "")
        .split(/[;,]/)
        .map(normalized);
    if (tags.includes(`plan_element_${name}`))
    {
      return true;
    }
    const entity_name = normalized(entity.name);
    const candidates = semantic_names
      .filter((semantic_name) =>
        entity_name === semantic_name ||
        entity_name.startsWith(`${semantic_name}_`),
      )
      .sort((left, right) =>
        right.length - left.length,
      );
    return candidates[0] === name;
  });
}

function semantic_instances(element, matches) {
  const expected_count = element.count ?? 1;
  const semantic_name = normalized(element.name);
  const exact = matches.find(
    (entity) =>
      normalized(entity.name) ===
      semantic_name,
  );
  if (expected_count === 1 && exact)
  {
    return [exact];
  }

  const numbered_instance = new RegExp(
    `^${semantic_name}_(?:instance_)?[0-9]+$`,
  );
  const numbered_instances = matches.filter((entry) => {
    const entity_name = normalized(entry.name);
    return numbered_instance.test(entity_name);
  });
  if (numbered_instances.length >= expected_count)
  {
    return numbered_instances;
  }
  const named_instances = exact
    ? [exact, ...numbered_instances]
    : numbered_instances;
  if (named_instances.length >= expected_count)
  {
    return named_instances;
  }

  const matched_ids = new Set(
    matches.map((entry) => String(entry.id)),
  );
  return matches.filter((entry) =>
    !matched_ids.has(String(entry.parent_id)),
  );
}

function semantic_bounds(element, matches) {
  const exact = matches.find(
    (entity) => normalized(entity.name) ===
      normalized(element.name),
  );
  if (exact && box_for(exact))
  {
    return box_for(exact);
  }
  const boxes = matches
    .map(box_for)
    .filter(Boolean);
  if (boxes.length === 0)
  {
    return null;
  }
  const min = [...boxes[0].min];
  const max = [...boxes[0].max];
  for (const box of boxes.slice(1))
  {
    for (let axis = 0; axis < 3; axis++)
    {
      min[axis] = Math.min(min[axis], box.min[axis]);
      max[axis] = Math.max(max[axis], box.max[axis]);
    }
  }
  return {
    min,
    max,
    center: min.map(
      (value, axis) => (value + max[axis]) * 0.5,
    ),
    size: min.map(
      (value, axis) => max[axis] - value,
    ),
  };
}

function validate_relationship(
  relationship,
  subject,
  object,
) {
  const tolerance = relationship.tolerance ?? 0.15;
  const distance = box_distance(subject, object);
  if (relationship.relation === "inside")
  {
    return contains_box(object, subject, tolerance);
  }
  if (relationship.relation === "on")
  {
    const vertical_gap =
      subject.min[1] - object.max[1];
    return (
      Math.abs(vertical_gap) <= tolerance &&
      horizontal_overlap(subject, object)
    );
  }
  if (relationship.relation === "connected_to")
  {
    return distance <= (
      relationship.max_distance ?? tolerance
    );
  }
  if (relationship.relation === "separated_from")
  {
    return distance >= (
      relationship.min_distance ?? tolerance
    );
  }
  if (relationship.relation === "aligned_with")
  {
    const axes = {
      x: 0,
      y: 1,
      z: 2,
    };
    const axis = axes[relationship.axis] ?? 1;
    return (
      Math.abs(
        subject.center[axis] -
        object.center[axis],
      ) <= tolerance
    );
  }
  if (relationship.relation === "beside")
  {
    const horizontally_separated =
      subject.max[0] <= object.min[0] + tolerance ||
      object.max[0] <= subject.min[0] + tolerance ||
      subject.max[2] <= object.min[2] + tolerance ||
      object.max[2] <= subject.min[2] + tolerance;
    return (
      horizontally_separated &&
      distance <= (
        relationship.max_distance ?? tolerance
      )
    );
  }
  return false;
}

async function audit_lighting(
  send_command,
  snapshot,
  plan,
  issues,
) {
  const lights = snapshot.entities.filter((entity) =>
    entity.active &&
    Array.isArray(entity.components) &&
    entity.components.some(
      (component) => normalized(component) === "light",
    ),
  );
  const minimum = plan.lighting?.min_lights ?? 1;
  if (lights.length < minimum)
  {
    add_issue(
      issues,
      "error",
      "insufficient_lights",
      `found ${lights.length}, expected at least ${minimum}`,
    );
  }
  const maximum = plan.lighting?.max_lights;
  if (
    Number.isInteger(maximum) &&
    lights.length > maximum
  )
  {
    add_issue(
      issues,
      "error",
      "excessive_lights",
      `found ${lights.length}, expected at most ${maximum}`,
    );
  }

  const scene_box = box_for(snapshot.entities[0]);
  const scene_samples = scene_box
    ? [
      scene_box.center,
      ...[scene_box.min[0], scene_box.max[0]]
        .flatMap((x) =>
          [scene_box.min[1], scene_box.max[1]]
            .flatMap((y) =>
              [
                scene_box.min[2],
                scene_box.max[2],
              ].map((z) => [x, y, z]),
            ),
        ),
    ]
    : [[0, 0, 0]];
  let covering_lights = 0;
  let shadow_lights = 0;
  const light_evidence = [];
  for (const light of lights)
  {
    const component = await send_command(
      "component_get",
      {
        id: light.id,
        type: "light",
      },
    );
    const properties = component.component?.properties ?? {};
    const light_type = normalized(properties.light_type);
    const intensity = Number(properties.intensity ?? 0);
    const range = Number(properties.range ?? 0);
    const minimum_intensity = light_type === "directional"
      ? 10000
      : light_type === "area"
        ? 1000
        : 500;
    const maximum_intensity = light_type === "directional"
      ? 250000
      : 100000;
    const intensity_ok =
      intensity >= minimum_intensity &&
      intensity <= maximum_intensity;
    const range_ok =
      light_type === "directional" ||
      range > 0;
    if (!intensity_ok || !range_ok)
    {
      add_issue(
        issues,
        "error",
        "uncalibrated_light",
        `light ${light.name} has intensity ${intensity} and range ${range}`,
        light.name,
      );
    }
    const angle_degrees = Number(
      properties.angle_degrees ?? 45,
    );
    const closest_scene_point = scene_box
      ? light.position.map(
        (value, axis) =>
          Math.max(
            scene_box.min[axis],
            Math.min(scene_box.max[axis], value),
          ),
      )
      : [0, 0, 0];
    const light_samples = [
      closest_scene_point,
      ...scene_samples,
    ];
    let best_direction_dot = -1;
    const covers_scene =
      light_type === "directional" ||
      light_samples.some((sample) => {
        const to_sample = light.position.map(
          (value, axis) => sample[axis] - value,
        );
        const distance = Math.hypot(...to_sample);
        const direction_dot =
          distance > 0.0001 &&
          Array.isArray(light.forward)
            ? light.forward.reduce(
              (total, value, axis) =>
                total +
                value *
                (
                  to_sample[axis] /
                  distance
                ),
              0,
            )
            : 1;
        best_direction_dot = Math.max(
          best_direction_dot,
          direction_dot,
        );
        const direction_ok = light_type === "spot"
          ? direction_dot >= Math.cos(
            angle_degrees *
            Math.PI /
            360,
          )
          : light_type === "area"
            ? direction_dot > 0
            : true;
        return (
          direction_ok &&
          distance <= range
        );
      });
    const effective_light =
      intensity_ok &&
      range_ok &&
      covers_scene;
    if (effective_light)
    {
      covering_lights++;
    }
    if (effective_light && properties.shadows)
    {
      shadow_lights++;
    }
    light_evidence.push({
      id: light.id,
      name: light.name,
      type: light_type,
      intensity,
      range,
      shadows: Boolean(properties.shadows),
      best_direction_dot,
      covers_scene,
      effective: effective_light,
    });
  }

  if (lights.length > 0 && covering_lights === 0)
  {
    add_issue(
      issues,
      "error",
      "lights_miss_scene",
      "no active light range reaches the scene bounds",
    );
  }
  if (
    plan.lighting?.require_shadows !== false &&
    lights.length > 0 &&
    shadow_lights === 0
  )
  {
    add_issue(
      issues,
      "error",
      "missing_shadow_light",
      "at least one purposeful light must cast shadows",
    );
  }

  return {
    count: lights.length,
    covering_lights,
    shadow_lights,
    lights: light_evidence,
  };
}

async function read_spatial_hierarchy(
  send_command,
  id,
) {
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
        total: page.total ?? entities.length,
        truncated: false,
      };
    }
    if (page_entities.length === 0 || added === 0)
    {
      return {
        ...page,
        ok: false,
        entities,
        count: entities.length,
        error: "spatial snapshot pagination made no progress",
      };
    }
    offset += page_entities.length;
  }
  return {
    ok: false,
    entities,
    count: entities.length,
    truncated: true,
    error: "scene hierarchy exceeds the 100000 entity audit budget",
  };
}

export async function audit_scene_layout(
  send_command,
  {
    id,
    root_name,
    namespace,
    plan: supplied_plan,
    minimum_created_at_ms,
  },
) {
  let plan = supplied_plan;
  if (!plan)
  {
    const stored = await read_scene_plan(
      namespace,
      root_name,
    );
    if (!stored.ok)
    {
      return {
        ok: false,
        pass: false,
        error: stored.error,
      };
    }
    if (
      Number.isFinite(minimum_created_at_ms) &&
      Date.parse(
        stored.updated_at ??
        stored.created_at,
      ) <
        minimum_created_at_ms
    )
    {
      return {
        ok: false,
        pass: false,
        error: `scene plan for ${root_name} is stale`,
      };
    }
    plan = stored.plan;
  }

  const snapshot = await read_spatial_hierarchy(
    send_command,
    id,
  );
  if (!snapshot.ok)
  {
    return {
      ok: false,
      pass: false,
      error: snapshot.error ??
        "spatial snapshot failed",
    };
  }

  const issues = [];
  if (snapshot.truncated)
  {
    add_issue(
      issues,
      "error",
      "spatial_snapshot_truncated",
      "scene hierarchy exceeds the spatial audit limit",
    );
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
    add_issue(
      issues,
      "error",
      "material_snapshot_failed",
      material_snapshot.error ??
        "material evidence is unavailable",
    );
  }
  const materials_by_id = new Map(
    (material_snapshot.materials ?? []).map(
      (entry) => [String(entry.id), entry],
    ),
  );
  const element_evidence = [];
  const resolved = new Map();
  for (const element of plan.elements ?? [])
  {
    const matches = matching_entities(
      element,
      snapshot.entities,
      plan.elements ?? [],
    );
    const instances = semantic_instances(
      element,
      matches,
    );
    const bounds = semantic_bounds(
      element,
      matches,
    );
    resolved.set(normalized(element.name), bounds);
    if (instances.length < (element.count ?? 1))
    {
      add_issue(
        issues,
        "error",
        "missing_element",
        `found ${instances.length}, expected ${element.count ?? 1}`,
        element.name,
      );
      continue;
    }
    if (element.role !== "light" && !bounds)
    {
      add_issue(
        issues,
        "error",
        "missing_bounds",
        "semantic element has no render bounds",
        element.name,
      );
      continue;
    }

    let size_ok = true;
    if (bounds && valid_vector3(element.expected_size, true))
    {
      const expected = element.allow_axis_permutation
        ? [...element.expected_size].sort(
          (left, right) => left - right,
        )
        : element.expected_size;
      const tolerance = element.size_tolerance ?? 0.4;
      const size_boxes =
        element.size_mode === "individual"
          ? instances
            .map(box_for)
            .filter(Boolean)
          : [bounds];
      size_ok =
        size_boxes.length >= (element.count ?? 1) &&
        size_boxes.every((size_box) => {
          const actual = element.allow_axis_permutation
            ? sorted_size(size_box)
            : size_box.size;
          return expected.every((value, axis) => {
            const ratio = actual[axis] / value;
            return (
              ratio >= 1 - tolerance &&
              ratio <= 1 + tolerance
            );
          });
        });
      if (!size_ok)
      {
        add_issue(
          issues,
          "error",
          "implausible_scale",
          `actual size ${bounds.size.join(",")} differs from expected ${element.expected_size.join(",")}`,
          element.name,
        );
      }
    }

    let zone_ok = true;
    if (element.zone)
    {
      const zone = plan.zones.find(
        (entry) => normalized(entry.name) ===
          normalized(element.zone),
      );
      if (zone)
      {
        const target_zone = zone_box(zone);
        const zone_tolerance =
          element.zone_tolerance ?? 0.25;
        zone_ok = bounds
          ? contains_box(
            target_zone,
            bounds,
            zone_tolerance,
          )
          : instances.every((match) =>
            contains_point(
              target_zone,
              match.position,
              zone_tolerance,
            ),
          );
        if (!zone_ok)
        {
          add_issue(
            issues,
            "error",
            "outside_zone",
            `element is outside zone ${element.zone}`,
            element.name,
          );
        }
      }
    }

    let support_ok = true;
    if (
      element.support === "ground" ||
      element.support === "surface" ||
      element.support === "wall" ||
      element.support === "ceiling"
    )
    {
      const matched_ids = new Set(
        matches.map((entry) => String(entry.id)),
      );
      for (const match of matches)
      {
        for (const descendant_id of hierarchy_ids(
          snapshot.entities,
          match.id,
        ))
        {
          matched_ids.add(descendant_id);
        }
      }
      const support_candidates = snapshot.entities.filter(
        (entity) =>
          matched_ids.has(String(entity.id)) &&
          entity.has_render_bounds,
      );
      const maximum_gap = element.max_support_gap ?? 0.08;
      const maximum_depth =
        element.max_intersection_depth ?? 0.05;
      const support_is_valid = (entity) => {
        let ray_support_valid = false;
        if (element.support === "wall")
        {
          ray_support_valid = (
            entity.wall_hit &&
            !matched_ids.has(
              String(entity.wall_entity_id),
            ) &&
            entity.wall_gap <= maximum_gap &&
            entity.wall_gap >= -maximum_depth
          );
        }
        else if (element.support === "ceiling")
        {
          ray_support_valid = (
            entity.ceiling_hit &&
            !matched_ids.has(
              String(entity.ceiling_entity_id),
            ) &&
            entity.ceiling_gap <= maximum_gap &&
            entity.ceiling_gap >= -maximum_depth
          );
        }
        else
        {
          ray_support_valid = (
            entity.support_hit &&
            !matched_ids.has(
              String(entity.support?.entity_id),
            ) &&
            entity.support_gap <= maximum_gap &&
            entity.support_gap >= -maximum_depth
          );
        }
        return (
          ray_support_valid ||
          has_geometric_support(
            entity,
            element.support,
            snapshot.entities,
            matched_ids,
            maximum_gap,
            maximum_depth,
          )
        );
      };
      if (element.size_mode === "individual")
      {
        const valid_support_count = instances.filter(
          (instance) => {
            const instance_ids = hierarchy_ids(
              snapshot.entities,
              instance.id,
            );
            let instance_candidates =
              support_candidates.filter((candidate) =>
                instance_ids.has(String(candidate.id)),
              );
            const instance_bounds = box_for(instance);
            if (
              (
                element.support === "ground" ||
                element.support === "surface"
              ) &&
              instance_bounds
            )
            {
              instance_candidates =
                instance_candidates.filter((candidate) =>
                  candidate.bounding_box.min[1] <=
                    instance_bounds.min[1] +
                    maximum_gap +
                    maximum_depth,
                );
            }
            return instance_candidates.some(
              support_is_valid,
            );
          },
        ).length;
        support_ok =
          valid_support_count >= (element.count ?? 1);
      }
      else
      {
        const aggregate_candidates =
          (
            element.support === "ground" ||
            element.support === "surface"
          ) &&
          bounds
            ? support_candidates.filter((entity) =>
              entity.bounding_box.min[1] <=
                bounds.min[1] +
                maximum_gap +
                maximum_depth,
            )
            : support_candidates;
        support_ok = aggregate_candidates.some(
          support_is_valid,
        );
      }
      if (!support_ok)
      {
        add_issue(
          issues,
          "error",
          "unsupported_or_floating",
          "no rendered part has valid physical support",
          element.name,
        );
      }
    }

    let material_ok = true;
    if (element.material_semantic)
    {
      const semantic = normalized(
        element.material_semantic,
      );
      material_ok = instances.every((instance) => {
        const instance_ids = hierarchy_ids(
          snapshot.entities,
          instance.id,
        );
        const relevant_materials = snapshot.entities
          .filter((entity) =>
            instance_ids.has(String(entity.id)),
          )
          .map((entity) =>
            materials_by_id.get(String(entity.id)),
          )
          .filter(Boolean);
        return relevant_materials.some((entry) =>
          !entry.default_material &&
          normalized(entry.material).includes(semantic),
        );
      });
      if (!material_ok)
      {
        add_issue(
          issues,
          "error",
          "semantic_material_failed",
          `expected semantic material ${element.material_semantic}`,
          element.name,
        );
      }
    }

    const required_tags = element.semantic_tags ?? [];
    const matched_tags = new Set(
      matches.flatMap((match) =>
        Array.isArray(match.tags)
          ? match.tags.map(normalized)
          : [],
      ),
    );
    const tags_ok = required_tags.every((tag) =>
      matched_tags.has(normalized(tag)),
    );
    if (!tags_ok)
    {
      add_issue(
        issues,
        "error",
        "semantic_tags_failed",
        `missing semantic tags ${required_tags.filter(
          (tag) => !matched_tags.has(normalized(tag)),
        ).join(",")}`,
        element.name,
      );
    }

    element_evidence.push({
      name: element.name,
      role: element.role,
      matches: instances.map((entry) => entry.name),
      bounds,
      size_ok,
      zone_ok,
      support_ok,
      material_ok,
      tags_ok,
      semantic_tags: [...matched_tags],
    });
  }

  for (const relationship of plan.relationships ?? [])
  {
    const subject = resolved.get(
      normalized(relationship.subject),
    );
    const object = resolved.get(
      normalized(relationship.object),
    );
    if (
      !subject ||
      !object ||
      !validate_relationship(
        relationship,
        subject,
        object,
      )
    )
    {
      add_issue(
        issues,
        "error",
        "relationship_failed",
        `${relationship.subject} is not ${relationship.relation} ${relationship.object}`,
        relationship.subject,
      );
    }
  }

  const planned_elements = plan.elements ?? [];
  for (
    let left_index = 0;
    left_index < planned_elements.length;
    left_index++
  )
  {
    const left_element = planned_elements[left_index];
    const left_bounds = resolved.get(
      normalized(left_element.name),
    );
    if (!left_bounds)
    {
      continue;
    }
    for (
      let right_index = left_index + 1;
      right_index < planned_elements.length;
      right_index++
    )
    {
      const right_element =
        planned_elements[right_index];
      const required_clearance = Math.max(
        left_element.clearance ?? 0,
        right_element.clearance ?? 0,
      );
      if (required_clearance <= 0)
      {
        continue;
      }
      const has_contact_relationship = (
        plan.relationships ?? []
      ).some((relationship) => {
        const subject = normalized(
          relationship.subject,
        );
        const object = normalized(
          relationship.object,
        );
        const matches_pair =
          (
            subject === normalized(left_element.name) &&
            object === normalized(right_element.name)
          ) ||
          (
            subject === normalized(right_element.name) &&
            object === normalized(left_element.name)
          );
        return (
          matches_pair &&
          [
            "on",
            "inside",
            "connected_to",
          ].includes(relationship.relation)
        );
      });
      if (has_contact_relationship)
      {
        continue;
      }
      const right_bounds = resolved.get(
        normalized(right_element.name),
      );
      if (
        right_bounds &&
        box_distance(
          left_bounds,
          right_bounds,
        ) < required_clearance
      )
      {
        add_issue(
          issues,
          "error",
          "clearance_failed",
          `${left_element.name} and ${right_element.name} require ${required_clearance} meters clearance`,
          left_element.name,
        );
      }
    }
  }

  const lighting = await audit_lighting(
    send_command,
    snapshot,
    plan,
    issues,
  );
  const failed = issues.filter(
    (issue) => issue.severity === "error",
  );
  const total_checks =
    (plan.elements?.length ?? 0) * 4 +
    (plan.elements ?? []).filter(
      (element) =>
        (element.semantic_tags?.length ?? 0) > 0,
    ).length +
    (plan.relationships?.length ?? 0) +
    3;
  const score = Math.max(
    0,
    Math.round(
      (
        total_checks -
        failed.length
      ) /
      Math.max(1, total_checks) *
      100,
    ),
  );

  return {
    ok: true,
    pass: failed.length === 0,
    score,
    root: {
      id,
      name: root_name,
    },
    issues,
    element_evidence,
    lighting,
    plan,
  };
}
