import fs from "node:fs/promises";
import path from "node:path";
import { createHash } from "node:crypto";

export const scene_recipe_version = 1;

export const scene_recipe_node_schema = {
  type: "object",
  required: ["name"],
  properties: {
    semantic_id: { type: "string" },
    name: { type: "string", minLength: 1 },
    kind: {
      enum: ["empty", "primitive", "parametric"],
    },
    parent_semantic_id: { type: "string" },
    transform: {
      type: "object",
      properties: {
        position: {
          type: "array",
          items: { type: "number" },
          minItems: 3,
          maxItems: 3,
        },
        rotation_euler: {
          type: "array",
          items: { type: "number" },
          minItems: 3,
          maxItems: 3,
        },
        scale: {
          type: "array",
          items: { type: "number" },
          minItems: 3,
          maxItems: 3,
        },
      },
      additionalProperties: false,
    },
    primitive: {
      type: "object",
      properties: {
        mesh: {
          enum: [
            "cube",
            "quad",
            "plane",
            "sphere",
            "cylinder",
            "cone",
          ],
        },
      },
      additionalProperties: true,
    },
    parametric: {
      type: "object",
      required: ["shape"],
      additionalProperties: true,
    },
    material: { type: "string" },
    material_role: { type: "string" },
    plan_element: { type: "string" },
    tags: {
      type: "array",
      items: { type: "string" },
    },
    semantic_tags: {
      type: "array",
      items: { type: "string" },
    },
    instances: {
      type: "array",
      minItems: 1,
      items: { type: "object" },
    },
    components: {
      type: "array",
      items: {
        type: "object",
        required: ["type", "properties"],
        properties: {
          type: { type: "string" },
          properties: { type: "object" },
        },
        additionalProperties: false,
      },
    },
    children: {
      type: "array",
      items: { type: "object" },
    },
  },
  additionalProperties: false,
};

export const scene_recipe_schema = {
  type: "object",
  required: ["version", "recipe_id", "nodes"],
  properties: {
    version: { const: scene_recipe_version },
    recipe_id: { type: "string", minLength: 1 },
    metadata: { type: "object" },
    nodes: {
      type: "array",
      items: scene_recipe_node_schema,
    },
  },
  additionalProperties: false,
};

const primitive_meshes = new Set([
  "cube",
  "quad",
  "plane",
  "sphere",
  "cylinder",
  "cone",
]);

const parametric_shapes = new Set([
  "beveled_box",
  "rounded_box",
  "wedge",
  "wall_opening",
  "wall_openings",
  "extruded_profile",
  "revolved_profile",
  "torus",
  "rounded_cylinder",
  "capsule",
  "pipe",
  "curved_profile",
  "loft",
  "arch",
  "inset_panel",
  "tapered_extrusion",
  "grid",
  "grass_blade",
  "flower",
]);

function stable_value(value)
{
  if (Array.isArray(value))
  {
    return value.map(stable_value);
  }
  if (value && typeof value === "object")
  {
    return Object.fromEntries(
      Object.keys(value)
        .sort()
        .filter((key) => value[key] !== undefined)
        .map((key) => [
          key,
          stable_value(value[key]),
        ]),
    );
  }
  return value;
}

function stable_json(value)
{
  return JSON.stringify(stable_value(value));
}

function short_hash(value, length = 16)
{
  return createHash("sha256")
    .update(
      typeof value === "string"
        ? value
        : stable_json(value),
    )
    .digest("hex")
    .slice(0, length);
}

function safe_id(value, fallback = "")
{
  const result = String(value ?? "")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_.-]+/g, "_")
    .replace(/^[_\-.]+|[_\-.]+$/g, "")
    .slice(0, 96);
  return result || fallback;
}

function safe_name(value, fallback)
{
  const result = String(value ?? "")
    .trim()
    .replace(/[\u0000-\u001f]+/g, " ")
    .replace(/\s+/g, " ")
    .slice(0, 120);
  return result || fallback;
}

function issue(
  severity,
  code,
  message,
  target,
)
{
  return {
    severity,
    code,
    message,
    ...(target ? { target } : {}),
  };
}

function vector3(value)
{
  return (
    Array.isArray(value) &&
    value.length === 3 &&
    value.every(Number.isFinite)
  )
    ? [...value]
    : undefined;
}

function positive_vector3(value)
{
  const result = vector3(value);
  return result?.every((entry) => entry > 0)
    ? result
    : undefined;
}

function normalize_transform(value, issues, target)
{
  if (!value)
  {
    return {};
  }
  const result = {};
  for (const key of [
    "position",
    "rotation_euler",
    "scale",
  ])
  {
    if (value[key] === undefined)
    {
      continue;
    }
    const normalized = key === "scale"
      ? positive_vector3(value[key])
      : vector3(value[key]);
    if (!normalized)
    {
      issues.push(
        issue(
          "error",
          "invalid_transform",
          `${key} must contain three finite numbers`,
          target,
        ),
      );
      continue;
    }
    result[key] = normalized;
  }
  return result;
}

