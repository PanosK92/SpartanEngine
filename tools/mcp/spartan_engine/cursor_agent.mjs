import fs from "node:fs/promises";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import {
  append_agent_memory,
  read_agent_memory,
  write_agent_memory,
} from "./agent_memory.mjs";
import {
  append_debug_log,
  read_debug_log,
} from "./debug_log.mjs";
import {
  build_construction_grammar,
  suggest_construction_grammars,
} from "./construction_grammar.mjs";
import {
  advanced_scene_tool_names,
  audit_scene_quality,
  infer_required_features,
  scene_quality_prompt_lines,
} from "./scene_quality.mjs";
import {
  audit_scene_layout,
} from "./scene_planning.mjs";
import {
  create_design_brief,
  infer_design_template,
  suggest_scene_plan,
} from "./design_intelligence.mjs";
import {
  scene_root_name_from_prompt,
} from "./intent_router.mjs";
import { get_project_root } from "./shared_codebase.mjs";
import {
  constrain_generated_resources,
  world_resource_directory,
} from "./world_resources.mjs";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const require_from_helper = createRequire(import.meta.url);
const { Agent, Cursor, CursorAgentError } = require_from_helper("@cursor/sdk");

const engine_tool_names = new Set([
  "spartan_status",
  "search_codebase",
  "read_source_file",
  "search_capabilities",
  "get_capability_details",
  "agent_memory_read",
  "agent_memory_append",
  "agent_memory_replace",
  "debug_log_read",
  "ping",
  "engine_status",
  "engine_set_mode",
  "context_snapshot",
  "camera_snapshot",
  "cvar_list",
  "cvar_get",
  "cvar_set",
  "console_read",
  "world_summary",
  "world_load",
  "world_save",
  "world_set_environment",
  "world_raycast",
  "entity_list",
  "entity_find",
  "entity_resolve",
  "entity_get",
  "selection_get",
  "entity_create_empty",
  "entity_create_light",
  "entity_create_light_batch",
  "lights_calibrate",
  "world_landmarks",
  "spline_create_road",
  "spline_set_control_points",
  "spline_connect",
  "spline_reroute",
  "spline_junction",
  "spline_decorate",
  "district_blockout",
  "city_blockout",
  "entity_create_primitive",
  "entity_create_primitive_batch",
  "entity_update",
  "entity_delete",
  "entity_delete_children",
  "entity_clone",
  "entity_select",
  "entity_set_transform",
  "entity_render_materials",
  "component_types",
  "primitive_types",
  "entity_add_component",
  "entity_remove_component",
  "component_get",
  "component_set",
  "execute_lua",
]);
const scene_mutating_tool_names = new Set([
  "entity_create_empty",
  "entity_create_light",
  "entity_create_light_batch",
  "entity_create_primitive",
  "entity_create_primitive_batch",
  "entity_update",
  "entity_delete",
  "entity_delete_children",
  "entity_clone",
  "entity_set_transform",
  "entity_set_transform_batch",
  "entity_add_component",
  "entity_remove_component",
  "component_set",
  "component_set_batch",
  "mesh_generate",
  "mesh_generate_batch",
  "render_set_mesh",
  "mesh_physics_bind",
  "compound_create",
  "construction_grammar_create",
  "detail_pattern_create",
  "material_create",
  "material_semantic_create",
  "material_palette_create",
  "material_apply_preset",
  "entity_snap",
  "spline_create_road",
  "spline_set_control_points",
  "spline_connect",
  "spline_reroute",
  "spline_junction",
  "spline_decorate",
  "district_blockout",
  "city_blockout",
  "prefab_load",
  "world_set_environment",
  "execute_lua",
]);
for (const tool_name of advanced_scene_tool_names)
{
  engine_tool_names.add(tool_name);
}

let cached_agent = null;
let cached_agent_key = "";
let agent_run_queue = Promise.resolve();
let active_assistant_context = null;
let assistant_command_queue = Promise.resolve();

function native_material_properties(properties = {}) {
  const values = {
    ...properties,
  };
  const base_color =
    properties.base_color ??
    properties.color;
  if (Array.isArray(base_color))
  {
    values.color_r = base_color[0];
    values.color_g = base_color[1];
    values.color_b = base_color[2];
    values.color_a = base_color[3] ?? 1;
  }
  if (properties.metallic !== undefined)
  {
    values.metalness = properties.metallic;
  }
  delete values.base_color;
  delete values.color;
  delete values.metallic;
  delete values.emissive;
  delete values.emissive_intensity;
  return Object.entries(values).filter(
    ([, value]) => Number.isFinite(value),
  );
}

async function set_material_properties(
  run,
  path_value,
  properties,
) {
  const updated = [];
  for (
    const [property, value] of
      native_material_properties(properties)
  )
  {
    const result = await run.tool(
      "material_set_property",
      {
        path: path_value,
        property,
        value,
      },
    );
    if (!result.ok)
    {
      return {
        ...result,
        updated,
      };
    }
    updated.push(property);
  }
  return {
    ok: true,
    path: path_value,
    updated,
  };
}

async function create_material_palette(run, args) {
  const materials = Array.isArray(args.materials)
    ? args.materials
    : [];
  if (materials.length === 0)
  {
    return {
      ok: false,
      error: "materials must contain at least one material",
    };
  }

  const created = [];
  for (const material of materials)
  {
    const name = String(material.name ?? "").trim();
    if (!name)
    {
      return {
        ok: false,
        error: "every material requires a name",
        created,
      };
    }
    const material_path =
      material.path ??
      `materials/${name}.material`;
    const result = await run.tool(
      "material_create",
      {
        path: material_path,
        name,
      },
    );
    if (!result.ok)
    {
      return {
        ...result,
        created,
        failed_material: name,
      };
    }
    const configured = await set_material_properties(
      run,
      material_path,
      material,
    );
    if (!configured.ok)
    {
      return {
        ...configured,
        created,
        failed_material: name,
      };
    }
    created.push({
      name,
      path: material_path,
    });
  }
  return {
    ok: true,
    created,
    created_count: created.length,
  };
}

async function delete_entity_batch(run, args) {
  const ids = Array.isArray(args.ids)
    ? args.ids
    : [];
  const deleted = [];
  for (const id of ids)
  {
    const result = await run.tool(
      "entity_delete",
      { id },
    );
    if (!result.ok)
    {
      return {
        ...result,
        deleted,
        failed_id: id,
      };
    }
    deleted.push(String(id));
  }
  return {
    ok: true,
    deleted,
    deleted_count: deleted.length,
  };
}

async function calibrate_lights(run, args) {
  const found = await run.tool(
    "entity_find_by_component",
    {
      type: "light",
      limit: args.limit ?? 1000,
    },
  );
  if (!found.ok)
  {
    return found;
  }

  const updated = [];
  const parent_id =
    args.parent_id ??
    args.root_id;
  for (const entity of found.entities ?? [])
  {
    if (
      parent_id &&
      String(entity.id) !== String(parent_id)
    )
    {
      let ancestor_id = entity.parent_id;
      let belongs_to_parent = false;
      for (
        let depth = 0;
        ancestor_id && depth < 64;
        depth++
      )
      {
        if (String(ancestor_id) === String(parent_id))
        {
          belongs_to_parent = true;
          break;
        }
        const ancestor = await run.tool(
          "entity_get",
          { id: ancestor_id },
        );
        if (!ancestor.ok)
        {
          break;
        }
        ancestor_id =
          ancestor.entity?.parent_id;
      }
      if (!belongs_to_parent)
      {
        continue;
      }
    }
    const component = await run.tool(
      "component_get",
      {
        id: entity.id,
        type: "light",
      },
    );
    if (!component.ok)
    {
      continue;
    }
    const type =
      component.component?.properties?.light_type ??
      "point";
    const directional = type === "directional";
    const area = type === "area";
    const intensity = directional
      ? 120000
      : area
        ? 12000
        : 8500;
    const range = directional
      ? undefined
      : area
        ? 40
        : 35;
    const items = [
      ["intensity", intensity],
      ["shadows", true],
      ["draw_distance", directional ? 200 : 80],
      ["shadow_distance", directional ? 150 : 60],
    ];
    if (range !== undefined)
    {
      items.push(["range", range]);
    }
    const mapped = {
      id: entity.id,
      type: "light",
      count: items.length,
    };
    for (let index = 0; index < items.length; index++)
    {
      mapped[`property_${index}`] = items[index][0];
      mapped[`value_${index}`] = items[index][1];
    }
    const result = await run.tool(
      "component_set_batch",
      mapped,
    );
    if (result.ok)
    {
      updated.push(entity.id);
    }
  }
  return {
    ok: true,
    updated_count: updated.length,
    updated,
  };
}

async function wait_for_screenshot(file_path, wait_ms = 5000) {
  const deadline = Date.now() + wait_ms;
  while (Date.now() < deadline)
  {
    try
    {
      const stats = await fs.stat(file_path);
      if (stats.size > 0)
      {
        return true;
      }
    }
    catch
    {
    }
    await new Promise(
      (resolve) => setTimeout(resolve, 100),
    );
  }
  return false;
}

