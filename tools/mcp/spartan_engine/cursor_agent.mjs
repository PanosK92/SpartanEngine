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
  prop_quality_profile,
  scene_quality_prompt_lines,
} from "./scene_quality.mjs";
import {
  audit_scene_layout,
} from "./scene_planning.mjs";
import {
  calculate_benchmark_metrics,
  compare_benchmark_results,
} from "./scene_benchmarks.mjs";
import {
  create_design_brief,
  infer_design_template,
  suggest_scene_plan,
} from "./design_intelligence.mjs";
import {
  is_scene_stage_request,
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
  resolve_readable_path,
} from "./shared_codebase.mjs";
import {
  constrain_generated_resources,
  generated_resource_command,
  material_file_name,
} from "./world_resources.mjs";
import {
  cleanup_run_resources,
  snapshot_run_resources,
} from "./run_cleanup.mjs";
import {
  auto_register_world_asset,
  resolve_world_resource_directory,
  world_asset_candidate_apply,
  world_asset_candidate_create,
  world_asset_candidate_discard,
  world_asset_candidate_status,
  world_asset_fork,
  world_asset_inspect,
  world_asset_load,
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
  "entity_list_children",
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
  "asset_viewer_refresh",
  "asset_viewer_list",
  "asset_viewer_inspect",
  "asset_viewer_set_selection",
  "asset_viewer_set_display",
  "asset_viewer_preview_path",
  "asset_viewer_reload",
  "asset_viewer_mesh",
  "asset_viewer_mesh_save",
  "asset_viewer_rename",
  "asset_viewer_delete",
  "asset_viewer_cleanup_scan",
  "asset_viewer_cleanup_apply",
  "asset_viewer_revision_status",
  "asset_viewer_revision_preview",
  "asset_viewer_revision_apply",
  "asset_viewer_revision_discard",
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
  "world_asset_fork",
  "world_asset_load",
  "world_material_inspect",
  "world_material_fork",
  "world_material_publish",
  "resource_read",
  "prefab_create",
  "scene_benchmark_score",
  "async_task_start",
  "async_task_get",
  "async_task_list",
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
  "prefab_save",
  "prefab_create",
  "world_set_environment",
  "execute_lua",
  "world_asset_register",
  "world_asset_fork",
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
const assistant_async_tasks = new Map();
const maximum_assistant_async_tasks = 100;
let next_assistant_async_task_id = 1;
const maximum_cursor_run_ms = Number.parseInt(
  process.env.SPARTAN_CURSOR_MAX_RUN_MS ??
  "360000",
  10,
);
const maximum_focused_asset_run_ms = Number.parseInt(
  process.env.SPARTAN_FOCUSED_ASSET_MAX_RUN_MS ??
  "240000",
  10,
);
const focused_finalization_reserve_ms = 45000;

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

function assistant_async_task_receipt(task)
{
  return {
    id: task.id,
    tool: task.tool,
    status: task.status,
    started_at: task.started_at,
    completed_at: task.completed_at,
    duration_ms: task.completed_at_ms
      ? task.completed_at_ms - task.started_at_ms
      : Date.now() - task.started_at_ms,
    is_error: task.status === "failed",
    result: task.result,
    error: task.error,
  };
}

function prune_assistant_async_tasks(required_slots = 0)
{
  const target_size =
    maximum_assistant_async_tasks - required_slots;
  for (const [id, task] of assistant_async_tasks)
  {
    if (assistant_async_tasks.size <= target_size)
    {
      break;
    }
    if (
      task.status === "completed" ||
      task.status === "failed"
    )
    {
      assistant_async_tasks.delete(id);
    }
  }
}

async function run_assistant_async_task(
  task,
  context,
  command,
  args,
)
{
  task.status = "running";
  try
  {
    const result = await dispatch_assistant_command(
      context,
      command,
      args,
    );
    task.result = result;
    task.error =
      result?.ok === false
        ? result.error ?? "command failed"
        : null;
    task.status =
      result?.ok === false
        ? "failed"
        : "completed";
  }
  catch (error)
  {
    task.result = null;
    task.error = error.message;
    task.status = "failed";
  }
  finally
  {
    task.completed_at_ms = Date.now();
    task.completed_at =
      new Date(task.completed_at_ms).toISOString();
    prune_assistant_async_tasks();
  }
}

function focused_command_timeout(
  context,
  requested_timeout,
)
{
  if (!context?.focused_deadline_at)
  {
    return requested_timeout;
  }
  const restoration_reserve =
    context.finalizing_asset &&
    !context.restoring_asset_state
      ? context.finalization_restore_reserve_ms ?? 0
      : 0;
  const remaining =
    context.focused_deadline_at -
    Date.now() -
    restoration_reserve;
  if (remaining <= 0)
  {
    throw new Error(
      "focused asset run exceeded its deadline",
    );
  }
  return Math.max(
    1,
    Math.min(
      requested_timeout,
      remaining,
    ),
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
// draw calls. this runs on the way to disk so the saved prefab is cheap and the authoring
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
      ...(context.asset_revision?.candidate_path
        ? {
            path: `${
              path.posix.dirname(
                context.asset_revision.candidate_path,
              )
            }/work/meshes/${
              context.asset_revision.asset_id
            }_merged.mesh`,
          }
        : {}),
    },
    focused_command_timeout(context, 120000),
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
  track_owned_resource_paths(
    context,
    "entity_make_game_ready",
    {
      id: entity_id,
    },
    report,
  );
  return report;
}

async function register_assistant_asset(
  context,
  command,
  args,
  result,
)
{
  if (
    context.focused_asset_run &&
    !context.finalizing_asset
  )
  {
    return result;
  }
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
    const current_path = registration.asset?.path;
    if (asset_id && current_path)
    {
      context.asset_viewer_asset_id = asset_id;
      context.asset_viewer_current = {
        asset_id,
        path: current_path,
      };
      context.latest_prefab_path = current_path;
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

  // the engine resolves a bare file name into the mcp materials folder and reports the full path
  // under material.resource.path, the bare name is not addressable afterwards
  const created_path =
    material.material?.resource?.path ??
    material.resource?.path ??
    material.material?.path ??
    material_path;

  // roughness and metalness go into the maps, the scalars stay at 1 because attaching a roughness
  // map resets the scalar to 1 and the shader multiplies the two, a 0.1 scalar over a 0.1 map is
  // a mirror, not a glaze
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
      normal_bevel: args.normal_bevel,
      base_roughness:
        args.base_roughness ??
        args.roughness,
      base_metalness:
        args.base_metalness ??
        args.metalness,
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

  // scalars land after the maps, texture attachment rewrites some of them and generation
  // arguments such as width or seed are not material properties and used to fail the whole call
  const scalars = textured_material_scalars(args);
  const properties = await set_material_properties(
    run,
    created_path,
    scalars,
  );
  if (!properties.ok)
  {
    return {
      ...properties,
      material_path: created_path,
      texture,
    };
  }

  const tiling = Number(args.tiling ?? 0);
  return {
    ok: true,
    material_path: created_path,
    texture,
    tiling: tiling > 0 ? tiling : 1,
    applied_properties: properties.updated,
  };
}

// entity_get returns child ids only, agents building an asset keep asking for the parts by name
// so this expands one level of children into id, name, components and local transform
async function list_entity_children(run, args) {
  const parent = await run.tool(
    "entity_get",
    {
      id: args.id ?? args.entity_id ?? args.parent_id,
      name: args.name ?? args.entity ?? args.parent,
    },
  );
  if (!parent.ok)
  {
    return parent;
  }
  const ids = Array.isArray(parent.entity?.children)
    ? parent.entity.children
    : [];
  const limit = Math.min(
    Math.max(Number(args.limit ?? 64), 1),
    256,
  );
  const children = [];
  for (const id of ids.slice(0, limit))
  {
    const child = await run.tool(
      "entity_get",
      { id },
    );
    if (!child.ok || !child.entity)
    {
      continue;
    }
    const entity = child.entity;
    children.push({
      id: entity.id,
      name: entity.name,
      active: entity.active,
      components: entity.components,
      position_local: entity.position_local,
      rotation_euler_local: entity.rotation_euler_local,
      scale_local: entity.scale_local,
      child_count: Array.isArray(entity.children)
        ? entity.children.length
        : 0,
    });
  }
  return {
    ok: true,
    parent: {
      id: parent.entity.id,
      name: parent.entity.name,
    },
    count: children.length,
    total: ids.length,
    truncated: ids.length > limit,
    children,
  };
}

// the subset of material_textured_create arguments that are material properties, everything
// else on the call describes the texture and must not reach material_set_property
const textured_material_scalar_keys = new Set([
  "color_r",
  "color_g",
  "color_b",
  "color_a",
  "normal",
  "height",
  "clearcoat",
  "clearcoat_roughness",
  "anisotropic",
  "anisotropic_rotation",
  "sheen",
  "subsurface_scattering",
  "ior",
  "absorption",
  "thickness",
  "emissive_from_albedo",
  "texture_tiling_x",
  "texture_tiling_y",
  "texture_offset_x",
  "texture_offset_y",
]);

function textured_material_scalars(args) {
  const scalars = {};
  for (const [key, value] of Object.entries(args))
  {
    if (
      textured_material_scalar_keys.has(key) &&
      Number.isFinite(value)
    )
    {
      scalars[key] = value;
    }
  }
  if (Array.isArray(args.color) || Array.isArray(args.base_color))
  {
    scalars.color = args.color ?? args.base_color;
  }
  const tiling = Number(args.tiling ?? 0);
  if (tiling > 0)
  {
    scalars.texture_tiling_x = tiling;
    scalars.texture_tiling_y = tiling;
  }
  return scalars;
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
  const possible_paths = path.isAbsolute(requested_path)
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
    for (const possible_path of possible_paths)
    {
      try
      {
        const stats = await fs.stat(possible_path);
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

async function screenshot_file_path(file_path)
{
  const requested_path = String(file_path ?? "")
    .replaceAll("\\", "/");
  const possible_paths = path.isAbsolute(requested_path)
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
  for (const possible_path of possible_paths)
  {
    try
    {
      const stats = await fs.stat(possible_path);
      if (stats.size > 0)
      {
        return possible_path;
      }
    }
    catch
    {
    }
  }
  return resolve_readable_path(requested_path);
}

async function capture_asset_viewer_review(run, root_id, label)
{
  const preview = await run.tool(
    "asset_viewer_preview_entity",
    {
      id: root_id,
    },
    10000,
  );
  if (!preview.ok)
  {
    return {
      ok: false,
      path: "",
      error: preview.error ?? "asset preview failed",
    };
  }
  const camera = await run.tool(
    "asset_viewer_set_view",
    {
      view: "perspective",
      zoom: 1,
    },
    10000,
  );
  if (!camera.ok)
  {
    return {
      ok: false,
      path: "",
      error: camera.error ?? "asset viewer camera failed",
    };
  }
  const screenshot = await run.tool(
    "asset_viewer_screenshot",
    {
      path: `asset_${root_id}_${label}.png`,
      width: 768,
      height: 768,
    },
    10000,
  );
  if (!screenshot.ok || !screenshot.path)
  {
    return {
      ok: false,
      path: "",
      error: screenshot.error ?? "asset viewer screenshot failed",
    };
  }
  const ready = await wait_for_screenshot(
    screenshot.path,
    12000,
  );
  if (!ready)
  {
    return {
      ok: false,
      path: "",
      error: "asset viewer screenshot was not written",
    };
  }
  const disk_path = await screenshot_file_path(
    screenshot.path,
  );
  return {
    ok: Boolean(disk_path),
    path: disk_path,
  };
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
  current = null,
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
    },
    10000,
  );
  if (!selected.ok)
  {
    return selected;
  }
  if (
    current?.path &&
    selected.loaded_path !== current.path
  )
  {
    return {
      ok: false,
      error:
        "Asset Viewer loaded a different asset file",
      expected_path: current.path,
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
        "current asset has no previewable geometry",
      loaded_path: selected.loaded_path,
    };
  }

  const requested_views = Array.isArray(args.views)
    ? args.views
    : ["perspective"];
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
          `asset_${asset_id}_current_${view}.png`,
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
          current?.path &&
          renderer_status.loaded_path !== current.path
        )
        {
          screenshot.ok = false;
          screenshot.error =
            "Asset Viewer changed assets during renderer capture";
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

function flatten_loft_profiles(profiles)
{
  if (!Array.isArray(profiles) || profiles.length === 0)
  {
    return {
      loft_profiles: profiles,
    };
  }
  if (profiles.every((entry) => Number.isFinite(entry)))
  {
    return {
      loft_profiles: profiles,
    };
  }
  const flattened = profiles.map((profile) =>
    flatten_points(profile, 2),
  );
  const point_count = flattened[0]?.length
    ? flattened[0].length / 2
    : 0;
  return {
    loft_profiles: flattened.flat(),
    loft_profile_points: point_count,
  };
}

function normalize_mesh_arguments(args) {
  const loft = flatten_loft_profiles(args.loft_profiles);
  const normalized = {
    ...args,
    profile: flatten_points(args.profile, 2),
    path_points: flatten_points(args.path_points, 3),
    loft_profiles: loft.loft_profiles,
    ...(
      loft.loft_profile_points
        ? { loft_profile_points: loft.loft_profile_points }
        : {}
    ),
  };
  // closed outlines are implicit in the engine, a repeated first point used to be appended here
  // and was then rejected as a zero length edge, the engine now strips one if the agent sends it
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
    prefab = await run.tool(
      "prefab_save",
      {
        id: root.entity.id,
        path: args.prefab_path,
      },
    );
    if (!prefab.ok)
    {
      return {
        ...prefab,
        root: root.entity,
        completed_parts,
      };
    }
    if (
      active_assistant_context &&
      !active_assistant_context.authoring_root_id
    )
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

const resource_writing_commands = new Set([
  "material_create",
  "material_semantic_create",
  "material_palette_create",
  "material_textured_create",
  "textured_material_create",
  "mesh_generate",
  "mesh_generate_batch",
  "mesh_raw_create",
  "entity_make_game_ready",
  "texture_generate",
  "prefab_save",
  "compound_create",
  "construction_grammar_create",
  "detail_pattern_create",
  "screenshot_take",
  "asset_viewer_screenshot",
  "world_asset_register",
]);

const revision_blocked_commands = new Set([
  "world_asset_load",
  "world_asset_fork",
  "world_material_fork",
  "world_material_publish",
  "asset_viewer_mesh_save",
  "asset_viewer_rename",
  "asset_viewer_delete",
  "asset_viewer_cleanup_apply",
  "execute_lua",
]);

function collect_owned_resource_paths(
  value,
  paths,
)
{
  if (typeof value === "string")
  {
    const normalized = value.replaceAll("\\", "/");
    if (
      normalized.toLowerCase().includes(
        "mcp/blockout/",
      ) &&
      path.posix.extname(normalized)
    )
    {
      paths.add(normalized);
    }
    return;
  }
  if (Array.isArray(value))
  {
    for (const item of value)
    {
      collect_owned_resource_paths(item, paths);
    }
    return;
  }
  if (!value || typeof value !== "object")
  {
    return;
  }
  for (const item of Object.values(value))
  {
    collect_owned_resource_paths(item, paths);
  }
}

function track_owned_resource_paths(
  context,
  command,
  args,
  result,
)
{
  if (
    !context?.focused_asset_run ||
    result?.ok !== true ||
    !resource_writing_commands.has(command)
  )
  {
    return;
  }
  const owned_paths =
    context.owned_resource_paths ??= new Set();
  collect_owned_resource_paths(args, owned_paths);
  collect_owned_resource_paths(result, owned_paths);
}

function route_revision_resource_paths(
  value,
  candidate_root,
)
{
  if (typeof value === "string")
  {
    const normalized = value.replaceAll("\\", "/");
    if (
      normalized === candidate_root ||
      normalized.startsWith(`${candidate_root}/`)
    )
    {
      return normalized;
    }
    const library_root = "project/mcp/blockout/";
    if (normalized.startsWith(library_root))
    {
      return `${candidate_root}/work/${
        normalized.slice(library_root.length)
      }`;
    }
    return value;
  }
  if (Array.isArray(value))
  {
    return value.map((entry) =>
      route_revision_resource_paths(
        entry,
        candidate_root,
      ),
    );
  }
  if (value && typeof value === "object")
  {
    return Object.fromEntries(
      Object.entries(value).map(([key, entry]) => [
        key,
        route_revision_resource_paths(
          entry,
          candidate_root,
        ),
      ]),
    );
  }
  return value;
}

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
    context?.focused_asset_run ||
    !context?.authoring_root_id ||
    !context?.authoring_prefab_path ||
    result?.ok !== true ||
    !part_changing_commands.has(command)
  )
  {
    return;
  }

  const now = Date.now();
  if (now - (context.authoring_saved_at ?? 0) < 10000)
  {
    return;
  }
  context.authoring_saved_at = now;

  try
  {
    const result = await context.run.tool(
      "prefab_save",
      {
        id: context.authoring_root_id,
        path: context.authoring_prefab_path,
      },
      25000,
    );
    track_owned_resource_paths(
      context,
      "prefab_save",
      {
        path: context.authoring_prefab_path,
      },
      result,
    );
  }
  catch
  {
  }
}

function track_asset_budget(
  _context,
  _command,
  _args,
  result,
)
{
  return result;
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
  if (command === "prefab_create")
  {
    command = "prefab_save";
  }
  // names agents reach for that logged as capability gaps, each maps onto what exists
  if (
    command === "entity_describe" ||
    command === "entity_inspect"
  )
  {
    command = "entity_get";
  }
  if (
    command === "entity_list_children" ||
    command === "entity_children" ||
    command === "entity_get_children"
  )
  {
    return list_entity_children(run, args);
  }
  if (
    command === "agent_memory_update" ||
    command === "agent_memory_write" ||
    command === "agent_memory_add" ||
    command === "agent_memory_note"
  )
  {
    command = "agent_memory_append";
  }
  if (
    command === "spartan_engine_command" ||
    command === "engine_command"
  )
  {
    const inner = String(
      args.command ?? args.name ?? args.tool ?? "",
    ).trim();
    if (!inner)
    {
      return {
        ok: false,
        error: "spartan_engine_command needs a command name, pass {command, args}",
        code: "invalid_arguments",
      };
    }
    const inner_args =
      args.args && typeof args.args === "object"
        ? args.args
        : args.arguments && typeof args.arguments === "object"
          ? args.arguments
          : Object.fromEntries(
              Object.entries(args).filter(
                ([key]) =>
                  !["command", "name", "tool"].includes(key),
              ),
            );
    return dispatch_assistant_command(context, inner, inner_args);
  }
  if (command === "resource_read")
  {
    const has_name =
      typeof args.name === "string" &&
      args.name.trim().length > 0;
    const has_path =
      typeof args.path === "string" &&
      args.path.trim().length > 0;
    const has_list_query =
      args.type !== undefined ||
      args.limit !== undefined ||
      args.offset !== undefined;
    const unsupported_fields = Object.keys(args).filter(
      (key) =>
        ![
          "name",
          "path",
          "type",
          "limit",
          "offset",
        ].includes(key),
    );
    if (unsupported_fields.length > 0)
    {
      return {
        ok: false,
        error:
          `resource_read does not accept ${unsupported_fields.join(", ")}`,
        code: "invalid_arguments",
      };
    }
    if (
      (
        has_name &&
        has_path
      ) ||
      (
        (has_name || has_path) &&
        has_list_query
      )
    )
    {
      return {
        ok: false,
        error:
          "resource_read is ambiguous, pass exactly one material name or path for material_get, or pass only type, limit, and offset for resource_list",
        code: "invalid_arguments",
      };
    }
    if (has_name || has_path)
    {
      command = "material_get";
      args = has_name
        ? { name: args.name }
        : { path: args.path };
    }
    else
    {
      command = "resource_list";
      args = {
        type: args.type,
        limit: args.limit,
        offset: args.offset,
      };
    }
  }
  if (command === "scene_benchmark_score")
  {
    if (
      !args.result ||
      typeof args.result !== "object" ||
      Array.isArray(args.result)
    )
    {
      return {
        ok: false,
        error:
          "scene_benchmark_score requires result as an object",
        code: "invalid_arguments",
      };
    }
    if (
      args.baseline !== undefined &&
      (
        !args.baseline ||
        typeof args.baseline !== "object" ||
        Array.isArray(args.baseline)
      )
    )
    {
      return {
        ok: false,
        error:
          "scene_benchmark_score baseline must be an object",
        code: "invalid_arguments",
      };
    }
    const score =
      calculate_benchmark_metrics(args.result);
    const comparison =
      args.baseline
        ? compare_benchmark_results(
            args.result,
            args.baseline,
          )
        : undefined;
    return {
      ...score,
      comparison,
    };
  }
  if (command === "async_task_start")
  {
    const target = String(args.tool ?? "").trim();
    if (!target)
    {
      return {
        ok: false,
        error:
          "async_task_start requires tool and optional args",
        code: "invalid_arguments",
      };
    }
    if (
      [
        "async_task_start",
        "async_task_get",
        "async_task_list",
      ].includes(target)
    )
    {
      return {
        ok: false,
        error:
          "async task commands cannot start async task commands",
        code: "invalid_arguments",
      };
    }
    if (!engine_tool_names.has(target))
    {
      return {
        ok: false,
        error: `unknown async task tool ${target}`,
        code: "target_resolution_failed",
      };
    }
    const target_args = args.args ?? {};
    if (
      !target_args ||
      typeof target_args !== "object" ||
      Array.isArray(target_args)
    )
    {
      return {
        ok: false,
        error: "async_task_start args must be an object",
        code: "invalid_arguments",
      };
    }
    prune_assistant_async_tasks(1);
    if (
      assistant_async_tasks.size >=
      maximum_assistant_async_tasks
    )
    {
      return {
        ok: false,
        error:
          "async task registry is full, wait for a running task to finish",
        code: "async_task_capacity_reached",
      };
    }
    const started_at_ms = Date.now();
    const task = {
      id: `cursor_task_${
        started_at_ms
      }_${
        next_assistant_async_task_id++
      }`,
      tool: target,
      status: "queued",
      started_at_ms,
      started_at:
        new Date(started_at_ms).toISOString(),
      completed_at_ms: null,
      completed_at: null,
      result: null,
      error: null,
    };
    assistant_async_tasks.set(task.id, task);
    if (scene_mutating_tool_names.has(target))
    {
      context.mark_scene_mutation?.();
    }
    const task_promise = run_assistant_async_task(
      task,
      context,
      target,
      target_args,
    );
    context.assistant_async_task_promises ??=
      new Set();
    context.assistant_async_task_promises.add(
      task_promise,
    );
    void task_promise.finally(() =>
    {
      context.assistant_async_task_promises?.delete(
        task_promise,
      );
    });
    return {
      ok: true,
      task: assistant_async_task_receipt(task),
    };
  }
  if (command === "async_task_get")
  {
    const id = String(args.id ?? "").trim();
    const task = assistant_async_tasks.get(id);
    if (!task)
    {
      return {
        ok: false,
        error: "async task not found",
        code: "target_resolution_failed",
      };
    }
    return {
      ok: true,
      task: assistant_async_task_receipt(task),
    };
  }
  if (command === "async_task_list")
  {
    return {
      ok: true,
      tasks: [...assistant_async_tasks.values()]
        .map(assistant_async_task_receipt),
    };
  }
  if (
    context.focused_asset_run &&
    !context.finalizing_asset &&
    command === "prefab_save"
  )
  {
    return {
      ok: false,
      deferred: true,
      error:
        "focused asset prefab save runs once during finalization",
    };
  }
  if (
    context.focused_asset_run &&
    !context.finalizing_asset &&
    command === "compound_create" &&
    args.prefab_path
  )
  {
    args = {
      ...args,
      prefab_path: undefined,
    };
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
  if (
    context.asset_revision?.candidate_path &&
    (
      generated_resource_command(command) ||
      resource_writing_commands.has(command)
    )
  )
  {
    args = route_revision_resource_paths(
      args,
      path.posix.dirname(
        context.asset_revision.candidate_path,
      ),
    );
  }
  if (
    context.asset_revision?.candidate_path &&
    (
      command === "material_set_property" ||
      command === "material_set_texture"
    )
  )
  {
    const candidate_root = path.posix.dirname(
      context.asset_revision.candidate_path,
    );
    const material_path = String(
      args.path ?? "",
    ).replaceAll("\\", "/");
    if (
      !material_path.startsWith(
        `${candidate_root}/`,
      )
    )
    {
      return {
        ok: false,
        error:
          "asset revisions can mutate only candidate material paths",
      };
    }
  }
  const catalog_directory =
    await assistant_resource_directory(context);
  const catalog_root = get_project_root();
  const catalog_send = (name, value) =>
    run.tool(name, value, 60000);
  if (
    context.asset_revision?.asset_id &&
    revision_blocked_commands.has(command)
  )
  {
    return {
      ok: false,
      error:
        "this command is blocked while editing a copy-on-write asset revision candidate",
    };
  }
  if (
    command === "asset_viewer_revision_status" ||
    command === "asset_viewer_revision_preview" ||
    command === "asset_viewer_revision_apply" ||
    command === "asset_viewer_revision_discard"
  )
  {
    if (
      context.asset_revision?.asset_id &&
      (
        command === "asset_viewer_revision_apply" ||
        command === "asset_viewer_revision_discard"
      )
    )
    {
      return {
        ok: false,
        deferred: true,
        error:
          "finish the revision run before the user can apply or discard its candidate",
      };
    }
    let asset_id =
      args.asset_id ??
      args.id ??
      context.asset_revision?.asset_id ??
      context.asset_viewer_asset_id;
    if (!asset_id)
    {
      const viewer_status = await run.tool(
        "asset_viewer_status",
        {},
        10000,
      );
      asset_id = viewer_status.selected_asset_id;
    }
    if (!asset_id)
    {
      return {
        ok: false,
        error:
          "select an asset or pass asset_id",
      };
    }
    const candidate_args = {
      ...args,
      asset_id,
    };
    if (command === "asset_viewer_revision_status")
    {
      return world_asset_candidate_status(
        catalog_root,
        catalog_directory,
        candidate_args,
      );
    }
    if (command === "asset_viewer_revision_apply")
    {
      const applied = await world_asset_candidate_apply(
        catalog_root,
        catalog_directory,
        candidate_args,
      );
      if (applied.ok)
      {
        await run.tool(
          "asset_viewer_refresh",
          {},
          10000,
        );
        applied.asset_viewer = await run.tool(
          "asset_viewer_select",
          { asset_id },
          10000,
        );
      }
      return applied;
    }
    if (command === "asset_viewer_revision_discard")
    {
      const discarded =
        await world_asset_candidate_discard(
          catalog_root,
          catalog_directory,
          candidate_args,
        );
      if (discarded.ok)
      {
        discarded.asset_viewer = await run.tool(
          "asset_viewer_select",
          { asset_id },
          10000,
        );
      }
      return discarded;
    }
    const status = await world_asset_candidate_status(
      catalog_root,
      catalog_directory,
      candidate_args,
    );
    if (!status.ok || !status.candidate_active)
    {
      return {
        ...status,
        ok: false,
        error:
          status.error ??
          "asset has no pending revision candidate",
      };
    }
    const candidate_type =
      status.candidate?.candidate_catalog_asset?.type;
    if (
      candidate_type === "material" ||
      candidate_type === "texture"
    )
    {
      const preview = await run.tool(
        "asset_viewer_preview_path",
        { path: status.candidate_path },
        10000,
      );
      return {
        ...status,
        ok: preview.ok,
        preview,
      };
    }
    const created = await run.tool(
      "entity_create_empty",
      {
        name: `${asset_id}_candidate_preview`,
        active: false,
        transient: true,
        tags: ["revision_candidate_preview"],
      },
      10000,
    );
    if (!created.ok)
    {
      return created;
    }
    const loaded = candidate_type === "mesh"
      ? await run.tool(
          "render_set_mesh",
          {
            id: created.entity.id,
            mesh: status.candidate_path,
          },
          20000,
        )
      : await run.tool(
          "prefab_load",
          {
            path: status.candidate_path,
            parent_id: created.entity.id,
            name: `${asset_id}_candidate_preview`,
          },
          30000,
        );
    if (!loaded.ok)
    {
      return loaded;
    }
    const preview = await run.tool(
      "asset_viewer_preview_entity",
      { id: created.entity.id },
      10000,
    );
    return {
      ...status,
      ok: preview.ok,
      preview,
      preview_entity_id: created.entity.id,
    };
  }
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
  if (command === "world_asset_register")
  {
    if (
      context.focused_asset_run &&
      !context.finalizing_asset
    )
    {
      return {
        ok: false,
        deferred: true,
        error:
          "focused asset registration runs once after final save",
      };
    }
    const revision =
      context.asset_revision?.asset_id
        ? context.asset_revision
        : null;
    if (revision)
    {
      args = {
        ...args,
        type: revision.asset_type,
        asset_id: revision.asset_id,
        name: revision.asset_name,
        path:
          context.asset_revision_path ??
          args.path,
      };
    }
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
      context.focused_asset_run
    )
    {
      const asset_id =
        result.asset?.id ??
        args.asset_id;
      const current_path =
        result.asset?.path;
      const selection = await run.tool(
        "asset_viewer_select",
        {
          asset_id,
        },
        10000,
      );
      if (selection.ok)
      {
        context.asset_viewer_asset_id =
          asset_id;
        context.asset_viewer_current = {
          asset_id,
          path: current_path,
        };
        context.latest_prefab_path = current_path;
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
    // a note without a section is still worth keeping, corrections is where lessons go
    const section = String(
      args.section ?? args.heading ?? "Corrections",
    ).replace(/^#+\s*/, "").trim() || "Corrections";
    const note =
      args.note ??
      args.text ??
      args.lesson ??
      args.content ??
      args.memory;
    if (!note)
    {
      return {
        ok: false,
        error: "agent_memory_append needs a note, pass {section, note}",
        code: "invalid_arguments",
      };
    }
    return {
      ok: true,
      section,
      memory: await append_agent_memory(section, note),
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
      resource_read: {
        material_read: {
          required_one_of: ["name", "path"],
          note:
            "pass exactly one material name or path, this dispatches to material_get",
        },
        resource_list: {
          optional: ["type", "limit", "offset"],
          note:
            "pass no name or path, empty arguments list all resources",
        },
      },
      prefab_create: {
        alias_for: "prefab_save",
        note:
          "uses prefab_save arguments, focused runs defer this alias because the finalizer owns the only prefab save",
      },
      entity_list_children: {
        required_one_of: ["id", "name"],
        optional: ["limit"],
        note:
          "expands one level of children into id, name, components and local transform",
      },
      entity_describe: {
        alias_for: "entity_get",
      },
      agent_memory_update: {
        alias_for: "agent_memory_append",
        note: "pass {section, note}, section defaults to Corrections",
      },
      spartan_engine_command: {
        required: ["command"],
        optional: ["args"],
        note: "forwards {command, args} to the named engine command",
      },
      scene_benchmark_score: {
        required: ["result"],
        optional: ["baseline"],
        note:
          "result and baseline are benchmark result objects scored locally",
      },
      async_task_start: {
        required: ["tool"],
        optional: ["args"],
        note:
          "args must be an object, async_task_start, async_task_get, and async_task_list cannot be nested",
      },
      async_task_get: {
        required: ["id"],
      },
      async_task_list: {
        required: [],
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
        ...(context.focused_asset_run
          ? {
              ...prop_quality_profile,
              required_features: [],
              required_roles: [],
              profile: "prop",
            }
          : {}),
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
    if (context.focused_asset_run)
    {
      return {
        ok: false,
        deferred: true,
        error:
          "focused asset review runs once after final save",
      };
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
    context.focused_asset_run
  )
  {
    return {
      ok: false,
      deferred: true,
      error:
        "focused asset review runs once after final save",
    };
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
              "project/mcp/blockout/thumbnails/",
            )
            ? requested_path
            : `project/mcp/blockout/thumbnails/${file_name}`,
      },
      60000,
    );
  }
  if (command === "viewport_frame")
  {
    if (context.focused_asset_run)
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
    const revision =
      context.asset_revision?.asset_id
        ? context.asset_revision
        : null;
    if (revision)
    {
      args = {
        ...args,
        skip_catalog_registration: true,
        library_asset: false,
        catalog_register: false,
      };
      if (command === "prefab_save")
      {
        args.name = revision.asset_name;
        args.path =
          `project/mcp/blockout/prefabs/${
            revision.asset_id
          }.prefab`;
      }
    }
    const result = await run.tool(
      command,
      args,
      60000,
    );
    if (
      revision &&
      command === "prefab_save" &&
      result.ok
    )
    {
      context.asset_revision_path =
        result.path ??
        args.path;
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
    if (
      command === "asset_viewer_screenshot" &&
      context.focused_asset_run &&
      !context.finalizing_asset
    )
    {
      return {
        ok: false,
        deferred: true,
        error:
          "focused asset review runs once after final save",
      };
    }
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
    "Local helpers include resource_read, prefab_create, scene_benchmark_score, async_task_start, async_task_get, and async_task_list.",
    "resource_read accepts exactly one material name or path, or a resource list query; empty arguments list all resources.",
    "async_task_start accepts tool and optional args, then returns an id for async_task_get polling.",
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
    if (
      assistant_context.focused_asset_run &&
      (
        assistant_context.construction_gate_closed ||
        Date.now() >=
          assistant_context
            .focused_construction_deadline_at
      )
    )
    {
      assistant_context.construction_gate_closed = true;
      return {
        ok: false,
        error: "focused asset construction is closed",
        code: "focused_construction_closed",
        retryable: false,
      };
    }
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
    if (
      assistant_context.focused_asset_run &&
      (
        assistant_context.construction_gate_closed ||
        Date.now() >=
          assistant_context
            .focused_construction_deadline_at
      )
    )
    {
      assistant_context.construction_gate_closed = true;
      release_command();
      return {
        ok: false,
        error: "focused asset construction is closed",
        code: "focused_construction_closed",
        retryable: false,
      };
    }
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
      const dispatch_context =
        assistant_context.focused_asset_run
          ? new Proxy(
              assistant_context,
              {
                get(target, property, receiver)
                {
                  if (property !== "run")
                  {
                    return Reflect.get(
                      target,
                      property,
                      receiver,
                    );
                  }
                  return new Proxy(
                    target.run,
                    {
                      get(run_target, run_property)
                      {
                        if (run_property !== "tool")
                        {
                          const value =
                            run_target[run_property];
                          return typeof value === "function"
                            ? value.bind(run_target)
                            : value;
                        }
                        return (
                          command_name,
                          command_args,
                          requested_timeout = 60000,
                        ) => run_target.tool(
                          command_name,
                          command_args,
                          focused_command_timeout(
                            target,
                            Math.min(
                              requested_timeout,
                              Math.max(
                                1,
                                target
                                  .focused_construction_deadline_at -
                                  Date.now(),
                              ),
                            ),
                          ),
                        );
                      },
                    },
                  );
                },
              },
            )
          : assistant_context;
      const result = await dispatch_assistant_command(
        dispatch_context,
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
      track_owned_resource_paths(
        assistant_context,
        command,
        command_arguments,
        result,
      );
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
  if (is_named_tool_event(value, "async_task_start"))
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
      asset_id: entry.asset?.asset_id,
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
        !asset.path ||
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

// a car or a hero reference cannot be built in four minutes, the old wall clock stopped the run after a
// first massing piece and the finalizer then saved whatever scene object matched the name
function focused_asset_time_budget_ms(prompt, has_images)
{
  const override = process.env.SPARTAN_FOCUSED_ASSET_MAX_MS;
  if (override)
  {
    return Number.parseInt(override, 10);
  }
  const budget = asset_detail_budget(prompt);
  const vehicle =
    /\b(?:car|cars|vehicle|vehicles|bike|motorcycle|truck|plane|helicopter|ship|boat|ferrari|porsche)\b/i
      .test(String(prompt ?? ""));
  if (budget.tier === "hero" || has_images || vehicle)
  {
    return 900000;
  }
  return 360000;
}

function prompt_is_vehicle(prompt)
{
  return (
    /\b(?:car|cars|vehicle|vehicles|bike|motorcycle|truck|van|bus|ferrari|porsche|testarossa|coupe|sedan|supercar)\b/i
      .test(String(prompt ?? ""))
  );
}

function authored_part_count(entity)
{
  const children = entity?.children;
  if (!Array.isArray(children))
  {
    return 0;
  }
  return children.length;
}

function asset_build_is_incomplete(entity, prompt)
{
  const parts = authored_part_count(entity);
  if (prompt_is_vehicle(prompt))
  {
    return parts < 8;
  }
  return parts < 3;
}

function incomplete_asset_continue_prompt(prompt, root_id, entity)
{
  const parts = authored_part_count(entity);
  return [
    `Continue the existing asset root id ${root_id}. Do not create a second root and do not start over.`,
    `The previous window ended with ${parts} authored parts. That is not a finished asset.`,
    "Stop probing mesh_generate. Use a loft for the body: path_points along the length, one loft_profiles cross section per station, identical point counts. Parent every part to this root.",
    "A car still needs four wheels, glass, lights, and body volumes that match the reference photo. A hull alone is a fail.",
    "Do not call prefab_save, world_asset_register, scene_visual_review, screenshot_take, or asset_viewer_screenshot.",
    `Original request: ${prompt}`,
  ].join("\n");
}

// blockout is the only remaining authored-scope reduction. every other request is unbounded
function asset_detail_budget(prompt)
{
  const value = String(prompt ?? "").toLowerCase();

  if (
    /\b(?:blockout|block[\s-]?out|greybox|grey[\s-]?box|proxy|placeholder|stand[\s-]?in|low[\s-]?poly|rough|simple|basic|minimal)\b/
      .test(value)
  )
  {
    return {
      tier: "blockout",
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
    };
  }
  return {
    tier: "unbounded",
  };
}

function focused_asset_quality_prompt_lines(
  prompt,
  reference_images = [],
)
{
  const budget = asset_detail_budget(prompt);
  const has_reference = reference_images.length > 0;
  const tier_line =
    budget.tier === "blockout"
      ? "This request asked for a blockout, so build the massing and proportions only and stop there."
      : budget.tier === "hero"
        ? "This request asked for a hero asset. Build the real object at the resolution the reference and the request need."
        : "There is no part, material, or triangle cap. Split every surface that needs its own shape or material, and keep constructing until the object is complete.";

  return [
    "Focused asset quality standard:",
    "Everything you build here is a real-time asset for a video game. Assume it has to render in a frame alongside hundreds of others, from a normal viewing distance. It is not for a render, a film, a turntable, or a portfolio piece.",
    tier_line,
    "There is no authored-part cap, no material cap, and no triangle cap. Create as many parts, components and materials as the object needs. Do not stop because a count feels high.",
    "Spend triangles on curvature and on parts a stranger uses to name the object. A dense tessellation of a boxy 8-point loft is still a box. Hero bodies use 24 to 64 profile points and 12 to 32 distinct stations, each station a different cross section.",
    "Model characteristic construction as geometry: body volumes, wheel arches, glass as thin shells, light housings, mirrors, and signature intakes or strakes. Do not model fasteners, screws, screw recesses, ports, sockets, connectors, cables, embossed text, regulatory markings, badges, or logos. Those belong in textures.",
    "Do not model anything the object hides from the viewer. A television, a wardrobe or a fridge stands against a wall, so its back is a flat panel with a material on it. A cabinet has no interior unless it opens. Nothing has internal components. If a surface is never seen in normal use, it is one quad.",
    "Infer the ordinary real-world construction, proportions, silhouette transitions, wall thickness, joins, rims, bevels, and material boundaries that make the object recognizable and credible. The user should not need to enumerate standard object anatomy.",
    "Build the primary form first, then every secondary part, component and material the object needs. Fasteners, print and wear still belong in textures.",
    "Do not approximate a continuous manufactured or organic surface by visibly stacking cylinders, boxes, spheres, cones, or capsules. Use mesh_generate, variable lofts, sweeps, profiles, shells, bends, tapers, or mesh_raw_create to produce continuous curved transitions with enough radial and longitudinal resolution for a clean solid silhouette.",
    "Primitives are acceptable only for hidden construction, genuinely primitive parts, or an explicitly requested blockout. If several visible primitive sections merely trace one continuous outline, replace them with one coherent generated surface.",
    "Do not spend the construction window probing mesh_generate argument names. A loft that works is: shape loft, path_points as world stations along the length, loft_profiles as one closed x,y cross section per station, same point count on every profile. Wheels are shape torus with mirror_axis z. Glass is a thin loft or extruded_profile, not a box.",
    "A car is unfinished until it has a body with changing cross sections, four wheels in arches, glass openings, light housings, and the signature side treatment. A single hull, wedge, or blob is a failed build even if it is painted.",
    "Use physically plausible dimensions and thickness. Avoid coplanar overlaps, open shells, abrupt radius jumps, floating trim, z-fighting, and decorative parts that do not follow the parent surface.",
    "For transparent materials, model the actual outer and inner surfaces or a valid shell and preserve believable thickness at rims and openings. Do not rely on transparency to imply missing geometry.",
    "Build only the reusable object under its prepared root. Do not surround it with a ground pad, route, display structure, studio set, or review lights unless the user explicitly requests those as part of the asset.",
    ...(has_reference
      ? [
          "Attached reference images are the acceptance test. A stranger looking at the Asset Viewer must name the same object as the photo. A wedge, slab, formula 1 tub, or generic hull is a failed build, even if it has paint.",
          "Do not substitute a library asset or a simpler vehicle for the photographed object. Model this specific object, including wheels, glass openings, light housings, and body volumes that match the photo.",
          "Keep constructing until the silhouette and the signature parts match. A painted wedge is not done.",
        ]
      : [
          "Use one construction pass. Build the current asset completely, then stop authoring. There are no automatic correction, polish, comparison, or alternate passes.",
        ]),
    "Do not call prefab_save, world_asset_register, scene_visual_review, screenshot_take, or asset_viewer_screenshot. The run finalizer performs one game-ready pass, one final prefab save, one current-asset catalog registration, and one perspective Asset Viewer screenshot review.",
    "Saving the prefab merges every part that shares a material into one mesh, so splitting a surface off for a genuine material change is cheap. Split as often as the object needs distinct materials or construction.",
    "Never generate the same part twice. Before adding a part, check whether you already made it. A regenerated duplicate leaves two copies of the same geometry in the asset.",
    "Author repetition as one mesh instead of one mesh per copy. When the same shape repeats in the same material, generate it once with the array and mirror modifiers on that mesh_generate call: radial_count with radial_axis, radial_radius and radial_step_degrees for spokes, castors, legs, bolts, flutes and anything arranged around an axis; linear_count with linear_step for slats, ribs, treads, rungs and rows; mirror_axis with mirror_plane for a symmetric pair such as two armrests. A five-spoke base is one call, not five. This is identical geometry at a fraction of the parts, so prefer it over generating each copy separately.",
    "Give a part its own entity whenever it needs its own material or its own geometry. Keep a collider, light, or sound on the functional entity it belongs to rather than on a part that exists only to be drawn.",
    "Assemble the asset as you go, one part at a time. The prepared root already exists and is previewing, so every part you make is joined to the asset the moment you make it: generate the part, parent it to the root, give it its material, and place it against the parts that are already there. Do not author a batch of loose meshes and materials with the intention of assembling them later.",
    "Work outward from the part that fixes the asset's scale and orientation, usually the primary body or the base, because every later part is positioned against what is already standing. Finish and place each part before starting the next one.",
    ...(has_reference
      ? [
          "Do not rotate the main viewport or capture it while constructing. The run will screenshot the Asset Viewer and send it back to you if the result still does not match the photo.",
        ]
      : [
          "Do not rotate or screenshot while constructing. Finish the current asset in one pass and leave finalization and review to the run finalizer.",
        ]),
    "The Asset Viewer preview follows the root live, so the asset is visible as it grows. Never activate the workspace root or move it into the scene to look at it, and never capture the main viewport for this. Preview and screenshot through the Asset Viewer.",
    ...(budget.tier === "blockout"
      ? [
          "For this blockout, stop after the construction pass. The run finalizer performs the single perspective review.",
        ]
      : []),
  ];
}

function visual_match_prompt(prompt, root_id, pass)
{
  return [
    `Visual match pass ${pass} of 2.`,
    `Keep editing the existing asset root id ${root_id}. Do not create a second root and do not start over.`,
    "The last attached image is the current Asset Viewer screenshot. The other attached images are the reference.",
    "If a stranger cannot name the same object from the screenshot as from the reference, the build has failed so far.",
    "A wedge, slab, formula 1 tub, display pad, or generic car hull is a fail. Missing wheels, missing glass, or the wrong body proportions is a fail. Paint on a blob is still a fail.",
    "Replace wrong parts. Add the missing wheels, glass, body volumes, and silhouette features from the photo. Keep constructing.",
    "Do not call prefab_save, world_asset_register, scene_visual_review, screenshot_take, or asset_viewer_screenshot.",
    `Original request: ${prompt}`,
  ].join("\n");
}

function asset_revision_prompt_lines(revision)
{
  const aspects = revision.aspects ?? [];

  if (!revision.root_id)
  {
    return [
      `This request continues work on an asset that already exists. It is not a request for a new asset. The name in the request, "${revision.hint}", matches several library assets equally well: ${revision.ambiguous.join(", ")}.`,
      "Inspect each match and ask the user which registered asset to copy into a revision candidate. Do not load or mutate any match.",
      "If you genuinely cannot tell which one is meant, change nothing and ask which asset to use. Do not build a duplicate or revise all matches.",
      "Once chosen, a new run must create a copy-on-write candidate before making changes.",
    ];
  }

  const lines = [
    "This request continues work on an asset that already exists. It is not a request for a new asset.",
    `A copy-on-write candidate is loaded and previewing in the Asset Viewer as entity id ${revision.root_id} named ${revision.root_name}, copied from ${revision.source}. Work only on that candidate root. Do not create a second root, and do not delete and rebuild the asset from scratch.`,
    "The registered asset is read only during this run. Use the exact candidate mesh, material, and texture paths returned by inspection. Never address a resource by display name because that can resolve the registered resource instead of the candidate copy.",
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
      "A texture has to change. Regenerate the affected map with texture_generate using layers and overwrite its existing texture path. Keep the resolution, tiling and seamless setting unless the request is about those, then reattach it to the same material slot. Do not create a replacement material, a differently named texture set, or an alternate prefab. A label or decal stays non-seamless with alpha.",
    );
  }

  lines.push(
    `Keep the same catalog asset id ${revision.asset_id}. Do not register it yourself. The run finalizer saves one persistent candidate and leaves the registered asset untouched.`,
    "After the candidate is ready, stop. Only the user can apply or discard it through the Asset Viewer or a confirmed MCP command.",
    `Asset being revised: ${safe_json(
      {
        asset_id: revision.asset_id,
        name: revision.asset_name,
        type: revision.asset_type,
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
      `The request matched this asset best, but the library also holds ${revision.alternatives.join(", ")}. If the loaded asset is clearly wrong, stop and name the better match instead of revising the wrong asset.`,
    );
  }

  return lines;
}

function task_kind_prompt_lines(intent, prompt, revision)
{
  if (revision || intent?.kind === "asset_revise")
  {
    return [
      "TASK KIND: library asset revision.",
      "This is not a new asset and not a scene command. Edit only the loaded candidate in Asset Viewer.",
    ];
  }
  if (intent?.kind === "focused_asset")
  {
    return [
      "TASK KIND: focused library asset creation.",
      "Author one reusable object in Asset Viewer. Do not blockout a scene, do not dress a set around the object, and do not treat this as an engine command.",
    ];
  }
  if (intent?.kind === "city_develop")
  {
    return [
      "TASK KIND: city layout command.",
      "Edit the live world. Do not open Asset Viewer. Do not create a library prefab for this request.",
    ];
  }
  if (
    intent?.greybox ||
    is_scene_stage_request(prompt)
  )
  {
    return [
      "TASK KIND: live scene greybox command.",
      "This is not asset creation. Do not open Asset Viewer. Do not register a library prefab. Do not generate hero meshes or material sets.",
      "Greybox in the current world with entity_create_primitive_batch and entity_create_light. Massing and proportions only. Stop when the volumes read.",
    ];
  }
  if (
    intent?.kind === "scene_rebuild" ||
    intent?.live_scene_action
  )
  {
    return [
      "TASK KIND: live scene construction.",
      "Build in the current world. This is not a focused Asset Viewer library asset.",
    ];
  }
  return [
    "TASK KIND: engine command.",
    "Do the requested action with the matching Spartan tool. Do not start asset creation or a scene rebuild unless the request clearly asks for one.",
  ];
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
  prepared_root = null,
  reference_images = [],
) {
  const lines = [
    "You are controlling Spartan Engine through the spartan_engine MCP tools.",
    ...task_kind_prompt_lines(intent, prompt, revision),
    "Use the spartan_engine_command custom tool as the primary live-engine bridge. Pass the native tool name in command and its arguments object in arguments.",
    "The spartan_engine_command tool handles both native and composite scene commands. Do not use shell commands or source-code tools for live scene work.",
    "Read agent_memory_read early when available, and treat it as project advice rather than absolute truth.",
    "For engine-control requests, use Spartan MCP tools first and group repetitive calls without sacrificing completeness or visual quality.",
    "For source-code questions, use search_codebase first, then read_source_file for focused line ranges.",
    "Use search_capabilities and get_capability_details when you are unsure which engine tool or resource to use.",
    "Use spartan_status when you need to know whether the MCP bridge, engine, or codebase index is ready.",
    "Use debug_log_read when diagnosing what commands the assistant sent to the engine and what came back.",
    "Use context_snapshot and entity_resolve instead of multiple separate read calls.",
    "Use camera_snapshot before camera-relative placement such as in front of camera, beside camera, or from camera.",
    "Use world_raycast for ground or surface-relative placement instead of assuming y=0 when precision matters.",
    "Use entity_create_light for every light. Never hand-roll lights with entity_create_empty + entity_add_component light + component_set; that path leaves weak invisible lights.",
    "Do not use execute_lua for API discovery, pairs/next probing, method listing, or exploratory scripts. Those crash or hang the engine.",
    "Prefer entity_create_primitive_batch over execute_lua for repeated primitives. Use execute_lua only when a native batch tool cannot express the edit, and then only with one focused script that uses known bindings.",
    "When you learn a durable lesson, correction, recurring problem, or maintainer improvement idea, update agent memory concisely.",
    "Do not reveal hidden chain of thought. Report only brief progress, blockers, and final results.",
  ];

  const focused_asset =
    Boolean(revision) ||
    intent?.kind === "focused_asset";
  const greybox =
    Boolean(intent?.greybox) ||
    is_scene_stage_request(prompt);
  if (focused_asset)
  {
    lines.push(
      ...focused_asset_quality_prompt_lines(
        prompt,
        reference_images,
      ),
    );
    lines.push(
      "For a focused single-asset request, build one current asset in isolation. There is no part, material, or triangle cap. Create every part and material the object needs.",
      "Begin editing the prepared asset root immediately. Do not spend multiple minutes narrating, repeating lookups, or redesigning the prepared baseline before the first mutation.",
      "Never move or capture the main scene viewport. The run finalizer performs the one Asset Viewer review.",
      "Texture every material that represents a real surface. Use material_textured_create so the material and its color, roughness, normal and packed maps are made together. Its roughness and metalness go into the maps; for glaze, varnish, paint or lacquer also pass clearcoat 1, clearcoat_roughness 0.04 and ior 1.5 on the same call. It returns the resolved material_path, use that path afterwards.",
      "Closed 2d profiles (curved_profile, loft, extruded_profile) are implicitly closed and any scale is fine, list the points once counter clockwise. A rejection names the exact problem, fix that instead of rescaling.",
      "For entity_add_component pass exactly id and a valid component type, for example {id, type: \"physics\"}; call component_types when the exact type is unknown.",
      "For material commands, pass the .material resource path returned by material creation or inspection as path, never an entity id or display name.",
      "material_set_texture requires {path, texture_type, texture_path}; slot is optional.",
    );
    if (prepared_root?.id && !revision)
    {
      lines.push(
        `The focused asset root already exists as entity id ${prepared_root.id}, named ${prepared_root.name}, with prefab path ${prepared_root.prefab_path}. Use this exact root.`,
        "This root is a new empty authoring workspace. Do not attach it to, replace, or save any existing scene entity whose name merely contains the subject.",
        "Do not call entity_find or entity_create_empty for the prepared root. Its prefab is intentionally not saved while empty; progress saving starts after the first successful part-changing command.",
        "Your first mutation must be mesh_generate or another direct geometry creation command for the primary form, parented to the prepared root. Do not mutate the root merely to begin.",
      );
    }
  }
  else if (greybox)
  {
    lines.push(
      "entity_create_light fully initializes intensity, range, angle, area size, shadows, and distances. Visible blockout defaults are point/spot 8500, area 12000, directional 120000.",
      "Do not pass tiny intensities like 25-100. If you omit intensity, the tool calibrates it.",
      "Parent new volumes under the resolved scene root or the current selection. Do not create a library prefab or open Asset Viewer.",
    );
  }
  else if (
    intent?.kind === "scene_rebuild" ||
    intent?.live_scene_action
  )
  {
    lines.push(
      "For every new build, design directly from the current request and prepared context. Do not search for persisted layouts, build definitions, or prior generated instructions.",
      "Before you build any recognisable object, ask the library for it first. Call world_asset_search with the plain object name, then with semantic aliases, tags, dimensions, style, and material constraints.",
      "For an environment build, reuse current library assets where they fit.",
      "Every persistent resource created through MCP belongs under the shared project/mcp/blockout directory.",
      "Use primitive-only single-area construction only when the user explicitly asks for a greybox. Normal environments require semantic planning, generated or compound geometry, materials, calibrated lighting, and correction audits.",
      ...scene_quality_prompt_lines(prompt, intent),
    );
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
        "Reusable asset matches were searched before this run. Reuse suitable entries with world_asset_load; inspect or improve only when their constraints do not fit.",
        `Prepared asset matches: ${safe_json(prepared_assets, 5000)}`,
      );
    }
  }

  if (
    intent?.target_name &&
    !revision &&
    !focused_asset
  )
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
    !greybox &&
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

  if (reference_images.length > 0)
  {
    const names = reference_images.map((file_path) =>
      path.basename(file_path),
    );
    lines.push(
      "",
      `${reference_images.length} reference image${reference_images.length === 1 ? " is" : "s are"} attached to this message. They are the visual specification of what to build.`,
      "Match silhouette, proportions, construction, materials, and colour from the images. The text request wins for scale in metres and anything to omit.",
      "Do not invent a different design. If the image and the text disagree about appearance, the image wins unless the text explicitly overrides that part.",
      `Attached files: ${names.join(", ")}`,
    );
  }

  // the brief is what the request implies rather than what it says, so it is advice, the request above
  // still decides what gets built and wins any disagreement between the two
  if (String(brief ?? "").trim().length > 0)
  {
    lines.push(
      "",
      focused_asset
        ? "Design brief expanded from that request. Use it to clarify proportions, construction, parts and materials. It cannot cap parts, materials or triangles. Where it conflicts with the request or the attached images, ignore it."
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
  options = {},
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

  if (options.exact_only)
  {
    return {
      ...last_result,
      root: null,
    };
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
    /^(?:create|make|build|generate|construct|block\s*out|grey\s*box|gray\s*box|design)\b/.test(
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

function asset_file_name(value)
{
  const safe = String(value ?? "asset")
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return safe || "asset";
}

async function ensure_edit_mode(run, snapshot)
{
  const playing =
    snapshot?.status?.playing === true ||
    snapshot?.playing === true;
  if (!playing)
  {
    return;
  }
  await run.tool(
    "engine_set_mode",
    {
      mode: "edit",
    },
    15000,
  );
}

async function prepare_focused_asset_root(
  run,
  snapshot,
  target_name,
)
{
  const prefab_path = `${asset_file_name(target_name)}.prefab`;

  await ensure_edit_mode(run, snapshot);

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

  await run.tool(
    "asset_viewer_preview_entity",
    {
      id: created.entity.id,
    },
    10000,
  );
  return {
    ...created.entity,
    prefab_path,
    prefab_ready: false,
  };
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
  let asset_hint = intent?.asset_hint ?? "";
  if (!asset_hint && intent?.use_selected)
  {
    const status = await run.tool(
      "asset_viewer_status",
      {},
      10000,
    );
    asset_hint = status.ok
      ? status.selected_asset_id ?? ""
      : "";
  }

  const resolved = await resolve_asset_by_name({
    project_root: get_project_root(),
    resource_directory:
      await assistant_resource_directory(context),
    hint: asset_hint,
  });
  if (!resolved.ok)
  {
    run.receipt("asset revision declined", {
      hint: asset_hint,
      reason: resolved.reason,
      ambiguous: resolved.ambiguous ?? [],
    });

    // several assets answer the name equally well. loading one of them would edit the wrong asset and
    // loading none of them would quietly build a duplicate of something the library already has, so the
    // choice goes to the agent, which can read the request and inspect the matches
    if ((resolved.ambiguous ?? []).length > 0)
    {
      return {
        ambiguous: resolved.ambiguous,
        hint: asset_hint,
        aspects: intent?.revision_aspects ?? [],
        root_id: null,
      };
    }
    return null;
  }

  const asset = resolved.asset;
  if (
    asset.type !== "prefab" &&
    asset.type !== "mesh"
  )
  {
    return {
      root_id: null,
      asset_id: asset.id,
      candidate_error:
        "automatic revisions require a prefab or mesh asset, revise standalone materials and textures through their owning prefab",
    };
  }
  const resource_directory =
    await assistant_resource_directory(context);
  const candidate =
    await world_asset_candidate_create(
      get_project_root(),
      resource_directory,
      {
        asset_id: asset.id,
        candidate_path: asset.path,
      },
    );
  if (!candidate.ok)
  {
    run.receipt("asset revision declined", {
      asset_id: asset.id,
      reason:
        candidate.error ??
        "could not create a copy-on-write candidate",
      pending_candidate:
        candidate.candidate_active === true,
      generation: candidate.generation ?? null,
    });
    return {
      root_id: null,
      asset_id: asset.id,
      candidate_error:
        candidate.error ??
        "could not create a copy-on-write candidate",
      candidate_generation:
        candidate.generation ?? null,
    };
  }
  const created = await run.tool(
    "entity_create_empty",
    {
      name: asset.name || asset.id,
      position: [0, 0, 0],
      active: false,
      transient: true,
      tags: [
        "authoring_workspace",
        "revision_candidate",
        "mcp_generated",
      ],
    },
    10000,
  );
  let root =
    created.ok && created.entity?.id
      ? created.entity
      : null;
  if (root && asset.type === "prefab")
  {
    const loaded = await run.tool(
      "prefab_load",
      {
        path: candidate.candidate_path,
        parent_id: root.id,
        name: asset.name || asset.id,
      },
      30000,
    );
    if (!loaded.ok)
    {
      root = null;
    }
  }
  else if (root && asset.type === "mesh")
  {
    const assigned = await run.tool(
      "render_set_mesh",
      {
        id: root.id,
        mesh: candidate.candidate_path,
      },
      20000,
    );
    if (!assigned.ok)
    {
      root = null;
    }
  }
  const source =
    `candidate copy of ${asset.path}`;

  if (!root?.id)
  {
    run.receipt("asset revision declined", {
      asset_id: asset.id,
      reason:
        "the candidate loaded but produced no entity to edit",
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
    aliases: asset.aliases ?? [],
    tags: asset.tags ?? [],
    constraints: asset.constraints ?? {},
    candidate_path: candidate.candidate_path,
    candidate_generation: candidate.generation,
    candidate_manifest_path: candidate.manifest_path,
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
    root_id: revision.root_id,
    source: revision.source,
    candidate_path: revision.candidate_path,
    candidate_generation:
      revision.candidate_generation,
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
  if (
    intent?.kind === "focused_asset" ||
    intent?.kind === "asset_revise"
  )
  {
    return null;
  }
  if (intent?.greybox || is_scene_stage_request(prompt))
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

const REFERENCE_IMAGE_MIME = {
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".webp": "image/webp",
};
const MAX_REFERENCE_IMAGES = 5;
const MAX_REFERENCE_IMAGE_BYTES = 15 * 1024 * 1024;

async function load_reference_images(paths)
{
  const images = [];
  const loaded = [];
  const skipped = [];
  const seen = new Set();
  for (const raw of Array.isArray(paths) ? paths : [])
  {
    const file_path = String(raw ?? "").trim();
    if (!file_path)
    {
      continue;
    }
    const key = file_path.toLowerCase();
    if (seen.has(key))
    {
      continue;
    }
    seen.add(key);
    if (images.length >= MAX_REFERENCE_IMAGES)
    {
      skipped.push({
        path: file_path,
        reason: "more than 5 images",
      });
      continue;
    }
    const mime = REFERENCE_IMAGE_MIME[path.extname(file_path).toLowerCase()];
    if (!mime)
    {
      skipped.push({
        path: file_path,
        reason: "use png, jpg, jpeg, gif, or webp",
      });
      continue;
    }
    try
    {
      const resolved = await resolve_readable_path(file_path);
      const buffer = await fs.readFile(resolved);
      if (buffer.length > MAX_REFERENCE_IMAGE_BYTES)
      {
        skipped.push({
          path: file_path,
          reason: "larger than 15 mb",
        });
        continue;
      }
      images.push({
        data: buffer.toString("base64"),
        mimeType: mime,
      });
      loaded.push(resolved);
    }
    catch (error)
    {
      skipped.push({
        path: file_path,
        reason: error.message,
      });
    }
  }
  return { images, loaded, skipped };
}

async function run_cursor_fallback_serial({ prompt, brief = "", api_key, model_id, engine_host, engine_port, run, timeout_ms, engine_first_timeout_ms, intent = null, images = [] }) {
  if (!api_key) {
    return {
      ok: false,
      text: "Cursor API key is missing. Paste it into the MCP Assistant window first.",
    };
  }

  const image_load = await load_reference_images(images);
  if (
    image_load.loaded.length > 0 ||
    image_load.skipped.length > 0
  )
  {
    run.receipt("reference images", {
      loaded: image_load.loaded,
      skipped: image_load.skipped,
    });
  }
  const prompt_images = image_load.images;
  const prompt_image_paths = image_load.loaded;
  if (
    prompt_image_paths.length > 0 &&
    intent?.kind === "asset_revise" &&
    !intent?.greybox &&
    !is_scene_stage_request(prompt)
  )
  {
    intent = {
      ...intent,
      kind: "focused_asset",
      use_selected: false,
    };
    run.receipt("reference image build", {
      reason:
        "attached images are a spec for a new asset, not a library revision",
    });
  }

  let cursor_run = null;
  const focused_asset_run =
    intent?.kind === "asset_revise" ||
    intent?.kind === "focused_asset";
  const focused_run_budget_ms = focused_asset_run
    ? focused_asset_time_budget_ms(
        prompt,
        prompt_image_paths.length > 0,
      )
    : maximum_focused_asset_run_ms;
  let focused_deadline_at = null;
  let focused_construction_deadline_at = null;
  let engine_tool_seen = false;
  let scene_mutation_seen = false;
  let cancel_message = "";
  active_assistant_context = {
    run,
    prompt,
    engine_host,
    engine_port,
    intent,
    asset_budget: focused_asset_run
      ? asset_detail_budget(prompt)
      : null,
    focused_asset_run,
    owned_resource_paths: new Set(),
    focused_deadline_at,
    focused_construction_deadline_at,
    construction_gate_closed: false,
    bridge_failure: "",
    mark_scene_mutation: () =>
    {
      scene_mutation_seen = true;
    },
    cancel_on_bridge_failure: (message) =>
    {
      cancel_message = message;
      if (focused_asset_run)
      {
        active_assistant_context
          .construction_gate_closed = true;
      }
      run.event("stage_note", { text: message });
      if (cursor_run?.supports?.("cancel"))
      {
        void cursor_run.cancel().catch(() => {});
      }
    },
  };
  let guard_timer = null;
  let wall_timer = null;
  let idle_timer = null;
  let activity_flush_timer = null;
  let last_activity_at = Date.now();
  let visual_review_seen = false;
  let cleanup_receipt = null;
  let cleanup_snapshot = null;
  const ensure_focused_time = (reserve_ms = 0) =>
  {
    if (
      focused_asset_run &&
      focused_deadline_at &&
      Date.now() + reserve_ms >=
        focused_deadline_at
    )
    {
      throw new Error(
        `focused asset run exceeded ${focused_run_budget_ms}ms`,
      );
    }
  };
  const close_construction_gate = () =>
  {
    if (!focused_asset_run)
    {
      return;
    }
    active_assistant_context.construction_gate_closed =
      true;
  };
  const start_focused_deadline = (budget_ms) =>
  {
    if (!focused_asset_run)
    {
      return;
    }
    const total_ms =
      Number(budget_ms) > 0
        ? Number(budget_ms)
        : focused_run_budget_ms;
    focused_deadline_at =
      Date.now() + total_ms;
    focused_construction_deadline_at =
      focused_deadline_at -
      focused_finalization_reserve_ms;
    active_assistant_context.focused_deadline_at =
      focused_deadline_at;
    active_assistant_context
      .focused_construction_deadline_at =
      focused_construction_deadline_at;
    active_assistant_context.construction_gate_closed =
      false;
  };
  const wait_for_assistant_tasks = async () =>
  {
    while (true)
    {
      await assistant_command_queue;
      const pending_tasks = [
        ...(
          active_assistant_context
            .assistant_async_task_promises ??
          []
        ),
      ];
      if (pending_tasks.length === 0)
      {
        ensure_focused_time();
        return;
      }
      const pending = Promise.all(pending_tasks);
      if (!focused_asset_run)
      {
        await pending;
        continue;
      }
      const remaining =
        focused_deadline_at - Date.now();
      if (remaining <= 0)
      {
        throw new Error(
          "focused asset run exceeded its deadline",
        );
      }
      const settled = await Promise.race([
        pending.then(() => true),
        new Promise((resolve) =>
        {
          const timer = setTimeout(
            () => resolve(false),
            remaining,
          );
          timer.unref?.();
        }),
      ]);
      if (!settled)
      {
        throw new Error(
          "assistant tasks did not settle before finalization",
        );
      }
    }
  };
  const clean_focused_run = async (outcome) =>
  {
    if (!focused_asset_run || cleanup_receipt)
    {
      return cleanup_receipt;
    }
    try
    {
      cleanup_receipt = await cleanup_run_resources({
        snapshot: cleanup_snapshot,
        final_prefab_path:
          active_assistant_context?.final_prefab_path ??
          "",
        latest_prefab_path:
          active_assistant_context?.latest_prefab_path ??
          "",
        protected_paths: [
          "project/mcp/blockout/catalog.json",
          active_assistant_context?.catalog_path ?? "",
        ].filter(Boolean),
        owned_paths: [
          ...(
            active_assistant_context
              ?.owned_resource_paths ??
            []
          ),
        ],
        outcome,
      });
    }
    catch (error)
    {
      cleanup_receipt = {
        ok: false,
        outcome,
        error: `focused asset cleanup failed: ${error.message}`,
        removed: [],
        failed: [],
      };
    }
    try
    {
      run.receipt(
        "focused asset cleanup",
        cleanup_receipt,
      );
    }
    catch
    {
    }
    return cleanup_receipt;
  };
  const finalize_response = async (response) =>
  {
    const outcome = response.ok
      ? "success"
      : (
          cancel_message ||
          String(response.text ?? "")
            .toLowerCase()
            .includes("cancelled")
        )
        ? "cancelled"
        : "failure";
    const receipt = await clean_focused_run(
      outcome,
    );
    return receipt
      ? {
          ...response,
          cleanup_receipt: receipt,
        }
      : response;
  };
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
    const required_asset_viewer_reviews =
      2;
    const successful_visual_review =
      is_visual_review &&
      object_contains(event, (value) =>
        value.ok === true &&
        Array.isArray(value.views) &&
        value.views.length >= required_asset_viewer_reviews &&
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
  const execute_agent_prompt = async (
    agent,
    prompt_text,
    options = {},
  ) => {
    start_focused_deadline(options.budget_ms);
    ensure_focused_time();
    last_activity_at = Date.now();
    const extra_images = options.extra_images ?? [];
    const send_images = [
      ...prompt_images.slice(
        0,
        Math.max(0, 5 - extra_images.length),
      ),
      ...extra_images,
    ];
    cursor_run = await agent.send(
      send_images.length > 0
        ? {
            text: prompt_text,
            images: send_images,
          }
        : prompt_text,
      {
        onStep: ({ step }) => {
          void observe(step);
        },
      },
    );
    run.receipt("cursor run", { id: cursor_run.id });

    const stream_task = cursor_run.stream ? (async () => {
      for await (const event of cursor_run.stream())
      {
        await observe(event);
      }
    })().catch(() => {}) : Promise.resolve();

    const wait_tasks = [
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
    ];
    if (focused_asset_run)
    {
      wait_tasks.push(
        new Promise((resolve) =>
        {
          const remaining = Math.max(
            1,
            focused_construction_deadline_at -
              Date.now(),
          );
          wall_timer = setTimeout(
            () =>
            {
              close_construction_gate();
              resolve({
                status: "completed",
                result:
                  "Focused construction window ended; finalizing the current asset.",
                focused_construction_timeout: true,
              });
              if (cursor_run?.supports?.("cancel"))
              {
                void cursor_run.cancel().catch(() => {});
              }
            },
            remaining,
          );
          wall_timer.unref?.();
        }),
      );
    }
    const result = await Promise.race(wait_tasks);
    if (wall_timer)
    {
      clearTimeout(wall_timer);
      wall_timer = null;
    }
    if (idle_timer)
    {
      clearInterval(idle_timer);
      idle_timer = null;
    }
    if (focused_asset_run)
    {
      close_construction_gate();
      if (
        result.focused_construction_timeout &&
        cursor_run?.supports?.("cancel")
      )
      {
        void cursor_run.cancel().catch(() => {});
      }
    }
    await wait_for_assistant_tasks();
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
    if (focused_asset_run)
    {
      cleanup_snapshot = await snapshot_run_resources({
        project_root: get_project_root(),
        resource_directory:
          active_assistant_context.resource_directory,
      });
    }
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
      "finding current reusable assets for this request",
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
      active_assistant_context.asset_revision =
        revision;
      if (!revision?.root_id)
      {
        if ((revision?.ambiguous ?? []).length > 0)
        {
          throw new Error(
            "the revision target is ambiguous, select the asset in the Asset Viewer or name it explicitly",
          );
        }
        if (revision?.candidate_error)
        {
          throw new Error(
            `${revision.candidate_error}, select the asset in the Asset Viewer or name it explicitly`,
          );
        }
        run.receipt("asset revision declined", {
          reason:
            "named asset is not in the library, creating a new one",
        });
        revision = null;
        active_assistant_context.asset_revision = null;
        intent = {
          ...intent,
          kind: "focused_asset",
          use_selected: false,
        };
        active_assistant_context.intent = intent;
      }
      if (revision?.root_id)
      {
        active_assistant_context.authoring_prefab_path =
          revision.candidate_path;
        active_assistant_context.candidate_generation =
          revision.candidate_generation;
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
      focused_asset_run
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
        !focused_asset_run
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
        close_construction_gate();
        run.event("stage_note", { text: cancel_message });
        if (cursor_run?.supports?.("cancel")) {
          void cursor_run.cancel().catch(() => {});
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
          initial_root,
          prompt_image_paths,
        ),
      );
    });

    if (
      cursor_result.status !== "error" &&
      cursor_result.status !== "cancelled" &&
      !focused_asset_run &&
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
      return finalize_response({
        ok: false,
        text: failure_message,
      });
    }

    if (cursor_result.status === "cancelled" || cancel_message) {
      return finalize_response({
        ok: false,
        text:
          cancel_message ||
          "Cursor run was cancelled.",
      });
    }

    if (
      focused_asset_run &&
      prompt_image_paths.length > 0 &&
      active_assistant_context.authoring_root_id &&
      cursor_result.status !== "error" &&
      cursor_result.status !== "cancelled"
    )
    {
      const built = await run.tool(
        "entity_get",
        {
          id: active_assistant_context.authoring_root_id,
          recursive: true,
        },
        10000,
      );
      if (
        built?.ok &&
        asset_build_is_incomplete(built.entity, prompt)
      )
      {
        run.receipt("incomplete asset continue", {
          parts: authored_part_count(built.entity),
          root_id: active_assistant_context.authoring_root_id,
        });
        cursor_result = await run.stage(
          "Continue Incomplete Asset",
          "the first window ended before the asset was recognisable",
          () => execute_agent_prompt(
            agent,
            incomplete_asset_continue_prompt(
              prompt,
              active_assistant_context.authoring_root_id,
              built.entity,
            ),
            {
              budget_ms: focused_run_budget_ms,
            },
          ),
        );
      }
    }

    if (
      focused_asset_run &&
      prompt_image_paths.length > 0 &&
      active_assistant_context.authoring_root_id &&
      cursor_result.status !== "error"
    )
    {
      for (let pass = 1; pass <= 2; pass++)
      {
        const review = await run.stage(
          `Capture Reference Match ${pass}`,
          "screenshotting the current asset against the reference",
          () => capture_asset_viewer_review(
            run,
            active_assistant_context.authoring_root_id,
            `match_${pass}`,
          ),
        );
        const extra_images = review?.path
          ? (
              await load_reference_images([review.path])
            ).images
          : [];
        run.receipt("reference match capture", {
          pass,
          ok: review?.ok === true,
          path: review?.path ?? "",
        });
        cursor_result = await run.stage(
          `Match Reference ${pass}`,
          "continuing construction until the asset matches the photo",
          () => execute_agent_prompt(
            agent,
            visual_match_prompt(
              prompt,
              active_assistant_context.authoring_root_id,
              pass,
            ),
            {
              extra_images,
              budget_ms: focused_run_budget_ms,
            },
          ),
        );
        if (
          cursor_result.status === "error" ||
          cursor_result.status === "cancelled"
        )
        {
          break;
        }
      }
    }
    if (
      !scene_mutation_seen &&
      (
        intent?.kind === "scene_rebuild" ||
        intent?.live_scene_action
      )
    )
    {
      return finalize_response({
        ok: false,
        text:
          "Cursor completed twice without using a Spartan engine tool. No scene changes were made.",
      });
    }

    if (
      intent?.greybox ||
      is_scene_stage_request(prompt)
    )
    {
      return finalize_response({
        ok: true,
        text: cursor_result.result?.trim() || "Done.",
      });
    }

    const is_scene_construction =
      intent?.kind === "scene_rebuild" ||
      intent?.live_scene_action ||
      focused_asset_run;
    if (
      !is_scene_construction ||
      (
        !intent?.target_name &&
        !intent?.use_selected
      )
    )
    {
      return finalize_response({
        ok: true,
        text: cursor_result.result?.trim() || "Done.",
      });
    }

    const found = await run.stage(
      "Resolve Quality Root",
      "finding the completed scene hierarchy",
      async () =>
      {
        if (
          focused_asset_run &&
          active_assistant_context.authoring_root_id
        )
        {
          const entity = await run.tool(
            "entity_get",
            { id: active_assistant_context.authoring_root_id },
          );
          if (entity.ok && entity.entity)
          {
            return {
              ok: true,
              root: entity.entity,
              resolution: "authoring_root",
            };
          }
        }
        return intent.target_name
          ? resolve_quality_root(
              run,
              intent.target_name,
              focused_asset_run ? 1 : 10,
              { exact_only: focused_asset_run },
            )
          : resolve_selected_quality_root(run);
      },
    );
    let root_id = found.root?.id;
    let root_name = scene_plan_root_name(
      found.root,
      intent.target_name,
    );
    if (!found.ok || !root_id)
    {
      return finalize_response({
        ok: false,
        text:
          `Scene quality gate could not resolve root entity ${root_name}.`,
      });
    }

    if (!focused_asset_run)
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
      focused_asset_run;
    const audit_args = focused_asset
      ? {
          id: root_id,
          ...prop_quality_profile,
          profile: "prop",
        }
      : {
          id: root_id,
          required_features:
            infer_required_features(prompt),
          scene_type: infer_design_template(prompt),
          planned_element_count:
            planned_elements.reduce(
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
      focused_asset
        ? "Audit Prop Quality"
        : "Audit Scene Quality",
      focused_asset
        ? "checking for renderable material content"
        : "checking geometry, materials, features, and lighting",
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

    const correction_attempts = focused_asset
      ? 0
      : 2;
    for (
      let attempt = 1;
      attempt <= correction_attempts &&
      (
        !audit.pass ||
        !layout_audit.pass ||
        !visual_review_seen
      );
      attempt++
    )
    {
      const correction_prompt = [
        "Perform a quality correction pass on the live Spartan Engine scene.",
        `Original request: ${prompt}`,
        `Root entity: ${root_name}, id ${root_id}.`,
        `Quality audit: ${safe_json(audit, 3500)}`,
        `Layout audit: ${safe_json(layout_audit, 5000)}`,
        "If the generic scene plan is missing or invalid, call scene_plan_create first with realistic expected dimensions, zones, support modes, relationships, and lighting intent inferred from the original request.",
        "Call scene_visual_review on the root with perspective and top views, then inspect both images.",
        "Fix every failed scene_layout_audit and scene_quality_audit check, including every render component listed by collision_coverage, plus the most visible weakness in the image.",
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
        return finalize_response({
          ok: false,
          text: `Scene was edited, but quality correction failed: ${failure_message}`,
        });
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

    let asset_prefab = null;
    if (focused_asset && root_id)
    {
      ensure_focused_time();
      asset_prefab = await run.stage(
        "Finalize Asset",
        "optimizing, saving, registering, and reviewing the asset",
        async () =>
        {
          active_assistant_context.finalizing_asset = true;
          active_assistant_context
            .finalization_restore_reserve_ms = 5000;
          let root_state_changed = false;
          try
          {
            ensure_focused_time(5000);
            const activated = await run.tool(
              "entity_update",
              {
                id: root_id,
                active: true,
                transient: false,
              },
              focused_command_timeout(
                active_assistant_context,
                30000,
              ),
            );
            if (!activated?.ok)
            {
              throw new Error(
                activated?.error ??
                "failed to activate asset root",
              );
            }
            root_state_changed = true;
            ensure_focused_time(5000);
            const game_ready = await make_game_ready(
              active_assistant_context,
              root_id,
            );
            ensure_focused_time(5000);
            const asset_type =
              revision?.asset_type ??
              "prefab";
            let revision_mesh_path = "";
            if (asset_type === "mesh")
            {
              ensure_focused_time(5000);
              const render_materials = await run.tool(
                "entity_render_materials",
                {
                  id: root_id,
                  include_descendants: true,
                },
                focused_command_timeout(
                  active_assistant_context,
                  30000,
                ),
              );
              if (!render_materials?.ok)
              {
                throw new Error(
                  render_materials?.error ??
                  "failed to query the finalized mesh",
                );
              }
              const mesh_paths = [
                ...new Set(
                  (render_materials.materials ?? [])
                    .map((entry) =>
                      String(
                        entry.mesh_path ??
                        entry.mesh ??
                        "",
                      ).trim(),
                    )
                    .filter(Boolean),
                ),
              ];
              if (mesh_paths.length === 0)
              {
                throw new Error(
                  "finalized mesh revision has no mesh path",
                );
              }
              if (mesh_paths.length > 1)
              {
                throw new Error(
                  "finalized mesh revision has multiple mesh paths",
                );
              }
              revision_mesh_path = mesh_paths[0];
            }
            const prefab_path =
              active_assistant_context.authoring_prefab_path ??
              `${asset_file_name(root_name)}.prefab`;
            const saved = asset_type === "mesh"
              ? {
                  ok: Boolean(revision_mesh_path),
                  path: revision_mesh_path,
                }
              : await run.tool(
                  "prefab_save",
                  {
                    id: root_id,
                    path: prefab_path,
                  },
                  focused_command_timeout(
                    active_assistant_context,
                    60000,
                  ),
                );
            active_assistant_context.final_prefab_path =
              saved?.ok && asset_type === "prefab"
                ? saved.path ?? prefab_path
                : "";
            if (asset_type === "prefab")
            {
              track_owned_resource_paths(
                active_assistant_context,
                "prefab_save",
                {
                  path: prefab_path,
                },
                saved,
              );
            }
            let registration = null;
            if (saved?.ok)
            {
              try
              {
                ensure_focused_time(5000);
                const registration_path =
                  asset_type === "mesh"
                    ? revision_mesh_path
                    : revision
                      ? saved.path
                      : saved.path ?? prefab_path;
                if (!registration_path)
                {
                  throw new Error(
                    "final prefab save returned no path",
                  );
                }
                registration = revision
                  ? await world_asset_candidate_create(
                      get_project_root(),
                      await assistant_resource_directory(
                        active_assistant_context,
                      ),
                      {
                        asset_id: revision.asset_id,
                        candidate_path:
                          registration_path,
                        generation:
                          revision.candidate_generation,
                        replace_existing: true,
                        entity_count:
                          game_ready?.renderers_after ??
                          null,
                        asset: {
                          name: revision.asset_name,
                          aliases:
                            revision.aliases ?? [],
                          tags: revision.tags ?? [],
                          constraints:
                            revision.constraints ?? {},
                        },
                      },
                    )
                  : await world_asset_register(
                      get_project_root(),
                      await assistant_resource_directory(
                        active_assistant_context,
                      ),
                      {
                        type: asset_type,
                        asset_id:
                          asset_file_name(root_name),
                        name: root_name,
                        path: registration_path,
                        aliases: [],
                        tags: [],
                        constraints: {},
                      },
                    );
                ensure_focused_time(5000);
                track_owned_resource_paths(
                  active_assistant_context,
                  "world_asset_register",
                  {
                    path: registration_path,
                  },
                  registration,
                );
              }
              catch (error)
              {
                registration = {
                  ok: false,
                  error: error.message,
                };
              }
              active_assistant_context.latest_prefab_path =
                (
                  revision
                    ? registration?.candidate_path
                    : registration?.asset?.path
                ) ??
                "";
              active_assistant_context.catalog_path =
                (
                  revision
                    ? registration?.manifest_path
                    : registration?.catalog_path
                ) ??
                "";
            }
            ensure_focused_time(5000);
            const preview = await run.tool(
              "asset_viewer_preview_entity",
              {
                id: root_id,
              },
              focused_command_timeout(
                active_assistant_context,
                10000,
              ),
            );
            let camera = {
              ok: false,
              error:
                "asset preview was not ready for framing",
            };
            if (preview.ok)
            {
              camera = await run.tool(
                "asset_viewer_set_view",
                {
                  view: "perspective",
                  zoom: 1,
                },
                focused_command_timeout(
                  active_assistant_context,
                  10000,
                ),
              );
            }
            let screenshot = {
              ok: false,
              ready: false,
              error:
                preview.ok
                  ? "asset viewer camera was not ready"
                  : "asset preview was not ready",
            };
            if (preview.ok && camera.ok)
            {
              ensure_focused_time(5000);
              screenshot = await run.tool(
                "asset_viewer_screenshot",
                {
                  path:
                    `asset_${root_id}_final.png`,
                  width: 768,
                  height: 768,
                },
                focused_command_timeout(
                  active_assistant_context,
                  10000,
                ),
              );
              if (
                screenshot.ok &&
                screenshot.path &&
                screenshot.ready !== true
              )
              {
                screenshot.ready = await wait_for_screenshot(
                  screenshot.path,
                  focused_command_timeout(
                    active_assistant_context,
                    12000,
                  ),
                );
              }
            }
            visual_review_seen =
              preview.ok === true &&
              camera.ok === true &&
              screenshot.ok === true &&
              screenshot.ready === true;
            if (visual_review_seen)
            {
              active_assistant_context.mark_visual_review?.();
            }
            return {
              ...saved,
              game_ready,
              registration,
              visual_review: {
                preview,
                camera,
                screenshot,
              },
              path:
                asset_type === "mesh"
                  ? revision_mesh_path
                  : saved.path ?? prefab_path,
            };
          }
          finally
          {
            active_assistant_context
              .restoring_asset_state = true;
            try
            {
              if (root_state_changed)
              {
                const restored = await run.tool(
                  "entity_update",
                  {
                    id: root_id,
                    active: false,
                    transient: true,
                  },
                  focused_command_timeout(
                    active_assistant_context,
                    5000,
                  ),
                );
                if (!restored?.ok)
                {
                  throw new Error(
                    restored?.error ??
                    "asset root state was not restored",
                  );
                }
              }
            }
            catch (error)
            {
              run.event(
                "stage_note",
                {
                  text:
                    `asset root restoration failed: ${
                      error.message
                    }`,
                },
              );
            }
            finally
            {
              active_assistant_context
                .restoring_asset_state = false;
              active_assistant_context
                .finalizing_asset = false;
            }
          }
        },
      );
    }

    const resource_cleanup = focused_asset
      ? {
          ok: true,
          removed: [],
          orphan_count: 0,
          scoped: true,
        }
      : await run.stage(
          "Clean World Resources",
          "removing unreferenced world assets",
          () => run.tool(
            "world_resources_clean",
            {},
          ),
        );
    if (
      focused_asset &&
      (
        !asset_prefab?.ok ||
        !asset_prefab.registration?.ok
      )
    )
    {
      return finalize_response({
        ok: false,
        text: [
          revision
            ? "The revision could not be packaged as a review candidate. The registered asset was not changed."
            : "The asset was built but could not be saved, so there is no reusable prefab.",
          `Prefab save error: ${asset_prefab?.error ?? "none"}`,
          revision
            ? `Candidate packaging error: ${asset_prefab?.registration?.error ?? "candidate packaging never ran"}`
            : `Catalog registration error: ${asset_prefab?.registration?.error ?? "the registration never ran"}`,
          `Root entity: ${root_name}, id ${root_id}.`,
          "Run-scoped cleanup removes generated files that the final prefab does not reach.",
        ].join("\n"),
      });
    }
    if (
      !audit.pass ||
      !layout_audit.pass ||
      !visual_review_seen
    )
    {
      return finalize_response({
        ok: false,
        text: [
          "Scene was edited, but the quality gate remains incomplete.",
          `Quality audit: ${safe_json(audit, 2500)}`,
          `Layout audit: ${safe_json(layout_audit, 3500)}`,
          `Visual review completed: ${visual_review_seen}.`,
          focused_asset
            ? "Run-scoped cleanup receipt attached."
            : (
                resource_cleanup.ok
                  ? `World resources cleaned: ${(resource_cleanup.removed ?? []).length} unused files removed, ${resource_cleanup.orphan_count ?? 0} undeleted orphans.`
                  : `World resource cleanup failed for ${(resource_cleanup.failed ?? []).length} files.`
              ),
          focused_asset
            ? "Final-state audits are authoritative."
            : "Final-state audits are authoritative for plan and correction completion.",
        ].join("\n"),
      });
    }

    return finalize_response({
      ok: true,
      text: [
        focused_asset
          ? (
              revision
                ? `Revision candidate for ${revision.asset_name} is ready for review.`
                : `Focused asset ${root_name} completed.`
            )
          : (
              final_result.result?.trim() ||
              cursor_result.result?.trim() ||
              "Done."
            ),
        focused_asset
          ? (
              prompt_image_paths.length > 0
                ? "Prefab saved. Compare the Asset Viewer to your reference photo. If the silhouette still does not match, send another prompt describing what to change."
                : "Prop quality passed: renderable material content and perspective review are complete."
            )
          : `Quality gates passed: content ${audit.score}/100, layout ${layout_audit.score}/100, visual review complete.`,
        ...(asset_prefab?.ok
          ? [
              revision
                ? `Candidate saved to ${asset_prefab.path}. The registered asset is unchanged.`
                : `Prefab saved to ${asset_prefab.path}.`,
              asset_prefab.game_ready?.renderers_before > asset_prefab.game_ready?.renderers_after
                ? `Game ready pass merged ${asset_prefab.game_ready.renderers_before} meshes down to ${asset_prefab.game_ready.renderers_after} by material.`
                : "Game ready pass found nothing to merge.",
            ]
          : []),
        ...(revision && asset_prefab?.registration?.ok
          ? [
              `Apply or discard candidate generation ${asset_prefab.registration.generation} in the Asset Viewer.`,
            ]
          : []),
        focused_asset
          ? "Run-scoped cleanup receipt attached."
          : (
              resource_cleanup.ok
                ? `World resources cleaned: ${(resource_cleanup.removed ?? []).length} unused files removed, ${resource_cleanup.orphan_count ?? 0} undeleted orphans.`
                : `World resource cleanup failed for ${(resource_cleanup.failed ?? []).length} files.`
            ),
      ].join("\n"),
    });
  } catch (error) {
    if (focused_asset_run)
    {
      close_construction_gate();
      if (cursor_run?.supports?.("cancel"))
      {
        void cursor_run.cancel().catch(() => {});
      }
    }
    try
    {
      await wait_for_assistant_tasks();
    }
    catch
    {
    }
    if (
      cursor_run?.supports?.("cancel") &&
      (
        error.message?.includes("within") ||
        error.message?.includes("focused asset run exceeded")
      )
    ) {
      void cursor_run.cancel().catch(() => {});
    }

    if (error instanceof CursorAgentError) {
      return finalize_response({
        ok: false,
        text: `Cursor startup failed: ${error.message}`,
      });
    }

    return finalize_response({
      ok: false,
      text: `Assistant failed: ${error.message}`,
    });
  } finally {
    try
    {
      await clean_focused_run(
        cancel_message ? "cancelled" : "failure",
      );
    }
    catch
    {
    }
    finally
    {
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
      if (wall_timer)
      {
        clearTimeout(wall_timer);
      }
      if (activity_flush_timer)
      {
        clearTimeout(activity_flush_timer);
      }
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