function normalize_components(value, issues, target)
{
  if (value === undefined)
  {
    return [];
  }
  if (!Array.isArray(value))
  {
    issues.push(
      issue(
        "error",
        "invalid_components",
        "components must be an array",
        target,
      ),
    );
    return [];
  }
  return value
    .map((component) => {
      const type = safe_id(component?.type);
      if (
        !type ||
        !component?.properties ||
        typeof component.properties !== "object" ||
        Array.isArray(component.properties)
      )
      {
        issues.push(
          issue(
            "error",
            "invalid_component",
            "component requires type and properties",
            target,
          ),
        );
        return null;
      }
      return {
        type,
        properties: stable_value(component.properties),
      };
    })
    .filter(Boolean)
    .sort((left, right) =>
      left.type.localeCompare(right.type),
    );
}

export function make_semantic_id(
  value,
  parent_semantic_id = "",
)
{
  const explicit = safe_id(value?.semantic_id);
  if (explicit)
  {
    return explicit;
  }
  const identity = [
    safe_id(parent_semantic_id, "root"),
    safe_id(value?.name, "node"),
    safe_id(
      value?.kind ??
        value?.parametric?.shape ??
        value?.primitive?.mesh,
      "empty",
    ),
  ].join("/");
  return `node_${short_hash(identity, 20)}`;
}

function infer_kind(node)
{
  if (
    node?.kind === "parametric" ||
    node?.parametric ||
    node?.shape
  )
  {
    return "parametric";
  }
  if (
    node?.kind === "primitive" ||
    node?.primitive ||
    node?.mesh
  )
  {
    return "primitive";
  }
  return node?.kind === "empty"
    ? "empty"
    : "empty";
}

function flatten_nodes(
  source,
  parent_semantic_id,
  output,
  issues,
)
{
  if (!Array.isArray(source))
  {
    issues.push(
      issue(
        "error",
        "invalid_nodes",
        "nodes must be an array",
      ),
    );
    return;
  }
  for (const raw of source)
  {
    if (!raw || typeof raw !== "object")
    {
      issues.push(
        issue(
          "error",
          "invalid_node",
          "each node must be an object",
        ),
      );
      continue;
    }
    if (raw.instances !== undefined)
    {
      if (
        !Array.isArray(raw.instances) ||
        raw.instances.length === 0
      )
      {
        issues.push(
          issue(
            "error",
            "invalid_instances",
            "instances must be a non empty array",
            raw.name,
          ),
        );
        continue;
      }
      if ((raw.children?.length ?? 0) > 0)
      {
        issues.push(
          issue(
            "error",
            "instance_children_unsupported",
            "instanced nodes cannot contain children",
            raw.name,
          ),
        );
        continue;
      }
      const base_semantic_id = make_semantic_id(
        raw,
        parent_semantic_id,
      );
      raw.instances.forEach((instance, index) => {
        const instance_number = index + 1;
        flatten_nodes(
          [
            {
              ...raw,
              semantic_id: safe_id(
                instance?.semantic_id,
                `${base_semantic_id}_instance_${instance_number}`,
              ),
              name: safe_name(
                instance?.name,
                `${safe_name(
                  raw.name,
                  base_semantic_id,
                )}_instance_${instance_number}`,
              ),
              transform: {
                ...(raw.transform ?? {}),
                ...(instance?.transform ?? instance ?? {}),
              },
              instances: undefined,
              children: [],
            },
          ],
          parent_semantic_id,
          output,
          issues,
        );
      });
      continue;
    }
    const semantic_id = make_semantic_id(
      raw,
      parent_semantic_id,
    );
    const name = safe_name(raw.name, semantic_id);
    const kind = infer_kind(raw);
    const parent = safe_id(
      raw.parent_semantic_id,
      parent_semantic_id,
    );
    const node = {
      semantic_id,
      name,
      kind,
      ...(parent
        ? { parent_semantic_id: parent }
        : {}),
      transform: normalize_transform(
        raw.transform ?? raw,
        issues,
        semantic_id,
      ),
      components: normalize_components(
        raw.components,
        issues,
        semantic_id,
      ),
      tags: [
        ...new Set(
          [
            ...(Array.isArray(raw.tags)
              ? raw.tags
              : []),
            ...(Array.isArray(raw.semantic_tags)
              ? raw.semantic_tags
              : []),
          ]
            .map((tag) => safe_id(tag))
            .filter(Boolean),
        ),
      ].sort(),
    };
    if (raw.material !== undefined)
    {
      node.material = String(raw.material).trim();
      if (!node.material)
      {
        issues.push(
          issue(
            "error",
            "invalid_material",
            "material cannot be empty",
            semantic_id,
          ),
        );
      }
    }
    if (raw.material_role !== undefined)
    {
      node.material_role = safe_id(raw.material_role);
    }
    if (raw.plan_element !== undefined)
    {
      node.plan_element = safe_id(raw.plan_element);
    }
    if (kind === "primitive")
    {
      const mesh = safe_id(
        raw.primitive?.mesh ?? raw.mesh,
        "cube",
      );
      node.primitive = { mesh };
      if (!primitive_meshes.has(mesh))
      {
        issues.push(
          issue(
            "error",
            "invalid_primitive",
            `unsupported primitive mesh ${mesh}`,
            semantic_id,
          ),
        );
      }
    }
    if (kind === "parametric")
    {
      const source_parametric = {
        ...(raw.parametric ?? {}),
      };
      if (raw.shape)
      {
        source_parametric.shape = raw.shape;
      }
      const shape = safe_id(source_parametric.shape);
      node.parametric = stable_value({
        ...source_parametric,
        shape,
      });
      if (!parametric_shapes.has(shape))
      {
        issues.push(
          issue(
            "error",
            "invalid_parametric_shape",
            `unsupported parametric shape ${shape}`,
            semantic_id,
          ),
        );
      }
    }
    output.push(node);
    flatten_nodes(
      raw.children ?? [],
      semantic_id,
      output,
      issues,
    );
  }
}