async function review_scene(run, args) {
  const id = args.id ?? args.root_id;
  const requested_views = Array.isArray(args.views)
    ? args.views
    : ["perspective", "top"];
  const view_aliases = {
    side: "right",
    driver_level: "perspective",
    interior: "perspective",
  };
  const views = [];
  for (const requested_view of requested_views.slice(0, 4))
  {
    const view =
      view_aliases[requested_view] ??
      requested_view;
    const camera = await run.tool(
      "viewport_frame",
      {
        id,
        view,
        padding: args.padding ?? 1.2,
      },
    );
    if (!camera.ok)
    {
      return {
        ...camera,
        views,
      };
    }
    await new Promise(
      (resolve) => setTimeout(resolve, 350),
    );
    const screenshot = await run.tool(
      "screenshot_take",
      {
        name:
          `scene_review_${requested_view}_${Date.now()}`,
        include_ui: false,
      },
    );
    if (screenshot.ok && screenshot.path)
    {
      screenshot.ready = await wait_for_screenshot(
        screenshot.path,
        args.wait_ms ?? 5000,
      );
    }
    views.push({
      view: requested_view,
      camera,
      screenshot,
    });
  }
  return {
    ok: views.every(
      (entry) =>
        entry.camera.ok &&
        entry.screenshot.ok &&
        entry.screenshot.ready,
    ),
    views,
  };
}

function generated_asset_name(value) {
  return String(value ?? "generated_mesh")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/^_+|_+$/g, "") ||
    "generated_mesh";
}

function map_batch_items(
  items,
  defaults = {},
  type_key = "primitive_type",
) {
  const mapped = {
    count: items.length,
  };
  for (let index = 0; index < items.length; index++)
  {
    const item = {
      ...defaults,
      ...items[index],
    };
    if (item.type && !item[type_key])
    {
      item[type_key] = item.type;
    }
    if (item.collision !== undefined)
    {
      item.with_physics = item.collision !== false;
    }
    delete item.type;
    delete item.collision;
    for (const [key, value] of Object.entries(item))
    {
      if (value !== undefined && value !== null)
      {
        mapped[`item_${index}_${key}`] = value;
      }
    }
  }
  return mapped;
}

async function bind_generated_mesh(run, args) {
  let mesh = args.mesh;
  if (!mesh)
  {
    const render = await run.tool(
      "component_get",
      {
        id: args.id,
        type: "render",
      },
    );
    mesh = render.component?.properties?.mesh;
  }
  if (!mesh)
  {
    return {
      ok: false,
      error: "mesh is required when the entity has no render mesh",
    };
  }

  const render = await run.tool(
    "render_set_mesh",
    {
      id: args.id,
      mesh,
      material: args.material,
    },
  );
  if (!render.ok)
  {
    return render;
  }
  if (!render.entity?.components?.includes("physics"))
  {
    const added = await run.tool(
      "entity_add_component",
      {
        id: args.id,
        type: "physics",
      },
    );
    if (!added.ok)
    {
      return added;
    }
  }
  const body_type =
    args.body_type ??
    (
      args.mode === "mesh" ?
        "mesh" :
        "mesh_convex"
    );
  const properties = [
    ["body_type", body_type],
    ["static", args.static ?? true],
    ["kinematic", args.kinematic ?? false],
    ["mass", args.mass ?? 1],
    ["friction", args.friction ?? 0.5],
    ["restitution", args.restitution ?? 0],
  ];
  const mapped = {
    id: args.id,
    type: "physics",
    count: properties.length,
  };
  for (let index = 0; index < properties.length; index++)
  {
    mapped[`property_${index}`] =
      properties[index][0];
    mapped[`value_${index}`] =
      properties[index][1];
  }
  const physics = await run.tool(
    "component_set_batch",
    mapped,
  );
  return {
    ...physics,
    render,
  };
}

