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
  names_a_place,
  scene_root_name_from_prompt,
} from "./intent_router.mjs";
import {
  build_reuse_plan,
  inventory_from_brief,
  inventory_from_plan,
  resolve_asset_by_name,
  reuse_prompt_lines,
} from "./asset_reuse.mjs";
import {
  get_project_root,
  get_shared_codebase,
} from "./shared_codebase.mjs";
import {
  constrain_generated_resources,
  generated_resource_command,
  material_file_name,
} from "./world_resources.mjs";
import {
  auto_register_world_asset,
  resolve_world_resource_directory,
  world_asset_compare,
  world_asset_fork,
  world_asset_inspect,
  world_asset_load,
  world_asset_promote,
  world_asset_register,
  world_asset_search,
  world_material_fork,
  world_material_inspect,
  world_material_publish,
} from "./world_asset_catalog.mjs";

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
  "world_resource_directory_get",
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
  "asset_viewer_open",
  "asset_viewer_status",
  "asset_viewer_select",
  "asset_viewer_preview_entity",
  "asset_viewer_set_view",
  "asset_viewer_screenshot",
  "entity_render_materials",
  "component_types",
  "primitive_types",
  "entity_add_component",
  "entity_remove_component",
  "component_get",
  "component_set",
  "execute_lua",
  "mesh_raw_create",
  "mesh_raw_get",
  "texture_generate",
  "material_textured_create",
  "world_asset_search",
  "world_asset_inspect",
  "world_asset_register",
  "world_asset_version",
  "world_asset_fork",
  "world_asset_compare",
  "world_asset_promote",
  "world_asset_load",
  "world_material_inspect",
  "world_material_fork",
  "world_material_publish",
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
  "mesh_raw_create",
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
  "world_asset_register",
  "world_asset_version",
  "world_asset_fork",
  "world_asset_promote",
  "world_asset_load",
  "world_material_fork",
  "world_material_publish",
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
const maximum_cursor_run_ms = Number.parseInt(
  process.env.SPARTAN_CURSOR_MAX_RUN_MS ??
  "360000",
  10,
);

function is_engine_bridge_failure(result)
{
  const code = String(result?.code ?? "");
  const error = String(result?.error ?? "")
    .toLowerCase();
  return (
    code === "engine_timeout" ||
    code === "engine_connect_timeout" ||
    error.includes("engine connection closed") ||
    error.includes("econnreset") ||
    error.includes("engine command") &&
      error.includes("timed out") ||
    error.includes("engine connection") &&
      error.includes("timed out")
  );
}

async function assistant_resource_directory(context)
{
  if (context.resource_directory)
  {
    return context.resource_directory;
  }
  context.resource_directory =
    await resolve_world_resource_directory(
      (command, args) => context.run.tool(
        command,
        args,
        60000,
      ),
    );
  return context.resource_directory;
}

// collapses the parts that share a material before the prefab is written
//
// the geometry stage is told to split a surface off into its own part whenever it needs its own material
// or its own parameters, which is the only way to author detail, and it leaves a chair sitting at forty
// draw calls. this runs on the way to disk so the saved prefab is the cheap version and the authoring
// stage never has to think about the cost
//
// the caller decides whether the entity is a single object. collapsing one is the whole point, collapsing
// a scene root would fuse every wooden thing in a room into one entity and lose the scene's structure
async function make_game_ready(
  context,
  entity_id,
)
{
  if (!entity_id)
  {
    return null;
  }

  const report = await context.run.tool(
    "entity_make_game_ready",
    {
      id: entity_id,
      generate_lods: true,
    },
    120000,
  );

  // a failure here costs an optimisation, not the asset, so the save carries on either way
  if (!report?.ok)
  {
    context.run.event(
      "stage_note",
      {
        text: `game ready pass skipped: ${
          report?.error ?? "the engine did not answer"
        }`,
      },
    );
    return null;
  }

  if (report.entities_removed > 0)
  {
    context.run.event(
      "stage_note",
      {
        text: `game ready: ${
          report.renderers_before
        } meshes merged down to ${
          report.renderers_after
        } by material`,
      },
    );
  }
  return report;
}

async function register_assistant_asset(
  context,
  command,
  args,
  result,
)
{
  const registration = await auto_register_world_asset(
    get_project_root(),
    await assistant_resource_directory(context),
    command,
    args,
    result,
  );
  if (registration)
  {
    result.asset_registration = registration;
    const asset_id = registration.asset?.id;
    const candidate_version = registration.version?.id;
    const candidate_path = registration.version?.path;
    if (asset_id && candidate_version && candidate_path)
    {
      context.asset_viewer_asset_id = asset_id;
      context.asset_viewer_candidate = {
        asset_id,
        version_id: candidate_version,
        path: candidate_path,
      };
      context.visual_review_candidate = null;
    }
    if (
      command === "prefab_save" &&
      asset_id &&
      is_focused_asset_request(context.prompt)
    )
    {
      result.asset_viewer = await context.run.tool(
        "asset_viewer_select",
        {
          asset_id,
          version_id: candidate_version,
        },
        10000,
      );
    }
  }
  return result;
}

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