export function validate_scene_recipe(recipe)
{
  const issues = [];
  if (recipe?.version !== scene_recipe_version)
  {
    issues.push(
      issue(
        "error",
        "unsupported_version",
        `recipe version must be ${scene_recipe_version}`,
      ),
    );
  }
  if (!safe_id(recipe?.recipe_id))
  {
    issues.push(
      issue(
        "error",
        "missing_recipe_id",
        "recipe_id is required",
      ),
    );
  }
  if (!Array.isArray(recipe?.nodes))
  {
    issues.push(
      issue(
        "error",
        "invalid_nodes",
        "nodes must be an array",
      ),
    );
    return {
      ok: false,
      issues,
    };
  }

  const ids = new Set();
  for (const node of recipe.nodes)
  {
    if (!node.semantic_id || ids.has(node.semantic_id))
    {
      issues.push(
        issue(
          "error",
          "duplicate_semantic_id",
          "semantic ids must be unique",
          node.semantic_id || node.name,
        ),
      );
    }
    ids.add(node.semantic_id);
  }
  for (const node of recipe.nodes)
  {
    if (
      node.parent_semantic_id &&
      !ids.has(node.parent_semantic_id)
    )
    {
      issues.push(
        issue(
          "error",
          "missing_parent",
          `parent ${node.parent_semantic_id} does not exist`,
          node.semantic_id,
        ),
      );
    }
    const visited = new Set([node.semantic_id]);
    let parent = node.parent_semantic_id;
    while (parent)
    {
      if (visited.has(parent))
      {
        issues.push(
          issue(
            "error",
            "parent_cycle",
            "node hierarchy contains a cycle",
            node.semantic_id,
          ),
        );
        break;
      }
      visited.add(parent);
      parent = recipe.nodes.find(
        (entry) => entry.semantic_id === parent,
      )?.parent_semantic_id;
    }
  }
  return {
    ok: !issues.some((entry) =>
      entry.severity === "error",
    ),
    issues,
  };
}

export function normalize_scene_recipe(input)
{
  const issues = [];
  if (
    input?.version !== undefined &&
    input.version !== scene_recipe_version
  )
  {
    issues.push(
      issue(
        "error",
        "unsupported_version",
        `recipe version must be ${scene_recipe_version}`,
      ),
    );
  }
  const recipe_id = safe_id(
    input?.recipe_id ?? input?.name,
    "scene",
  );
  const nodes = [];
  flatten_nodes(
    input?.nodes ?? [],
    "",
    nodes,
    issues,
  );
  nodes.sort((left, right) =>
    left.semantic_id.localeCompare(right.semantic_id),
  );
  const recipe = {
    version: scene_recipe_version,
    recipe_id,
    metadata: stable_value(input?.metadata ?? {}),
    nodes,
  };
  const validation = validate_scene_recipe(recipe);
  issues.push(...validation.issues);
  return {
    ok: !issues.some((entry) =>
      entry.severity === "error",
    ),
    recipe,
    issues,
  };
}

export function get_scene_recipe_path({
  world_path,
  project_root,
  recipe_id,
})
{
  const world_file = String(world_path ?? "").trim();
  const root = world_file
    ? path.dirname(path.resolve(world_file))
    : path.resolve(project_root ?? process.cwd());
  const id = safe_id(recipe_id, "scene");
  const file_name = `${id}_${short_hash(id, 8)}.json`;
  return path.join(
    root,
    ".spartan",
    "generated",
    "scene_recipes",
    file_name,
  );
}