function normalized_mesh_arguments(args) {
  const known_shapes = new Set([
    "beveled_box",
    "rounded_box",
    "wedge",
    "wall_opening",
    "wall_openings",
    "extruded_profile",
    "revolved_profile",
    "torus",
    "capsule",
    "rounded_cylinder",
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
  const nested =
    (
      typeof args.shape === "object" ?
        args.shape :
        null
    ) ??
    (
      typeof args.generator === "object" ?
        args.generator :
        null
    ) ??
    args.geometry ??
    (
      typeof args.mesh === "object" ?
        args.mesh :
        null
    ) ??
    {};
  const shape =
    (
      typeof args.shape === "string" ?
        args.shape :
        null
    ) ??
    (
      typeof args.generator === "string" ?
        args.generator :
        null
    ) ??
    nested.shape ??
    nested.type ??
    (
      known_shapes.has(args.path) ?
        args.path :
        null
    );
  const name = generated_asset_name(
    args.name ??
    shape,
  );
  const normalized = {
    ...nested,
    ...args,
    shape,
    path:
      (
        known_shapes.has(args.path) ?
          null :
          args.path
      ) ??
      args.mesh_path ??
      `meshes/${name}.mesh`,
  };
  if (
    shape === "rounded_box" &&
    Array.isArray(normalized.size) &&
    normalized.size.length === 3
  )
  {
    const radius_limit =
      Math.min(...normalized.size) * 0.49;
    if (
      Number.isFinite(normalized.radius) &&
      radius_limit > 0
    )
    {
      normalized.radius = Math.min(
        Math.max(normalized.radius, 0.0005),
        radius_limit,
      );
    }
  }
  return normalized;
}

async function generate_mesh(run, args) {
  const normalized =
    normalized_mesh_arguments(args);
  if (!normalized.shape)
  {
    return {
      ok: true,
      introspection: true,
      required: ["shape"],
      optional: [
        "path",
        "name",
        "parent_id",
        "position",
        "size",
        "material",
      ],
      example: {
        shape: "beveled_box",
        size: [1, 1, 1],
        bevel: 0.1,
      },
    };
  }
  const generated = await run.tool(
    "mesh_generate",
    normalized,
  );
  if (!generated.ok)
  {
    return generated;
  }

  const should_create_entity =
    Boolean(args.name) &&
    (
      Boolean(args.parent_id) ||
      Boolean(args.position) ||
      Boolean(args.material)
    );
  if (!should_create_entity)
  {
    return generated;
  }
  const created = await run.tool(
    "entity_create_empty",
    {
      name: args.name,
      parent_id: args.parent_id,
    },
  );
  if (!created.ok)
  {
    return {
      ...created,
      generated,
    };
  }
  if (
    args.position ||
    args.rotation_euler ||
    args.scale
  )
  {
    const transformed = await run.tool(
      "entity_set_transform",
      {
        id: created.entity.id,
        position: args.position,
        rotation_euler: args.rotation_euler,
        scale: args.scale,
      },
    );
    if (!transformed.ok)
    {
      return {
        ...transformed,
        generated,
        entity: created.entity,
      };
    }
  }
  const bound = await bind_generated_mesh(
    run,
    {
      id: created.entity.id,
      mesh:
        generated.resource?.path ??
        normalized.path,
      material: args.material,
      body_type: args.body_type,
      static: args.static,
      kinematic: args.kinematic,
      mass: args.mass,
      friction: args.friction,
      restitution: args.restitution,
    },
  );
  return {
    ...bound,
    generated,
    entity: created.entity,
  };
}

async function generate_mesh_batch(run, args)
{
  const items = Array.isArray(args.items)
    ? args.items
    : [];
  if (items.length === 0)
  {
    return {
      ok: true,
      introspection: true,
      required: ["items"],
      note: "items must contain one to 32 mesh descriptions",
    };
  }
  if (items.length > 32)
  {
    return {
      ok: false,
      error: "mesh batch cannot exceed 32 items",
    };
  }

  const generated = [];
  for (let index = 0; index < items.length; index++)
  {
    const result = await generate_mesh(
      run,
      {
        parent_id: args.parent_id,
        ...items[index],
      },
    );
    generated.push(result);
    if (!result.ok)
    {
      return {
        ok: false,
        generated,
        generated_count: index,
        failed_index: index,
        error:
          result.error ??
          "mesh batch item failed",
      };
    }
  }
  return {
    ok: true,
    generated,
    generated_count: generated.length,
  };
}

async function create_compound(run, args) {
  const parts = Array.isArray(args.parts)
    ? args.parts
    : [];
  if (!args.name || parts.length === 0)
  {
    return {
      ok: true,
      introspection: true,
      required: [
        "name",
        "parts",
      ],
      note:
        "each part requires exactly one of mesh or shape",
    };
  }
  const root = await run.tool(
    "entity_create_empty",
    {
      name: args.name,
      parent_id: args.parent_id,
    },
  );
  if (!root.ok)
  {
    return root;
  }
  if (
    args.position ||
    args.rotation_euler ||
    args.scale
  )
  {
    const transformed = await run.tool(
      "entity_set_transform",
      {
        id: root.entity.id,
        position: args.position,
        rotation_euler: args.rotation_euler,
        scale: args.scale,
      },
    );
    if (!transformed.ok)
    {
      return transformed;
    }
  }

  const completed_parts = [];
  for (let index = 0; index < parts.length; index++)
  {
    const part = parts[index];
    let result;
    if (part.shape)
    {
      result = await generate_mesh(
        run,
        {
          ...part,
          name:
            part.name ??
            `${args.name}_${index}`,
          parent_id: root.entity.id,
          path:
            part.mesh_path ??
            `${args.asset_directory ?? "meshes"}/${generated_asset_name(args.name)}_${index}.mesh`,
        },
      );
    }
    else
    {
      const child = await run.tool(
        "entity_create_empty",
        {
          name:
            part.name ??
            `${args.name}_${index}`,
          parent_id: root.entity.id,
        },
      );
      if (!child.ok)
      {
        return child;
      }
      if (
        part.position ||
        part.rotation_euler ||
        part.scale
      )
      {
        await run.tool(
          "entity_set_transform",
          {
            id: child.entity.id,
            position: part.position,
            rotation_euler: part.rotation_euler,
            scale: part.scale,
          },
        );
      }
      result = await bind_generated_mesh(
        run,
        {
          ...part,
          id: child.entity.id,
        },
      );
    }
    if (!result.ok)
    {
      return {
        ...result,
        root: root.entity,
        completed_parts,
        failed_index: index,
      };
    }
    completed_parts.push(result);
  }
  return {
    ok: true,
    root: root.entity,
    completed_parts,
    completed_count: completed_parts.length,
  };
}

async function create_construction_grammar(run, args)
{
  const materials =
    (
      args.materials &&
      typeof args.materials === "object"
    )
      ? args.materials
      : {};
  const grammar_args = {
    ...args,
    primary_material:
      args.primary_material ??
      materials.structure ??
      materials.frame ??
      materials.wall ??
      materials.floor,
    secondary_material:
      args.secondary_material ??
      materials.wall ??
      materials.floor ??
      materials.roof,
    accent_material:
      args.accent_material ??
      materials.accent,
    glass_material:
      args.glass_material ??
      materials.glass,
    emissive_material:
      args.emissive_material ??
      materials.emissive,
  };
  let grammar;
  try
  {
    grammar = build_construction_grammar(
      grammar_args,
    );
  }
  catch (error)
  {
    return {
      ok: false,
      code: "invalid_arguments",
      error: error.message,
    };
  }

  const compound = await create_compound(
    run,
    {
      ...grammar_args,
      parts: grammar.parts,
    },
  );
  if (!compound.ok)
  {
    return {
      ...compound,
      grammar: grammar.metadata,
    };
  }
  if (
    compound.root?.id &&
    Array.isArray(args.tags) &&
    args.tags.length > 0
  )
  {
    await run.tool(
      "entity_update",
      {
        id: compound.root.id,
        tags: args.tags,
        tags_mode: "merge",
      },
    );
  }
  let snap = null;
  if (compound.root?.id && args.snap_mode)
  {
    snap = await run.tool(
      "entity_snap",
      {
        id: compound.root.id,
        mode: args.snap_mode,
        target: args.snap_target,
        offset: args.snap_offset,
        align_to_surface:
          args.align_to_surface,
      },
    );
    if (!snap.ok)
    {
      return {
        ...snap,
        root: compound.root,
        completed_parts:
          compound.completed_parts,
        completed_count:
          compound.completed_count,
        grammar: grammar.metadata,
      };
    }
  }
  return {
    ...compound,
    grammar: grammar.metadata,
    snap,
  };
}

async function dispatch_assistant_command(
  context,
  command,
  args,
) {
  const run = context.run;
  if (
    args.arguments &&
    typeof args.arguments === "object" &&
    !Array.isArray(args.arguments)
  )
  {
    args = {
      ...args,
      ...args.arguments,
    };
    delete args.arguments;
  }
  args = constrain_generated_resources(
    command,
    args,
    context.resource_directory ??
      world_resource_directory(),
  );
  if (command === "agent_memory_read")
  {
    return {
      ok: true,
      text: await read_agent_memory(),
    };
  }
  if (command === "agent_memory_append")
  {
    return {
      ok: true,
      memory: await append_agent_memory(
        args.section,
        args.note ?? args.text,
      ),
    };
  }
  if (command === "agent_memory_replace")
  {
    return {
      ok: true,
      memory: await write_agent_memory(
        args.memory ?? args.text,
      ),
    };
  }
  if (command === "spartan_status")
  {
    const ping = await run.tool(
      "ping",
      {},
    );
    const engine = ping.ok
      ? await run.tool(
        "engine_status",
        {},
      )
      : null;
    return {
      ok: ping.ok && Boolean(engine?.ok),
      ping,
      engine,
    };
  }
  if (command === "search_capabilities")
  {
    const terms = String(args.query ?? "")
      .toLowerCase()
      .split(/[^a-z0-9_]+/g)
      .filter(Boolean);
    const tools = [...engine_tool_names]
      .filter((name) =>
        terms.length === 0 ||
        terms.some((term) => name.includes(term)),
      )
      .slice(0, args.limit ?? 25);
    return {
      ok: true,
      tools,
    };
  }
  if (command === "get_capability_details")
  {
    const tool =
      args.tool ??
      args.name;
    const details = {
      scene_plan_create: {
        required: ["plan"],
        note: "plan requires root_name, purpose, scale_reference, zones, elements, relationships, lighting, and quality_goals",
      },
      material_palette_create: {
        required: ["materials"],
        note: "each material requires name and can include base_color, roughness, and metallic",
      },
      construction_grammar_suggest: {
        required: ["purpose"],
        optional: ["limit"],
      },
      entity_create_primitive_batch: {
        required: ["items"],
        note: "each item uses primitive_type or type, name, transform, material, and parent_id",
      },
      mesh_generate: {
        required: ["shape", "path"],
        note: "name and placement fields also create and bind an entity",
      },
      compound_create: {
        required: ["name", "parts"],
      },
    };
    return {
      ok: Boolean(details[tool]),
      tool,
      details:
        details[tool] ??
        {
          note:
            "pass the documented native command arguments directly",
        },
    };
  }
  if (command === "mesh_geometry_capabilities")
  {
    return {
      ok: true,
      generators: [
        "beveled_box",
        "rounded_box",
        "wedge",
        "wall_opening",
        "wall_openings",
        "extruded_profile",
        "revolved_profile",
        "torus",
        "capsule",
        "rounded_cylinder",
        "pipe",
        "curved_profile",
        "loft",
        "arch",
        "inset_panel",
        "tapered_extrusion",
        "grid",
        "grass_blade",
        "flower",
      ],
      modifiers: [
        "taper",
        "bend",
        "mirror",
        "shell",
        "linear_array",
        "radial_array",
      ],
      openings: {
        available: true,
        generators: [
          "wall_opening",
          "wall_openings",
        ],
      },
      profiles: {
        concave_extrusion: true,
        variable_loft: true,
        variable_sweep_scale: true,
        variable_sweep_twist: true,
      },
      collision: {
        generated_mesh: true,
        generated_convex: true,
        tool: "mesh_physics_bind",
      },
      booleans: {
        union: {
          available: false,
          alternative: "compound_create",
        },
        subtract: {
          available: false,
          alternative: "wall_opening",
        },
        intersect: {
          available: false,
        },
      },
    };
  }
  if (command === "scene_plan_suggest")
  {
    const brief = create_design_brief(
      args.request ?? args.prompt ?? "",
      {
        ...args,
        root_name: args.root_name,
      },
    );
    const plan = suggest_scene_plan(brief);
    return {
      ok: plan.ok !== false,
      brief,
      plan,
    };
  }
  if (command === "construction_grammar_suggest")
  {
    return {
      ok: true,
      suggestions: suggest_construction_grammars(
        args.purpose,
        args.limit ?? 5,
      ),
    };
  }
  if (command === "construction_grammar_create")
  {
    return create_construction_grammar(
      run,
      args,
    );
  }
  if (command === "material_palette_create")
  {
    return create_material_palette(run, args);
  }
  if (
    command === "material_set" ||
    command === "material_update" ||
    command === "material_configure" ||
    command === "material_set_properties"
  )
  {
    return set_material_properties(
      run,
      args.path ?? args.name,
      args.properties ?? args,
    );
  }
  if (command === "entity_delete_batch")
  {
    return delete_entity_batch(run, args);
  }
  if (
    command === "entity_tag_add" ||
    command === "entity_add_tag"
  )
  {
    return run.tool(
      "entity_update",
      {
        id: args.id,
        tags: [args.tag],
        tags_mode: "merge",
      },
    );
  }
  if (
    command === "entity_tags_set" ||
    command === "entity_set_tags" ||
    command === "semantic_tag_set"
  )
  {
    return run.tool(
      "entity_update",
      {
        id: args.id,
        tags: args.tags ?? [],
        tags_mode: "replace",
      },
    );
  }
  if (command === "entity_create_primitive_batch")
  {
    const items = Array.isArray(args.items)
      ? args.items
      : [];
    if (items.length === 0)
    {
      return {
        ok: true,
        introspection: true,
        required: ["items"],
        note:
          "items must contain one to 64 primitive descriptions",
      };
    }
    return run.tool(
      command,
      map_batch_items(
        items,
        {
          parent_id: args.parent_id,
        },
      ),
      60000,
    );
  }
  if (command === "entity_create_light_batch")
  {
    const items = Array.isArray(args.items)
      ? args.items
      : [];
    if (items.length === 0)
    {
      return {
        ok: true,
        introspection: true,
        required: ["items"],
        note:
          "items must contain one to 64 light descriptions",
      };
    }
    return run.tool(
      command,
      map_batch_items(
        items,
        {
          parent_id: args.parent_id,
        },
        "light_type",
      ),
      60000,
    );
  }
  if (
    command === "entity_create_primitive" &&
    (
      args.primitive_type === "mesh" ||
      args.type === "mesh"
    ) &&
    args.mesh
  )
  {
    const created = await run.tool(
      "entity_create_empty",
      {
        name: args.name,
        parent_id: args.parent_id,
      },
    );
    if (!created.ok)
    {
      return created;
    }
    if (
      args.position ||
      args.rotation_euler ||
      args.scale
    )
    {
      await run.tool(
        "entity_set_transform",
        {
          id: created.entity.id,
          position: args.position,
          rotation_euler: args.rotation_euler,
          scale: args.scale,
        },
      );
    }
    return bind_generated_mesh(
      run,
      {
        ...args,
        id: created.entity.id,
      },
    );
  }
  if (command === "entity_set_transform")
  {
    return run.tool(
      command,
      {
        ...args,
        id:
          args.id ??
          args.entity_id,
        position:
          args.position ??
          args.position_local,
        rotation_euler:
          args.rotation_euler ??
          args.rotation_local,
        scale:
          args.scale ??
          args.scale_local,
      },
      60000,
    );
  }
  if (command === "entity_select")
  {
    return run.tool(
      command,
      {
        ...args,
        id:
          args.id ??
          args.entity_id,
      },
      60000,
    );
  }
  if (command === "entity_create_primitive")
  {
    return run.tool(
      command,
      {
        ...args,
        primitive_type:
          args.primitive_type ??
          args.type,
        with_physics:
          args.with_physics ??
          (
            args.collision === undefined ?
              undefined :
              args.collision !== false
          ),
      },
      60000,
    );
  }
  if (command === "mesh_generate")
  {
    return generate_mesh(run, args);
  }
  if (command === "mesh_generate_batch")
  {
    return generate_mesh_batch(run, args);
  }
  if (command === "mesh_physics_bind")
  {
    return bind_generated_mesh(run, args);
  }
  if (command === "compound_create")
  {
    return create_compound(run, args);
  }
  if (command === "debug_log_read")
  {
    const text = await read_debug_log(
      args.limit ?? 80,
    );
    const filter = String(
      args.filter ?? "",
    ).toLowerCase();
    return {
      ok: true,
      text: filter
        ? text
          .split(/\r?\n/)
          .filter((line) =>
            line.toLowerCase().includes(filter),
          )
          .join("\n")
        : text,
    };
  }
  if (command === "lights_calibrate")
  {
    return calibrate_lights(run, args);
  }
  if (command === "scene_quality_audit")
  {
    return audit_scene_quality(
      (name, value) => run.tool(name, value),
      {
        ...args,
        id: args.id ?? args.root_id,
      },
    );
  }
  if (command === "scene_layout_audit")
  {
    const root_name =
      args.root_name ??
      context.intent?.target_name ??
      "";
    return audit_scene_layout(
      (name, value) => run.tool(name, value),
      {
        ...args,
        id: args.id ?? args.root_id,
        root_name,
        plan:
          args.plan ??
          context.prepared_plan ??
          null,
      },
    );
  }
  if (command === "scene_visual_review")
  {
    return review_scene(run, args);
  }
  if (command === "screenshot_take" && args.path)
  {
    const requested_path =
      String(args.path).replaceAll("\\", "/");
    const file_name =
      path.posix.basename(requested_path);
    return run.tool(
      command,
      {
        ...args,
        path:
          requested_path
            .toLowerCase()
            .startsWith("screenshots/")
            ? requested_path
            : `screenshots/${file_name}`,
      },
      60000,
    );
  }
  if (command === "viewport_frame")
  {
    const view_aliases = {
      side: "right",
      driver_height: "perspective",
      driver_level: "perspective",
      interior: "perspective",
    };
    const target_id =
      args.id ??
      args.entity_id;
    if (target_id)
    {
      const selected = await run.tool(
        "entity_select",
        { id: target_id },
        60000,
      );
      if (!selected.ok)
      {
        return selected;
      }
    }
    return run.tool(
      command,
      {
        ...args,
        id: undefined,
        entity_id: undefined,
        view:
          view_aliases[args.view] ??
          args.view,
      },
      60000,
    );
  }
  return run.tool(
    command,
    args,
    60000,
  );
}

const spartan_engine_command_tool = {
  description: [
    "Execute one Spartan Engine command against the live editor.",
    "This bridge supports native commands and composite helpers.",
    "Use this as the primary tool for all scene reads and edits.",
    "Input command examples include context_snapshot, entity_find, entity_create_empty, entity_create_primitive_batch, entity_create_light_batch, mesh_generate, material_create, component_set_batch, screenshot_take, and viewport_frame.",
  ].join(" "),
  inputSchema: {
    type: "object",
    properties: {
      command: {
        type: "string",
      },
      arguments: {
        type: "object",
        additionalProperties: true,
      },
    },
    required: [
      "command",
    ],
    additionalProperties: false,
  },
  execute: async (args) => {
    if (!active_assistant_context)
    {
      return {
        ok: false,
        error: "no active Spartan assistant run",
      };
    }
    const command = String(args.command ?? "").trim();
    if (!command)
    {
      return {
        ok: false,
        error: "command is required",
      };
    }
    const command_arguments =
      args.arguments &&
      typeof args.arguments === "object" &&
      !Array.isArray(args.arguments)
        ? args.arguments
        : {};
    const assistant_context =
      active_assistant_context;
    const previous_command =
      assistant_command_queue;
    let release_command;
    assistant_command_queue = new Promise(
      (resolve) =>
      {
        release_command = resolve;
      },
    );
    await previous_command;
    try
    {
      return await dispatch_assistant_command(
        assistant_context,
        command,
        command_arguments,
      );
    }
    finally
    {
      release_command();
    }
  },
};

export async function list_models(api_key) {
  if (!api_key) {
    return {
      ok: false,
      text: "Cursor API key is missing.",
    };
  }

  try {
    const models = await Cursor.models.list({ apiKey: api_key });
    const lines = ["auto\tAuto"];
    for (const model of models) {
      lines.push(`${model.id}\t${model.displayName ?? model.id}`);
    }

    return { ok: true, text: lines.join("\n") };
  } catch (error) {
    return { ok: false, text: `Cursor model list failed: ${error.message}` };
  }
}

export async function dispose_cached_agent() {
  const agent = cached_agent;
  cached_agent = null;
  cached_agent_key = "";
  if (!agent) {
    return;
  }

  if (agent?.[Symbol.asyncDispose]) {
    await agent[Symbol.asyncDispose]();
  } else if (agent?.close) {
    await agent.close();
  }
}

function agent_key(api_key, model_id, engine_host, engine_port) {
  return JSON.stringify({ api_key, model_id, engine_host, engine_port });
}

async function get_agent({ api_key, model_id, engine_host, engine_port, run }) {
  const key = agent_key(api_key, model_id, engine_host, engine_port);
  if (cached_agent && cached_agent_key === key) {
    return cached_agent;
  }

  await dispose_cached_agent();
  run.event("stage_note", { text: "starting cursor agent" });
  cached_agent = await Agent.create({
    apiKey: api_key,
    model: { id: model_id },
    mode: "agent",
    local: {
      cwd: __dirname,
      settingSources: [],
      customTools: {
        spartan_engine_command:
          spartan_engine_command_tool,
      },
    },
  });
  cached_agent_key = key;
  return cached_agent;
}

function compact_text(text, max_length = 1800) {
  const value = String(text ?? "").trim();
  if (value.length <= max_length) {
    return value;
  }

  return `${value.slice(0, max_length).trimEnd()}\n...`;
}

function compact_line(text, max_length = 420) {
  return compact_text(String(text ?? "").replace(/\s+/g, " "), max_length);
}

function safe_json(value, max_length = 1200) {
  try {
    return compact_text(JSON.stringify(value), max_length);
  } catch {
    return "";
  }
}

function text_from_value(value, seen = new Set(), depth = 0) {
  if (value === undefined || value === null || depth > 5) {
    return "";
  }

  if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
    return String(value).trim();
  }

  if (typeof value !== "object") {
    return "";
  }

  if (seen.has(value)) {
    return "";
  }
  seen.add(value);

  const fields = [
    "message",
    "errorMessage",
    "error",
    "details",
    "detail",
    "reason",
    "code",
    "cause",
    "description",
    "result",
    "text",
    "status",
    "stderr",
    "stdout",
  ];
  const parts = [];
  for (const field of fields) {
    if (Object.prototype.hasOwnProperty.call(value, field)) {
      const text = text_from_value(value[field], seen, depth + 1);
      if (text && !parts.includes(text)) {
        parts.push(text);
      }
    }
  }

  if (Array.isArray(value)) {
    for (const entry of value) {
      const text = text_from_value(entry, seen, depth + 1);
      if (text && !parts.includes(text)) {
        parts.push(text);
      }
    }
  }

  if (parts.length > 0) {
    return parts.join("\n");
  }

  try {
    return JSON.stringify(value);
  } catch {
    return "";
  }
}

async function run_failure_message(run, result) {
  const id = result?.id ?? run?.id ?? "unknown";
  const details = [];
  const result_text = text_from_value(result);
  if (result_text && result_text !== id && result_text !== "error") {
    details.push(result_text);
  }

  const raw_result = safe_json(result);
  let latest_message = "";
  if (run?.supports?.("conversation")) {
    try {
      const conversation = await run.conversation();
      for (let i = conversation.length - 1; i >= 0; i--) {
        const text = text_from_value(conversation[i]);
        if (text.toLowerCase().includes("error") || text.toLowerCase().includes("failed")) {
          details.push(text);
          break;
        }
        if (!latest_message && text) {
          latest_message = text;
        }
      }

      if (!details.length && latest_message) {
        details.push(`Last Cursor message: ${latest_message}`);
      }
    } catch (error) {
      const detail = text_from_value(error);
      if (detail) {
        details.push(`Could not read run details: ${detail}`);
      }
    }
  }

  if (!details.length && raw_result) {
    details.push(`Raw result: ${raw_result}`);
  }

  if (!details.length) {
    details.push("No failure detail was returned by the Cursor SDK. This is usually a model, MCP server startup, or tool schema failure.");
  }

  const first_line = compact_line(`Cursor run failed: ${id}. ${details[0]}`);
  const extra = details.slice(1).map((detail) => compact_text(detail)).join("\n\n");
  return extra ? `${first_line}\n\n${extra}` : first_line;
}

function activity_text_from_value(value, seen = new Set(), depth = 0) {
  if (value === undefined || value === null || depth > 5) {
    return "";
  }

  if (typeof value === "string") {
    return compact_text(value.replace(/\s+/g, " "), 220);
  }

  if (typeof value !== "object" || seen.has(value)) {
    return "";
  }
  seen.add(value);

  if (Array.isArray(value)) {
    for (const entry of value) {
      const text = activity_text_from_value(entry, seen, depth + 1);
      if (text) {
        return text;
      }
    }
    return "";
  }

  for (const field of ["text", "content", "message", "summary", "title", "name"]) {
    if (Object.prototype.hasOwnProperty.call(value, field)) {
      const text = activity_text_from_value(value[field], seen, depth + 1);
      if (text) {
        return text;
      }
    }
  }

  return "";
}

function tool_name_from_event(event) {
  if (!event || typeof event !== "object") {
    return "";
  }

  return (
    event.name ??
    event.toolName ??
    event.tool_name ??
    event.command ??
    event.message?.args?.toolName ??
    event.message?.args?.command ??
    event.message?.type ??
    ""
  );
}

function is_generic_activity(text) {
  const value = String(text ?? "").toLowerCase().replace(/\s+/g, " ").trim();
  return (
    value === "" ||
    value === "thinking" ||
    value === "writing" ||
    value === "using mcp" ||
    value === "using tool" ||
    value === "tool call" ||
    value === "callmcptool" ||
    value.includes("thinking") && value.length < 32 ||
    value.includes("writing") && value.length < 32
  );
}

function friendly_tool_status(name) {
  const value = String(name ?? "").replaceAll("-", "_").toLowerCase();
  if (!value || value === "callmcptool" || value === "tool" || value === "mcp") {
    return "using Spartan engine tools";
  }

  if (engine_tool_names.has(value)) {
    return `using ${value}`;
  }

  return `using ${String(name).replaceAll("_", " ")}`;
}

function activity_from_event(event) {
  if (!event || typeof event !== "object") {
    return "";
  }

  if (event.type === "thinking") {
    const text = activity_text_from_value(event.text ?? event);
    return text && !is_generic_activity(text) ? `Thinking: ${text}` : "";
  }

  if (event.type === "assistant" || event.type === "assistantMessage") {
    const text = activity_text_from_value(event);
    if (!text || is_generic_activity(text)) {
      return "";
    }

    return text.toLowerCase().startsWith("progress:") ? text.replace(/^progress:\s*/i, "") : `Cursor: ${text}`;
  }

  const name = tool_name_from_event(event);
  if (name) {
    return friendly_tool_status(name);
  }

  if (event.type) {
    const text = `Cursor ${String(event.type).replaceAll("_", " ")}`;
    return is_generic_activity(text) ? "" : text;
  }

  return "";
}

function value_contains(value, predicate, seen = new Set(), depth = 0) {
  if (value === undefined || value === null || depth > 6) {
    return false;
  }

  if (typeof value === "string") {
    return predicate(value);
  }

  if (typeof value !== "object" || seen.has(value)) {
    return false;
  }
  seen.add(value);

  if (Array.isArray(value)) {
    return value.some((entry) => value_contains(entry, predicate, seen, depth + 1));
  }

  for (const [key, entry] of Object.entries(value)) {
    if (predicate(key) || value_contains(entry, predicate, seen, depth + 1)) {
      return true;
    }
  }

  return false;
}

function object_contains(
  value,
  predicate,
  seen = new Set(),
  depth = 0,
) {
  if (
    value === undefined ||
    value === null ||
    typeof value !== "object" ||
    seen.has(value) ||
    depth > 8
  )
  {
    return false;
  }
  seen.add(value);
  if (predicate(value))
  {
    return true;
  }
  if (Array.isArray(value))
  {
    return value.some((entry) =>
      object_contains(
        entry,
        predicate,
        seen,
        depth + 1,
      ),
    );
  }
  return Object.values(value).some((entry) =>
    object_contains(
      entry,
      predicate,
      seen,
      depth + 1,
    ),
  );
}

function is_engine_tool_event(value) {
  return value_contains(value, (text) => {
    const normalized = text.toLowerCase().replaceAll("-", "_");
    return normalized.includes("spartan_engine") || engine_tool_names.has(normalized);
  });
}

function is_tool_event(value) {
  return value_contains(value, (text) => {
    const normalized = text.toLowerCase();
    return normalized === "toolcall" || normalized === "tool_call" || normalized === "mcp" || normalized.includes("callmcptool");
  });
}

function is_named_tool_event(value, tool_name) {
  if (!is_tool_event(value))
  {
    return false;
  }

  return value_contains(value, (text) =>
    text.toLowerCase().replaceAll("-", "_").includes(
      tool_name,
    ),
  );
}

function is_scene_mutation_event(value)
{
  if (!is_tool_event(value))
  {
    return false;
  }
  return value_contains(value, (text) => {
    const normalized = text
      .toLowerCase()
      .replaceAll("-", "_");
    return [...scene_mutating_tool_names].some(
      (tool_name) => normalized.includes(tool_name),
    );
  });
}

function build_prompt(
  prompt,
  snapshot,
  intent = null,
  prepared_plan = null,
) {
  const lines = [
    "You are controlling Spartan Engine through the spartan_engine MCP tools.",
    "Use the spartan_engine_command custom tool as the primary live-engine bridge. Pass the native tool name in command and its arguments object in arguments.",
    "The spartan_engine_command tool handles both native and composite scene commands. Do not use shell commands or source-code tools for live scene work.",
    "Read agent_memory_read early when available, and treat it as project advice rather than absolute truth.",
    "For engine-control requests, use Spartan MCP tools first and group repetitive calls without sacrificing completeness or visual quality.",
    "For source-code questions, use search_codebase first, then read_source_file for focused line ranges.",
    "Use search_capabilities and get_capability_details when you are unsure which engine tool or resource to use.",
    "Use spartan_status when you need to know whether the MCP bridge, engine, or codebase index is ready.",
    "Use debug_log_read when diagnosing what commands the assistant sent to the engine and what came back.",
    "Use context_snapshot and entity_resolve instead of multiple separate read calls.",
    "For every new build, design directly from the current request and prepared context. Do not search for stored build definitions or prior generated instructions.",
    "Use camera_snapshot before camera-relative placement such as in front of camera, beside camera, or from camera.",
    "Use world_raycast for ground or surface-relative placement instead of assuming y=0 when precision matters.",
    "Before deleting or rebuilding existing geometry while preserving look, call entity_render_materials on the target parent and reuse material names in entity_create_primitive_batch or component_set.",
    "Use mesh_geometry_capabilities before deciding that requested procedural geometry is unavailable.",
    "Prefer concave extruded profiles, multi-opening walls, variable lofts or sweeps, shell thickness, and seam-split box UVs when they express the design better than stacked boxes.",
    "For multiple materials, split semantic surfaces into compound parts because one render entity owns one material.",
    "Every created renderable must have static collision. Use mesh_convex for generated or imported meshes and the matching box, sphere, capsule, or plane collider for standard primitives. Never leave collision coverage partial.",
    "Use entity_create_light for every light. Never hand-roll lights with entity_create_empty + entity_add_component light + component_set; that path leaves weak invisible lights.",
    "entity_create_light fully initializes the light: intensity is lux for directional and lumens otherwise. Visible blockout defaults are point/spot 8500, area 12000, directional 120000, plus range, angle, area size, shadows, and draw/shadow distances.",
    "Do not pass tiny intensities like 25-100 for blockout lights. If you omit intensity, the tool calibrates it. Only set calibrated false when you intentionally want a dim light.",
    "To calibrate existing scene lights, call lights_calibrate once. Do not write execute_lua or dozens of component_set calls for that.",
    "For city development: massing first, roads second. Use city_blockout / district_blockout for districts; never hand-place hundreds of cubes for a city.",
    "district_blockout presets: market, downtown/skyscrapers, park, industrial, residential, parking, plaza, gas_station. city_blockout lays several districts with corridor gaps and avoid_existing landmarks.",
    "Architect rules: leave corridors between districts for arterials; do not stamp on runway/existing landmarks; vary density by preset.",
    "Road pass after massing: world_landmarks -> arterial that skirts large districts -> spur branches to district edges -> spline_junction -> spline_decorate. Never triangle center-to-center through an airway.",
    "To fix an existing road that cuts through buildings or other roads, call spline_reroute on it. It skirts obstacles and redistributes lights/cameras/props along the new path without deleting them.",
    "Never drive through an airway/runway, dockyard footprint, or building mass. Approach district edges, not centers. Use via points when an arterial must go around a district.",
    "spline_decorate adds sidewalks, street lights, and roadside props. Never stop at bare undecorated lines.",
    "Never hand-build spline_point_* children. Do not search source code for city prompts. Do not invent Lua APIs.",
    "Use primitive-only single-area construction only when the user explicitly asks for a greybox. Normal environments require semantic planning, generated or compound geometry, materials, calibrated lighting, and correction audits.",
    "Do not use execute_lua for API discovery, pairs/next probing, method listing, or exploratory scripts. Those crash or hang the engine.",
    "Prefer entity_create_primitive_batch over execute_lua for repeated primitives. Use execute_lua only when a native batch tool cannot express the edit, and then only with one focused script that uses known bindings.",
    "Known Lua facts if you must use it: World.CreateEntity, World.GetEntityByName, World.GetEntityById(id_string), entity:SetParent, entity:AddComponent(ComponentType.Renderable|Light|...), Renderable:SetMesh(MeshType.Cube), Light:SetLightType(LightType.Point), never pairs() on World.GetEntities or GetChildren, use ForEachChild instead.",
    "When you learn a durable lesson, correction, recurring problem, or maintainer improvement idea, update agent memory concisely.",
    "world_resources_clean is available for explicit cleanup receipts. Finished scene construction runs it automatically.",
    "Do not reveal hidden chain of thought. Report only brief progress, blockers, and final results.",
  ];

  if (intent?.kind === "scene_rebuild" || intent?.live_scene_action)
  {
    lines.push(...scene_quality_prompt_lines(prompt, intent));
    lines.push(
      "Work through these internal stages in order and finish each stage before moving on:",
      "1 layout, establish zones, circulation, entrances, service access, and primary spatial hierarchy",
      "2 structure, create supported architectural massing and functional boundaries at credible metric scale",
      "3 function, add the objects and clearances that explain how the requested place is actually used",
      "4 finish, replace primitive-looking silhouettes where useful, apply coordinated materials, details, wear, and calibrated lighting",
      "5 verify, run layout and quality audits, inspect deliberate views, make targeted corrections, and recheck",
      "Do not create decorative clutter until circulation, structure, and functional placement are coherent.",
      "Do not use identical geometry for every repeated object. Reuse meshes where appropriate, but vary transforms, grouped details, or material accents without breaking function.",
    );
    if (prepared_plan?.plan)
    {
      lines.push(
        "A validated internal baseline plan has already been prepared. Before geometry, expand or revise it with request-specific functions, zones, relationships, and details that the baseline template missed, then keep the resulting plan as the spatial contract.",
        `Prepared plan: ${safe_json(prepared_plan.plan, 7000)}`,
      );
    }
  }

  if (intent?.target_name)
  {
    lines.push(`Resolved parent entity name from the request: ${intent.target_name}. Call entity_find with exact matching first. If several entities share the name, use the first root-level match by id and never call entity_resolve by ambiguous name. Create the root with entity_create_empty only when no exact match exists, then parent all planned environment content under that entity id.`);
  }
  if (intent?.kind === "city_develop")
  {
    lines.push("This is a city-planning request. If the user wants districts/areas/blockout, use city_blockout or district_blockout first. If they want roads, plan an arterial that skirts large footprints and spur to edges — never a triangle through centers. Massing and roads can be separate passes.");
    if (Array.isArray(intent.landmarks) && intent.landmarks.length > 0)
    {
      lines.push(`Landmarks mentioned in the prompt: ${intent.landmarks.join(", ")}. Prefer these, but still scan world_landmarks and use their bounding boxes for edge approaches.`);
    }
  }
  if (intent?.kind === "scene_rebuild" || intent?.live_scene_action)
  {
    lines.push("This is a live scene construction request. Build a finished, visually reviewed scene under the requested parent. Do not search source code and do not invent Lua APIs.");
  }

  lines.push(
    "Engine state snapshot:",
    JSON.stringify(snapshot),
    "",
    "User request:",
    prompt,
  );
  return lines.join("\n");
}

function quality_root_from_matches(matches)
{
  const match_ids = new Set(
    matches.map((match) => String(match.id)),
  );
  const hierarchy_roots = matches.filter(
    (match) =>
      !match.parent_id ||
      String(match.parent_id) === "0" ||
      !match_ids.has(String(match.parent_id)),
  );
  return (
    hierarchy_roots.find((match) =>
      Array.isArray(match.tags) &&
      match.tags.some((tag) =>
          String(tag).startsWith("semantic_id="),
        ),
    ) ??
    hierarchy_roots[0] ??
    matches[0]
  );
}

async function resolve_quality_root(run, target_name)
{
  let last_result = {
    ok: false,
    error: "quality root not found",
  };
  for (let attempt = 0; attempt < 10; attempt++)
  {
    const exact = await run.tool(
      "entity_find",
      {
        name: target_name,
        match: "exact",
        limit: 100,
      },
    );
    const exact_root = quality_root_from_matches(
      exact.matches ?? [],
    );
    last_result = exact;
    if (exact.ok && exact_root)
    {
      return {
        ...exact,
        root: exact_root,
        resolution: "exact_name",
      };
    }
    if (attempt < 9)
    {
      await new Promise(
        (resolve) => setTimeout(resolve, 500),
      );
    }
  }

  const partial = await run.tool(
    "entity_find",
    {
      name: target_name,
      match: "contains",
      limit: 100,
    },
  );
  const partial_root = quality_root_from_matches(
    partial.matches ?? [],
  );
  if (partial.ok && partial_root)
  {
    return {
      ...partial,
      root: partial_root,
      resolution: "partial_name",
    };
  }
  return {
    ...last_result,
    root: null,
  };
}

async function resolve_selected_quality_root(run)
{
  const selection = await run.tool(
    "selection_get",
    {},
  );
  const selected_id = selection.selected_ids?.[0];
  if (!selection.ok || !selected_id)
  {
    return {
      ok: false,
      root: null,
      error: "quality root selection is empty",
    };
  }

  const entity = await run.tool(
    "entity_get",
    { id: selected_id },
  );
  if (!entity.ok || !entity.entity)
  {
    return {
      ...entity,
      root: null,
    };
  }

  return {
    ...entity,
    root: entity.entity,
    resolution: "selection",
  };
}

function scene_plan_root_name(root, fallback)
{
  return String(
    root?.name ??
    fallback ??
    "selected_scene",
  );
}

function recover_new_build_intent(prompt, intent, run)
{
  const value = String(prompt ?? "")
    .toLowerCase()
    .trim();
  const starts_new_build =
    /^(?:create|make|build|generate|construct|blockout|design)\b/.test(
      value,
    );
  const explicitly_existing =
    /\b(?:existing|selected|current|this)\s+(?:scene|environment|entity|area|level|map)\b/.test(
      value,
    );
  if (
    intent?.kind !== "scene_rebuild" ||
    !starts_new_build ||
    explicitly_existing ||
    (intent.target_name && !intent.use_selected)
  )
  {
    return intent;
  }

  const target_name =
    scene_root_name_from_prompt(prompt);
  const recovered = {
    ...intent,
    target_name,
    use_selected: false,
  };
  run.receipt("build target corrected", {
    previous_target_name: intent.target_name ?? "",
    previous_use_selected: Boolean(intent.use_selected),
    target_name,
  });
  return recovered;
}

async function prepare_scene_build_plan({
  prompt,
  intent,
  run,
})
{
  const is_scene_construction =
    intent?.kind === "scene_rebuild" ||
    intent?.kind === "city_develop";
  if (!is_scene_construction || !intent?.target_name)
  {
    return null;
  }

  const brief = create_design_brief(
    prompt,
    {
      root_name: intent.target_name,
    },
  );
  const suggested = suggest_scene_plan(brief);
  if (suggested.ok === false)
  {
    run.receipt("scene design warning", {
      root_name: intent.target_name,
      errors: suggested.errors ?? [],
    });
    return null;
  }

  run.receipt("scene design ready", {
    root_name: intent.target_name,
    source: "generated",
    zone_count: suggested.zones?.length ?? 0,
    element_count: suggested.elements?.length ?? 0,
  });
  return {
    ok: true,
    pass: true,
    plan: suggested,
  };
}

async function run_cursor_fallback_serial({ prompt, api_key, model_id, engine_host, engine_port, run, timeout_ms, engine_first_timeout_ms, intent = null }) {
  if (!api_key) {
    return {
      ok: false,
      text: "Cursor API key is missing. Paste it into the MCP Assistant window first.",
    };
  }

  active_assistant_context = {
    run,
    engine_host,
    engine_port,
    intent,
  };
  let cursor_run = null;
  let engine_tool_seen = false;
  let scene_mutation_seen = false;
  let cancel_message = "";
  let guard_timer = null;
  let idle_timer = null;
  let activity_flush_timer = null;
  let last_activity_at = Date.now();
  let visual_review_seen = false;
  let activity_buffer = "";
  let activity_prefix = "";
  let last_emitted_activity = "";
  const emit_activity = (text) => {
    const value = compact_line(text);
    if (!value || value === last_emitted_activity)
    {
      return;
    }
    last_emitted_activity = value;
    run.event("stage_note", { text: value });
  };
  const flush_activity = () => {
    if (activity_flush_timer)
    {
      clearTimeout(activity_flush_timer);
      activity_flush_timer = null;
    }
    const text = activity_buffer.trim();
    if (text)
    {
      emit_activity(`${activity_prefix}${text}`);
    }
    activity_buffer = "";
    activity_prefix = "";
  };
  const queue_activity = (activity) => {
    const match = activity.match(
      /^(Cursor: |Thinking: )(.*)$/,
    );
    if (!match)
    {
      flush_activity();
      emit_activity(activity);
      return;
    }

    const prefix = match[1];
    const chunk = match[2].trim();
    if (!chunk)
    {
      return;
    }
    if (activity_prefix && activity_prefix !== prefix)
    {
      flush_activity();
    }
    activity_prefix = prefix;

    if (!activity_buffer)
    {
      activity_buffer = chunk;
    }
    else if (chunk.startsWith(activity_buffer))
    {
      activity_buffer = chunk;
    }
    else if (!activity_buffer.endsWith(chunk))
    {
      const joins_without_space =
        /^[,.;:!?)}\]'’]/.test(chunk) ||
        /[(\[{]$/.test(activity_buffer);
      activity_buffer += joins_without_space
        ? chunk
        : ` ${chunk}`;
    }

    while (true)
    {
      const sentence = activity_buffer.match(
        /^(.+?[.!?])(?:\s+|$)/,
      );
      if (!sentence)
      {
        break;
      }
      emit_activity(
        `${activity_prefix}${sentence[1].trim()}`,
      );
      activity_buffer = activity_buffer
        .slice(sentence[0].length)
        .trim();
    }

    if (activity_flush_timer)
    {
      clearTimeout(activity_flush_timer);
    }
    activity_flush_timer = setTimeout(
      flush_activity,
      1600,
    );
    activity_flush_timer.unref?.();
  };
  const observe = async (event) => {
    last_activity_at = Date.now();
    const is_visual_review = is_named_tool_event(
      event,
      "scene_visual_review",
    );
    const successful_visual_review =
      is_visual_review &&
      object_contains(event, (value) =>
        value.ok === true &&
        Array.isArray(value.views) &&
        value.views.length >= 2 &&
        value.views.every((review) =>
          review?.camera?.ok === true &&
          review?.screenshot?.ok === true &&
          review?.screenshot?.ready === true,
        ),
      );
    visual_review_seen ||=
      successful_visual_review;
    scene_mutation_seen ||=
      is_scene_mutation_event(event);
    if (!engine_tool_seen && is_engine_tool_event(event)) {
      engine_tool_seen = true;
      run.event("stage_note", { text: "engine tool interaction confirmed" });
    }

    const activity = activity_from_event(event);
    if (activity) {
      queue_activity(activity);
    }
  };
  const execute_agent_prompt = async (agent, prompt_text) => {
    last_activity_at = Date.now();
    cursor_run = await agent.send(prompt_text, {
      onStep: ({ step }) => {
        void observe(step);
      },
    });
    run.receipt("cursor run", { id: cursor_run.id });

    const stream_task = cursor_run.stream ? (async () => {
      for await (const event of cursor_run.stream())
      {
        await observe(event);
      }
    })().catch(() => {}) : Promise.resolve();

    const result = await Promise.race([
      cursor_run.wait(),
      new Promise((_, reject) => {
        idle_timer = setInterval(() => {
          if (Date.now() - last_activity_at >= timeout_ms)
          {
            reject(
              new Error(
                `Cursor produced no activity within ${timeout_ms}ms.`,
              ),
            );
          }
        }, 1000);
        idle_timer.unref?.();
      }),
    ]);
    if (idle_timer)
    {
      clearInterval(idle_timer);
      idle_timer = null;
    }
    await Promise.race([
      stream_task,
      new Promise((resolve) => setTimeout(resolve, 1000)),
    ]);
    await append_debug_log({
      type: "cursor_run_result",
      source: "cursor_agent",
      cursor_run_id: cursor_run.id,
      status: result.status,
      result: result.result ?? "",
    });
    flush_activity();
    return result;
  };

  try {
    const agent = await run.stage("Prepare Cursor", "starting or reusing the Cursor agent", () => get_agent({ api_key, model_id, engine_host, engine_port, run }));
    const snapshot = await run.stage("Read Context", "reading engine state for Cursor", () => run.tool("context_snapshot"));
    active_assistant_context.resource_directory =
      world_resource_directory(snapshot.world);
    intent = recover_new_build_intent(
      prompt,
      intent,
      run,
    );
    active_assistant_context.intent = intent;
    const prepared_plan = await run.stage(
      "Design Scene",
      "inferring scale, layout, circulation, and functional requirements",
      () => prepare_scene_build_plan({
        prompt,
        intent,
        run,
      }),
    );
    active_assistant_context.prepared_plan =
      prepared_plan?.plan ??
      null;
    const should_focus_build =
      intent?.kind === "scene_rebuild" ||
      intent?.live_scene_action;
    if (
      should_focus_build &&
      (
        intent?.target_name ||
        intent?.use_selected
      )
    )
    {
      const initial_root = intent.target_name
        ? await resolve_quality_root(
          run,
          intent.target_name,
        )
        : await resolve_selected_quality_root(run);
      if (initial_root.ok && initial_root.root?.id)
      {
        await run.stage(
          "Focus Build Location",
          "moving the editor camera to the build",
          () => run.tool(
            "viewport_frame",
            {
              id: initial_root.root.id,
              view: "perspective",
              padding: 1.35,
            },
          ),
        );
      }
    }
    let cursor_result = await run.stage("Plan And Act", "waiting for Cursor to use Spartan tools", async () => {
      const engine_tool_deadline_at =
        Date.now() + engine_first_timeout_ms;
      guard_timer = setInterval(() => {
        if (engine_tool_seen || cancel_message) {
          clearInterval(guard_timer);
          return;
        }
        if (Date.now() < engine_tool_deadline_at) {
          return;
        }

        cancel_message =
          `cancelled, no Spartan engine tool was used within ${engine_first_timeout_ms}ms`;
        run.event("stage_note", { text: cancel_message });
        if (cursor_run?.supports?.("cancel")) {
          void cursor_run.cancel();
        }
      }, 1000);
      guard_timer.unref?.();

      return execute_agent_prompt(
        agent,
        build_prompt(
          prompt,
          snapshot,
          intent,
          prepared_plan,
        ),
      );
    });

    if (
      cursor_result.status !== "error" &&
      cursor_result.status !== "cancelled" &&
      !scene_mutation_seen &&
      (
        intent?.kind === "scene_rebuild" ||
        intent?.live_scene_action
      )
    )
    {
      run.receipt("engine build retry", {
        reason:
          "Cursor completed without using a Spartan engine tool",
        target_name: intent.target_name ?? "",
      });
      cursor_result = await run.stage(
        "Retry Engine Build",
        "requiring direct Spartan scene construction",
        () => execute_agent_prompt(
          agent,
          [
            "Continue the original live scene construction now.",
            "Your previous response completed without changing Spartan Engine.",
            "Your first action must call the spartan_engine_command custom tool with command context_snapshot and arguments {}.",
            "Continue using spartan_engine_command for native scene reads and edits.",
            `Required root entity: ${intent.target_name}.`,
            "Do not answer with a plan or explanation. Perform the complete build, audits, visual review, and corrections.",
            `Original request: ${prompt}`,
          ].join("\n"),
        ),
      );
    }

    if (cursor_result.status === "error") {
      const failure_message = await run_failure_message(cursor_run, cursor_result);
      run.receipt("cursor failure", {
        id: cursor_result.id ?? cursor_run?.id,
        status: cursor_result.status,
        detail: compact_line(failure_message),
      });
      await dispose_cached_agent();
      return { ok: false, text: failure_message };
    }

    if (cursor_result.status === "cancelled" || cancel_message) {
      return { ok: false, text: cancel_message || "Cursor run was cancelled." };
    }
    if (
      !scene_mutation_seen &&
      (
        intent?.kind === "scene_rebuild" ||
        intent?.live_scene_action
      )
    )
    {
      return {
        ok: false,
        text:
          "Cursor completed twice without using a Spartan engine tool. No scene changes were made.",
      };
    }

    const is_scene_construction =
      intent?.kind === "scene_rebuild" ||
      intent?.live_scene_action;
    if (
      !is_scene_construction ||
      (
        !intent?.target_name &&
        !intent?.use_selected
      )
    )
    {
      return {
        ok: true,
        text: cursor_result.result?.trim() || "Done.",
      };
    }

    const found = await run.stage(
      "Resolve Quality Root",
      "finding the completed scene hierarchy",
      () => intent.target_name
        ? resolve_quality_root(
          run,
          intent.target_name,
        )
        : resolve_selected_quality_root(run),
    );
    let root_id = found.root?.id;
    let root_name = scene_plan_root_name(
      found.root,
      intent.target_name,
    );
    if (!found.ok || !root_id)
    {
      return {
        ok: false,
        text:
          `Scene quality gate could not resolve root entity ${root_name}.`,
      };
    }

    await run.stage(
      "Focus Completed Build",
      "framing the constructed scene",
      () => run.tool(
        "viewport_frame",
        {
          id: root_id,
          view: "perspective",
          padding: 1.35,
        },
      ),
    );

    const send_command = (name, args) =>
      run.tool(name, args);
    const plan =
      prepared_plan?.plan ??
      null;
    const planned_elements = plan?.elements ?? [];
    const audit_args = {
      id: root_id,
      required_features: infer_required_features(prompt),
      scene_type: infer_design_template(prompt),
      planned_element_count: planned_elements.reduce(
        (total, element) =>
          total + (element.count ?? 1),
        0,
      ),
      required_roles: [
        ...new Set(
          planned_elements.flatMap(
            (element) =>
              element.semantic_tags ?? [],
          ),
        ),
      ],
    };
    let audit = await run.stage(
      "Audit Scene Quality",
      "checking geometry, materials, features, and lighting",
      () => audit_scene_quality(
        send_command,
        audit_args,
      ),
    );
    const layout_audit_args = {
      id: root_id,
      root_name,
    };
    const audit_current_layout = async () => {
      return audit_scene_layout(
        send_command,
        {
          ...layout_audit_args,
          plan,
        },
      );
    };
    let layout_audit = await run.stage(
      "Audit Scene Layout",
      "checking scale, support, relationships, and lighting",
      audit_current_layout,
    );
    let final_result = cursor_result;

    for (
      let attempt = 1;
      attempt <= 2 &&
      (
        !audit.pass ||
        !layout_audit.pass ||
        !visual_review_seen
      );
      attempt++
    )
    {
      const correction_prompt = [
        "Perform a mandatory quality correction pass on the live Spartan Engine scene.",
        `Original request: ${prompt}`,
        `Root entity: ${root_name}, id ${root_id}.`,
        `Quality audit: ${safe_json(audit, 3500)}`,
        `Layout audit: ${safe_json(layout_audit, 5000)}`,
        "If the generic scene plan is missing or invalid, call scene_plan_create first with realistic expected dimensions, zones, support modes, relationships, and lighting intent inferred from the original request.",
        "Call scene_visual_review on the root with perspective and top views, then inspect both images.",
        "Fix every failed scene_layout_audit and scene_quality_audit check, including every renderable listed by collision_coverage, plus the most visible weakness in the image.",
        "Keep entities aligned with plan element names, plan_element values, semantic_tags, and repeated instances so the layout audit can verify the authored result.",
        "Use generated or compound geometry, semantic palette materials, descriptive feature names, snapping, and calibrated lighting as needed.",
        "Use entity_create_light for lights and mesh_physics_bind or compound_create for collidable generated geometry. Do not expand these atomic tools into probe and component-setting sequences.",
        "Resolve every correction parent from the current scene and use the returned id. Never retry a missing parent with another guessed id.",
        "Preserve all good existing work and keep every addition under the root.",
        "Call scene_layout_audit and scene_quality_audit after corrections and do not report completion unless both pass.",
      ].join("\n");

      final_result = await run.stage(
        `Quality Correction ${attempt}`,
        "waiting for visual review and targeted corrections",
        () => execute_agent_prompt(
          agent,
          correction_prompt,
        ),
      );
      if (
        final_result.status === "error" ||
        final_result.status === "cancelled"
      )
      {
        const failure_message = await run_failure_message(
          cursor_run,
          final_result,
        );
        return {
          ok: false,
          text: `Scene was edited, but quality correction failed: ${failure_message}`,
        };
      }

      const corrected_root = intent?.use_selected
        ? await resolve_selected_quality_root(run)
        : await resolve_quality_root(
          run,
          intent.target_name ?? root_name,
        );
      if (
        corrected_root.ok &&
        corrected_root.root?.id
      )
      {
        root_id = corrected_root.root.id;
        root_name = scene_plan_root_name(
          corrected_root.root,
          root_name,
        );
        audit_args.id = root_id;
        layout_audit_args.id = root_id;
        layout_audit_args.root_name = root_name;
      }

      audit = await run.stage(
        `Verify Quality ${attempt}`,
        "rechecking the corrected scene",
        () => audit_scene_quality(
          send_command,
          audit_args,
        ),
      );
      layout_audit = await run.stage(
        `Verify Layout ${attempt}`,
        "rechecking scale, support, relationships, and lighting",
        audit_current_layout,
      );
    }

    const resource_cleanup = await run.stage(
      "Clean World Resources",
      "removing unreferenced world assets",
      () => run.tool(
        "world_resources_clean",
        {},
      ),
    );
    if (
      !audit.pass ||
      !layout_audit.pass ||
      !visual_review_seen
    )
    {
      return {
        ok: false,
        text: [
          "Scene was edited, but the quality gate remains incomplete.",
          `Quality audit: ${safe_json(audit, 2500)}`,
          `Layout audit: ${safe_json(layout_audit, 3500)}`,
          `Visual review completed: ${visual_review_seen}.`,
          resource_cleanup.ok
            ? `World resources cleaned: ${(resource_cleanup.removed ?? []).length} unused files removed, ${resource_cleanup.orphan_count ?? 0} undeleted orphans.`
            : `World resource cleanup failed for ${(resource_cleanup.failed ?? []).length} files.`,
          "Final-state audits are authoritative for plan and correction completion.",
        ].join("\n"),
      };
    }

    return {
      ok: true,
      text: [
        final_result.result?.trim() ||
          cursor_result.result?.trim() ||
          "Done.",
        `Quality gates passed: content ${audit.score}/100, layout ${layout_audit.score}/100, visual review complete.`,
        resource_cleanup.ok
          ? `World resources cleaned: ${(resource_cleanup.removed ?? []).length} unused files removed, ${resource_cleanup.orphan_count ?? 0} undeleted orphans.`
          : `World resource cleanup failed for ${(resource_cleanup.failed ?? []).length} files.`,
      ].join("\n"),
    };
  } catch (error) {
    if (cursor_run?.supports?.("cancel") && error.message?.includes("within")) {
      await cursor_run.cancel();
    }

    if (error instanceof CursorAgentError) {
      return { ok: false, text: `Cursor startup failed: ${error.message}` };
    }

    return { ok: false, text: `Assistant failed: ${error.message}` };
  } finally {
    if (active_assistant_context?.run === run)
    {
      active_assistant_context = null;
    }
    if (guard_timer) {
      clearTimeout(guard_timer);
    }
    if (idle_timer) {
      clearInterval(idle_timer);
    }
    if (activity_flush_timer)
    {
      clearTimeout(activity_flush_timer);
    }
  }
}

export async function run_cursor_fallback(args) {
  const previous_run = agent_run_queue;
  let release_run;
  agent_run_queue = new Promise((resolve) => {
    release_run = resolve;
  });
  await previous_run;

  try
  {
    return await run_cursor_fallback_serial(args);
  }
  finally
  {
    release_run();
  }
}