// creates the material and the maps it needs in one call, on its own the agent
// makes a flat material and never comes back to texture it
async function create_textured_material(run, args) {
  const name = String(args.name ?? "").trim();
  if (!name)
  {
    return {
      ok: false,
      error: "name is required",
    };
  }
  if (!Array.isArray(args.layers) || args.layers.length === 0)
  {
    return {
      ok: true,
      introspection: true,
      required: [
        "name",
        "layers",
      ],
      note:
        "layers describe the texture, each layer needs a type such as fill, noise, bricks, spots, scratches, shape or text",
    };
  }

  const material_path =
    args.material_path ??
    args.path ??
    material_file_name(name);
  const material = await run.tool(
    "material_create",
    {
      path: material_path,
      name,
    },
  );
  if (!material.ok)
  {
    return material;
  }

  const created_path =
    material.resource?.path ??
    material.material?.path ??
    material_path;
  const properties = await set_material_properties(
    run,
    created_path,
    args,
  );
  if (!properties.ok)
  {
    return properties;
  }

  const texture = await run.tool(
    "texture_generate",
    {
      name: args.texture_name ?? name,
      path: args.texture_path,
      layers: args.layers,
      width: args.width,
      height: args.height,
      seed: args.seed,
      seamless: args.seamless,
      normal_strength: args.normal_strength,
      base_roughness:
        args.base_roughness ??
        args.roughness,
      base_metalness: args.base_metalness,
      library_asset: args.library_asset,
      material_path: created_path,
    },
    60000,
  );
  if (!texture.ok)
  {
    return {
      ...texture,
      material_path: created_path,
    };
  }

  const tiling = Number(args.tiling ?? 0);
  if (tiling > 0)
  {
    for (const property of [
      "texture_tiling_x",
      "texture_tiling_y",
    ])
    {
      await run.tool(
        "material_set_property",
        {
          path: created_path,
          property,
          value: tiling,
        },
      );
    }
  }

  return {
    ok: true,
    material_path: created_path,
    texture,
    tiling: tiling > 0 ? tiling : 1,
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
    if (active_assistant_context)
    {
      await register_assistant_asset(
        active_assistant_context,
        "material_create",
        {
          ...material,
          path: material_path,
          source: {
            properties: material,
          },
        },
        result,
      );
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
  const requested_path = String(file_path ?? "")
    .replaceAll("\\", "/");
  const candidates = path.isAbsolute(requested_path)
    ? [requested_path]
    : [
        path.resolve(
          get_project_root(),
          "binaries",
          requested_path,
        ),
        path.resolve(
          get_project_root(),
          requested_path,
        ),
      ];
  const deadline = Date.now() + wait_ms;
  while (Date.now() < deadline)
  {
    for (const candidate of candidates)
    {
      try
      {
        const stats = await fs.stat(candidate);
        if (stats.size > 0)
        {
          return true;
        }
      }
      catch
      {
      }
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

async function review_asset_viewer(
  run,
  args,
  asset_id,
  candidate = null,
)
{
  if (!asset_id)
  {
    return {
      ok: false,
      error:
        "select or register a mesh or prefab in the Asset Viewer before visual review",
    };
  }
  const selected = await run.tool(
    "asset_viewer_select",
    {
      asset_id,
      version_id:
        candidate?.version_id ??
        args.version_id ??
        args.version,
    },
    10000,
  );
  if (!selected.ok)
  {
    return selected;
  }
  if (
    candidate?.path &&
    selected.loaded_path !== candidate.path
  )
  {
    return {
      ok: false,
      error:
        "Asset Viewer loaded a different version than the promotion candidate",
      expected_path: candidate.path,
      loaded_path: selected.loaded_path,
    };
  }
  if (
    /\.(mesh|prefab)$/i.test(selected.loaded_path ?? "") &&
    Number(selected.vertex_count ?? 0) <= 0
  )
  {
    return {
      ok: false,
      error:
        "promotion candidate has no previewable geometry",
      loaded_path: selected.loaded_path,
    };
  }

  const requested_views = Array.isArray(args.views)
    ? args.views
    : [
        "perspective",
        "front",
        "right",
        "top",
      ];
  const views = [];
  for (const view of requested_views.slice(0, 4))
  {
    const camera = await run.tool(
      "asset_viewer_set_view",
      {
        view,
        zoom: args.zoom ?? 1,
      },
      10000,
    );
    if (!camera.ok)
    {
      return {
        ...camera,
        views,
      };
    }
    const screenshot = await run.tool(
      "asset_viewer_screenshot",
      {
        path:
          `asset_${asset_id}_${
            candidate?.version_id ?? "active"
          }_${view}.png`,
        width: args.width ?? 768,
        height: args.height ?? 768,
      },
      10000,
    );
    if (
      screenshot.ok &&
      screenshot.async &&
      screenshot.path
    )
    {
      const expected_generation =
        Number(screenshot.generation ?? 0);
      if (expected_generation <= 0)
      {
        screenshot.ok = false;
        screenshot.error =
          "Asset Viewer did not return a renderer generation";
      }
      screenshot.ready = await wait_for_screenshot(
        screenshot.path,
        10000,
      );
      if (!screenshot.ready)
      {
        screenshot.ok = false;
        screenshot.error =
          "Asset Viewer screenshot was not written within 10 seconds";
      }
      if (screenshot.ok)
      {
        const renderer_status = await run.tool(
          "asset_viewer_status",
          {},
          10000,
        );
        if (
          !renderer_status.ok ||
          Number(renderer_status.renderer_generation ?? 0) <
            expected_generation
        )
        {
          screenshot.ok = false;
          screenshot.error =
            "Asset Viewer renderer generation was not ready";
        }
        else if (
          candidate?.path &&
          renderer_status.loaded_path !== candidate.path
        )
        {
          screenshot.ok = false;
          screenshot.error =
            "Asset Viewer changed versions during renderer capture";
        }
      }
    }
    views.push({
      view,
      camera,
      screenshot,
    });
    if (!screenshot.ok)
    {
      return {
        ...screenshot,
        views,
      };
    }
  }
  return {
    ok: true,
    target: "asset_viewer",
    asset_id,
    version_id:
      candidate?.version_id ??
      selected.selected_version_id,
    loaded_path: selected.loaded_path,
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

function flatten_points(value, dimensions) {
  if (!Array.isArray(value))
  {
    return value;
  }
  if (value.every((entry) => Number.isFinite(entry)))
  {
    return value;
  }
  const keys = dimensions === 2
    ? ["x", "y"]
    : ["x", "y", "z"];
  return value.flatMap((entry) => {
    if (Array.isArray(entry))
    {
      return entry.slice(0, dimensions);
    }
    if (entry && typeof entry === "object")
    {
      return keys.map((key) => entry[key]);
    }
    return [entry];
  });
}

function normalize_mesh_arguments(args) {
  const normalized = {
    ...args,
    profile: flatten_points(args.profile, 2),
    path_points: flatten_points(args.path_points, 3),
  };
  if (
    normalized.shape === "curved_profile" &&
    Array.isArray(normalized.profile) &&
    normalized.profile.length >= 6
  )
  {
    const profile = normalized.profile;
    const last = profile.length - 2;
    if (
      profile[0] !== profile[last] ||
      profile[1] !== profile[last + 1]
    )
    {
      normalized.profile = [
        ...profile,
        profile[0],
        profile[1],
      ];
    }
  }
  return normalized;
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
  const modifiers = Array.isArray(normalized.modifiers)
    ? normalized.modifiers
    : [];
  for (const modifier of modifiers)
  {
    if (!modifier || typeof modifier !== "object")
    {
      continue;
    }
    const type = String(modifier.type ?? "").toLowerCase();
    if (type === "radial_array")
    {
      normalized.radial_count =
        modifier.count ??
        normalized.radial_count;
      normalized.radial_axis =
        modifier.axis ??
        normalized.radial_axis;
      normalized.radial_radius =
        modifier.radius ??
        normalized.radial_radius;
      normalized.radial_step_degrees =
        modifier.step_degrees ??
        normalized.radial_step_degrees;
    }
    else if (type === "linear_array")
    {
      normalized.linear_count =
        modifier.count ??
        normalized.linear_count;
      normalized.linear_step =
        modifier.step ??
        normalized.linear_step;
    }
    else if (type === "shell")
    {
      normalized.shell_thickness =
        modifier.thickness ??
        normalized.shell_thickness;
    }
    else if (type === "taper")
    {
      normalized.taper_start =
        modifier.start ??
        normalized.taper_start;
      normalized.taper_end =
        modifier.end ??
        normalized.taper_end;
    }
  }
  normalized.rotation_euler =
    normalized.rotation_euler ??
    normalized.rotation;
  if (
    shape === "revolved_profile" &&
    Number.isFinite(normalized.segments)
  )
  {
    normalized.segments = Math.min(
      64,
      Math.max(3, Math.trunc(normalized.segments)),
    );
  }
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
  if (active_assistant_context)
  {
    await register_assistant_asset(
      active_assistant_context,
      "mesh_generate",
      normalized,
      generated,
    );
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
    normalized.position ||
    normalized.rotation_euler ||
    normalized.scale
  )
  {
    const transformed = await run.tool(
      "entity_set_transform",
      {
        id: created.entity.id,
        position: normalized.position,
        rotation_euler: normalized.rotation_euler,
        scale: normalized.scale,
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
  const bind_args = {
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
  };
  const collision_requested =
    args.with_physics === true ||
    args.collision === true ||
    (
      args.collision &&
      typeof args.collision === "object"
    ) ||
    Boolean(args.body_type);
  const bound = collision_requested
    ? await bind_generated_mesh(
        run,
        bind_args,
      )
    : await run.tool(
        "render_set_mesh",
        {
          id: bind_args.id,
          mesh: bind_args.mesh,
          material: bind_args.material,
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
      const collision_requested =
        part.with_physics === true ||
        part.collision === true ||
        Boolean(part.body_type);
      result = collision_requested
        ? await bind_generated_mesh(
            run,
            {
              ...part,
              id: child.entity.id,
            },
          )
        : await run.tool(
            "render_set_mesh",
            {
              id: child.entity.id,
              mesh: part.mesh,
              material: part.material,
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
  let prefab = null;
  if (args.prefab_path)
  {
    // a compound is one object by construction, so its parts are always safe to collapse. the guard is
    // only that a focused asset run is what asked for it, a scene run saves props it did not isolate
    let game_ready = null;
    if (active_assistant_context?.authoring_root_id)
    {
      game_ready = await make_game_ready(
        active_assistant_context,
        root.entity.id,
      );
    }
    prefab = await run.tool(
      "prefab_save",
      {
        id: root.entity.id,
        path: args.prefab_path,
      },
    );
    if (game_ready)
    {
      prefab.game_ready = game_ready;
    }
    if (!prefab.ok)
    {
      return {
        ...prefab,
        root: root.entity,
        completed_parts,
      };
    }
    if (active_assistant_context)
    {
      await register_assistant_asset(
        active_assistant_context,
        "prefab_save",
        {
          name: args.name,
          path: args.prefab_path,
          tags: args.tags,
          constraints: args.constraints,
        },
        prefab,
      );
    }
  }
  return {
    ok: true,
    root: root.entity,
    completed_parts,
    completed_count: completed_parts.length,
    prefab,
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

// commands that change what the asset is made of. a transform or a material property changes how a part
// looks, these change whether the prefab has it at all
const part_changing_commands = new Set([
  "mesh_generate",
  "mesh_generate_batch",
  "mesh_raw_create",
  "render_set_mesh",
  "entity_delete",
  "entity_create_primitive",
  "entity_create_primitive_batch",
  "entity_set_parent",
  "compound_create",
]);

// the prefab is rewritten as the asset grows, so the file on disk always holds what has been built so far.
// a run that dies at part thirty leaves thirty usable parts instead of nothing, and the asset appears in the
// library while it is being made rather than in one lump at the end
//
// throttled, because a build issues dozens of these in a row and the point is a recent file, not every
// intermediate state. a failure here is not the model's problem and must not surface as a tool error
async function save_asset_progress(
  context,
  command,
  result,
)
{
  if (
    !context?.authoring_root_id ||
    !context?.authoring_prefab_path ||
    result?.ok !== true ||
    !part_changing_commands.has(command)
  )
  {
    return;
  }

  const now = Date.now();
  if (now - (context.authoring_saved_at ?? 0) < 4000)
  {
    return;
  }
  context.authoring_saved_at = now;

  try
  {
    await context.run.tool(
      "prefab_save",
      {
        id: context.authoring_root_id,
        path: context.authoring_prefab_path,
      },
      60000,
    );
  }
  catch
  {
  }
}

// a budget stated once in the system prompt is a suggestion by the fortieth part. one that answers back in the
// reply to the call that spent it is a fact the run cannot read past, so the accounting rides along with the
// geometry commands and speaks up at the moment the limit is crossed
function track_asset_budget(
  context,
  command,
  args,
  result,
)
{
  const budget = context?.asset_budget;
  if (
    !budget ||
    result?.ok !== true
  )
  {
    return result;
  }

  if (
    command === "material_create" ||
    command === "material_semantic_create"
  )
  {
    const name = String(
      result.resource?.path ??
      result.material?.path ??
      args?.path ??
      args?.name ??
      "",
    ).toLowerCase();
    const materials = (context.asset_materials ??= new Set());
    if (name)
    {
      materials.add(name);
    }

    const count = materials.size;
    const over_materials = count > budget.materials;
    if (
      !over_materials &&
      count <= budget.materials * 0.7
    )
    {
      return result;
    }

    return {
      ...result,
      asset_budget: {
        tier: budget.tier,
        materials_used: count,
        materials_budget: budget.materials,
        over_budget: over_materials,
      },
      guidance: over_materials
        ? `this asset is over its ${budget.materials} material limit, stop creating materials and reuse or merge the existing set`
        : `material budget check: ${count} of ${budget.materials} materials used, reuse these materials for the remaining parts`,
    };
  }

  if (
    command !== "mesh_generate" &&
    command !== "mesh_raw_create"
  )
  {
    return result;
  }

  const triangles = Math.round(
    (Number(result.index_count) || 0) / 3,
  );
  const name = String(args?.name ?? args?.path ?? "").toLowerCase();

  // a reused mesh is the same geometry the asset already paid for
  if (result.reused === true)
  {
    return result;
  }

  const seen = (context.asset_parts ??= new Map());
  const duplicate = name.length > 0 && seen.has(name);
  if (name.length > 0)
  {
    seen.set(name, triangles);
  }
  context.asset_triangles =
    (context.asset_triangles ?? 0) + triangles;

  const spent = context.asset_triangles;
  const parts = seen.size;
  const over_triangles = spent > budget.triangles;
  const over_parts = parts > budget.parts;
  const notes = [];

  if (duplicate)
  {
    notes.push(
      `a part named ${name} was already generated for this asset, so this call duplicated geometry that already existed, delete one of the two`,
    );
  }
  if (over_triangles || over_parts)
  {
    notes.push(
      `this asset is over budget at ${spent.toLocaleString("en-US")} triangles across ${parts} parts, against a budget of ${budget.triangles.toLocaleString("en-US")} triangles and ${budget.parts} parts`,
      "stop adding parts now. finish the asset with what it already has: verify the parts are placed and materialled, then let it be saved. if something essential to recognising the object is genuinely missing, remove or simplify existing geometry to pay for it, starting with anything on a face the viewer never sees",
    );
  }
  else if (spent > budget.triangles * 0.7)
  {
    notes.push(
      `budget check: ${spent.toLocaleString("en-US")} of ${budget.triangles.toLocaleString("en-US")} triangles and ${parts} of ${budget.parts} parts used, so only the parts the object is recognised by are still affordable`,
    );
  }

  if (notes.length === 0)
  {
    return result;
  }

  return {
    ...result,
    asset_budget: {
      tier: budget.tier,
      triangles_used: spent,
      triangles_budget: budget.triangles,
      parts_used: parts,
      parts_budget: budget.parts,
      over_budget: over_triangles || over_parts,
    },
    guidance: notes.join(". "),
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
  if (command === "mesh_generate")
  {
    args = normalize_mesh_arguments(args);
  }
  if (
    command === "mesh_generate_batch" &&
    Array.isArray(args.items)
  )
  {
    args = {
      ...args,
      items: args.items.map(normalize_mesh_arguments),
    };
  }
  if (
    (
      command === "entity_render_materials" ||
      command === "entity_get" ||
      command === "entity_delete"
    ) &&
    args.entity_id &&
    !args.id
  )
  {
    args.id = args.entity_id;
  }
  if (
    command === "entity_find" &&
    args.query &&
    !args.name &&
    !args.tag
  )
  {
    args.name = args.query;
  }
  if (
    command === "component_set_batch" &&
    args.body_type === "static"
  )
  {
    args = {
      ...args,
      body_type: undefined,
      static: true,
    };
  }
  if (
    (
      command === "material_get" ||
      command === "material_set_property" ||
      command === "material_set_texture"
    ) &&
    typeof args.path === "string" &&
    args.path.endsWith(".material")
  )
  {
    args.path = `${args.path.slice(0, -9)}.xml`;
  }
  if (
    (
      command === "component_set" ||
      command === "entity_add_component" ||
      command === "entity_remove_component"
    ) &&
    args.component &&
    !args.type
  )
  {
    args.type = args.component;
  }
  if (
    command === "prefab_save" &&
    args.entity_id &&
    !args.id
  )
  {
    args.id = args.entity_id;
  }
  if (
    command === "asset_viewer_select" &&
    args.asset &&
    !args.asset_id
  )
  {
    args.asset_id = args.asset;
  }
  if (
    command === "asset_viewer_select" &&
    args.entity_id
  )
  {
    return run.tool(
      "asset_viewer_preview_entity",
      {
        id: args.entity_id,
      },
      10000,
    );
  }
  if (
    command === "asset_viewer_preview_entity" &&
    args.entity_id &&
    !args.id
  )
  {
    args.id = args.entity_id;
  }
  if (command === "entity_set_active")
  {
    const requested =
      args.active ?? args.value;
    const active =
      typeof requested === "string" ?
        ![
          "false",
          "0",
          "no",
          "off",
        ].includes(requested.toLowerCase()) :
        Boolean(requested);
    return run.tool(
      "entity_update",
      {
        id: args.id ?? args.entity_id,
        active,
      },
      10000,
    );
  }
  if (command === "wait_for_screenshot")
  {
    const screenshot_path =
      args.path ?? args.file_path;
    if (!screenshot_path)
    {
      return {
        ok: false,
        ready: false,
        error: "screenshot path is required",
      };
    }
    const ready = await wait_for_screenshot(
      screenshot_path,
      args.wait_ms ?? 10000,
    );
    return {
      ok: ready,
      ready,
      path: screenshot_path,
      ...(
        ready ?
          {} :
          {
            error:
              "screenshot was not written before the timeout",
          }
      ),
    };
  }
  if (command === "search_codebase")
  {
    const query = String(args.query ?? "").trim();
    if (!query)
    {
      return {
        ok: false,
        error: "query is required",
      };
    }
    const codebase = get_shared_codebase();
    return {
      ok: true,
      ready: codebase.status().ready,
      query,
      results: await codebase.search(
        query,
        args.top_k ?? 8,
      ),
    };
  }
  if (command === "read_source_file")
  {
    try
    {
      return {
        ok: true,
        ...await get_shared_codebase().read_file(
          args.path,
          {
            start_line: args.start_line ?? 1,
            line_count: args.line_count ?? 160,
          },
        ),
      };
    }
    catch (error)
    {
      return {
        ok: false,
        error: error.message,
      };
    }
  }
  // the library is shared across worlds, so a generated resource has somewhere to go whether or not a world
  // is open. this used to refuse the command instead of resolving the directory
  if (
    generated_resource_command(command) &&
    !context.resource_directory
  )
  {
    await assistant_resource_directory(context);
  }
  args = constrain_generated_resources(
    command,
    args,
    context.resource_directory ?? "",
  );
  const catalog_directory =
    await assistant_resource_directory(context);
  const catalog_root = get_project_root();
  const catalog_send = (name, value) =>
    run.tool(name, value, 60000);
  if (command === "world_asset_search")
  {
    return world_asset_search(
      catalog_root,
      catalog_directory,
      args,
    );
  }
  if (command === "world_asset_inspect")
  {
    return world_asset_inspect(
      catalog_root,
      catalog_directory,
      args,
    );
  }
  if (
    command === "world_asset_register" ||
    command === "world_asset_version"
  )
  {
    const result = await world_asset_register(
      catalog_root,
      catalog_directory,
      args,
    );
    if (
      result.ok &&
      (
        args.type === "mesh" ||
        args.type === "prefab"
      ) &&
      is_focused_asset_request(
        context.prompt ??
        context.intent?.prompt
      )
    )
    {
      const asset_id =
        result.asset?.id ??
        args.asset_id;
      const candidate_version =
        result.version?.id;
      const candidate_path =
        result.version?.path;
      const selection = await run.tool(
        "asset_viewer_select",
        {
          asset_id,
          version_id: candidate_version,
        },
        10000,
      );
      if (selection.ok)
      {
        context.asset_viewer_asset_id =
          asset_id;
        context.asset_viewer_candidate = {
          asset_id,
          version_id: candidate_version,
          path: candidate_path,
        };
        context.visual_review_candidate = null;
      }
      result.asset_viewer = selection;
    }
    return result;
  }
  if (command === "world_asset_fork")
  {
    return world_asset_fork(
      catalog_root,
      catalog_directory,
      args,
    );
  }
  if (command === "world_material_fork")
  {
    return world_material_fork(
      catalog_root,
      catalog_directory,
      args,
      catalog_send,
    );
  }
  if (command === "world_asset_compare")
  {
    return world_asset_compare(
      catalog_root,
      catalog_directory,
      args,
    );
  }
  if (command === "world_asset_promote")
  {
    if (is_focused_asset_request(context.prompt))
    {
      const asset_id = String(
        args.asset_id ?? args.id ?? "",
      );
      const version_id = String(
        args.version ??
        args.candidate_version ??
        args.version_id ??
        "",
      );
      const evidence = context.visual_review_candidate;
      if (!version_id)
      {
        return {
          ok: false,
          error:
            "focused asset promotion requires an explicit candidate version",
        };
      }
      if (
        !evidence ||
        evidence.asset_id !== asset_id ||
        evidence.version_id !== version_id
      )
      {
        return {
          ok: false,
          error:
            "review the exact candidate version in the Asset Viewer before promotion",
          candidate: {
            asset_id,
            version_id,
          },
          reviewed: evidence,
        };
      }
      args.version = version_id;
    }
    return world_asset_promote(
      catalog_root,
      catalog_directory,
      args,
    );
  }
  if (command === "world_asset_load")
  {
    return world_asset_load(
      catalog_root,
      catalog_directory,
      args,
      catalog_send,
    );
  }
  if (command === "world_material_inspect")
  {
    return world_material_inspect(
      catalog_root,
      catalog_directory,
      args,
      catalog_send,
    );
  }
  if (command === "world_material_publish")
  {
    return world_material_publish(
      catalog_root,
      catalog_directory,
      args,
      catalog_send,
    );
  }
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
  if (command === "scene_plan_create")
  {
    if (
      !args.plan ||
      typeof args.plan !== "object" ||
      Array.isArray(args.plan)
    )
    {
      return {
        ok: false,
        error: "plan must be an object",
      };
    }
    context.prepared_plan = args.plan;
    return {
      ok: true,
      plan: args.plan,
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
    command === "material_textured_create" ||
    command === "textured_material_create"
  )
  {
    return create_textured_material(run, args);
  }
  if (
    command === "texture_generate" &&
    !Array.isArray(args.layers)
  )
  {
    return {
      ok: true,
      introspection: true,
      required: [
        "name",
        "layers",
      ],
      note:
        "layers is an array of objects, each with a type of fill, linear_gradient, radial_gradient, noise, checker, stripes, bricks, tiles, spots, scratches, shape or text",
    };
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
    const bind_args = {
      ...args,
      id: created.entity.id,
    };
    const collision_requested =
      args.with_physics === true ||
      args.collision === true ||
      Boolean(args.body_type);
    return collision_requested
      ? bind_generated_mesh(
          run,
          bind_args,
        )
      : run.tool(
          "render_set_mesh",
          {
            id: created.entity.id,
            mesh: args.mesh,
            material: args.material,
          },
        );
  }
  if (command === "entity_set_transform_batch")
  {
    const items =
      args.items ??
      args.entities ??
      args.transforms;
    if (!Array.isArray(items))
    {
      return run.tool(
        command,
        args,
        60000,
      );
    }
    if (items.length < 1 || items.length > 64)
    {
      return {
        ok: false,
        error:
          "entity_set_transform_batch requires one to 64 items",
      };
    }
    const mapped = {
      count: items.length,
    };
    for (let index = 0; index < items.length; index++)
    {
      const item = items[index] ?? {};
      const rotation = item.rotation;
      mapped[`item_${index}_id`] =
        item.id ??
        item.entity_id;
      mapped[`item_${index}_position`] =
        item.position ??
        item.position_local;
      mapped[`item_${index}_rotation_euler`] =
        item.rotation_euler ??
        item.rotation_local ??
        (
          Array.isArray(rotation) &&
          rotation.length === 3
            ? rotation
            : undefined
        );
      mapped[`item_${index}_rotation`] =
        Array.isArray(rotation) &&
        rotation.length === 4
          ? rotation
          : undefined;
      mapped[`item_${index}_scale`] =
        item.scale ??
        item.scale_local;
    }
    return run.tool(
      command,
      mapped,
      60000,
    );
  }
  if (command === "entity_set_transform")
  {
    const rotation = args.rotation;
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
          args.rotation_local ??
          (
            Array.isArray(rotation) &&
            rotation.length === 3
              ? rotation
              : undefined
          ),
        rotation:
          Array.isArray(rotation) &&
          rotation.length === 4
            ? rotation
            : undefined,
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
  if (command === "mesh_raw_create")
  {
    const result = await run.tool(
      command,
      args,
      60000,
    );
    return register_assistant_asset(
      context,
      command,
      args,
      result,
    );
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
    if (is_focused_asset_request(context.prompt))
    {
      const review = await review_asset_viewer(
        run,
        args,
        context.asset_viewer_asset_id,
        context.asset_viewer_candidate,
      );
      if (review.ok)
      {
        context.visual_review_candidate = {
          asset_id: review.asset_id,
          version_id: review.version_id,
          path: review.loaded_path,
        };
        context.mark_visual_review?.();
      }
      return review;
    }
    const review = await review_scene(run, args);
    if (review.ok)
    {
      context.mark_visual_review?.();
    }
    return review;
  }
  if (
    command === "screenshot_take" &&
    is_focused_asset_request(context.prompt)
  )
  {
    if (!context.asset_viewer_asset_id)
    {
      return {
        ok: false,
        error:
          "register or select the reusable asset before taking an Asset Viewer screenshot",
      };
    }
    const screenshot = await run.tool(
      "asset_viewer_screenshot",
      {
        path:
          args.path ??
          `asset_${context.asset_viewer_asset_id}.png`,
        width: args.width ?? 768,
        height: args.height ?? 768,
      },
      10000,
    );
    if (
      screenshot.ok &&
      screenshot.async &&
      screenshot.path
    )
    {
      screenshot.ready = await wait_for_screenshot(
        screenshot.path,
        10000,
      );
      if (!screenshot.ready)
      {
        return {
          ...screenshot,
          ok: false,
          error:
            "Asset Viewer screenshot was not written within 10 seconds",
        };
      }
    }
    return screenshot;
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
            .startsWith(
              "project/mcp_resources/thumbnails/",
            )
            ? requested_path
            : `project/mcp_resources/thumbnails/${file_name}`,
      },
      60000,
    );
  }
  if (command === "viewport_frame")
  {
    if (is_focused_asset_request(context.prompt))
    {
      const result = await run.tool(
        context.asset_viewer_asset_id
          ? "asset_viewer_set_view"
          : "asset_viewer_open",
        context.asset_viewer_asset_id
          ? {
              view:
                args.view === "side"
                  ? "right"
                  : args.view ?? "perspective",
            }
          : {},
        10000,
      );
      return {
        ...result,
        target: "asset_viewer",
        note:
          context.asset_viewer_asset_id
            ? "framed in Asset Viewer"
            : "Asset Viewer opened; register or select the reusable asset before framing",
      };
    }
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
  if (
    command === "material_create" ||
    command === "material_semantic_create" ||
    command === "prefab_save"
  )
  {
    // only the isolated root a focused asset was built under, anything else being saved is a scene
    const target_id = args.id ?? args.entity_id;
    const game_ready =
      command === "prefab_save" &&
      context.authoring_root_id &&
      String(context.authoring_root_id) === String(target_id)
        ? await make_game_ready(context, target_id)
        : null;
    const result = await run.tool(
      command,
      args,
      60000,
    );
    if (game_ready)
    {
      result.game_ready = game_ready;
    }
    return register_assistant_asset(
      context,
      command,
      args,
      result,
    );
  }
  if (
    command === "asset_viewer_open" ||
    command === "asset_viewer_status" ||
    command === "asset_viewer_select" ||
    command === "asset_viewer_preview_entity" ||
    command === "asset_viewer_set_view" ||
    command === "asset_viewer_screenshot"
  )
  {
    if (command === "asset_viewer_screenshot")
    {
      args = {
        ...args,
        path:
          args.path ??
          args.filename ??
          args.file ??
          args.file_path ??
          args.name ??
          `asset_${
            context.asset_viewer_asset_id ?? "preview"
          }.png`,
      };
      delete args.filename;
      delete args.file;
      delete args.file_path;
      delete args.name;
    }
    const result = await run.tool(
      command,
      args,
      15000,
    );
    if (
      command === "asset_viewer_select" &&
      result.ok
    )
    {
      context.asset_viewer_asset_id =
        result.selected_asset_id ??
        args.asset_id ??
        args.id;
    }

    // the engine deletes the target file, queues a render, and answers before the image exists, because it
    // cannot block the thread that has to draw the frame it is waiting for. scene_visual_review waits for the
    // file, this path did not, so a direct capture answered ready false and whatever read the path next got
    // the leftover from an earlier capture instead of the asset as it is now
    if (
      command === "asset_viewer_screenshot" &&
      result.ok &&
      result.ready !== true
    )
    {
      result.ready = await wait_for_screenshot(
        result.path ?? args.path,
        12000,
      );
      result.async = false;
      if (!result.ready)
      {
        return {
          ...result,
          ok: false,
          error:
            "the asset viewer did not finish rendering the screenshot, the file was never written",
          suggested_action:
            "confirm an asset is previewing with asset_viewer_status, then capture again",
        };
      }
    }
    return result;
  }
  if (command === "world_load" || command === "world_new")
  {
    const result = await run.tool(
      command,
      args,
      60000,
    );
    if (result.ok)
    {
      context.resource_directory = null;
      await assistant_resource_directory(context);
    }
    return result;
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
    if (assistant_context.bridge_failure)
    {
      return {
        ok: false,
        error: assistant_context.bridge_failure,
        code: "engine_bridge_unhealthy",
        retryable: false,
        suggested_action:
          "restart the engine before starting another assistant run",
      };
    }
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
    if (assistant_context.bridge_failure)
    {
      release_command();
      return {
        ok: false,
        error: assistant_context.bridge_failure,
        code: "engine_bridge_unhealthy",
        retryable: false,
        suggested_action:
          "restart the engine before starting another assistant run",
      };
    }
    try
    {
      const result = await dispatch_assistant_command(
        assistant_context,
        command,
        command_arguments,
      );
      if (is_engine_bridge_failure(result))
      {
        assistant_context.bridge_failure =
          `engine bridge became unresponsive during ${command}`;
        assistant_context.cancel_on_bridge_failure?.(
          assistant_context.bridge_failure,
        );
        return {
          ...result,
          retryable: false,
          suggested_action:
            "restart the engine before starting another assistant run",
        };
      }
      await save_asset_progress(
        assistant_context,
        command,
        result,
      );
      return track_asset_budget(
        assistant_context,
        command,
        command_arguments,
        result,
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

// asks the library, once, for every object this scene is going to need. the agent was already told in
// prose to search before building and reliably did not, partly because it only ever learns it needs a
// table halfway through, so the asking happens here where the inventory is already known
async function prepare_asset_reuse_plan(
  context,
  run,
  brief,
  prepared_plan,
)
{
  const items = [
    ...inventory_from_brief(brief),
    ...inventory_from_plan(prepared_plan?.plan),
  ];
  if (items.length === 0)
  {
    return null;
  }

  const plan = await build_reuse_plan({
    project_root: get_project_root(),
    resource_directory:
      await assistant_resource_directory(context),
    items,
  });

  run.receipt("asset library checked", {
    wanted: items.length,
    reusable: plan.reuse.length,
    to_build: plan.missing.length,
    library_size: plan.library_size,
    reuse: plan.reuse.map((entry) => ({
      wanted: entry.wanted,
      asset_id: entry.candidates[0]?.asset_id,
    })),
  });
  return plan;
}

async function prepare_asset_library_context(
  context,
  prompt,
  prepared_plan,
)
{
  const elements = prepared_plan?.plan?.elements ?? [];
  const requests = [
    {
      query: prompt,
      tags: [],
    },
    ...elements.slice(0, 12).map((element) => ({
      query:
        element.name ??
        element.role ??
        element.purpose ??
        "",
      tags: element.semantic_tags ?? [],
    })),
  ].filter((request) => request.query);
  const resource_directory =
    await assistant_resource_directory(context);
  const matches = [];
  const seen = new Set();
  for (const request of requests)
  {
    const result = await world_asset_search(
      get_project_root(),
      resource_directory,
      {
        ...request,
        limit: 3,
      },
    );
    for (const asset of result.matches ?? [])
    {
      if (
        !asset.active_version ||
        seen.has(asset.id)
      )
      {
        continue;
      }
      seen.add(asset.id);
      matches.push(asset);
    }
  }
  return matches.slice(0, 20);
}

// every asset here is an environment prop for a video game unless the request says otherwise, and only the
// request can say otherwise. the previous wording made every prompt a hero prop and told the run that part
// count was never worth quality, which is how a flat screen television for a living room ended up with
// modelled hdmi ports, screw recesses, speaker perforations and a hundred and nineteen thousand triangles
//
// the escalation words are deliberately narrow. detailed, nice and high quality are words people use about
// ordinary work, so they must not buy a thirty thousand triangle budget
function asset_detail_budget(prompt)
{
  const value = String(prompt ?? "").toLowerCase();

  if (
    /\b(?:blockout|block[\s-]?out|greybox|grey[\s-]?box|proxy|placeholder|stand[\s-]?in|low[\s-]?poly|rough)\b/
      .test(value)
  )
  {
    return {
      tier: "blockout",
      triangles: 800,
      parts: 6,
      materials: 2,
    };
  }
  const explicitly_hero =
    /\bhero[\s-]+(?:asset|prop|quality)\b/.test(value) &&
    !/\b(?:not|no|non)[\s-]+(?:a[\s-]+)?hero[\s-]+(?:asset|prop|quality)\b/
      .test(value);
  if (explicitly_hero)
  {
    return {
      tier: "hero",
      triangles: 30000,
      parts: 40,
      materials: 8,
    };
  }
  return {
    tier: "environment_prop",
    triangles: 6000,
    parts: 12,
    materials: 4,
  };
}

function focused_asset_quality_prompt_lines(prompt)
{
  const budget = asset_detail_budget(prompt);
  const tier_line =
    budget.tier === "blockout"
      ? "This request asked for a blockout, so build the massing and proportions only and stop there."
      : budget.tier === "hero"
        ? "This request asked for a hero asset, so it earns close-up detail. Spend it on the silhouette and on the surfaces that face the viewer, never on hidden faces."
        : "This is an environment prop. That is the default for every asset request and only the explicit words hero asset or hero quality can promote it. Close-up, photorealistic, detailed, premium, flagship and high quality do not change the tier. An environment prop is placed in a game alongside many other objects and seen from a normal viewing distance, so build it recognisable, correctly proportioned, cleanly made and game ready, then stop.";

  return [
    "Focused asset quality standard:",
    "Everything you build here is a real-time asset for a video game. Assume it has to render in a frame alongside hundreds of others, on a budget, from a normal viewing distance. It is not for a render, a film, a turntable, or a portfolio piece. Triangles and draw calls are the currency you are spending and the game is what you are spending them on.",
    tier_line,
    `Budget for this asset: about ${budget.triangles.toLocaleString("en-US")} triangles in total, at most ${budget.parts} authored parts and at most ${budget.materials} materials. Treat these as real limits. Reuse materials across parts, merge geometry that shares a material, and spend geometry on the shape the object is recognised by. Coming in well under budget with a clean, readable object is better than using all of it.`,
    "Model what changes the silhouette or the material. Everything else is the texture's job. Do not model fasteners, screws, screw recesses, ports, sockets, connectors, cables, vents, grilles, perforations, panel seams, embossed text, regulatory markings, badges, or logos as geometry. Those belong in the colour, normal and roughness maps, where they cost nothing.",
    "Do not model anything the object hides from the viewer. A television, a wardrobe or a fridge stands against a wall, so its back is a flat panel with a material on it. A cabinet has no interior unless it opens. Nothing has internal components. If a surface is never seen in normal use, it is one quad.",
    "Infer the ordinary real-world construction, proportions, silhouette transitions, wall thickness, joins, rims, bevels, and material boundaries that make the object recognizable and credible. The user should not need to enumerate standard object anatomy.",
    "Build the primary form first, then add only secondary construction that changes the silhouette, function or material boundary. Put tertiary identity detail into textures and stop when the prop reads clearly at normal gameplay distance.",
    "Do not approximate a continuous manufactured or organic surface by visibly stacking cylinders, boxes, spheres, cones, or capsules. Use mesh_generate, variable lofts, sweeps, profiles, shells, bends, tapers, or mesh_raw_create to produce continuous curved transitions with enough radial and longitudinal resolution for a clean solid silhouette.",
    "Primitives are acceptable only for hidden construction, genuinely primitive parts, or an explicitly requested blockout. If several visible primitive sections merely trace one continuous outline, replace them with one coherent generated surface.",
    "Use physically plausible dimensions and thickness. Avoid coplanar overlaps, open shells, abrupt radius jumps, floating trim, z-fighting, and decorative parts that do not follow the parent surface.",
    "For transparent materials, model the actual outer and inner surfaces or a valid shell and preserve believable thickness at rims and openings. Do not rely on transparency to imply missing geometry.",
    "Build only the reusable object under its prepared root. Do not surround it with a ground pad, route, display structure, studio set, or review lights unless the user explicitly requests those as part of the asset.",
    "Review the candidate in the Asset Viewer using solid mode first, then wire or points only for topology diagnosis. Inspect perspective plus front or side views. A recognizable outline, smooth curvature, clean transitions, and readable secondary details are mandatory before registration.",
    "Compare against any active version and never promote a candidate that regresses silhouette, topology or necessary material separation. Quality scoring never authorizes exceeding the prop budget or adding hero detail. Correction passes should improve proportions, placement, topology and textures before adding geometry.",
    "Saving the prefab merges every part that shares a material into one mesh, so splitting a surface off for a genuine material change is cheap. That is a reason to split for material and construction, not a reason to ignore the budget above, because merging parts does not remove a single triangle.",
    "Never generate the same part twice. Before adding a part, check whether you already made it. A regenerated duplicate wastes the budget and leaves two copies of the same geometry in the asset.",
    "Author repetition as one mesh instead of one mesh per copy. When the same shape repeats in the same material, generate it once with the array and mirror modifiers on that mesh_generate call: radial_count with radial_axis, radial_radius and radial_step_degrees for spokes, castors, legs, bolts, flutes and anything arranged around an axis; linear_count with linear_step for slats, ribs, treads, rungs and rows; mirror_axis with mirror_plane for a symmetric pair such as two armrests. A five-spoke base is one call, not five. This is identical geometry at a fraction of the parts, so prefer it over generating each copy separately.",
    "Give a part its own entity only when it needs its own material or its own geometry. Do not split one surface across several entities that all end up with the same material, and keep a collider, light, or sound on the functional entity it belongs to rather than on a part that exists only to be drawn.",
    "Assemble the asset as you go, one part at a time. The prefab root already exists and is already saved and previewing, so every part you make is joined to the asset the moment you make it: generate the part, parent it to the root, give it its material, and place it against the parts that are already there. Do not author a batch of loose meshes and materials with the intention of assembling them later.",
    "Work outward from the part that fixes the asset's scale and orientation, usually the primary body or the base, because every later part is positioned against what is already standing. Finish and place each part before starting the next one.",
    "Look at the asset while you build it, not only when it is finished. After each part that changes the silhouette, take one Asset Viewer screenshot and check that the new part sits where you intended, at the right size, touching what it should touch. Fix a part that landed wrong immediately, while it is the only thing that could be wrong. A single review at the end cannot tell you which part is misplaced.",
    "The Asset Viewer preview follows the root live, so the asset is visible as it grows. Never activate the workspace root or move it into the scene to look at it, and never capture the main viewport for this. Preview and screenshot through the Asset Viewer.",
  ];
}

function asset_revision_prompt_lines(revision)
{
  const aspects = revision.aspects ?? [];

  if (!revision.root_id)
  {
    return [
      `This request continues work on an asset that already exists. It is not a request for a new asset. The name in the request, "${revision.hint}", matches several library assets equally well: ${revision.ambiguous.join(", ")}.`,
      "Call world_asset_inspect on each of those and decide which one the request actually means. Then load it with world_asset_load, preview it with asset_viewer_preview_entity, and revise that asset in place.",
      "If you genuinely cannot tell which one is meant, change nothing, and reply naming the candidates and asking which. Do not build a new asset, the library already holds what the request is about, and do not revise all of them.",
      "Once you have chosen, change only what the request asks for and leave the rest of the asset alone. Publish the result as a new version of that same asset id with world_asset_register plus world_asset_promote. Never register it under a new asset id.",
    ];
  }

  const lines = [
    "This request continues work on an asset that already exists. It is not a request for a new asset.",
    `The asset is already loaded and previewing in the Asset Viewer as entity id ${revision.root_id} named ${revision.root_name}, taken from ${revision.source}. Work on that entity. Do not create a second root, and do not delete and rebuild the asset from scratch.`,
    "Change only what the request asks for, and leave the rest of the asset exactly as it is. Everything you find already there was deliberate. If a requested change forces a neighbouring part to change with it, change that part too and say so, but do not take the opportunity to redesign anything else.",
    "Start by reading what is there before changing it. entity_get on the root with descendants, entity_render_materials for the material on each part, mesh_raw_get when you need the actual geometry of a part, and material_get for the properties and texture slots you are about to alter. A change made without reading the current value first is a guess.",
    "Prefer the narrowest tool that expresses the change. A property is material_set_property. A map is texture_generate plus material_set_texture. A dimension or profile is a regenerated part via mesh_generate for that part alone. Rebuild a part only when its geometry itself has to differ.",
  ];

  if (aspects.includes("geometry"))
  {
    lines.push(
      "The geometry has to change. Identify the specific parts involved, keep the rest of the hierarchy and every material assignment intact, and preserve the proportions and detail that were not mentioned. When you replace a part, give it the same name and the same material as the part it replaces so the asset stays consistent.",
    );
  }
  if (aspects.includes("material"))
  {
    lines.push(
      "A material has to change. Edit the existing material with material_set_property rather than creating a replacement, so every part already using it stays in step. Create a new material only when the request is asking for one part to stop matching the others.",
    );
  }
  if (aspects.includes("texture"))
  {
    lines.push(
      "A texture has to change. Regenerate the affected map with texture_generate using layers, keep the resolution, tiling and seamless setting the existing map used unless the request is about those, and reattach it to the same material slot. A label or decal stays non-seamless with alpha.",
    );
  }

  lines.push(
    `Publish the result as a new version of the same catalog asset. Call world_asset_register with type ${revision.asset_type}, asset_id ${revision.asset_id}, and parent_version ${revision.active_version ?? "the active version"}, then world_asset_promote once the Asset Viewer review and checks pass. Never register this as a new asset id, the history of this asset has to stay in one place.`,
    `Before promoting, compare against the active version with world_asset_compare. Promote only if the requested change is actually visible and nothing else regressed. If your change made the asset worse, say so and keep the active version rather than promoting a regression.`,
    `Asset being revised: ${safe_json(
      {
        asset_id: revision.asset_id,
        name: revision.asset_name,
        type: revision.asset_type,
        active_version: revision.active_version,
        aliases: revision.aliases,
        tags: revision.tags,
        constraints: revision.constraints,
        root_id: revision.root_id,
        parts: revision.parts,
      },
      6000,
    )}`,
  );

  if ((revision.alternatives ?? []).length > 0)
  {
    lines.push(
      `The request matched this asset best, but the library also holds ${revision.alternatives.join(", ")}. If the loaded asset is clearly not the one the user meant, stop and say which you think they meant instead of revising the wrong asset.`,
    );
  }

  return lines;
}

function build_prompt(
  prompt,
  snapshot,
  intent = null,
  prepared_plan = null,
  prepared_assets = [],
  brief = "",
  reuse_plan = null,
  revision = null,
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
    "For every new build, design directly from the current request and prepared context. Do not search for persisted layouts, build definitions, or prior generated instructions.",
    "Before you build any recognisable object, ask the library for it first. Call world_asset_search with the plain object name, then with semantic aliases, tags, dimensions, style, and material constraints. If a promoted match fits, load it with world_asset_load and place it, rather than modelling or approximating it again. Primitives are the fallback for objects the library does not have, never the first choice for objects it might.",
    "For a focused single-asset request, build the asset in isolation as a game-ready environment prop with budgeted geometry and a small reused material set, review it, register an immutable version, and promote it only after verified checks. Only an explicit request for a hero asset or hero quality changes this tier.",
    "For focused asset work, begin editing the prepared asset root immediately. Do not spend multiple minutes narrating, repeating lookups, or redesigning the prepared baseline before the first mutation.",
    "For focused asset work, never move or capture the main scene viewport. Register the candidate, use asset_viewer_select, asset_viewer_set_view, and asset_viewer_screenshot, and perform scene_visual_review through the Asset Viewer.",
    "For an environment build, reuse promoted library assets where they fit. Attempt at most one focused improvement per reused asset during the run; otherwise keep the active version and continue the environment.",
    "When an environment build makes you model a recognisable standalone object the library did not have, register it as a library prefab once it looks right, and give it plain aliases and tags a later request would search by, meaning the everyday name of the object rather than its role in this scene. A workbench is registered as workbench with the alias table, not as rear_wall_prop. This is how the library grows enough to make the next blockout better than this one.",
    "Every persistent resource created through MCP belongs under the shared project/mcp_resources directory. Put meshes, materials, textures, prefabs, editable sources, thumbnails, and catalog metadata in their matching shared subdirectories. Never write MCP-generated resources into a world-specific resource directory.",
    "Use camera_snapshot before camera-relative placement such as in front of camera, beside camera, or from camera.",
    "Use world_raycast for ground or surface-relative placement instead of assuming y=0 when precision matters.",
    "Before deleting or rebuilding existing geometry while preserving look, call entity_render_materials on the target parent and reuse material names in entity_create_primitive_batch or component_set.",
    "Use mesh_geometry_capabilities before deciding that requested procedural geometry is unavailable.",
    "Prefer concave extruded profiles, multi-opening walls, variable lofts or sweeps, shell thickness, and seam-split box UVs when they express the design better than stacked boxes.",
    "When one shape repeats in one material, generate it once with the array or mirror modifiers on that mesh_generate call rather than once per copy: radial_count for anything arranged around an axis, linear_count with linear_step for rows, mirror_axis for a symmetric pair. The geometry is identical and the part count collapses.",
    "For multiple materials, split semantic surfaces into compound parts because one render entity owns one material. Saving a focused asset merges the parts that ended up sharing a material back into one mesh, so split for material and construction reasons rather than counting parts.",
    "Texture every material that represents a real surface. Use material_textured_create so the material and its color, roughness, normal and packed maps are made together, and set tiling so the pattern reads at the right scale.",
    "Build textures from layers: fill for the base, noise for variation, bricks, tiles, stripes or checker for structure, spots and scratches for wear and dirt, shape and text for labels, signage and decals.",
    "Give layers relief for bumps, roughness and roughness_b for finish, and metalness for metal, otherwise the surface stays flat and uniformly shiny.",
    "Keep environment textures seamless and check seam_error in the response. Labels and decals are not tiled, so set seamless false and use alpha.",
    "Skip textures only for glass, pure emitters, and placeholder greybox volumes.",
    "Add collision only where gameplay needs it. For focused assets, use one simplified collider on the functional root or primary body; never add mesh_convex physics to decorative shells, threads, labels, liners, trim, or repeated details. For environments, cover structural and traversable surfaces without giving every visual detail its own body.",
    "Use entity_create_light for every light. Never hand-roll lights with entity_create_empty + entity_add_component light + component_set; that path leaves weak invisible lights.",
    "A light entity satisfies a key_lights plan element. Never add a render component to it or fabricate emissive primitives merely to satisfy an audit. If the requested scene needs a visible fixture, create a separate child primitive and tag it as detail rather than light or plan_element:key_lights.",
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
    "Known Lua facts if you must use it: World.CreateEntity, World.GetEntityByName, World.GetEntityById(id_string), entity:SetParent, entity:AddComponent(ComponentType.Render|Light|...), Render:SetMesh(MeshType.Cube), Light:SetLightType(LightType.Point), never pairs() on World.GetEntities or GetChildren, use ForEachChild instead.",
    "When you learn a durable lesson, correction, recurring problem, or maintainer improvement idea, update agent memory concisely.",
    "world_resources_clean is available for explicit cleanup receipts. Finished scene construction runs it automatically.",
    "Do not reveal hidden chain of thought. Report only brief progress, blockers, and final results.",
  ];

  const focused_asset =
    is_focused_asset_request(prompt) ||
    Boolean(revision);
  if (focused_asset)
  {
    lines.push(...focused_asset_quality_prompt_lines(prompt));
  }

  // the scene construction block below plans zones, circulation and lighting, which is the wrong shape of
  // work for changing one part of one object, and its stage list would talk the run into a rebuild
  if (
    !focused_asset &&
    (intent?.kind === "scene_rebuild" || intent?.live_scene_action)
  )
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
    if (prepared_assets.length > 0)
    {
      lines.push(
        "Promoted reusable asset matches were searched before this run. Reuse suitable entries with world_asset_load; inspect or improve only when their constraints do not fit.",
        `Prepared asset matches: ${safe_json(prepared_assets, 5000)}`,
      );
    }
  }

  if (intent?.target_name && !revision)
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
  if (
    !focused_asset &&
    (intent?.kind === "scene_rebuild" || intent?.live_scene_action)
  )
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

  // the brief is what the request implies rather than what it says, so it is advice, the request above
  // still decides what gets built and wins any disagreement between the two
  if (String(brief ?? "").trim().length > 0)
  {
    lines.push(
      "",
      focused_asset
        ? "Design brief expanded from that request. Use it only to clarify proportions, construction and materials within the environment-prop budget above. It cannot increase the triangle, part or material limits, and it cannot promote the asset to hero quality. Where it conflicts with the request or budget, ignore it."
        : "Design brief expanded from that request. Treat it as the default specification for anything the request left unsaid, and build to this level of detail. Where the brief and the request disagree, the request wins. Where the brief is wrong about this subject, correct it rather than following it.",
      String(brief).trim(),
    );
  }

  // the library answer goes last, after the request and the brief, because it is the part most likely
  // to be skipped and the part that decides whether this ends up boxes or a furnished room
  const reuse_lines = reuse_prompt_lines(reuse_plan);
  if (reuse_lines.length > 0)
  {
    lines.push("", ...reuse_lines);
  }

  // a revision goes after even that, it is the instruction the rest of this prompt most needs overriding by,
  // because everything above is written for building something that does not exist yet
  if (revision)
  {
    lines.push("", ...asset_revision_prompt_lines(revision));
  }

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

async function resolve_quality_root(
  run,
  target_name,
  attempts = 10,
)
{
  let last_result = {
    ok: false,
    error: "quality root not found",
  };
  for (let attempt = 0; attempt < attempts; attempt++)
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
    if (is_engine_bridge_failure(exact))
    {
      return {
        ...exact,
        root: null,
      };
    }
    if (exact.ok && exact_root)
    {
      return {
        ...exact,
        root: exact_root,
        resolution: "exact_name",
      };
    }
    if (attempt < attempts - 1)
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

// a bare request for one object, make a book, is single asset work even though it never says the word asset
//
// without this such a request went down the scene path, which planned zones and circulation for a book and
// skipped the focused asset rules, including the one that says not to build a studio set around the subject
function is_bare_object_build(value)
{
  const match = value.match(
    /^\s*(?:please\s+)?(?:could\s+you\s+|can\s+you\s+|i\s+want\s+you\s+to\s+)?(?:create|make|build|generate|design|model)\s+(?:me\s+)?(?:a|an)\s+([a-z0-9][a-z0-9 _-]{0,60}?)(?=\s*$|[,.;]|\s+(?:with|that|which|featuring|made\s+of|using|from)\b)/,
  );
  if (!match?.[1])
  {
    return false;
  }

  const subject = match[1].trim();

  // only the head noun decides, a place word before it is a modifier. an office chair is a chair, a
  // warehouse interior is an interior
  const head = subject
    .split(/[\s_-]+/)
    .filter(Boolean)
    .pop() ?? "";
  if (subject.length < 3 || names_a_place(head))
  {
    return false;
  }

  // several subjects make a scene, and a placement phrase means the object is being put somewhere rather
  // than authored, which is scene work either way
  if (/\b(?:and|plus|along\s+with|together\s+with)\b/.test(value))
  {
    return false;
  }
  if (
    /\b(?:onto|inside|next\s+to|beside|around|near|under|underneath|above|behind|in\s+front\s+of|scattered|arranged|placed|populate|fill)\b/.test(
      value,
    ) ||
    /\bon\s+(?:a|an|the)\b/.test(value)
  )
  {
    return false;
  }

  return true;
}

// an entity name becomes a file name, and a prefab whose name came from the user's words has to survive
// spaces and punctuation without producing a path the engine will reject
function asset_file_name(value)
{
  const safe = String(value ?? "asset")
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return safe || "asset";
}

function is_focused_asset_request(prompt)
{
  const value = String(prompt ?? "").toLowerCase();
  return (
    /\bfocused[\s-]+(?:hero[\s-]+)?asset\b/.test(value) ||
    /\breusable\s+.+\s+(?:model|asset)\b/.test(value) ||
    /\basset[\s-]+(?:library|catalog|catalogue)\b/.test(value) ||
    /\b(?:standalone|isolated|hero)[\s-]+(?:asset|model|prop)\b/.test(value) ||
    /\b(?:create|make|build|generate|design|model)\b[^.\n]{0,120}\b(?:asset|prefab|prop|model)\b/.test(value) ||
    is_bare_object_build(value)
  );
}

async function prepare_focused_asset_root(
  run,
  snapshot,
  target_name,
)
{
  const prefab_path = `${asset_file_name(target_name)}.prefab`;

  // the prefab exists before the first part does. building every mesh and material first and assembling at
  // the end asks the model to hold the whole object in its head and hope the pieces fit, and it means a run
  // that stops early leaves a pile of parts with nothing that joins them
  const open_workspace = async (root) =>
  {
    await run.tool(
      "asset_viewer_preview_entity",
      {
        id: root.id,
      },
      10000,
    );
    const saved = await run.tool(
      "prefab_save",
      {
        id: root.id,
        path: prefab_path,
      },
      60000,
    );
    return {
      ...root,
      prefab_path,
      prefab_ready: saved?.ok === true,
    };
  };

  const existing = await resolve_quality_root(
    run,
    target_name,
    1,
  );
  if (existing.ok && existing.root?.id)
  {
    await run.tool(
      "entity_update",
      {
        id: existing.root.id,
        active: false,
        transient: true,
      },
      10000,
    );
    return open_workspace(existing.root);
  }

  const created = await run.tool(
    "entity_create_empty",
    {
      name: target_name,
      position: [0, 0, 0],
      active: false,
      transient: true,
      tags: [
        "authoring_workspace",
        "mcp_generated",
      ],
    },
    10000,
  );
  if (!created.ok)
  {
    return null;
  }

  return open_workspace(created.entity);
}

// puts the asset the user named in front of the agent, already loaded, so the run continues the asset
// instead of designing a replacement for it
//
// the router only recognised the shape of the request, so this is where the claim is tested. if the named
// asset is not in the library the answer is null and the run carries on as an ordinary build, which is the
// right outcome, a request to revise something that does not exist is a request to create it
async function prepare_asset_revision({
  context,
  intent,
  run,
})
{
  const resolved = await resolve_asset_by_name({
    project_root: get_project_root(),
    resource_directory:
      await assistant_resource_directory(context),
    hint: intent?.asset_hint ?? "",
  });
  if (!resolved.ok)
  {
    run.receipt("asset revision declined", {
      hint: intent?.asset_hint ?? "",
      reason: resolved.reason,
      ambiguous: resolved.ambiguous ?? [],
    });

    // several assets answer the name equally well. loading one of them would edit the wrong asset and
    // loading none of them would quietly build a duplicate of something the library already has, so the
    // choice goes to the agent, which can read the request and inspect the candidates
    if ((resolved.ambiguous ?? []).length > 0)
    {
      return {
        ambiguous: resolved.ambiguous,
        hint: intent?.asset_hint ?? "",
        aspects: intent?.revision_aspects ?? [],
        root_id: null,
      };
    }
    return null;
  }

  const asset = resolved.asset;

  // a root already in the world is the copy the user has been looking at, reusing it keeps whatever was
  // done since the last promotion instead of reverting to the promoted version behind their back
  const live = await resolve_quality_root(
    run,
    asset.name || asset.id,
    1,
  );
  let root = live.ok && live.root?.id ? live.root : null;
  let source = "live scene";

  if (!root)
  {
    const loaded = await world_asset_load(
      get_project_root(),
      await assistant_resource_directory(context),
      { asset_id: asset.id },
      (command, args) => run.tool(command, args, 30000),
    );
    if (!loaded.ok)
    {
      run.receipt("asset revision declined", {
        asset_id: asset.id,
        reason: `could not load the active version: ${loaded.error ?? "unknown error"}`,
      });
      return null;
    }

    source = `version ${loaded.version?.id ?? "unknown"}`;

    // a prefab spawns its hierarchy, anything else only warms the resource cache, so a mesh needs an
    // entity built around it before there is something to edit
    if (asset.type === "prefab")
    {
      const spawned = await resolve_quality_root(
        run,
        asset.name || asset.id,
        4,
      );
      root = spawned.ok && spawned.root?.id ? spawned.root : null;
    }
    else if (asset.type === "mesh")
    {
      const created = await run.tool(
        "entity_create_empty",
        {
          name: asset.name || asset.id,
          position: [0, 0, 0],
          active: false,
          transient: true,
          tags: [
            "authoring_workspace",
            "mcp_generated",
          ],
        },
        10000,
      );
      if (created.ok && created.entity?.id)
      {
        await run.tool(
          "render_set_mesh",
          {
            id: created.entity.id,
            mesh: loaded.version.path,
          },
          20000,
        );
        root = created.entity;
      }
    }
  }

  if (!root?.id)
  {
    run.receipt("asset revision declined", {
      asset_id: asset.id,
      reason:
        "the active version loaded but produced no entity to edit",
    });
    return null;
  }

  // work happens off the main viewport, the same isolation a focused build gets
  await run.tool(
    "entity_update",
    {
      id: root.id,
      active: false,
      transient: true,
    },
    10000,
  );
  await run.tool(
    "asset_viewer_open",
    {},
    10000,
  );
  await run.tool(
    "asset_viewer_preview_entity",
    { id: root.id },
    10000,
  );

  // the parts and their materials are what the change has to be aimed at, handing them over saves the
  // agent from rediscovering the asset before it can touch it, which is where the redesign creeps in
  const parts = await run.tool(
    "entity_render_materials",
    { id: root.id },
    20000,
  );

  const revision = {
    asset_id: asset.id,
    asset_name: asset.name || asset.id,
    asset_type: asset.type,
    active_version: asset.active_version,
    aliases: asset.aliases ?? [],
    tags: asset.tags ?? [],
    constraints: asset.constraints ?? {},
    root_id: root.id,
    root_name: root.name ?? asset.name ?? asset.id,
    source,
    aspects: intent?.revision_aspects ?? [],
    parts: parts.ok ? (parts.materials ?? []) : [],
    matched_on: resolved.matched_on ?? [],
    alternatives: resolved.alternatives ?? [],
  };

  run.receipt("asset revision prepared", {
    asset_id: revision.asset_id,
    asset_type: revision.asset_type,
    active_version: revision.active_version,
    root_id: revision.root_id,
    source: revision.source,
    aspects: revision.aspects,
    part_count: Array.isArray(revision.parts)
      ? revision.parts.length
      : 0,
  });
  return revision;
}

async function prepare_scene_build_plan({
  prompt,
  intent,
  run,
})
{
  if (is_focused_asset_request(prompt))
  {
    return null;
  }
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

async function run_cursor_fallback_serial({ prompt, brief = "", api_key, model_id, engine_host, engine_port, run, timeout_ms, engine_first_timeout_ms, intent = null }) {
  if (!api_key) {
    return {
      ok: false,
      text: "Cursor API key is missing. Paste it into the MCP Assistant window first.",
    };
  }

  let cursor_run = null;
  let engine_tool_seen = false;
  let scene_mutation_seen = false;
  let cancel_message = "";
  active_assistant_context = {
    run,
    prompt,
    engine_host,
    engine_port,
    intent,
    asset_budget: is_focused_asset_request(prompt)
      ? asset_detail_budget(prompt)
      : null,
    asset_triangles: 0,
    bridge_failure: "",
    cancel_on_bridge_failure: (message) =>
    {
      cancel_message = message;
      run.event("stage_note", { text: message });
      if (cursor_run?.supports?.("cancel"))
      {
        void cursor_run.cancel();
      }
    },
  };
  let guard_timer = null;
  let idle_timer = null;
  let activity_flush_timer = null;
  let last_activity_at = Date.now();
  let visual_review_seen = false;
  let asset_viewer_reviews = 0;
  active_assistant_context.mark_visual_review = () =>
  {
    visual_review_seen = true;
  };
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
    // looking at the asset through the viewer directly counts as having looked at it. crediting only
    // scene_visual_review meant a run that framed and shot the asset six times still failed the gate, which
    // spent two correction passes re-reviewing work that was already reviewed and then reported a failure
    // for a build that had passed
    // the screenshot reply comes back before the image is on disk, so acceptance and a path is the only
    // signal available at this point, and two of them means the asset was looked at from more than one side
    if (
      is_named_tool_event(event, "asset_viewer_screenshot") &&
      object_contains(event, (value) =>
        value.ok === true &&
        typeof value.path === "string" &&
        value.path.length > 0,
      )
    )
    {
      asset_viewer_reviews += 1;
    }
    visual_review_seen ||=
      successful_visual_review ||
      asset_viewer_reviews >= 2;
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
      await resolve_world_resource_directory(
        (command, args) => run.tool(
          command,
          args,
          60000,
        ),
        snapshot.world,
      );
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
    const prepared_assets = await run.stage(
      "Search Asset Library",
      "finding promoted reusable assets for this request",
      () => prepare_asset_library_context(
        active_assistant_context,
        prompt,
        prepared_plan,
      ),
    );
    const reuse_plan = await run.stage(
      "Match Library To Scene",
      "checking the library for every object this scene needs",
      () => prepare_asset_reuse_plan(
        active_assistant_context,
        run,
        brief,
        prepared_plan,
      ),
    );
    let initial_root = null;
    let revision = null;
    if (intent?.kind === "asset_revise")
    {
      revision = await run.stage(
        "Open Asset For Revision",
        "loading the asset the request names",
        () => prepare_asset_revision({
          context: active_assistant_context,
          intent,
          run,
        }),
      );
      if (revision?.root_id)
      {
        initial_root = {
          id: revision.root_id,
          name: revision.root_name,
        };
        scene_mutation_seen = true;
        active_assistant_context.authoring_root_id =
          revision.root_id;

        // the router guessed the root name from the user's words, the catalog knows the real one, and the
        // quality gate downstream resolves the root by that name
        if (revision.root_name !== intent.target_name)
        {
          intent = {
            ...intent,
            target_name: revision.root_name,
          };
          active_assistant_context.intent = intent;
        }
      }
    }

    if (
      !revision &&
      intent?.target_name &&
      is_focused_asset_request(prompt)
    )
    {
      initial_root = await run.stage(
        "Prepare Asset Workspace",
        "creating an isolated root for focused asset work",
        () => prepare_focused_asset_root(
          run,
          snapshot,
          intent.target_name,
        ),
      );
      scene_mutation_seen = Boolean(initial_root?.id);
      // remembering the root is what lets the save path collapse it, a save of anything else is a scene
      // and has to be left alone
      active_assistant_context.authoring_root_id =
        initial_root?.id ?? null;
      active_assistant_context.authoring_prefab_path =
        initial_root?.prefab_path ?? null;
      await run.tool(
        "asset_viewer_open",
        {},
        10000,
      );
    }
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
      const resolved_initial_root = initial_root
        ? {
            ok: true,
            root: initial_root,
          }
        : intent.target_name
          ? await resolve_quality_root(
          run,
          intent.target_name,
          1,
        )
          : await resolve_selected_quality_root(run);
      if (
        resolved_initial_root.ok &&
        resolved_initial_root.root?.id &&
        !is_focused_asset_request(prompt)
      )
      {
        await run.stage(
          "Focus Build Location",
          "moving the editor camera to the build",
          () => run.tool(
            "viewport_frame",
            {
              id: resolved_initial_root.root.id,
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
          prepared_assets,
          brief,
          reuse_plan,
          revision,
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

    if (!is_focused_asset_request(prompt))
    {
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
    }

    const send_command = (name, args) =>
      run.tool(name, args);
    const plan =
      prepared_plan?.plan ??
      null;
    const planned_elements = plan?.elements ?? [];
    const focused_asset =
      is_focused_asset_request(prompt);
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
      ...(focused_asset
        ? {
            min_entities: 2,
            min_unique_materials: 1,
            min_advanced_mesh_ratio: 0.15,
            require_light: false,
            min_collision_ratio: 0,
            max_duplicate_geometry: 2,
            max_repetition_ratio: 0.5,
            max_dominant_geometry_ratio: 0.96,
          }
        : {}),
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
      if (focused_asset)
      {
        return {
          ok: true,
          pass: true,
          skipped: true,
          reason:
            "focused assets use geometry and visual quality gates",
        };
      }
      return audit_scene_layout(
        send_command,
        {
          ...layout_audit_args,
          plan,
        },
      );
    };
    let layout_audit = focused_asset
      ? await audit_current_layout()
      : await run.stage(
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
        ...(focused_asset
          ? [
              ...focused_asset_quality_prompt_lines(prompt),
              "This correction must replace any visibly stacked primitive approximation with coherent generated geometry before re-registration. Review focused assets from perspective, front, and side views in solid mode.",
              "If the asset is over its triangle budget, this pass reduces it. Delete geometry that does not read at normal viewing distance, starting with anything on a hidden face, and lower the segment counts on curved parts. Do not add parts during a correction pass unless the audit named a missing one.",
            ]
          : []),
        ...(focused_asset
          ? [
              "Do not create an environment plan, ground pad, route, display structure, or studio lights around the asset. The reusable object itself is the complete deliverable.",
              "Call scene_visual_review on the root with perspective, front, and side Asset Viewer views, then inspect the images.",
            ]
          : [
              "If the generic scene plan is missing or invalid, call scene_plan_create first with realistic expected dimensions, zones, support modes, relationships, and lighting intent inferred from the original request.",
              "Call scene_visual_review on the root with perspective and top views, then inspect both images.",
            ]),
        focused_asset
          ? "Fix every failed scene_quality_audit check, including every render component listed by collision_coverage, plus the most visible weakness in the image."
          : "Fix every failed scene_layout_audit and scene_quality_audit check, including every render component listed by collision_coverage, plus the most visible weakness in the image.",
        ...(!focused_asset
          ? [
              "Keep entities aligned with plan element names, plan_element values, semantic_tags, and repeated instances so the layout audit can verify the authored result.",
            ]
          : []),
        "Use generated or compound geometry, semantic palette materials, descriptive feature names, snapping, and calibrated lighting as needed.",
        ...(focused_asset
          ? [
              "Use mesh_physics_bind or compound_create for collidable generated geometry. Do not add lighting unless the asset itself is a functional light.",
            ]
          : [
              "Use entity_create_light for lights and mesh_physics_bind or compound_create for collidable generated geometry. Do not expand these atomic tools into probe and component-setting sequences.",
            ]),
        "Resolve every correction parent from the current scene and use the returned id. Never retry a missing parent with another guessed id.",
        "Preserve all good existing work and keep every addition under the root.",
        focused_asset
          ? "Call scene_quality_audit after corrections and do not report completion unless it passes."
          : "Call scene_layout_audit and scene_quality_audit after corrections and do not report completion unless both pass.",
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

    // a focused asset run has to end with an asset on disk, and the orchestration is what has to do it.
    // asking the model to save the prefab means a run can author forty parts, review them, correct them and
    // then stop after the last screenshot with nothing usable, which is exactly what happens. the merge is
    // bolted to the save, so a skipped save also silently skips the merge and leaves every part its own mesh
    let asset_prefab = null;
    if (focused_asset && root_id)
    {
      asset_prefab = await run.stage(
        "Finalize Asset",
        "merging parts by material and saving the prefab",
        async () =>
        {
          // the workspace root is parked inactive and transient while it is built, and the model tends to
          // switch it live mid run, so the save decides the state rather than inheriting whatever it left
          await run.tool(
            "entity_update",
            {
              id: root_id,
              active: true,
              transient: false,
            },
            30000,
          );
          const game_ready = await make_game_ready(
            active_assistant_context,
            root_id,
          );
          // the same file the incremental saves have been writing, so the finished asset replaces the
          // partial one instead of appearing beside it under a second name
          const prefab_path =
            active_assistant_context.authoring_prefab_path ??
            `${asset_file_name(root_name)}.prefab`;
          const saved = await run.tool(
            "prefab_save",
            {
              id: root_id,
              path: prefab_path,
            },
            60000,
          );
          if (saved?.ok)
          {
            await register_assistant_asset(
              active_assistant_context,
              "prefab_save",
              {
                name: root_name,
                path: prefab_path,
              },
              saved,
            );
          }

          // the deliverable is the file, so the world is put back the way it was found rather than left
          // holding a loose copy of the asset
          await run.tool(
            "entity_update",
            {
              id: root_id,
              active: false,
              transient: true,
            },
            30000,
          );
          return {
            ...saved,
            game_ready,
            path: prefab_path,
          };
        },
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
    if (focused_asset && !asset_prefab?.ok)
    {
      return {
        ok: false,
        text: [
          "The asset was built but could not be saved, so there is no reusable prefab.",
          `Prefab save error: ${asset_prefab?.error ?? "the save never ran"}`,
          `Root entity: ${root_name}, id ${root_id}.`,
          "The generated meshes, materials, and textures are on disk but nothing assembles them.",
        ].join("\n"),
      };
    }
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
        ...(asset_prefab?.ok
          ? [
              `Prefab saved to ${asset_prefab.path}.`,
              asset_prefab.game_ready?.renderers_before > asset_prefab.game_ready?.renderers_after
                ? `Game ready pass merged ${asset_prefab.game_ready.renderers_before} meshes down to ${asset_prefab.game_ready.renderers_after} by material.`
                : "Game ready pass found nothing to merge.",
            ]
          : []),
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