export async function read_scene_recipe(options)
{
  const file_path = get_scene_recipe_path(options);
  try
  {
    const parsed = JSON.parse(
      await fs.readFile(file_path, "utf8"),
    );
    const normalized = normalize_scene_recipe(parsed);
    return {
      ...normalized,
      found: true,
      path: file_path,
    };
  }
  catch (error)
  {
    if (error?.code === "ENOENT")
    {
      return {
        ok: true,
        found: false,
        path: file_path,
        recipe: null,
        issues: [],
      };
    }
    return {
      ok: false,
      found: false,
      path: file_path,
      recipe: null,
      issues: [
        issue(
          "error",
          "recipe_read_failed",
          error.message,
          file_path,
        ),
      ],
    };
  }
}

export async function write_scene_recipe(
  input,
  options = {},
)
{
  const normalized = normalize_scene_recipe(input);
  const file_path = get_scene_recipe_path({
    ...options,
    recipe_id: normalized.recipe.recipe_id,
  });
  if (!normalized.ok)
  {
    return {
      ...normalized,
      path: file_path,
      written: false,
    };
  }
  const temporary_path = [
    file_path,
    ".",
    process.pid,
    ".tmp",
  ].join("");
  try
  {
    await fs.mkdir(
      path.dirname(file_path),
      { recursive: true },
    );
    await fs.writeFile(
      temporary_path,
      `${JSON.stringify(normalized.recipe, null, 2)}\n`,
      "utf8",
    );
    await fs.rename(temporary_path, file_path);
    return {
      ...normalized,
      path: file_path,
      written: true,
    };
  }
  catch (error)
  {
    await fs.rm(
      temporary_path,
      { force: true },
    ).catch(() => {});
    return {
      ...normalized,
      ok: false,
      path: file_path,
      written: false,
      issues: [
        ...normalized.issues,
        issue(
          "error",
          "recipe_write_failed",
          error.message,
          file_path,
        ),
      ],
    };
  }
}

function changed_fields(previous, next)
{
  const keys = new Set([
    ...Object.keys(previous ?? {}),
    ...Object.keys(next ?? {}),
  ]);
  return [...keys]
    .filter((key) =>
      stable_json(previous?.[key]) !==
        stable_json(next?.[key]),
    )
    .sort();
}

export function diff_scene_recipes(
  previous_recipe,
  next_recipe,
)
{
  const previous = new Map(
    (previous_recipe?.nodes ?? []).map(
      (node) => [node.semantic_id, node],
    ),
  );
  const next = new Map(
    (next_recipe?.nodes ?? []).map(
      (node) => [node.semantic_id, node],
    ),
  );
  const create = [];
  const update = [];
  const remove = [];
  const unchanged = [];
  const ids = [...new Set([
    ...previous.keys(),
    ...next.keys(),
  ])].sort();
  for (const semantic_id of ids)
  {
    const before = previous.get(semantic_id);
    const after = next.get(semantic_id);
    if (!before)
    {
      create.push({
        semantic_id,
        node: after,
      });
    }
    else if (!after)
    {
      remove.push({
        semantic_id,
        node: before,
      });
    }
    else
    {
      const fields = changed_fields(before, after);
      if (fields.length === 0)
      {
        unchanged.push({ semantic_id });
      }
      else
      {
        update.push({
          semantic_id,
          changed_fields: fields,
          before,
          after,
        });
      }
    }
  }
  return {
    create,
    update,
    delete: remove,
    unchanged,
    counts: {
      create: create.length,
      update: update.length,
      delete: remove.length,
      unchanged: unchanged.length,
    },
  };
}

function ownership_tags(
  recipe_id,
  semantic_id,
  semantic_tags = [],
)
{
  return [
    `scene_recipe=${recipe_id}`,
    `semantic_id=${semantic_id}`,
    ...semantic_tags,
  ].join(",");
}

function owned_name(recipe_id, node)
{
  return [
    safe_name(node.name, node.semantic_id).slice(0, 80),
    "__sr_",
    short_hash(recipe_id, 8),
    "_",
    short_hash(node.semantic_id, 10),
  ].join("");
}

function ownership_from_entity(entity)
{
  const tags = Array.isArray(entity?.tags)
    ? entity.tags.map(String)
    : String(entity?.tags ?? "")
      .split(/[;,]/)
      .map((tag) => tag.trim())
      .filter(Boolean);
  const recipe = tags.find((tag) =>
    tag.startsWith("scene_recipe="),
  )?.slice("scene_recipe=".length);
  const semantic_id = tags.find((tag) =>
    tag.startsWith("semantic_id="),
  )?.slice("semantic_id=".length);
  return {
    recipe_id: recipe,
    semantic_id,
  };
}

async function list_entities(send_command)
{
  const entities = [];
  const limit = 1000;
  for (let offset = 0; offset < 100000; offset += limit)
  {
    const result = await send_command(
      "entity_list",
      { offset, limit },
    );
    if (!result?.ok)
    {
      return {
        ok: false,
        error: result?.error ?? "entity_list failed",
        entities,
      };
    }
    const page = result.entities ?? [];
    entities.push(...page);
    if (
      page.length < limit ||
      entities.length >= Number(result.total ?? 0)
    )
    {
      break;
    }
  }
  return {
    ok: true,
    entities,
  };
}

function depth_for(node, by_id)
{
  let depth = 0;
  let parent = node.parent_semantic_id;
  const visited = new Set();
  while (parent && !visited.has(parent))
  {
    visited.add(parent);
    depth++;
    parent = by_id.get(parent)?.parent_semantic_id;
  }
  return depth;
}

function ordered_nodes(recipe, reverse = false)
{
  const by_id = new Map(
    recipe.nodes.map((node) => [
      node.semantic_id,
      node,
    ]),
  );
  return [...recipe.nodes].sort((left, right) => {
    const difference =
      depth_for(left, by_id) -
      depth_for(right, by_id);
    return (
      (reverse ? -difference : difference) ||
      left.semantic_id.localeCompare(right.semantic_id)
    );
  });
}

function engine_error(result, fallback)
{
  return result?.error ??
    result?.message ??
    fallback;
}

async function run_command(
  send_command,
  command,
  arguments_value,
  operation,
  results,
)
{
  const result = await send_command(
    command,
    arguments_value,
  );
  results.push({
    ...operation,
    command,
    ok: Boolean(result?.ok),
    result,
  });
  if (!result?.ok)
  {
    throw new Error(
      engine_error(result, `${command} failed`),
    );
  }
  return result;
}

function generated_mesh_path(recipe_id, node)
{
  const signature = {
    semantic_id: node.semantic_id,
    parametric: node.parametric,
  };
  return [
    "project/generated/mcp/scene_recipes/",
    safe_id(recipe_id),
    "/",
    safe_id(node.semantic_id, "node"),
    "_",
    short_hash(signature, 12),
    ".mesh",
  ].join("");
}

function generated_mesh_paths(recipe)
{
  return new Set(
    (recipe?.nodes ?? [])
      .filter((node) =>
        node.kind === "parametric",
      )
      .map((node) =>
        node.parametric?.path ??
        generated_mesh_path(
          recipe.recipe_id,
          node,
        ),
      ),
  );
}

async function cleanup_generated_assets(
  send_command,
  previous_recipe,
  next_recipe,
  project_root,
)
{
  const previous_paths =
    generated_mesh_paths(previous_recipe);
  const next_paths = generated_mesh_paths(next_recipe);
  const owned_prefix = [
    "project/generated/mcp/scene_recipes/",
    safe_id(next_recipe.recipe_id),
    "/",
  ].join("");
  const removed = [];
  const issues = [];
  for (const resource_path of previous_paths)
  {
    if (
      next_paths.has(resource_path) ||
      !resource_path
        .replaceAll("\\", "/")
        .startsWith(owned_prefix)
    )
    {
      continue;
    }
    const cache_result = await send_command(
      "resource_remove",
      {
        path: resource_path,
        type: "mesh",
      },
    );
    const relative_path = resource_path
      .replaceAll("\\", "/")
      .replace(/^project\//, "");
    const file_path = path.resolve(
      project_root ?? process.cwd(),
      relative_path,
    );
    try
    {
      await fs.rm(file_path, { force: true });
    }
    catch (error)
    {
      issues.push(
        issue(
          "warning",
          "generated_asset_cleanup_failed",
          error.message,
          resource_path,
        ),
      );
    }
    removed.push({
      path: resource_path,
      cache_removed: Boolean(cache_result?.ok),
      file_path,
    });
  }
  return {
    removed,
    issues,
  };
}

async function set_transform(
  send_command,
  entity_id,
  transform,
  operation,
  results,
)
{
  if (Object.keys(transform ?? {}).length === 0)
  {
    return;
  }
  await run_command(
    send_command,
    "entity_set_transform",
    {
      id: entity_id,
      ...transform,
    },
    operation,
    results,
  );
}

async function set_render(
  send_command,
  entity_id,
  node,
  operation,
  results,
)
{
  if (node.kind === "primitive")
  {
    await run_command(
      send_command,
      "component_set",
      {
        id: entity_id,
        type: "render",
        property: "mesh",
        value: node.primitive.mesh,
      },
      operation,
      results,
    );
    if (node.material)
    {
      await run_command(
        send_command,
        "component_set",
        {
          id: entity_id,
          type: "render",
          property: "material",
          value: node.material,
        },
        operation,
        results,
      );
    }
  }
  if (node.kind === "parametric")
  {
    const mesh_path = node.parametric.path ??
      generated_mesh_path(
        operation.recipe_id,
        node,
      );
    const generated = await run_command(
      send_command,
      "mesh_generate",
      {
        ...node.parametric,
        path: mesh_path,
        reuse_existing: true,
      },
      operation,
      results,
    );
    await run_command(
      send_command,
      "render_set_mesh",
      {
        id: entity_id,
        mesh: generated.resource?.path ?? mesh_path,
        ...(node.material
          ? { material: node.material }
          : {}),
      },
      operation,
      results,
    );
  }
  if (
    node.kind !== "empty" &&
    !node.material
  )
  {
    await run_command(
      send_command,
      "component_set",
      {
        id: entity_id,
        type: "render",
        property: "default_material",
        value: true,
      },
      operation,
      results,
    );
  }
}

async function set_components(
  send_command,
  entity_id,
  node,
  created,
  previous,
  operation,
  results,
)
{
  if (node.kind !== "empty")
  {
    const primitive_body_types = {
      sphere: "sphere",
      quad: "plane",
      plane: "plane",
      cylinder: "capsule",
    };
    const body_type = node.kind === "parametric"
      ? "mesh_convex"
      : primitive_body_types[node.primitive?.mesh] ?? "box";
    const had_physics = previous?.components?.some(
      (entry) =>
        (
          typeof entry === "string"
            ? entry
            : entry.type
        ) === "physics",
    );
    const primitive_created =
      created && node.kind === "primitive";
    if (!primitive_created && (created || !had_physics))
    {
      await run_command(
        send_command,
        "entity_add_component",
        {
          id: entity_id,
          type: "physics",
        },
        operation,
        results,
      );
    }
    for (const [property, value] of [
      ["body_type", body_type],
      ["static", true],
      ["kinematic", false],
    ])
    {
      await run_command(
        send_command,
        "component_set",
        {
          id: entity_id,
          type: "physics",
          property,
          value,
        },
        operation,
        results,
      );
    }
  }

  for (const component of node.components)
  {
    const existed = previous?.components?.some(
      (entry) =>
        (
          typeof entry === "string"
            ? entry
            : entry.type
        ) === component.type,
    );
    if (
      component.type !== "render" &&
      (
        node.kind === "empty" ||
        component.type !== "physics"
      ) &&
      (created || !existed)
    )
    {
      await run_command(
        send_command,
        "entity_add_component",
        {
          id: entity_id,
          type: component.type,
        },
        operation,
        results,
      );
    }
    for (const [property, value] of Object.entries(
      component.properties,
    ))
    {
      await run_command(
        send_command,
        "component_set",
        {
          id: entity_id,
          type: component.type,
          property,
          value,
        },
        operation,
        results,
      );
    }
  }
}

async function create_node(
  send_command,
  recipe_id,
  node,
  parent_id,
  results,
  created_ids,
)
{
  const operation = {
    action: "create",
    recipe_id,
    semantic_id: node.semantic_id,
  };
  const identity_tags = ownership_tags(
    recipe_id,
    node.semantic_id,
    [
      ...node.tags,
      ...(node.plan_element
        ? [`plan_element=${node.plan_element}`]
        : []),
    ],
  );
  let created;
  if (node.kind === "primitive")
  {
    created = await run_command(
      send_command,
      "entity_create_primitive",
      {
        name: owned_name(recipe_id, node),
        mesh: node.primitive.mesh,
        ...(parent_id ? { parent_id } : {}),
        ...node.transform,
        ...(node.material
          ? { material: node.material }
          : {}),
        tags: identity_tags,
      },
      operation,
      results,
    );
  }
  else
  {
    created = await run_command(
      send_command,
      "entity_create_empty",
      {
        name: owned_name(recipe_id, node),
        ...(parent_id ? { parent_id } : {}),
        tags: identity_tags,
      },
      operation,
      results,
    );
  }
  const entity_id = created.entity?.id;
  if (!entity_id)
  {
    throw new Error(
      "create command returned no entity id",
    );
  }
  created_ids.push(entity_id);
  if (node.kind !== "primitive")
  {
    await set_transform(
      send_command,
      entity_id,
      node.transform,
      operation,
      results,
    );
    if (node.kind === "parametric")
    {
      await set_render(
        send_command,
        entity_id,
        node,
        operation,
        results,
      );
    }
  }
  await run_command(
    send_command,
    "entity_update",
    {
      id: entity_id,
      tags: identity_tags,
    },
    operation,
    results,
  );
  await set_components(
    send_command,
    entity_id,
    node,
    true,
    null,
    operation,
    results,
  );
  return {
    id: entity_id,
    name: owned_name(recipe_id, node),
  };
}

async function update_node(
  send_command,
  recipe_id,
  node,
  entity,
  parent_id,
  previous,
  results,
)
{
  const operation = {
    action: "update",
    recipe_id,
    semantic_id: node.semantic_id,
  };
  const update = {
    id: entity.id,
    name: owned_name(recipe_id, node),
    parent_id: parent_id ?? "0",
    tags: ownership_tags(
      recipe_id,
      node.semantic_id,
      [
        ...node.tags,
        ...(node.plan_element
          ? [`plan_element=${node.plan_element}`]
          : []),
      ],
    ),
  };
  await run_command(
    send_command,
    "entity_update",
    update,
    operation,
    results,
  );
  await set_transform(
    send_command,
    entity.id,
    node.transform,
    operation,
    results,
  );
  if (
    previous?.kind === "empty" &&
    node.kind !== "empty"
  )
  {
    await run_command(
      send_command,
      "entity_add_component",
      {
        id: entity.id,
        type: "render",
      },
      operation,
      results,
    );
  }
  if (
    previous?.kind !== "empty" &&
    node.kind === "empty"
  )
  {
    await run_command(
      send_command,
      "entity_remove_component",
      {
        id: entity.id,
        type: "render",
      },
      operation,
      results,
    );
  }
  if (node.kind !== "empty")
  {
    await set_render(
      send_command,
      entity.id,
      node,
      operation,
      results,
    );
  }
  await set_components(
    send_command,
    entity.id,
    node,
    false,
    {
      ...previous,
      components: [
        ...(previous?.components ?? []),
        ...(entity.components ?? []),
      ],
    },
    operation,
    results,
  );
}

async function resolve_context(
  send_command,
  options,
)
{
  if (options.world_path)
  {
    return {
      world_path: options.world_path,
      project_root: options.project_root,
    };
  }
  const world = await send_command(
    "world_summary",
    {},
  );
  return {
    world_path: world?.ok
      ? world.file_path
      : "",
    project_root: options.project_root,
    world_result: world,
  };
}

export async function preview_scene_recipe(
  send_command,
  input,
  options = {},
)
{
  const normalized = normalize_scene_recipe(input);
  const context = await resolve_context(
    send_command,
    options,
  );
  const stored = await read_scene_recipe({
    ...context,
    recipe_id: normalized.recipe.recipe_id,
  });
  const diff = diff_scene_recipes(
    stored.recipe,
    normalized.recipe,
  );
  const listed = await list_entities(send_command);
  const owned = listed.entities?.filter((entity) => {
    const ownership = ownership_from_entity(entity);
    return (
      ownership.recipe_id ===
        normalized.recipe.recipe_id
    );
  }) ?? [];
  const issues = [
    ...normalized.issues,
    ...(stored.issues ?? []),
  ];
  if (!listed.ok)
  {
    issues.push(
      issue(
        "error",
        "entity_snapshot_failed",
        listed.error,
      ),
    );
  }
  const desired_ids = new Set(
    normalized.recipe.nodes.map(
      (node) => node.semantic_id,
    ),
  );
  const cleanup = owned
    .filter((entity) =>
      !desired_ids.has(
        ownership_from_entity(entity).semantic_id,
      ),
    )
    .map((entity) => ({
      id: entity.id,
      name: entity.name,
      semantic_id:
        ownership_from_entity(entity).semantic_id,
    }))
    .sort((left, right) =>
      String(left.semantic_id).localeCompare(
        String(right.semantic_id),
      ),
    );
  return {
    ok: !issues.some((entry) =>
      entry.severity === "error",
    ),
    dry_run: true,
    recipe: normalized.recipe,
    path: stored.path,
    previous_found: stored.found,
    diff,
    reconciliation: {
      owned_count: owned.length,
      cleanup,
    },
    issues,
  };
}

export async function cleanup_scene_recipe_ownership(
  send_command,
  {
    recipe_id,
    keep_semantic_ids = [],
    entities,
    dry_run = false,
  },
)
{
  const listed = entities
    ? { ok: true, entities }
    : await list_entities(send_command);
  if (!listed.ok)
  {
    return {
      ok: false,
      deleted: [],
      issues: [
        issue(
          "error",
          "entity_snapshot_failed",
          listed.error,
        ),
      ],
    };
  }
  const keep = new Set(
    keep_semantic_ids.map(safe_id),
  );
  const owned = listed.entities.filter((entity) => {
    const ownership = ownership_from_entity(entity);
    return (
      ownership.recipe_id === safe_id(recipe_id) &&
      !keep.has(ownership.semantic_id)
    );
  });
  const by_id = new Map(
    listed.entities.map((entity) => [
      String(entity.id),
      entity,
    ]),
  );
  const entity_depth = (entity) => {
    let depth = 0;
    let current = entity;
    const visited = new Set();
    while (
      current?.parent_id &&
      !visited.has(String(current.parent_id))
    )
    {
      visited.add(String(current.parent_id));
      depth++;
      current = by_id.get(String(current.parent_id));
    }
    return depth;
  };
  owned.sort((left, right) =>
    entity_depth(right) - entity_depth(left) ||
    String(left.id).localeCompare(String(right.id)),
  );
  const deleted = [];
  const issues = [];
  for (const entity of owned)
  {
    if (dry_run)
    {
      deleted.push({
        id: entity.id,
        name: entity.name,
        dry_run: true,
      });
      continue;
    }
    const result = await send_command(
      "entity_delete",
      { id: entity.id },
    );
    if (!result?.ok)
    {
      issues.push(
        issue(
          "error",
          "ownership_cleanup_failed",
          engine_error(
            result,
            "entity_delete failed",
          ),
          entity.id,
        ),
      );
      break;
    }
    deleted.push({
      id: entity.id,
      name: entity.name,
    });
  }
  return {
    ok: issues.length === 0,
    deleted,
    issues,
  };
}

export async function apply_scene_recipe(
  send_command,
  input,
  options = {},
)
{
  if (options.dry_run)
  {
    return preview_scene_recipe(
      send_command,
      input,
      options,
    );
  }
  const preview = await preview_scene_recipe(
    send_command,
    input,
    options,
  );
  if (!preview.ok)
  {
    return {
      ...preview,
      applied: false,
    };
  }

  const recipe = preview.recipe;
  const context = await resolve_context(
    send_command,
    options,
  );
  const listed = await list_entities(send_command);
  if (!listed.ok)
  {
    return {
      ok: false,
      applied: false,
      recipe,
      diff: preview.diff,
      issues: [
        issue(
          "error",
          "entity_snapshot_failed",
          listed.error,
        ),
      ],
      operations: [],
    };
  }

  const previous_recipe = preview.previous_found
    ? (
      await read_scene_recipe({
        ...context,
        recipe_id: recipe.recipe_id,
      })
    ).recipe
    : null;
  const previous_by_id = new Map(
    (previous_recipe?.nodes ?? []).map((node) => [
      node.semantic_id,
      node,
    ]),
  );
  const owned_by_semantic_id = new Map();
  const owned_name_map = new Map();
  for (const entity of listed.entities)
  {
    const ownership = ownership_from_entity(entity);
    if (ownership.recipe_id === recipe.recipe_id)
    {
      owned_by_semantic_id.set(
        ownership.semantic_id,
        entity,
      );
    }
    owned_name_map.set(entity.name, entity);
  }

  const entity_by_semantic_id = new Map();
  const created_ids = [];
  const operations = [];
  const issues = [];
  try
  {
    for (const node of ordered_nodes(recipe))
    {
      let entity = owned_by_semantic_id.get(
        node.semantic_id,
      );
      if (!entity)
      {
        const by_name = owned_name_map.get(
          owned_name(recipe.recipe_id, node),
        );
        if (by_name)
        {
          entity = by_name;
        }
      }
      const parent_id = node.parent_semantic_id
        ? entity_by_semantic_id.get(
          node.parent_semantic_id,
        )?.id
        : (
          options.root_parent_id ??
          entity?.parent_id
        );
      if (node.parent_semantic_id && !parent_id)
      {
        throw new Error(
          `parent ${node.parent_semantic_id} was not reconciled`,
        );
      }
      if (entity)
      {
        await update_node(
          send_command,
          recipe.recipe_id,
          node,
          entity,
          parent_id,
          previous_by_id.get(node.semantic_id),
          operations,
        );
      }
      else
      {
        entity = await create_node(
          send_command,
          recipe.recipe_id,
          node,
          parent_id,
          operations,
          created_ids,
        );
      }
      entity_by_semantic_id.set(
        node.semantic_id,
        entity,
      );
    }

    const cleanup = await cleanup_scene_recipe_ownership(
      send_command,
      {
        recipe_id: recipe.recipe_id,
        keep_semantic_ids: recipe.nodes.map(
          (node) => node.semantic_id,
        ),
        entities: listed.entities,
      },
    );
    if (!cleanup.ok)
    {
      throw new Error(
        cleanup.issues[0]?.message ??
          "ownership cleanup failed",
      );
    }
    for (const deleted of cleanup.deleted)
    {
      operations.push({
        action: "delete",
        command: "entity_delete",
        ok: true,
        entity: deleted,
      });
    }
    const asset_cleanup = await cleanup_generated_assets(
      send_command,
      previous_recipe,
      recipe,
      context.project_root,
    );
    issues.push(...asset_cleanup.issues);

    const written = await write_scene_recipe(
      recipe,
      context,
    );
    if (!written.ok)
    {
      throw new Error(
        written.issues.at(-1)?.message ??
          "recipe persistence failed",
      );
    }
    return {
      ok: true,
      applied: true,
      dry_run: false,
      recipe,
      path: written.path,
      diff: preview.diff,
      operations,
      created_count: created_ids.length,
      updated_count: recipe.nodes.length -
        created_ids.length,
      deleted_count: cleanup.deleted.length,
      removed_assets: asset_cleanup.removed,
      issues,
    };
  }
  catch (error)
  {
    const rolled_back = [];
    for (const id of [...created_ids].reverse())
    {
      const result = await send_command(
        "entity_delete",
        { id },
      );
      rolled_back.push({
        id,
        ok: Boolean(result?.ok),
        error: result?.ok
          ? undefined
          : engine_error(
            result,
            "rollback delete failed",
          ),
      });
    }
    issues.push(
      issue(
        "error",
        "recipe_apply_failed",
        error.message,
      ),
    );
    if (rolled_back.some((entry) => !entry.ok))
    {
      issues.push(
        issue(
          "error",
          "rollback_incomplete",
          "one or more newly created entities could not be removed",
        ),
      );
    }
    return {
      ok: false,
      applied: false,
      dry_run: false,
      recipe,
      path: preview.path,
      diff: preview.diff,
      operations,
      rolled_back,
      issues,
    };
  }
}
