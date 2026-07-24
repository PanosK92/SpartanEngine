#!/usr/bin/env node

import http from "node:http";
import fs from "node:fs/promises";
import path from "node:path";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { EngineClient } from "./engine_client.mjs";
import { append_debug_log, debug_log_path, read_debug_log } from "./debug_log.mjs";
import { get_project_root, get_shared_codebase } from "./shared_codebase.mjs";
import { component_schema_markdown, construction_grammar_guide, edit_rules, engine_overview, parametric_modeling_guide, scene_planning_guide, search_capability_catalog } from "./knowledge.mjs";
import { json_schema_from_raw_shape, normalize_result, output_schemas, parse_raw_shape, structured_error } from "./schemas.mjs";
import { agent_memory_path, append_agent_memory, ensure_agent_memory, read_agent_memory, write_agent_memory } from "./agent_memory.mjs";
import { calibrate_existing_light } from "./light_calibration.mjs";
import { audit_scene_quality } from "./scene_quality.mjs";
import {
  audit_scene_layout,
  create_scene_plan,
  make_scene_plan_namespace,
  read_scene_plan,
} from "./scene_planning.mjs";
import {
  build_construction_grammar,
  construction_grammar_names,
  suggest_construction_grammars,
} from "./construction_grammar.mjs";
import {
  create_design_brief,
  design_template_names,
  semantic_role_catalog,
  suggest_scene_plan,
} from "./design_intelligence.mjs";
import {
  calculate_benchmark_metrics,
  compare_benchmark_results,
  get_benchmark,
  list_benchmarks,
} from "./scene_benchmarks.mjs";
import {
  constrain_generated_resources,
  generated_resource_command,
  world_resource_directory,
} from "./world_resources.mjs";

const project_root = get_project_root();
await ensure_agent_memory();

const defaults = {
  host: process.env.SPARTAN_ENGINE_HOST ?? "127.0.0.1",
  port: Number.parseInt(process.env.SPARTAN_ENGINE_PORT ?? "47777", 10),
  timeout_ms: Number.parseInt(process.env.SPARTAN_ENGINE_TOOL_TIMEOUT_MS ?? "30000", 10),
};

function read_arg(name, fallback) {
  const prefix = `--${name}=`;
  const match = process.argv.find((arg) => arg.startsWith(prefix));
  if (!match) {
    return fallback;
  }

  return match.slice(prefix.length);
}

const engine_host = read_arg("host", defaults.host);
const engine_port = Number.parseInt(read_arg("port", String(defaults.port)), 10);
const engine_timeout_ms = Number.parseInt(read_arg("timeout-ms", String(defaults.timeout_ms)), 10);
const requested_transport = read_arg("transport", "stdio");
const http_enabled = requested_transport === "http" || process.argv.includes("--http") || process.argv.some((arg) => arg.startsWith("--http-port="));
const stdio_enabled = !process.argv.includes("--no-stdio") && requested_transport !== "http";
const http_port = Number.parseInt(read_arg("http-port", process.env.SPARTAN_MCP_HTTP_PORT ?? "8765"), 10);
const read_only_mode = process.argv.includes("--read-only") || ["1", "true", "yes", "on"].includes(String(process.env.SPARTAN_MCP_READ_ONLY ?? "").toLowerCase());

if (!Number.isInteger(engine_port) || engine_port <= 0 || engine_port > 65535) {
  console.error("invalid spartan engine port");
  process.exit(1);
}

if (!Number.isInteger(engine_timeout_ms) || engine_timeout_ms <= 0) {
  console.error("invalid spartan engine timeout");
  process.exit(1);
}

if (http_enabled && (!Number.isInteger(http_port) || http_port <= 0 || http_port > 65535)) {
  console.error("invalid spartan MCP HTTP port");
  process.exit(1);
}

const engine = new EngineClient({
  host: engine_host,
  port: engine_port,
  timeout_ms: engine_timeout_ms,
  source: "server",
});
void append_debug_log({
  type: "server_started",
  source: "server",
  engine_host,
  engine_port,
  transport: requested_transport,
  http_enabled,
  stdio_enabled,
  client_features: {
    idle_socket_reuse: true,
    idle_close_ms: 250,
    command_timeout_names: true,
    bridge_request_ids: true,
    async_task_reporting: true,
  },
});

const codebase = get_shared_codebase();

async function current_scene_plan_namespace() {
  const world = await send_engine_command(
    "world_summary",
  );
  return make_scene_plan_namespace({
    project_root,
    engine_host,
    engine_port,
    world,
  });
}

async function benchmark_baseline_path(benchmark_id) {
  const world = await send_engine_command(
    "world_summary",
  );
  const world_path =
    world.file_path ??
    world.path ??
    "";
  const storage_root = world_path
    ? path.dirname(path.resolve(world_path))
    : project_root;
  return path.join(
    storage_root,
    ".spartan",
    "scene_benchmarks",
    `${safe_asset_name(benchmark_id)}.json`,
  );
}

let active_resource_directory = null;

async function send_engine_command(command, args = {}) {
  if (
    generated_resource_command(command) &&
    !active_resource_directory
  )
  {
    const world = await engine.command(
      "world_summary",
      {},
      engine_timeout_ms,
    );
    if (world.ok)
    {
      active_resource_directory =
        world_resource_directory(world);
    }
  }
  if (generated_resource_command(command))
  {
    args = constrain_generated_resources(
      command,
      args,
      active_resource_directory ??
        world_resource_directory(),
    );
  }
  const result = await engine.command(
    command,
    args,
    engine_timeout_ms,
  );
  if (command === "world_summary" && result.ok)
  {
    active_resource_directory =
      world_resource_directory(result);
  }
  else if (
    command === "context_snapshot" &&
    result.world
  )
  {
    active_resource_directory =
      world_resource_directory(result.world);
  }
  else if (
    command === "world_load" ||
    command === "world_new"
  )
  {
    active_resource_directory = null;
  }
  return result;
}

function tool_result(result) {
  const normalized = normalize_result(result);
  return {
    structuredContent: normalized,
    content: [
      {
        type: "text",
        text: JSON.stringify(normalized),
      },
    ],
    isError: !normalized.ok,
  };
}

const tool_registry = new Map();
const resource_registry = new Map();

function register_local_tool(name, config, handler) {
  if (read_only_mode && config.annotations && !config.annotations.readOnlyHint) {
    return;
  }

  const input_schema = config.inputSchema ?? {};
  const validated_handler = async (args) => {
    const started_at = Date.now();
    let result;
    try
    {
      const parsed = parse_raw_shape(
        input_schema,
        args,
      );
      if (!parsed.ok)
      {
        result = tool_result(parsed.error);
      }
      else
      {
        result = await handler(parsed.value);
        const output_schema = config.outputSchema;
        if (
          output_schema?.safeParse &&
          result?.structuredContent
        )
        {
          const output = output_schema.safeParse(
            result.structuredContent,
          );
          if (!output.success)
          {
            result = tool_result(
              structured_error(
                "tool output failed its declared schema",
                {
                  code: "invalid_tool_output",
                  retryable: false,
                },
              ),
            );
          }
        }
      }
      if (
        config.traceLocal !== false &&
        name !== "debug_log_read"
      )
      {
        await append_debug_log({
          type: "local_tool",
          source: "server",
          tool: name,
          args,
          duration_ms: Date.now() - started_at,
          ok: !result?.isError,
          result: result?.structuredContent,
        });
      }
      return result;
    }
    catch (error)
    {
      if (config.traceLocal !== false)
      {
        await append_debug_log({
          type: "local_tool",
          source: "server",
          tool: name,
          args,
          duration_ms: Date.now() - started_at,
          ok: false,
          error: error.message,
        });
      }
      throw error;
    }
  };
  const tool = {
    name,
    title: config.title ?? name.replaceAll("_", " "),
    description: config.description ?? "",
    inputSchema: input_schema,
    outputSchema: config.outputSchema,
    annotations: config.annotations,
    handler: validated_handler,
  };

  tool_registry.set(name, tool);
  server.registerTool(name, {
    title: tool.title,
    description: tool.description,
    inputSchema: input_schema,
    outputSchema: tool.outputSchema,
    annotations: tool.annotations,
  }, validated_handler);
}

function register_tool(server, name, description, schema, command, options = {}) {
  register_local_tool(name, {
    title: options.title ?? name.replaceAll("_", " "),
    description,
    inputSchema: schema,
    outputSchema: options.outputSchema ?? output_schemas.generic,
    annotations: options.annotations,
    traceLocal: false,
  }, async (args) => {
    let mapped_args = args;
    try {
      const map_args = options.map_args ?? ((value) => value);
      mapped_args = map_args(args);
    } catch (error) {
      return tool_result(structured_error(error.message, { code: "invalid_arguments" }));
    }

    const result = await send_engine_command(command, mapped_args);
    return tool_result(result);
  });
}

const async_tasks = new Map();
let next_async_task_id = 1;

function async_task_receipt(task) {
  return {
    id: task.id,
    tool: task.tool,
    status: task.status,
    started_at: task.started_at,
    completed_at: task.completed_at,
    duration_ms: task.completed_at ? task.completed_at_ms - task.started_at_ms : Date.now() - task.started_at_ms,
    is_error: task.is_error,
    result: task.result,
  };
}

function prune_async_tasks() {
  if (async_tasks.size <= 100)
  {
    return;
  }

  for (const [id, task] of async_tasks)
  {
    if (task.status === "completed" || task.status === "failed")
    {
      async_tasks.delete(id);
      if (async_tasks.size <= 100)
      {
        return;
      }
    }
  }
}

async function run_async_task(task, tool, args) {
  task.status = "running";
  try
  {
    const result = await tool.handler(args);
    task.result = normalize_result(result?.structuredContent ?? { ok: false, error: "tool returned no structured result" });
    task.is_error = Boolean(result?.isError);
    task.status = task.is_error ? "failed" : "completed";
  }
  catch (error)
  {
    task.result = structured_error(error.message, { code: "async_task_failed" });
    task.is_error = true;
    task.status = "failed";
  }
  finally
  {
    task.completed_at_ms = Date.now();
    task.completed_at = new Date(task.completed_at_ms).toISOString();
    prune_async_tasks();
  }
}

const vector3 = z.array(z.number()).length(3);
const vector2 = z.array(z.number()).length(2);
const quaternion = z.array(z.number()).length(4);
const vector4 = z.array(z.number()).length(4);
const geometry_axis = z.enum(["x", "y", "z"]);
const positive_vector3 = z.array(
  z.number().positive(),
).length(3);
const scene_zone = z.object({
  name: z.string().min(1),
  purpose: z.string().min(1),
  center: vector3,
  size: positive_vector3,
});
const scene_element = z.object({
  name: z.string().min(1),
  role: z.enum([
    "surface",
    "structure",
    "route",
    "functional",
    "furnishing",
    "prop",
    "detail",
    "light",
  ]),
  zone: z.string().optional(),
  expected_size: positive_vector3.optional(),
  size_mode: z.enum([
    "individual",
    "aggregate",
  ]).optional(),
  allow_axis_permutation: z.boolean().optional(),
  size_tolerance: z.number().min(0.05).max(2).optional(),
  support: z.enum([
    "ground",
    "surface",
    "wall",
    "ceiling",
    "suspended",
    "none",
  ]),
  max_support_gap: z.number().min(0).max(10).optional(),
  max_intersection_depth: z.number().min(0).max(10).optional(),
  zone_tolerance: z.number().min(0).max(100).optional(),
  count: z.number().int().min(1).max(1000).optional(),
  clearance: z.number().min(0).max(1000).optional(),
  material_semantic: z.string().optional(),
  semantic_tags: z.array(
    z.string().min(1),
  ).max(16).optional(),
});
const scene_relationship = z.object({
  subject: z.string().min(1),
  relation: z.enum([
    "on",
    "inside",
    "connected_to",
    "separated_from",
    "aligned_with",
    "beside",
  ]),
  object: z.string().min(1),
  axis: z.enum(["x", "y", "z"]).optional(),
  tolerance: z.number().min(0).max(1000).optional(),
  min_distance: z.number().min(0).max(10000).optional(),
  max_distance: z.number().min(0).max(10000).optional(),
});
const scene_plan_input = {
  root_name: z.string().min(1),
  purpose: z.string().min(1),
  scale_reference: z.object({
    name: z.string().min(1),
    size: positive_vector3,
    rationale: z.string().min(1),
  }),
  zones: z.array(scene_zone).min(1).max(32),
  elements: z.array(scene_element).min(5).max(256),
  relationships: z.array(
    scene_relationship,
  ).max(512).optional(),
  lighting: z.object({
    intent: z.string().min(1),
    min_lights: z.number().int().min(1).max(64),
    max_lights: z.number().int().min(1).max(128).optional(),
    require_shadows: z.boolean().optional(),
  }),
  quality_goals: z.array(z.string()).max(32).optional(),
};
const numeric_array = z.array(z.number()).min(2).max(16);
const mesh_type = z.enum(["cube", "quad", "plane", "sphere", "cylinder", "cone"]);
const body_type = z.enum(["box", "sphere", "plane", "capsule", "mesh", "mesh_convex", "controller", "vehicle", "cloth"]);
const resource_type = z.enum(["all", "unknown", "texture", "audio", "material", "mesh", "cubemap", "animation", "font", "shader"]);
const material_texture_type = z.enum(["color", "albedo", "base_color", "roughness", "metalness", "metallic", "normal", "occlusion", "ao", "emission", "emissive", "height", "alpha_mask", "alpha", "packed"]);
const component_type = z.enum([
  "audio_source",
  "camera",
  "light",
  "physics",
  "render",
  "spline",
  "spline_follower",
  "terrain",
  "volume",
  "script",
  "particle_system",
  "skid_marks",
  "water",
  "traffic",
  "spawn_point",
  "car_reset",
  "text_3d",
]);
const component_value = z.union([z.string(), z.number(), z.boolean(), numeric_array]);
const light_type = z.enum(["directional", "point", "spot", "area"]);
const entity_identity_args = {
  tags: z.array(z.string().min(1)).max(32).optional(),
  semantic_id: z.string().min(1).optional(),
  plan_element: z.string().min(1).optional(),
  semantic_tags: z.array(
    z.string().min(1),
  ).max(16).optional(),
};
const transform_set_args = {
  id: z.string(),
  position: vector3.optional(),
  rotation: quaternion.optional(),
  rotation_euler: vector3.optional(),
  scale: vector3.optional(),
};
const primitive_create_args = {
  mesh: mesh_type.optional(),
  name: z.string().optional(),
  parent_id: z.string().optional(),
  position: vector3.optional(),
  rotation_euler: vector3.optional(),
  scale: vector3.optional(),
  material: z.string().optional().describe("cached material name, cached path, material file path, or default"),
  with_physics: z.boolean().optional().describe("defaults to true, set false only for intentionally non-colliding visual geometry"),
  body_type: body_type.optional(),
  physics_static: z.boolean().optional(),
  physics_kinematic: z.boolean().optional(),
  physics_mass: z.number().optional(),
  physics_friction: z.number().optional(),
  physics_restitution: z.number().optional(),
  ...entity_identity_args,
};
const light_create_args = {
  name: z.string().optional(),
  parent_id: z.string().optional(),
  position: vector3.optional(),
  rotation_euler: vector3.optional(),
  scale: vector3.optional(),
  light_type: light_type.optional().describe("point, spot, area, or directional"),
  color: vector4.optional(),
  temperature: z.number().optional().describe("kelvin"),
  intensity: z.number().optional().describe("lux for directional, lumens otherwise; weak values are replaced by calibrated defaults"),
  range: z.number().optional().describe("meters, point/spot/area"),
  angle_degrees: z.number().optional().describe("spot cone degrees"),
  area_width: z.number().optional().describe("area light width in meters"),
  area_height: z.number().optional().describe("area light height in meters"),
  shadows: z.boolean().optional(),
  volumetric: z.boolean().optional(),
  draw_distance: z.number().optional(),
  shadow_distance: z.number().optional(),
  volumetric_distance: z.number().optional(),
  calibrated: z.boolean().optional().describe("default true; set false only for intentionally weak lights"),
  ...entity_identity_args,
};
const parametric_shape = z.enum([
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
const profile2d = z.array(
  z.array(z.number()).length(2),
).min(3).max(32);
const parametric_mesh_args = {
  shape: parametric_shape,
  path: z.string().describe("immutable mesh cache key, use a new path when geometry parameters change"),
  size: vector3.optional().describe("full width, height, and depth in meters"),
  opening_size: vector2.optional().describe("wall opening width and height in meters"),
  opening_center: vector2.optional().describe("wall opening center x and y in local meters"),
  openings: z.array(
    z.object({
      size: vector2,
      center: vector2,
    }),
  ).min(1).max(16).optional(),
  radius: z.number().positive().optional(),
  bevel: z.number().positive().optional(),
  segments: z.number().int().min(1).max(64).optional().describe("rounded boxes allow 1 to 16, revolved profiles allow 3 to 64"),
  profile: profile2d.optional().describe("simple counter clockwise x,y points for extrusion and sweeps or radius,y points for revolution"),
  depth: z.number().positive().optional(),
  height: z.number().positive().optional(),
  major_radius: z.number().positive().optional(),
  minor_radius: z.number().positive().optional(),
  minor_segments: z.number().int().min(3).max(48).optional(),
  bevel_segments: z.number().int().min(1).max(16).optional(),
  path_points: z.array(vector3).min(2).max(64).optional(),
  loft_profiles: z.array(profile2d).min(2).max(64).optional(),
  sweep_scales: z.array(
    z.number().positive().max(100),
  ).min(2).max(64).optional(),
  sweep_twists_degrees: z.array(
    z.number().min(-3600).max(3600),
  ).min(2).max(64).optional(),
  thickness: z.number().positive().optional(),
  border: z.number().positive().optional(),
  inset: z.number().positive().optional(),
  scale_start: z.number().positive().optional(),
  scale_end: z.number().positive().optional(),
  grid_points: z.number().int().min(2).max(256).optional(),
  extent: z.number().positive().max(10000).optional(),
  petal_count: z.number().int().min(3).max(64).optional(),
  petal_segments: z.number().int().min(2).max(32).optional(),
  modifier_pivot: vector3.optional(),
  taper_axis: geometry_axis.optional(),
  taper_start: z.number().positive().max(100).optional(),
  taper_end: z.number().positive().max(100).optional(),
  bend_axis: geometry_axis.optional(),
  bend_radial_axis: geometry_axis.optional(),
  bend_degrees: z.number().min(-360).max(360).optional(),
  mirror_axis: geometry_axis.optional(),
  mirror_plane: z.number().optional(),
  shell_thickness: z.number().positive().max(1000).optional(),
  linear_count: z.number().int().min(1).max(128).optional(),
  linear_step: vector3.optional(),
  radial_count: z.number().int().min(1).max(128).optional(),
  radial_axis: geometry_axis.optional(),
  radial_step_degrees: z.number().min(-360).max(360).optional(),
  uv_projection: z.enum([
    "planar",
    "box",
    "cylindrical",
  ]).optional(),
  uv_axis: geometry_axis.optional(),
  uv_scale: vector2.optional(),
  uv_offset: vector2.optional(),
  uv_split_seams: z.boolean().optional(),
  reuse_existing: z.boolean().optional().describe("load the existing path instead of validating new parameters"),
};
const compound_part_args = {
  name: z.string(),
  mesh: z.string().optional().describe("cached mesh name or mesh file path"),
  shape: parametric_shape.optional(),
  mesh_path: z.string().optional(),
  size: vector3.optional(),
  opening_size: vector2.optional(),
  opening_center: vector2.optional(),
  openings: z.array(
    z.object({
      size: vector2,
      center: vector2,
    }),
  ).min(1).max(16).optional(),
  radius: z.number().positive().optional(),
  bevel: z.number().positive().optional(),
  segments: z.number().int().min(1).max(64).optional(),
  profile: profile2d.optional(),
  depth: z.number().positive().optional(),
  height: z.number().positive().optional(),
  major_radius: z.number().positive().optional(),
  minor_radius: z.number().positive().optional(),
  minor_segments: z.number().int().min(3).max(48).optional(),
  bevel_segments: z.number().int().min(1).max(16).optional(),
  path_points: z.array(vector3).min(2).max(64).optional(),
  loft_profiles: z.array(profile2d).min(2).max(64).optional(),
  sweep_scales: z.array(
    z.number().positive().max(100),
  ).min(2).max(64).optional(),
  sweep_twists_degrees: z.array(
    z.number().min(-3600).max(3600),
  ).min(2).max(64).optional(),
  thickness: z.number().positive().optional(),
  border: z.number().positive().optional(),
  inset: z.number().positive().optional(),
  scale_start: z.number().positive().optional(),
  scale_end: z.number().positive().optional(),
  grid_points: z.number().int().min(2).max(256).optional(),
  extent: z.number().positive().max(10000).optional(),
  petal_count: z.number().int().min(3).max(64).optional(),
  petal_segments: z.number().int().min(2).max(32).optional(),
  modifier_pivot: vector3.optional(),
  taper_axis: geometry_axis.optional(),
  taper_start: z.number().positive().max(100).optional(),
  taper_end: z.number().positive().max(100).optional(),
  bend_axis: geometry_axis.optional(),
  bend_radial_axis: geometry_axis.optional(),
  bend_degrees: z.number().min(-360).max(360).optional(),
  mirror_axis: geometry_axis.optional(),
  mirror_plane: z.number().optional(),
  shell_thickness: z.number().positive().max(1000).optional(),
  linear_count: z.number().int().min(1).max(128).optional(),
  linear_step: vector3.optional(),
  radial_count: z.number().int().min(1).max(128).optional(),
  radial_axis: geometry_axis.optional(),
  radial_step_degrees: z.number().min(-360).max(360).optional(),
  uv_projection: z.enum([
    "planar",
    "box",
    "cylindrical",
  ]).optional(),
  uv_axis: geometry_axis.optional(),
  uv_scale: vector2.optional(),
  uv_offset: vector2.optional(),
  uv_split_seams: z.boolean().optional(),
  reuse_existing: z.boolean().optional(),
  position: vector3.optional(),
  rotation_euler: vector3.optional(),
  scale: vector3.optional(),
  material: z.string().optional(),
  with_physics: z.boolean().optional(),
  body_type: z.enum([
    "mesh",
    "mesh_convex",
  ]).optional(),
  physics_static: z.boolean().optional(),
  physics_kinematic: z.boolean().optional(),
  physics_mass: z.number().positive().optional(),
  physics_friction: z.number().min(0).optional(),
  physics_restitution: z.number().min(0).max(1).optional(),
};

function safe_asset_name(value) {
  const safe = String(value ?? "part")
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return safe || "part";
}

function short_hash(value) {
  let hash = 2166136261;
  const text = JSON.stringify(value);
  for (let i = 0; i < text.length; i++)
  {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(16).padStart(8, "0");
}

function is_convex_counter_clockwise(profile) {
  if (!Array.isArray(profile) || profile.length < 3)
  {
    return false;
  }
  let winding = 0;
  for (let index = 0; index < profile.length; index++)
  {
    const first = profile[index];
    const second = profile[
      (index + 1) % profile.length
    ];
    const third = profile[
      (index + 2) % profile.length
    ];
    const cross =
      (second[0] - first[0]) *
        (third[1] - second[1]) -
      (second[1] - first[1]) *
        (third[0] - second[0]);
    if (Math.abs(cross) <= 1e-7)
    {
      continue;
    }
    if (cross < 0)
    {
      return false;
    }
    winding++;
  }
  return winding > 0;
}

function validate_parametric_mesh_args(args) {
  const size = args.size ?? [1, 1, 1];
  if (
    args.shape === "rounded_box" ||
    args.shape === "beveled_box"
  )
  {
    const radius = args.shape === "rounded_box"
      ? args.radius
      : args.bevel;
    const maximum_radius = Math.min(...size) * 0.5;
    if (
      radius !== undefined &&
      radius >= maximum_radius
    )
    {
      throw new Error(
        "radius must be smaller than half the smallest size component",
      );
    }
  }
  if (args.shape === "rounded_cylinder")
  {
    const radius =
      args.radius ?? Math.min(size[0], size[2]) * 0.5;
    const height = args.height ?? size[1];
    const bevel =
      args.bevel ??
      Math.min(radius, height * 0.5) * 0.15;
    const segments = args.segments ?? 24;
    if (
      bevel >= radius ||
      bevel * 2 >= height ||
      segments < 3
    )
    {
      throw new Error(
        "rounded_cylinder requires bevel below radius and half height, with at least 3 segments",
      );
    }
  }
  if (args.shape === "inset_panel")
  {
    const border =
      args.border ?? Math.min(size[0], size[1]) * 0.1;
    const bevel =
      args.bevel ?? Math.min(...size) * 0.08;
    if (
      border * 2 >= Math.min(size[0], size[1]) ||
      bevel >= Math.min(...size) * 0.5
    )
    {
      throw new Error(
        "inset_panel border must fit the face and bevel must be below half the smallest size component",
      );
    }
  }
  if (
    args.shape === "tapered_extrusion" &&
    !is_convex_counter_clockwise(args.profile)
  )
  {
    throw new Error(
      "tapered_extrusion requires a convex counter clockwise profile",
    );
  }
  if (args.shape === "wall_openings")
  {
    if (!args.openings)
    {
      throw new Error(
        "wall_openings requires at least one opening",
      );
    }
    args.opening_count = args.openings.length;
    args.opening_sizes = args.openings.flatMap(
      (opening) => opening.size,
    );
    args.opening_centers = args.openings.flatMap(
      (opening) => opening.center,
    );
  }
  if (args.shape === "loft")
  {
    if (
      !args.path_points ||
      !args.loft_profiles ||
      args.path_points.length !== args.loft_profiles.length
    )
    {
      throw new Error(
        "loft requires one profile for every path point",
      );
    }
    const point_count = args.loft_profiles[0].length;
    if (
      args.loft_profiles.some((profile) =>
        profile.length !== point_count,
      )
    )
    {
      throw new Error(
        "all loft profiles must have the same point count",
      );
    }
    args.loft_profile_points = point_count;
  }
  if (
    args.sweep_scales &&
    args.sweep_scales.length !== args.path_points?.length
  )
  {
    throw new Error(
      "sweep_scales must match path_points",
    );
  }
  if (
    args.sweep_twists_degrees &&
    args.sweep_twists_degrees.length !==
      args.path_points?.length
  )
  {
    throw new Error(
      "sweep_twists_degrees must match path_points",
    );
  }
  if (
    args.shape === "rounded_box" &&
    args.segments !== undefined &&
    args.segments > 16
  )
  {
    throw new Error(
      "rounded_box segments must be between 1 and 16",
    );
  }
  if (
    args.shape === "revolved_profile" &&
    args.segments !== undefined &&
    args.segments < 3
  )
  {
    throw new Error(
      "revolved_profile segments must be between 3 and 64",
    );
  }
  if (
    (
      args.shape === "extruded_profile" ||
      args.shape === "revolved_profile" ||
      args.shape === "curved_profile" ||
      args.shape === "tapered_extrusion"
    ) &&
    !args.profile
  )
  {
    throw new Error(
      `${args.shape} requires a profile`,
    );
  }
  if (
    (
      args.shape === "pipe" ||
      args.shape === "curved_profile" ||
      args.shape === "loft"
    ) &&
    !args.path_points
  )
  {
    throw new Error(
      `${args.shape} requires path_points`,
    );
  }
  if (
    args.shape === "capsule" &&
    args.segments !== undefined &&
    (args.segments < 4 || args.segments > 48)
  )
  {
    throw new Error(
      "capsule segments must be between 4 and 48",
    );
  }
  if (
    (
      args.shape === "pipe" ||
      args.shape === "curved_profile"
    ) &&
    args.segments !== undefined &&
    (args.segments < 3 || args.segments > 32)
  )
  {
    throw new Error(
      "sweep segments must be between 3 and 32",
    );
  }
  if (
    (args.linear_count ?? 1) > 1 &&
    !args.linear_step
  )
  {
    throw new Error(
      "linear_count greater than one requires linear_step",
    );
  }
  if (
    args.bend_degrees !== undefined &&
    args.bend_axis &&
    args.bend_radial_axis &&
    args.bend_axis === args.bend_radial_axis
  )
  {
    throw new Error(
      "bend_axis and bend_radial_axis must differ",
    );
  }
  if (
    args.uv_scale?.some((value) =>
      value === 0,
    )
  )
  {
    throw new Error(
      "uv_scale components cannot be zero",
    );
  }
}

function srgb_to_linear(color) {
  return color.map((value, index) => {
    if (index === 3)
    {
      return value;
    }
    return value <= 0.04045
      ? value / 12.92
      : ((value + 0.055) / 1.055) ** 2.4;
  });
}

const semantic_material = z.enum([
  "painted_wall",
  "paint",
  "wood",
  "black_plastic",
  "fabric",
  "metal",
  "chrome",
  "glass",
  "screen",
  "screen_on",
  "emissive",
  "concrete",
  "asphalt",
  "masonry",
  "rubber",
  "road_paint",
  "painted_metal",
]);

const palette_themes = {
  cozy: {
    painted_wall: [0.58, 0.68, 0.48, 1],
    paint: [0.72, 0.48, 0.3, 1],
    wood: [0.46, 0.24, 0.1, 1],
    fabric: [0.72, 0.56, 0.42, 1],
    metal: [0.58, 0.54, 0.48, 1],
    screen: [0.015, 0.02, 0.028, 1],
    screen_on: [0.16, 0.48, 0.92, 1],
    emissive: [1, 0.72, 0.38, 1],
  },
  neutral: {
    painted_wall: [0.72, 0.72, 0.68, 1],
    paint: [0.5, 0.52, 0.55, 1],
    wood: [0.38, 0.24, 0.14, 1],
    fabric: [0.52, 0.54, 0.58, 1],
    metal: [0.62, 0.64, 0.68, 1],
    screen: [0.012, 0.016, 0.022, 1],
    screen_on: [0.2, 0.55, 0.95, 1],
    emissive: [0.9, 0.92, 1, 1],
  },
  cool: {
    painted_wall: [0.42, 0.58, 0.72, 1],
    paint: [0.24, 0.42, 0.72, 1],
    wood: [0.32, 0.25, 0.2, 1],
    fabric: [0.38, 0.5, 0.68, 1],
    metal: [0.56, 0.64, 0.72, 1],
    screen: [0.008, 0.014, 0.026, 1],
    screen_on: [0.08, 0.62, 1, 1],
    emissive: [0.55, 0.78, 1, 1],
  },
  vibrant: {
    painted_wall: [0.38, 0.7, 0.48, 1],
    paint: [0.9, 0.22, 0.16, 1],
    wood: [0.5, 0.2, 0.06, 1],
    fabric: [0.72, 0.28, 0.68, 1],
    metal: [0.7, 0.72, 0.76, 1],
    screen: [0.01, 0.012, 0.022, 1],
    screen_on: [0.12, 0.8, 1, 1],
    emissive: [1, 0.35, 0.08, 1],
  },
  industrial: {
    painted_wall: [0.48, 0.5, 0.48, 1],
    paint: [0.82, 0.48, 0.08, 1],
    wood: [0.28, 0.18, 0.1, 1],
    fabric: [0.26, 0.3, 0.32, 1],
    metal: [0.48, 0.52, 0.56, 1],
    screen: [0.008, 0.012, 0.016, 1],
    screen_on: [0.12, 0.68, 0.9, 1],
    emissive: [1, 0.58, 0.18, 1],
  },
  retail: {
    painted_wall: [0.78, 0.76, 0.7, 1],
    paint: [0.12, 0.48, 0.72, 1],
    wood: [0.52, 0.32, 0.16, 1],
    fabric: [0.62, 0.56, 0.48, 1],
    metal: [0.66, 0.68, 0.7, 1],
    screen: [0.012, 0.016, 0.022, 1],
    screen_on: [0.16, 0.72, 1, 1],
    emissive: [0.96, 0.78, 0.42, 1],
  },
  aviation: {
    painted_wall: [0.62, 0.66, 0.68, 1],
    paint: [0.1, 0.32, 0.58, 1],
    wood: [0.32, 0.24, 0.18, 1],
    fabric: [0.28, 0.36, 0.44, 1],
    metal: [0.58, 0.64, 0.7, 1],
    screen: [0.006, 0.014, 0.022, 1],
    screen_on: [0.08, 0.7, 1, 1],
    emissive: [0.9, 0.76, 0.34, 1],
  },
};

const semantic_finish_rules = {
  painted_wall: {
    roughness: 0.78,
    metalness: 0,
  },
  paint: {
    roughness: 0.42,
    metalness: 0.05,
    clearcoat: 0.18,
  },
  wood: {
    roughness: 0.62,
    metalness: 0,
  },
  black_plastic: {
    roughness: 0.48,
    metalness: 0,
  },
  fabric: {
    roughness: 0.9,
    metalness: 0,
    sheen: 0.25,
  },
  metal: {
    roughness: 0.34,
    metalness: 0.9,
  },
  chrome: {
    roughness: 0.12,
    metalness: 1,
  },
  glass: {
    roughness: 0.08,
    metalness: 0,
  },
  screen: {
    roughness: 0.2,
    metalness: 0.05,
  },
  screen_on: {
    roughness: 0.16,
    metalness: 0.05,
  },
  emissive: {
    roughness: 0.28,
    metalness: 0,
  },
  concrete: {
    roughness: 0.86,
    metalness: 0,
  },
  asphalt: {
    roughness: 0.92,
    metalness: 0,
  },
  masonry: {
    roughness: 0.82,
    metalness: 0,
  },
  rubber: {
    roughness: 0.76,
    metalness: 0,
  },
  road_paint: {
    roughness: 0.58,
    metalness: 0,
  },
  painted_metal: {
    roughness: 0.38,
    metalness: 0.62,
    clearcoat: 0.12,
  },
};

const server = new McpServer({
  name: "spartan-engine",
  version: "0.1.0",
});

const read_only = {
  readOnlyHint: true,
  destructiveHint: false,
  idempotentHint: true,
  openWorldHint: false,
};

const edit_tool = {
  readOnlyHint: false,
  destructiveHint: false,
  idempotentHint: false,
  openWorldHint: true,
};

const destructive_tool = {
  readOnlyHint: false,
  destructiveHint: true,
  idempotentHint: false,
  openWorldHint: true,
};

register_local_tool("async_task_start", {
  title: "async task start",
  description: "Start a registered Spartan MCP tool in the background and return a task id for polling.",
  inputSchema: {
    tool: z.string(),
    args: z.record(z.string(), z.any()).optional(),
  },
  outputSchema: output_schemas.async_task,
  annotations: destructive_tool,
}, async ({ tool, args = {} }) => {
  if (["async_task_start", "async_task_get", "async_task_list"].includes(tool))
  {
    return tool_result(structured_error("async task tools cannot start themselves", { code: "invalid_arguments" }));
  }

  const target = tool_registry.get(tool);
  if (!target)
  {
    return tool_result(structured_error("unknown tool", { code: "target_resolution_failed" }));
  }

  const parsed = parse_raw_shape(target.inputSchema, args);
  if (!parsed.ok)
  {
    return tool_result(parsed.error);
  }

  const started_at_ms = Date.now();
  const task = {
    id: `task_${next_async_task_id++}`,
    tool,
    status: "queued",
    started_at_ms,
    started_at: new Date(started_at_ms).toISOString(),
    completed_at_ms: null,
    completed_at: null,
    is_error: false,
    result: null,
  };
  async_tasks.set(task.id, task);
  void run_async_task(task, target, parsed.value);

  return tool_result({
    ok: true,
    task: async_task_receipt(task),
  });
});

register_local_tool("async_task_get", {
  title: "async task get",
  description: "Read the current status and result of an async Spartan MCP task.",
  inputSchema: {
    id: z.string(),
  },
  outputSchema: output_schemas.async_task,
  annotations: read_only,
}, async ({ id }) => {
  const task = async_tasks.get(id);
  if (!task)
  {
    return tool_result(structured_error("async task not found", { code: "target_resolution_failed" }));
  }

  return tool_result({
    ok: true,
    task: async_task_receipt(task),
  });
});

register_local_tool("async_task_list", {
  title: "async task list",
  description: "List recent async Spartan MCP tasks.",
  inputSchema: {},
  outputSchema: output_schemas.async_task_list,
  annotations: read_only,
}, async () => {
  return tool_result({
    ok: true,
    tasks: [...async_tasks.values()].map((task) => async_task_receipt(task)),
  });
});

register_local_tool("spartan_status", {
  title: "Spartan Status",
  description: "Return Spartan MCP bridge, engine, and codebase-index status.",
  inputSchema: {},
  outputSchema: output_schemas.spartan_status,
  annotations: read_only,
}, async () => {
  const engine_status = await send_engine_command("engine_status");
  const transport =
    stdio_enabled && http_enabled ? "stdio+http" :
    http_enabled ? "http" :
    "stdio";
  return tool_result({
    ok: true,
    server: "spartan-engine",
    transport,
    http_endpoint: http_enabled ? `http://127.0.0.1:${http_port}/mcp` : null,
    project_root,
    engine_host,
    engine_port,
    read_only_mode,
    engine: engine_status,
    codebase: codebase.status(),
    features: {
      bridge_request_ids: true,
      async_task_reporting: true,
    },
    async_tasks: {
      total: async_tasks.size,
      active: [...async_tasks.values()].filter((task) => task.status === "queued" || task.status === "running").length,
    },
  });
});

register_local_tool("search_codebase", {
  title: "Search Codebase",
  description: "Search Spartan source code using a local keyword index. Use this for source questions instead of filesystem tools.",
  inputSchema: {
    query: z.string(),
    top_k: z.number().int().min(1).max(25).optional(),
  },
  outputSchema: output_schemas.search_codebase,
  annotations: read_only,
}, async ({ query, top_k }) => {
  if (!query?.trim()) {
    return tool_result(structured_error("query is required", { code: "invalid_arguments" }));
  }

  const results = await codebase.search(query, top_k ?? 8);
  return tool_result({
    ok: true,
    ready: codebase.status().ready,
    query,
    results,
  });
});

register_local_tool("read_source_file", {
  title: "Read Source File",
  description: "Read a validated Spartan source file by project-relative path and optional line range.",
  inputSchema: {
    path: z.string(),
    start_line: z.number().int().min(1).optional(),
    line_count: z.number().int().min(1).max(400).optional(),
  },
  outputSchema: output_schemas.read_source_file,
  annotations: read_only,
}, async ({ path, start_line, line_count }) => {
  try {
    const file = await codebase.read_file(path, {
      start_line: start_line ?? 1,
      line_count: line_count ?? 160,
    });
    return tool_result({
      ok: true,
      ...file,
    });
  } catch (error) {
    return tool_result(structured_error(error.message, {
      code: "source_read_failed",
      suggested_action: "use search_codebase first and pass a returned project-relative path",
    }));
  }
});

register_local_tool("search_capabilities", {
  title: "Search Capabilities",
  description: "Search Spartan MCP tools and resources by natural language. Use this before guessing tool names.",
  inputSchema: {
    query: z.string().optional(),
    limit: z.number().int().min(1).max(25).optional(),
  },
  outputSchema: output_schemas.search_capabilities,
  annotations: read_only,
}, async ({ query, limit }) => {
  return tool_result({
    ok: true,
    query: query ?? "",
    matches: search_capability_catalog(tool_registry, resource_registry, query ?? "", limit ?? 8),
  });
});

register_local_tool("get_capability_details", {
  title: "Get Capability Details",
  description: "Return full schema and metadata for one Spartan MCP tool or resource.",
  inputSchema: {
    name: z.string(),
  },
  outputSchema: output_schemas.get_capability_details,
  annotations: read_only,
}, async ({ name }) => {
  const tool = tool_registry.get(name);
  if (tool) {
    return tool_result({
      ok: true,
      name,
      kind: "tool",
      tool: {
        name: tool.name,
        title: tool.title,
        description: tool.description,
        inputSchema: json_schema_from_raw_shape(tool.inputSchema),
        outputSchema: json_schema_from_raw_shape(tool.outputSchema),
        annotations: tool.annotations,
      },
    });
  }

  const resource = resource_registry.get(name) ?? [...resource_registry.values()].find((entry) => entry.uri === name);
  if (resource) {
    return tool_result({
      ok: true,
      name: resource.name,
      kind: "resource",
      resource,
    });
  }

  return tool_result(structured_error(`unknown capability: ${name}`, {
    code: "unknown_capability",
    suggested_action: "call search_capabilities with a natural language query",
  }));
});

register_local_tool("agent_memory_read", {
  title: "Agent Memory Read",
  description: "Read the shared Spartan agent memory markdown file. Use this early for project-specific lessons and maintainer advice.",
  inputSchema: {},
  outputSchema: output_schemas.agent_memory,
  annotations: read_only,
}, async () => {
  return tool_result({
    ok: true,
    path: agent_memory_path,
    memory: await read_agent_memory(),
  });
});

register_local_tool("agent_memory_append", {
  title: "Agent Memory Append",
  description: "Append one durable lesson to a named section in the shared Spartan agent memory file.",
  inputSchema: {
    section: z.string(),
    note: z.string(),
  },
  outputSchema: output_schemas.agent_memory,
  annotations: edit_tool,
}, async ({ section, note }) => {
  try {
    return tool_result({
      ok: true,
      path: agent_memory_path,
      memory: await append_agent_memory(section, note),
    });
  } catch (error) {
    return tool_result(structured_error(error.message, {
      code: "agent_memory_update_failed",
      suggested_action: "read agent_memory_read, prune or correct the memory text, then retry",
    }));
  }
});

register_local_tool("agent_memory_replace", {
  title: "Agent Memory Replace",
  description: "Replace the shared Spartan agent memory file after pruning stale or inaccurate notes.",
  inputSchema: {
    memory: z.string(),
  },
  outputSchema: output_schemas.agent_memory,
  annotations: edit_tool,
}, async ({ memory }) => {
  try {
    return tool_result({
      ok: true,
      path: agent_memory_path,
      memory: await write_agent_memory(memory),
    });
  } catch (error) {
    return tool_result(structured_error(error.message, {
      code: "agent_memory_update_failed",
      suggested_action: "keep the memory concise, preserve the title, and retry",
    }));
  }
});

register_local_tool("debug_log_read", {
  title: "Debug Log Read",
  description: "Read recent Spartan MCP debug trace entries, including assistant prompts and engine command results.",
  inputSchema: {
    limit: z.number().int().min(1).max(500).optional(),
  },
  outputSchema: output_schemas.debug_log,
  annotations: read_only,
}, async ({ limit }) => {
  return tool_result({
    ok: true,
    path: debug_log_path,
    log: await read_debug_log(limit ?? 80),
  });
});

register_tool(server, "ping", "Check that the Spartan live-control endpoint is reachable.", {}, "ping", {
  annotations: read_only,
});

register_tool(server, "engine_status", "Read editor/runtime status, frame metrics, and loading state.", {}, "engine_status", {
  annotations: read_only,
  outputSchema: output_schemas.engine_status,
});

register_tool(
  server,
  "profiler_snapshot",
  "Read profiler frame metrics and CPU/GPU time blocks with per-pass timings for performance analysis and optimization.",
  {
    type: z.enum(["all", "cpu", "gpu"]).optional(),
    sort: z.enum(["duration", "timeline"]).optional(),
    top: z.number().int().min(1).optional(),
  },
  "profiler_snapshot",
  { annotations: read_only, outputSchema: output_schemas.profiler_snapshot },
);

register_tool(
  server,
  "engine_set_mode",
  "Set play, edit, pause, resume, or individual engine flags.",
  {
    mode: z.enum(["edit", "play", "pause", "resume"]).optional(),
    playing: z.boolean().optional(),
    paused: z.boolean().optional(),
    editor_visible: z.boolean().optional(),
  },
  "engine_set_mode",
  { annotations: edit_tool, outputSchema: output_schemas.engine_status },
);

register_tool(
  server,
  "undo_redo",
  "Run editor undo or redo through the command stack.",
  {
    action: z.enum(["undo", "redo"]),
  },
  "undo_redo",
  { annotations: edit_tool, outputSchema: output_schemas.generic },
);

register_tool(server, "cvar_list", "List registered console variables.", {}, "cvar_list", {
  annotations: read_only,
});

register_tool(
  server,
  "cvar_get",
  "Read a console variable by name.",
  {
    name: z.string(),
  },
  "cvar_get",
  { annotations: read_only },
);

register_tool(
  server,
  "cvar_set",
  "Set a guarded console variable by name.",
  {
    name: z.string(),
    value: z.string(),
  },
  "cvar_set",
  { annotations: edit_tool },
);

register_tool(
  server,
  "console_read",
  "Read recent Spartan console/log output, optionally filtered by minimum severity.",
  {
    limit: z.number().int().min(1).max(500).optional(),
    minimum_type: z.enum(["info", "warning", "error"]).optional(),
  },
  "console_read",
  { annotations: read_only, outputSchema: output_schemas.console_read },
);

register_tool(server, "world_summary", "Read the current world name, path, counts, environment, and bounds.", {}, "world_summary", {
  annotations: read_only,
  outputSchema: output_schemas.world_summary,
});

register_tool(server, "context_snapshot", "Read engine status, world summary, and current selection in one native request.", {}, "context_snapshot", {
  annotations: read_only,
  outputSchema: output_schemas.context_snapshot,
});

register_tool(server, "camera_snapshot", "Read the live editor camera position and basis vectors for camera-relative placement.", {}, "camera_snapshot", {
  annotations: read_only,
  outputSchema: output_schemas.camera_snapshot,
});

register_tool(
  server,
  "camera_set_view",
  "Set the editor camera position and rotation, or look at a target point. look_at is an alias for target.",
  {
    position: vector3.optional(),
    rotation_euler: vector3.optional(),
    target: vector3.optional(),
    look_at: vector3.optional(),
  },
  "camera_set_view",
  { annotations: edit_tool, outputSchema: output_schemas.camera_snapshot },
);

register_tool(server, "sequencer_get", "Read the sequencer state: duration, loop, playback time, the sorted list of camera cut events, and the spline_events track (each with start_time, end_time and the follower entity driven along its spline during that window).", {}, "sequencer_get", {
  annotations: read_only,
});

register_tool(
  server,
  "sequencer_set",
  "Set sequencer duration in seconds, loop flag, playhead time, or widget visibility. Only the arguments you pass change.",
  {
    duration: z.number().min(1).max(3600).optional(),
    loop: z.boolean().optional(),
    time: z.number().min(0).optional(),
    visible: z.boolean().optional(),
  },
  "sequencer_set",
  { annotations: edit_tool },
);

register_tool(
  server,
  "sequencer_playback",
  "Control sequencer playback. While playing, the event under the playhead drives the render camera.",
  {
    action: z.enum(["play", "pause", "stop"]),
  },
  "sequencer_playback",
  { annotations: edit_tool },
);

register_tool(
  server,
  "sequencer_event_add",
  "Add a camera cut event at a time in seconds. The camera stays active from its event time until the next event. camera accepts an entity id or an entity name that has a Camera component. target optionally locks the camera onto an entity (id or name) so it pans to keep it in view while the event is active.",
  {
    time: z.number().min(0),
    camera: z.union([z.string(), z.number().int()]),
    target: z.union([z.string(), z.number().int()]).optional(),
  },
  "sequencer_event_add",
  { annotations: edit_tool },
);

register_tool(
  server,
  "sequencer_event_update",
  "Change the time, camera or lock target of an existing event by index. Pass target 'none' to clear the lock. Events are re-sorted by time, so re-read indices from the returned state.",
  {
    index: z.number().int().min(0),
    time: z.number().min(0).optional(),
    camera: z.union([z.string(), z.number().int()]).optional(),
    target: z.union([z.string(), z.number().int()]).optional(),
  },
  "sequencer_event_update",
  { annotations: edit_tool },
);

register_tool(
  server,
  "spline_query",
  [
    "Read everything needed for spline math in one call: arc length in meters, closed loop flag, world space control points, and every spline_follower entity driving along it with speed, follow mode, progress and travel_time_seconds (length / speed).",
    "Omit id to auto-pick the spline that has followers. By default every camera in the world is projected onto the spline; pass closest_to as a comma separated list of entity ids or names to project specific entities instead.",
    "Each closest entry has arc_distance from the start and pass_time_seconds (arc_distance / follower speed), which are the exact sequencer camera cut moments, no sampling needed on your side.",
  ].join(" "),
  {
    id: z.union([z.string(), z.number().int()]).optional(),
    closest_to: z.string().optional(),
  },
  "spline_query",
  { annotations: read_only },
);

register_tool(
  server,
  "spline_distribute",
  [
    "Spread entities evenly along a spline by arc length in one call, no math needed on your side.",
    "Omit id to auto-pick the first spline entity. Omit entities to distribute every camera child of the spline entity; or pass entities as a comma separated list of ids or names.",
    "Entities keep their order along the road and their lateral offset and framing relative to it, so cameras stay aimed sensibly. Returns each entity with its new arc_distance and position.",
    "To move entities off the road use edge_offset: signed meters beyond the road edge (positive = right of travel, negative = left), tracking the road width at each point, so edge_offset 2 always clears the asphalt even when the width varies.",
    "Optional lateral_offset is signed meters from the road centerline instead (ignores road width); optional height places entities that many meters above the road surface.",
  ].join(" "),
  {
    id: z.union([z.string(), z.number().int()]).optional(),
    entities: z.string().optional(),
    edge_offset: z.number().optional(),
    lateral_offset: z.number().optional(),
    height: z.number().optional(),
  },
  "spline_distribute",
  { annotations: edit_tool },
);

register_tool(
  server,
  "world_landmarks",
  [
    "Scan the world for city-scale landmarks in one call: root entities plus any entity tagged landmark.",
    "Returns id, name, position, child_count, optional subtree bounding_box, and filters out ground/camera/sun noise.",
    "Use this before city development prompts such as connecting gas_station, dockyard, and airway with roads.",
  ].join(" "),
  {
    limit: z.number().int().min(1).max(1000).optional(),
    include_tagged: z.boolean().optional(),
  },
  "world_landmarks",
  { annotations: read_only },
);

register_tool(
  server,
  "spline_create_road",
  [
    "Create a driveable spline road in one call from world-space control points.",
    "points is a flat xyz list with at least two points, e.g. x0,y0,z0,x1,y1,z1. Defaults: profile road, conform_to_terrain true, mesh_enabled true.",
    "Prefer this over hand-building spline_point_* children. Returns entity receipt, point_count, and length.",
  ].join(" "),
  {
    points: z.string().describe("flat comma separated world xyz list, at least two points"),
    name: z.string().optional(),
    parent_id: z.string().optional(),
    road_width: z.number().optional(),
    profile: z.enum(["road", "wall", "tube", "fence", "channel"]).optional(),
    conform_to_terrain: z.boolean().optional(),
    closed_loop: z.boolean().optional(),
    sidewalk_enabled: z.boolean().optional(),
    sidewalk_width: z.number().optional(),
    mesh_enabled: z.boolean().optional(),
  },
  "spline_create_road",
  { annotations: edit_tool },
);

register_tool(
  server,
  "spline_set_control_points",
  [
    "Replace or append world-space control points on an existing spline, then regenerate the road mesh when mesh_enabled.",
    "points is a flat xyz list. append false replaces all spline_point_* children; append true adds more points.",
  ].join(" "),
  {
    id: z.union([z.string(), z.number().int()]),
    points: z.string().describe("flat comma separated world xyz list"),
    append: z.boolean().optional(),
    road_width: z.number().optional(),
    profile: z.enum(["road", "wall", "tube", "fence", "channel"]).optional(),
    conform_to_terrain: z.boolean().optional(),
    closed_loop: z.boolean().optional(),
    mesh_enabled: z.boolean().optional(),
  },
  "spline_set_control_points",
  { annotations: edit_tool },
);

register_tool(
  server,
  "spline_reroute",
  [
    "Reroute an existing spline road around buildings, districts, and other roads without deleting its lights/cameras/props.",
    "Captures child lateral offsets, skirts obstacles with clearance, regenerates the mesh, then redistributes cameras/lights/road props along the new path.",
    "Also reclaims stranded road_light_* / road_prop_* furniture left behind by earlier edits and reparents it under the road.",
    "Pass id or name (defaults to spline_road). Optional from/to landmark names snap ends to district edges. Optional via adds midpoints.",
  ].join(" "),
  {
    id: z.union([z.string(), z.number().int()]).optional(),
    name: z.string().optional().describe("spline entity name, e.g. spline_road"),
    from: z.string().optional().describe("start landmark name or id"),
    to: z.string().optional().describe("end landmark name or id"),
    via: z.string().optional().describe("optional flat xyz midpoints"),
    clearance: z.number().optional().describe("meters of padding around obstacles, default 14"),
    keep_children: z.boolean().optional().describe("default true, redistribute lights/cameras/props"),
  },
  "spline_reroute",
  { annotations: edit_tool },
);

register_tool(
  server,
  "spline_connect",
  [
    "Connect ordered landmarks with one spline road. landmarks is a comma separated list of entity names or ids, at least two.",
    "Approaches district edges instead of centers, so roads do not drive through runways, yards, or building footprints.",
    "Skirts large root AABBs by default (avoid_obstacles true, clearance 12, standoff clearance+4). Optional via adds explicit midpoints for arterial routing.",
    "For a network, prefer one arterial with spur branches and junctions, not a complete triangle between every landmark.",
  ].join(" "),
  {
    landmarks: z.string().describe("comma separated entity names or ids, at least two"),
    name: z.string().optional(),
    parent_id: z.string().optional(),
    via: z.string().optional().describe("optional flat xyz midpoints for planned arterial routing"),
    avoid_obstacles: z.boolean().optional().describe("default true, detour around large root entity bounds including destinations"),
    clearance: z.number().optional().describe("meters of padding around obstacles, default 12"),
    standoff: z.number().optional().describe("meters outside landmark bounds for approach points, default clearance+4"),
    road_width: z.number().optional(),
    profile: z.enum(["road", "wall", "tube", "fence", "channel"]).optional(),
    conform_to_terrain: z.boolean().optional(),
    closed_loop: z.boolean().optional(),
    sidewalk_enabled: z.boolean().optional(),
    sidewalk_width: z.number().optional(),
    mesh_enabled: z.boolean().optional(),
  },
  "spline_connect",
  { annotations: edit_tool },
);

register_tool(
  server,
  "spline_junction",
  [
    "Snap two or more existing spline roads so their nearest endpoints share one junction point and regenerate meshes.",
    "roads is a comma separated list of road entity names or ids. Optional point is an explicit world xyz junction; otherwise the nearest endpoints are averaged.",
    "Use after spline_connect legs so a network meets cleanly at shared intersections.",
  ].join(" "),
  {
    roads: z.string().describe("comma separated spline road names or ids, at least two"),
    point: z.string().optional().describe("optional single world xyz junction point"),
  },
  "spline_junction",
  { annotations: edit_tool },
);

register_tool(
  server,
  "spline_decorate",
  [
    "Turn a bare spline road into a readable street: optional sidewalks, street-light poles with calibrated point lights, and roadside barrier props.",
    "Pass the road id. Defaults: sidewalks on, lights on, props on, spacing 28m, replace previous road_light_* / road_prop_* decoration.",
    "Call this after spline_connect / spline_junction. For richer custom decoration, still use entity_create_primitive_batch under a roadside parent.",
  ].join(" "),
  {
    id: z.union([z.string(), z.number().int()]),
    spacing: z.number().optional().describe("meters between light poles, default 28"),
    lights: z.boolean().optional(),
    props: z.boolean().optional(),
    sidewalks: z.boolean().optional(),
    sidewalk_width: z.number().optional(),
    road_width: z.number().optional(),
    replace: z.boolean().optional(),
  },
  "spline_decorate",
  { annotations: edit_tool },
);

register_tool(
  server,
  "district_blockout",
  [
    "Block out one city district in a single call: root landmark parent, greybox massing, and calibrated lights.",
    "Presets: market, downtown (skyscrapers), park, industrial, residential, parking, plaza, gas_station.",
    "position is world xyz. footprint is width,depth meters. density is low, medium, or high. replace clears an existing same-named parent.",
    "Prefer this over hand-placing dozens of cubes. Use city_blockout to lay out many districts at once.",
  ].join(" "),
  {
    preset: z.enum(["market", "downtown", "skyscrapers", "park", "industrial", "residential", "parking", "plaza", "gas_station"]),
    name: z.string().optional(),
    position: z.string().optional().describe("world xyz, e.g. 10,0,-40"),
    footprint: z.string().optional().describe("width,depth meters"),
    rotation_y: z.number().optional(),
    seed: z.number().int().optional(),
    lights: z.boolean().optional(),
    density: z.enum(["low", "medium", "high"]).optional(),
    replace: z.boolean().optional(),
  },
  "district_blockout",
  { annotations: edit_tool },
);

register_tool(
  server,
  "city_blockout",
  [
    "Lay out a multi-district city skeleton as landmark roots ready for a later road pass.",
    "Default mix: downtown, market, park, industrial, residential, parking around center with corridor gaps and avoid_existing landmarks.",
    "districts is an optional comma list of presets or preset:name pairs, e.g. market:market_east,downtown:towers.",
    "Returns district receipts plus road_hints approach points. connect_roads defaults false; set true only if you also want spur roads now.",
  ].join(" "),
  {
    center: z.string().optional().describe("world xyz city center"),
    extent: z.number().optional().describe("half-size of city footprint in meters, default 220"),
    seed: z.number().int().optional(),
    districts: z.string().optional().describe("comma presets or preset:name pairs"),
    names: z.string().optional().describe("optional override names matching districts order"),
    footprints: z.string().optional().describe("flat width,depth pairs"),
    positions: z.string().optional().describe("flat world xyz list, one per district"),
    avoid_existing: z.boolean().optional().describe("default true, do not stamp on existing landmark AABBs"),
    corridor: z.number().optional().describe("meters of gap between districts for future roads, default 28"),
    lights: z.boolean().optional(),
    density: z.enum(["low", "medium", "high"]).optional(),
    replace: z.boolean().optional(),
    connect_roads: z.boolean().optional().describe("default false, roads are a second pass"),
  },
  "city_blockout",
  { annotations: edit_tool },
);

register_tool(
  server,
  "sequencer_event_remove",
  "Remove one camera cut event by index, or pass all=true to clear every event.",
  {
    index: z.number().int().min(0).optional(),
    all: z.boolean().optional(),
  },
  "sequencer_event_remove",
  { annotations: edit_tool },
);

register_tool(
  server,
  "sequencer_spline_add",
  "Add a spline follower event on the second track. During the window from start to end (seconds) the follower entity is driven along its spline by the timeline, so it moves only while the playhead is inside the window and holds at the edges otherwise. follower accepts an entity id or an entity name that has a SplineFollower component.",
  {
    start: z.number().min(0),
    end: z.number().min(0),
    follower: z.union([z.string(), z.number().int()]),
  },
  "sequencer_spline_add",
  { annotations: edit_tool },
);

register_tool(
  server,
  "sequencer_spline_update",
  "Change the start, end or follower of an existing spline event by index. Only the arguments you pass change.",
  {
    index: z.number().int().min(0),
    start: z.number().min(0).optional(),
    end: z.number().min(0).optional(),
    follower: z.union([z.string(), z.number().int()]).optional(),
  },
  "sequencer_spline_update",
  { annotations: edit_tool },
);

register_tool(
  server,
  "sequencer_spline_remove",
  "Remove one spline follower event by index, or pass all=true to clear every spline event.",
  {
    index: z.number().int().min(0).optional(),
    all: z.boolean().optional(),
  },
  "sequencer_spline_remove",
  { annotations: edit_tool },
);

register_local_tool("screenshot_take", {
  title: "screenshot take",
  description: "Request a renderer screenshot and return the target PNG path. If the async save completes quickly, the PNG is also returned as image content.",
  inputSchema: {
    path: z.string().optional().describe("png path inside the engine screenshots directory"),
    wait_ms: z.number().int().min(0).max(10000).optional(),
  },
  outputSchema: output_schemas.screenshot_take,
  annotations: edit_tool,
}, async (args) => {
  let requested_path = args.path?.replaceAll("\\", "/");
  if (
    requested_path &&
    !requested_path.toLowerCase().startsWith("screenshots/")
  )
  {
    requested_path =
      `screenshots/${path.posix.basename(requested_path)}`;
  }
  if (
    requested_path &&
    !requested_path.toLowerCase().endsWith(".png")
  )
  {
    requested_path += ".png";
  }
  let previous_file = null;
  if (requested_path)
  {
    try
    {
      const stat = await fs.stat(requested_path);
      previous_file = {
        modified_ms: stat.mtimeMs,
        size: stat.size,
      };
    }
    catch
    {
    }
  }

  const request_started_ms = Date.now();
  const result = await send_engine_command(
    "screenshot_take",
    { path: requested_path },
  );
  if (!result.ok || !result.path)
  {
    return tool_result(result);
  }

  const wait_ms = args.wait_ms ?? 2500;
  const deadline = Date.now() + wait_ms;
  let image_buffer = null;
  while (Date.now() <= deadline)
  {
    try
    {
      const stat = await fs.stat(result.path);
      const is_fresh = previous_file
        ? (
            stat.mtimeMs > previous_file.modified_ms ||
            stat.size !== previous_file.size
          )
        : stat.mtimeMs >= request_started_ms - 5;
      if (is_fresh)
      {
        image_buffer = await fs.readFile(result.path);
        break;
      }
    }
    catch
    {
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }

  const normalized = normalize_result({
    ...result,
    ready: Boolean(image_buffer),
  });
  const content = [
    {
      type: "text",
      text: JSON.stringify(normalized),
    },
  ];
  if (image_buffer)
  {
    content.push({
      type: "image",
      mimeType: "image/png",
      data: image_buffer.toString("base64"),
    });
  }

  return {
    structuredContent: normalized,
    content,
    isError: false,
  };
});

register_local_tool(
  "scene_visual_review",
  {
    title: "scene visual review",
    description: "Frame a complete entity hierarchy from one or more deliberate perspectives, capture each PNG as MCP image content, then restore the user's camera and selection. Framing fits the hierarchy bounds to the camera FOV.",
    inputSchema: {
      id: z.string().optional(),
      frame: z.boolean().optional(),
      views: z.array(z.enum([
        "perspective",
        "front",
        "back",
        "left",
        "right",
        "top",
      ])).min(1).max(4).optional(),
      padding: z.number().min(1).max(4).optional(),
      include_materials: z.boolean().optional(),
      path: z.string().optional(),
      settle_ms: z.number().int().min(0).max(6000).optional(),
      wait_ms: z.number().int().min(0).max(10000).optional(),
    },
    outputSchema: output_schemas.scene_visual_review,
    annotations: edit_tool,
  },
  async ({
    id,
    frame = true,
    views = [
      "perspective",
      "top",
    ],
    padding = 1.2,
    include_materials = true,
    path,
    settle_ms = 350,
    wait_ms = 5000,
  }) => {
    const context = await send_engine_command(
      "context_snapshot",
    );
    let materials = null;
    if (id && include_materials)
    {
      materials = await send_engine_command(
        "entity_render_materials",
        {
          id,
          include_descendants: true,
        },
      );
    }
    const renderer_debug = await send_engine_command(
      "renderer_debug_get",
    );
    const screenshot_tool = tool_registry.get(
      "screenshot_take",
    );
    const requested_views = frame
      ? views
      : ["current"];
    const review_views = [];
    const image_content = [];
    const original_camera = await send_engine_command(
      "camera_snapshot",
    );
    try
    {
      for (const view of requested_views)
      {
        let camera = await send_engine_command(
          "camera_snapshot",
        );
        if (frame)
        {
          camera = await send_engine_command(
            "viewport_frame",
            {
              id,
              view,
              padding,
            },
          );
        }
        if (!camera.ok)
        {
          return tool_result({
            ...camera,
            context,
            view,
          });
        }
        if (settle_ms > 0)
        {
          await new Promise(
            (resolve) => setTimeout(resolve, settle_ms),
          );
        }

        let review_path = path;
        if (path && requested_views.length > 1)
        {
          review_path = path.toLowerCase().endsWith(".png")
            ? `${path.slice(0, -4)}_${view}.png`
            : `${path}_${view}.png`;
        }
        const screenshot_result =
          await screenshot_tool.handler({
            path: review_path,
            wait_ms,
          });
        const screenshot =
          screenshot_result.structuredContent ??
          {
            ok: false,
            error: "missing screenshot result",
          };
        review_views.push({
          view,
          camera,
          screenshot,
        });
        image_content.push(
          ...(screenshot_result.content ?? []).filter(
            (entry) => entry.type === "image",
          ),
        );
      }
      const first_view = review_views[0] ?? {};
      const evidence_ok =
        Boolean(context.ok) &&
        (!materials || Boolean(materials.ok)) &&
        Boolean(renderer_debug.ok) &&
        review_views.length === requested_views.length &&
        review_views.every((review) =>
          Boolean(review.camera?.ok) &&
          Boolean(review.screenshot?.ok) &&
          Boolean(review.screenshot?.ready),
        );
      const structured = normalize_result({
        ok: evidence_ok,
        error: evidence_ok
          ? undefined
          : "visual review evidence is incomplete",
        context,
        camera: first_view.camera,
        materials,
        renderer_debug,
        screenshot: first_view.screenshot,
        views: review_views,
      });
      const content = [
        {
          type: "text",
          text: JSON.stringify(structured),
        },
        ...image_content,
      ];

      return {
        structuredContent: structured,
        content,
        isError: !structured.ok,
      };
    }
    finally
    {
      if (original_camera.ok)
      {
        const restore_target = original_camera.position.map(
          (value, index) =>
            value + original_camera.forward[index],
        );
        await send_engine_command(
          "camera_set_view",
          {
            position: original_camera.position,
            target: restore_target,
          },
        );
      }
      const selected_ids =
        context.selection?.selected_ids ?? [];
      await send_engine_command(
        "selection_update",
        { action: "clear" },
      );
      for (const selected_id of selected_ids)
      {
        await send_engine_command(
          "selection_update",
          {
            action: "add",
            id: selected_id,
          },
        );
      }
    }
  },
);

register_local_tool(
  "design_brief_create",
  {
    title: "design brief create",
    description: "Create a structured art-direction brief with metric rules, focal hierarchy, PBR palette guidance, lighting intent, and a detail budget before scene planning.",
    inputSchema: {
      prompt: z.string().min(1),
      template_name:
        z.enum(design_template_names).optional(),
      overrides: z.record(
        z.string(),
        z.any(),
      ).optional(),
    },
    outputSchema: output_schemas.generic,
    annotations: read_only,
  },
  async ({
    prompt,
    template_name,
    overrides = {},
  }) => {
    const brief = create_design_brief(
      prompt,
      {
        ...overrides,
        template_name,
      },
    );
    return tool_result({
      ok: brief.validation.valid,
      brief,
      templates: design_template_names,
      semantic_roles: semantic_role_catalog,
    });
  },
);

register_local_tool(
  "scene_plan_suggest",
  {
    title: "scene plan suggest",
    description: "Suggest a complete metric semantic scene plan from a structured design brief template. Review and pass the returned plan to scene_plan_create.",
    inputSchema: {
      prompt: z.string().min(1),
      template_name:
        z.enum(design_template_names).optional(),
      root_name: z.string().min(1).optional(),
      overrides: z.record(
        z.string(),
        z.any(),
      ).optional(),
    },
    outputSchema: output_schemas.generic,
    annotations: read_only,
  },
  async ({
    prompt,
    template_name,
    root_name,
    overrides = {},
  }) => {
    const brief = create_design_brief(
      prompt,
      {
        ...overrides,
        template_name,
        root_name,
      },
    );
    const plan = suggest_scene_plan(brief);
    return tool_result({
      ok: plan.ok !== false,
      brief,
      plan,
    });
  },
);

register_local_tool(
  "scene_benchmark_list",
  {
    title: "scene benchmark list",
    description: "List fixed scene-generation benchmark prompts, categories, camera views, and pass thresholds.",
    inputSchema: {
      category: z.string().optional(),
      include_prompts: z.boolean().optional(),
    },
    outputSchema: output_schemas.generic,
    annotations: read_only,
  },
  async (args) => tool_result(
    list_benchmarks(args),
  ),
);

register_local_tool(
  "scene_benchmark_get",
  {
    title: "scene benchmark get",
    description: "Read one benchmark prompt with fixed views, expected features, human rubric, and score weights.",
    inputSchema: {
      benchmark_id: z.string().min(1),
    },
    outputSchema: output_schemas.generic,
    annotations: read_only,
  },
  async ({ benchmark_id }) => tool_result(
    get_benchmark(benchmark_id),
  ),
);

register_local_tool(
  "scene_benchmark_capture",
  {
    title: "scene benchmark capture",
    description: "Capture the fixed camera views for one benchmark scene root and return PNG evidence ready for visual and human scoring.",
    inputSchema: {
      benchmark_id: z.string().min(1),
      id: z.string(),
      padding: z.number().min(1).max(4).optional(),
    },
    outputSchema: output_schemas.scene_visual_review,
    annotations: edit_tool,
  },
  async ({
    benchmark_id,
    id,
    padding = 1.2,
  }) => {
    const benchmark = get_benchmark(benchmark_id);
    if (!benchmark.ok)
    {
      return tool_result(benchmark);
    }
    const views = [
      ...new Set(
        benchmark.camera_views.map(
          (entry) => entry.view,
        ),
      ),
    ].slice(0, 4);
    const visual_tool = tool_registry.get(
      "scene_visual_review",
    );
    const review = await visual_tool.handler({
      id,
      views,
      padding,
      include_materials: true,
      path: `benchmarks/${safe_asset_name(
        benchmark_id,
      )}.png`,
    });
    return {
      ...review,
      structuredContent: {
        ...review.structuredContent,
        benchmark_id,
        expected_features:
          benchmark.benchmark.expected_features,
        human_rubric: benchmark.human_rubric,
      },
    };
  },
);

register_local_tool(
  "scene_benchmark_score",
  {
    title: "scene benchmark score",
    description: "Validate and score one benchmark result from plan fidelity, duplicates, material coverage, audits, corrections, visual metrics, and human ratings.",
    inputSchema: {
      result: z.record(z.string(), z.any()),
      baseline: z.record(
        z.string(),
        z.any(),
      ).optional(),
    },
    outputSchema: output_schemas.generic,
    annotations: read_only,
  },
  async ({ result, baseline }) => {
    const score = calculate_benchmark_metrics(result);
    const comparison = baseline
      ? compare_benchmark_results(
        result,
        baseline,
      )
      : undefined;
    return tool_result({
      ...score,
      comparison,
    });
  },
);

register_local_tool(
  "scene_benchmark_baseline_save",
  {
    title: "scene benchmark baseline save",
    description: "Persist a validated benchmark result beside the current world as the comparison baseline.",
    inputSchema: {
      result: z.record(z.string(), z.any()),
    },
    outputSchema: output_schemas.generic,
    annotations: edit_tool,
  },
  async ({ result }) => {
    const score = calculate_benchmark_metrics(result);
    if (!score.ok)
    {
      return tool_result(score);
    }
    const file_path = await benchmark_baseline_path(
      score.benchmark_id,
    );
    await fs.mkdir(
      path.dirname(file_path),
      { recursive: true },
    );
    const temporary_path =
      `${file_path}.${process.pid}.tmp`;
    await fs.writeFile(
      temporary_path,
      `${JSON.stringify(
        {
          version: 1,
          saved_at: new Date().toISOString(),
          result,
          score,
        },
        null,
        2,
      )}\n`,
      "utf8",
    );
    await fs.rename(temporary_path, file_path);
    return tool_result({
      ok: true,
      path: file_path,
      benchmark_id: score.benchmark_id,
      score,
    });
  },
);

register_local_tool(
  "scene_benchmark_baseline_get",
  {
    title: "scene benchmark baseline get",
    description: "Read the persisted comparison baseline for one benchmark in the current world.",
    inputSchema: {
      benchmark_id: z.string().min(1),
    },
    outputSchema: output_schemas.generic,
    annotations: read_only,
  },
  async ({ benchmark_id }) => {
    const benchmark = get_benchmark(benchmark_id);
    if (!benchmark.ok)
    {
      return tool_result(benchmark);
    }
    const file_path = await benchmark_baseline_path(
      benchmark_id,
    );
    try
    {
      return tool_result({
        ok: true,
        path: file_path,
        baseline: JSON.parse(
          await fs.readFile(file_path, "utf8"),
        ),
      });
    }
    catch
    {
      return tool_result({
        ok: false,
        error: `benchmark baseline not found for ${benchmark_id}`,
        path: file_path,
      });
    }
  },
);

register_local_tool(
  "scene_plan_create",
  {
    title: "scene plan create",
    description: "Create and validate a generic semantic environment plan before geometry is built. Plans define scale reference, purposeful zones, element roles and expected dimensions, support modes, spatial relationships, and lighting intent without baking in a specific environment type.",
    inputSchema: scene_plan_input,
    outputSchema: output_schemas.scene_plan,
    annotations: edit_tool,
  },
  async (args) => {
    const result = await create_scene_plan(
      args,
      await current_scene_plan_namespace(),
    );
    return tool_result(result);
  },
);

register_local_tool(
  "scene_plan_get",
  {
    title: "scene plan get",
    description: "Read the latest validated generic scene plan for a root name.",
    inputSchema: {
      root_name: z.string().min(1),
    },
    outputSchema: output_schemas.scene_plan,
    annotations: read_only,
  },
  async ({ root_name }) => {
    const result = await read_scene_plan(
      await current_scene_plan_namespace(),
      root_name,
    );
    return tool_result(result);
  },
);

register_local_tool(
  "scene_layout_audit",
  {
    title: "scene layout audit",
    description: "Audit a built environment against its generic semantic plan. Checks expected real-world scale, zone containment, ground or surface support, declared spatial relationships, active calibrated lights, shadowing, and light coverage.",
    inputSchema: {
      id: z.string(),
      root_name: z.string().min(1),
    },
    outputSchema: output_schemas.scene_layout_audit,
    annotations: read_only,
  },
  async (args) => {
    const result = await audit_scene_layout(
      send_engine_command,
      {
        ...args,
        namespace:
          await current_scene_plan_namespace(),
      },
    );
    return tool_result(result);
  },
);

register_local_tool(
  "scene_quality_audit",
  {
    title: "scene quality audit",
    description: "Deterministically audit a completed scene hierarchy for requested features, materials, geometry, collision coverage, detail density, and lighting. A false pass means the scene needs another correction pass.",
    inputSchema: {
      id: z.string(),
      required_features: z.array(z.string()).max(20).optional(),
      min_entities: z.number().int().min(1).max(5000).optional(),
      max_default_material_ratio: z.number().min(0).max(1).optional(),
      min_unique_materials: z.number().int().min(1).max(100).optional(),
      min_advanced_mesh_ratio: z.number().min(0).max(1).optional(),
      require_light: z.boolean().optional(),
      scene_type: z.enum([
        "generic",
        "room",
        "storefront",
        "gas_station",
        "airport",
        "warehouse",
        "road",
      ]).optional(),
      planned_element_count:
        z.number().int().min(1).max(1000).optional(),
      required_roles: z.array(
        z.string().min(1),
      ).max(32).optional(),
      max_duplicate_geometry:
        z.number().int().min(0).max(1000).optional(),
      min_collision_ratio: z.number().min(0).max(1).optional(),
    },
    outputSchema: output_schemas.scene_quality_audit,
    annotations: read_only,
  },
  async (args) => {
    const result = await audit_scene_quality(
      send_engine_command,
      args,
    );
    return tool_result(result);
  },
);

register_tool(
  server,
  "world_load",
  "Load an absolute .world file path.",
  {
    path: z.string(),
  },
  "world_load",
  { annotations: destructive_tool },
);

register_tool(
  server,
  "world_save",
  "Save the current world and automatically prune files in its managed resources directory that are not referenced by live world components.",
  {
    path: z.string().optional(),
  },
  "world_save",
  { annotations: edit_tool, outputSchema: output_schemas.generic },
);

register_tool(
  server,
  "world_resources_clean",
  "Save the current world, remove unreferenced files from its managed resources directory, include removals from the immediately preceding automatic save, and report any files that could not be deleted.",
  {},
  "world_resources_clean",
  {
    annotations: destructive_tool,
    outputSchema: output_schemas.generic,
  },
);

register_tool(
  server,
  "world_set_environment",
  "Set world time of day, wind, or description in edit mode.",
  {
    time_of_day: z.number().min(0).max(1).optional(),
    wind: vector3.optional(),
    description: z.string().optional(),
  },
  "world_set_environment",
  { annotations: edit_tool, outputSchema: output_schemas.world_summary },
);

register_tool(
  server,
  "world_raycast",
  "Cast a ray against static world physics and return the hit position, normal, distance, and entity.",
  {
    origin: vector3,
    direction: vector3,
    max_distance: z.number().positive().optional(),
  },
  "world_raycast",
  { annotations: read_only, outputSchema: output_schemas.world_raycast },
);

register_tool(
  server,
  "entity_snap",
  "Snap an entity hierarchy against static floor, ceiling, wall, or arbitrary surface geometry using its rendered bounds. Surface and wall modes can align orientation to the hit normal.",
  {
    id: z.string(),
    mode: z.enum([
      "floor",
      "ceiling",
      "wall",
      "surface",
    ]),
    target: vector3.optional(),
    origin: vector3.optional(),
    direction: vector3.optional(),
    offset: z.number().optional(),
    align_to_surface: z.boolean().optional(),
    max_distance: z.number().positive().max(10000).optional(),
  },
  "entity_snap",
  {
    annotations: edit_tool,
    outputSchema: output_schemas.entity_snap,
  },
);

register_tool(
  server,
  "entity_spatial_snapshot",
  "Return generic spatial evidence for an entity hierarchy: world render bounds, subtree bounds, components, tags, and downward support gaps. Use this to validate scale, grounding, relationships, and layout without domain-specific assumptions.",
  {
    id: z.string(),
    include_descendants: z.boolean().optional(),
    limit: z.number().int().min(1).max(5000).optional(),
    offset: z.number().int().min(0).max(100000).optional(),
  },
  "entity_spatial_snapshot",
  {
    annotations: read_only,
    outputSchema: output_schemas.entity_spatial_snapshot,
  },
);

register_tool(
  server,
  "entity_list",
  "List entities (id, name, parent_id, component names) without transforms. Paginated; prefer entity_find to resolve a named entity.",
  {
    limit: z.number().int().min(1).max(1000).optional(),
    offset: z.number().int().min(0).optional(),
  },
  "entity_list",
  { annotations: read_only, outputSchema: output_schemas.entity_list },
);

register_tool(
  server,
  "entity_find",
  "Find entities by name and/or tag without listing the whole scene. Tags mark roles, e.g. car parts carry wheel, wheel_front, wheel_left tags.",
  {
    name: z.string().optional(),
    tag: z.string().optional(),
    match: z.enum(["exact", "contains"]).optional(),
    limit: z.number().int().min(1).max(100).optional(),
  },
  "entity_find",
  { annotations: read_only, outputSchema: output_schemas.entity_find },
);

register_tool(
  server,
  "entity_find_by_component",
  "Find entities that have a specific component type.",
  {
    type: z.string().optional(),
    component_type: z.string().optional(),
    limit: z.number().int().min(1).max(1000).optional(),
    offset: z.number().int().min(0).optional(),
  },
  "entity_find_by_component",
  {
    annotations: read_only,
    outputSchema: output_schemas.entity_list,
    map_args: (args) => {
      const raw = args.type ?? args.component_type;
      if (raw === undefined) {
        throw new Error("missing type");
      }
      const type = String(raw).toLowerCase();
      if (!component_type.options.includes(type)) {
        throw new Error(`unknown component type '${raw}', expected one of ${component_type.options.join(", ")}`);
      }
      return { type, limit: args.limit, offset: args.offset };
    },
  },
);

register_tool(
  server,
  "entity_resolve",
  "Resolve one entity by id, name, or current selection and return a compact entity receipt.",
  {
    id: z.string().optional(),
    name: z.string().optional(),
    selected: z.boolean().optional(),
  },
  "entity_resolve",
  { annotations: read_only, outputSchema: output_schemas.entity },
);

register_tool(
  server,
  "entity_get",
  "Read a single entity by id or name.",
  {
    id: z.string().optional(),
    entity: z.string().optional(),
    name: z.string().optional(),
  },
  "entity_get",
  {
    annotations: read_only,
    outputSchema: output_schemas.entity,
    map_args: (args) => {
      const id = args.id ?? args.entity ?? args.name;
      if (id === undefined) {
        throw new Error("missing id");
      }
      return { id: String(id) };
    },
  },
);

register_tool(server, "selection_get", "Read the selected entity ids.", {}, "selection_get", {
  annotations: read_only,
  outputSchema: output_schemas.selection_get,
});

register_tool(
  server,
  "selection_update",
  "Update editor selection: clear, set/add/remove/toggle one entity, or set selection by component type.",
  {
    action: z.enum(["clear", "set", "add", "remove", "toggle", "set_by_component"]),
    id: z.string().optional(),
    type: component_type.optional(),
  },
  "selection_update",
  { annotations: edit_tool, outputSchema: output_schemas.selection_get },
);

register_tool(server, "component_types", "List valid Spartan component type names.", {}, "component_types", {
  annotations: read_only,
  outputSchema: output_schemas.component_types,
});

register_tool(server, "primitive_types", "List valid Spartan built-in primitive mesh names and aliases.", {}, "primitive_types", {
  annotations: read_only,
  outputSchema: output_schemas.primitive_types,
});

register_tool(
  server,
  "entity_create_empty",
  "Create an empty entity in edit mode.",
  {
    name: z.string().optional(),
    parent_id: z.string().optional(),
    ...entity_identity_args,
  },
  "entity_create_empty",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_local_tool(
  "entity_create_light",
  {
    title: "entity create light",
    description: [
      "Create a fully initialized light in one call. Always prefer this over entity_create_empty + entity_add_component light + component_set.",
      "intensity is lux for directional and lumens for point/spot/area. Visible blockout defaults: point/spot 8500, area 12000, directional 120000.",
      "Values below the per-type floor are replaced with calibrated defaults unless calibrated is false.",
      "Also initializes color, temperature, range, angle_degrees, area size, shadows, and draw/shadow distances for the light type.",
    ].join(" "),
    inputSchema: light_create_args,
    outputSchema: output_schemas.entity,
    annotations: edit_tool,
  },
  async (args) => tool_result(
    await send_engine_command("entity_create_light", args),
  ),
);

register_local_tool(
  "entity_create_light_batch",
  {
    title: "entity create light batch",
    description: "Create up to 64 calibrated lights in one native engine request. Returns completed lights and a failed index if an item fails.",
    inputSchema: {
      items: z.array(
        z.object(light_create_args),
      ).min(1).max(64),
    },
    outputSchema: output_schemas.batch_receipt,
    annotations: edit_tool,
  },
  async ({ items }) => {
    const args = {
      count: items.length,
    };
    const keys = Object.keys(light_create_args);
    for (let index = 0; index < items.length; index++)
    {
      for (const key of keys)
      {
        if (
          items[index][key] !== undefined &&
          items[index][key] !== null
        )
        {
          args[`item_${index}_${key}`] =
            items[index][key];
        }
      }
    }
    return tool_result(
      await send_engine_command(
        "entity_create_light_batch",
        args,
      ),
    );
  },
);

register_local_tool(
  "lights_calibrate",
  {
    title: "lights calibrate",
    description: [
      "Calibrate every light in the scene, or lights under an optional parent, using photometric defaults based on light type and entity name role.",
      "Sets color, temperature, intensity, range, and type-specific fields. Specialty car lights stay intentionally dim: brake ~180 lm, exhaust ~90 lm, headlights ~3200 lm.",
      "Blockout/yard/warehouse/canopy lights use visible lumen defaults. Prefer this over execute_lua or many component_set calls.",
    ].join(" "),
    inputSchema: {
      parent_id: z.string().optional().describe("optional parent entity id; when set, only lights under that hierarchy are calibrated"),
      limit: z.number().int().min(1).max(1000).optional(),
    },
    outputSchema: output_schemas.batch_receipt,
    annotations: edit_tool,
  },
  async (args) => {
    const limit = args.limit ?? 1000;
    const found = await send_engine_command("entity_find_by_component", { type: "light", limit });
    if (!found.ok)
    {
      return tool_result(found);
    }

    const entities = Array.isArray(found.entities) ? found.entities : [];
    const role_counts = {};
    const updated = [];
    let skipped = 0;

    for (const entity of entities)
    {
      const id = entity?.id;
      if (!id)
      {
        skipped++;
        continue;
      }

      if (args.parent_id)
      {
        let current = entity;
        let under_parent = current.id === args.parent_id;
        let guard = 0;
        while (!under_parent && current?.parent_id && guard < 32)
        {
          if (current.parent_id === args.parent_id)
          {
            under_parent = true;
            break;
          }
          const parent = await send_engine_command("entity_get", { id: current.parent_id });
          if (!parent.ok || !parent.entity)
          {
            break;
          }
          current = parent.entity;
          under_parent = current.id === args.parent_id;
          guard++;
        }
        if (!under_parent)
        {
          skipped++;
          continue;
        }
      }

      const component = await send_engine_command("component_get", { id, type: "light" });
      if (!component.ok)
      {
        skipped++;
        continue;
      }

      const properties = component.component?.properties ?? component.properties ?? {};
      const plan = calibrate_existing_light(entity.name, properties);
      const items = Object.entries(plan.updates).map(([property, value]) => ({ property, value }));
      if (items.length === 0)
      {
        skipped++;
        continue;
      }

      const mapped = {
        id,
        type: "light",
        count: items.length,
      };
      for (let i = 0; i < items.length; i++)
      {
        mapped[`property_${i}`] = items[i].property;
        mapped[`value_${i}`] = items[i].value;
      }

      const result = await send_engine_command("component_set_batch", mapped);
      if (!result.ok)
      {
        return tool_result({
          ...result,
          updated_count: updated.length,
          skipped_count: skipped,
          role_counts,
          failed_id: id,
        });
      }

      role_counts[plan.role] = (role_counts[plan.role] ?? 0) + 1;
      updated.push({ id, name: entity.name, role: plan.role, light_type: plan.light_type, intensity: plan.updates.intensity, range: plan.updates.range });
    }

    return tool_result({
      ok: true,
      updated_count: updated.length,
      skipped_count: skipped,
      role_counts,
      updated: updated.slice(0, 32),
    });
  },
);

register_tool(
  server,
  "entity_create_primitive",
  "Create a primitive render entity in edit mode with static matching collision by default, plus optional transform, parent, material, and physics overrides.",
  primitive_create_args,
  "entity_create_primitive",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_local_tool(
  "mesh_geometry_capabilities",
  {
    title: "mesh geometry capabilities",
    description: "Report procedural geometry, modifier, UV, opening, material, collision, and boolean capabilities with honest availability status.",
    inputSchema: {},
    outputSchema: output_schemas.generic,
    annotations: read_only,
  },
  async () => tool_result({
    ok: true,
    generators: parametric_shape.options,
    modifiers: [
      "taper",
      "bend",
      "mirror",
      "shell",
      "linear_array",
      "radial_array",
    ],
    uv_projection: [
      "planar",
      "box",
      "cylindrical",
    ],
    uv_seam_splitting: [
      "box",
    ],
    openings: {
      available: true,
      generators: [
        "wall_opening",
        "wall_openings",
      ],
      watertight_parts: true,
    },
    profiles: {
      concave_extrusion: true,
      arbitrary_holes: false,
      variable_loft: true,
      variable_sweep_scale: true,
      variable_sweep_twist: true,
    },
    material_assignment: {
      per_compound_part: true,
      generated_face_slots: false,
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
  }),
);

register_local_tool(
  "mesh_generate",
  {
    title: "mesh generate",
    description: "Generate, modify, save, and cache one bounded procedural mesh. Supports multiple architectural openings, concave profiles, variable lofts and sweeps, shell, arrays, deformations, and UV projection.",
    inputSchema: parametric_mesh_args,
    annotations: edit_tool,
    outputSchema: output_schemas.parametric_mesh,
  },
  async (args) => {
    try
    {
      validate_parametric_mesh_args(args);
    }
    catch (error)
    {
      return tool_result(
        structured_error(
          error.message,
          { code: "invalid_arguments" },
        ),
      );
    }

    return tool_result(
      await send_engine_command("mesh_generate", args),
    );
  },
);

register_local_tool(
  "mesh_generate_batch",
  {
    title: "mesh generate batch",
    description: "Generate, save, and cache up to 32 parametric meshes in one native engine request.",
    inputSchema: {
      items: z.array(
        z.object(parametric_mesh_args),
      ).min(1).max(32),
    },
    outputSchema: output_schemas.parametric_mesh_batch,
    annotations: edit_tool,
  },
  async ({ items }) => {
    try
    {
      for (const item of items)
      {
        validate_parametric_mesh_args(item);
      }
    }
    catch (error)
    {
      return tool_result(
        structured_error(
          error.message,
          { code: "invalid_arguments" },
        ),
      );
    }

    const args = { count: items.length };
    const keys = [
      ...Object.keys(parametric_mesh_args),
      "opening_count",
      "opening_sizes",
      "opening_centers",
      "loft_profile_points",
    ];
    for (let i = 0; i < items.length; i++)
    {
      for (const key of keys)
      {
        if (items[i][key] !== undefined && items[i][key] !== null)
        {
          args[`item_${i}_${key}`] = items[i][key];
        }
      }
    }

    const result = await send_engine_command(
      "mesh_generate_batch",
      args,
    );
    return tool_result(result);
  },
);

register_tool(
  server,
  "render_set_mesh",
  "Assign a cached or file-backed mesh to an entity render component. The render component is created when absent. Supports generated meshes and imported meshes with a sub-mesh index.",
  {
    id: z.string(),
    mesh: z.string(),
    sub_mesh_index: z.number().int().min(0).optional(),
    material: z.string().optional(),
  },
  "render_set_mesh",
  {
    annotations: edit_tool,
    outputSchema: output_schemas.render_set_mesh,
  },
);

async function bind_mesh_physics(args) {
    const render = await send_engine_command(
      "render_set_mesh",
      {
        id: args.id,
        mesh: args.mesh,
        material: args.material,
      },
    );
    if (!render.ok)
    {
      return render;
    }
    const has_physics =
      render.entity?.components?.includes("physics") ??
      false;
    if (!has_physics)
    {
      const added = await send_engine_command(
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
    const properties = [
      ["body_type", args.body_type ?? "mesh_convex"],
      ["static", args.static ?? true],
      ["kinematic", args.kinematic ?? false],
      ["mass", args.mass ?? 1],
      ["friction", args.friction ?? 0.5],
      ["restitution", args.restitution ?? 0],
    ];
    const physics_args = {
      id: args.id,
      type: "physics",
      count: properties.length,
    };
    for (
      let index = 0;
      index < properties.length;
      index++
    )
    {
      physics_args[`property_${index}`] =
        properties[index][0];
      physics_args[`value_${index}`] =
        properties[index][1];
    }
    const physics = await send_engine_command(
      "component_set_batch",
      physics_args,
    );
    return {
      ...physics,
      render,
    };
}

register_local_tool(
  "mesh_physics_bind",
  {
    title: "mesh physics bind",
    description: "Assign a generated or imported mesh and bind static convex collision by default in one edit operation.",
    inputSchema: {
      id: z.string(),
      mesh: z.string(),
      material: z.string().optional(),
      body_type: z.enum([
        "mesh",
        "mesh_convex",
      ]).optional(),
      static: z.boolean().optional(),
      kinematic: z.boolean().optional(),
      mass: z.number().positive().optional(),
      friction: z.number().min(0).optional(),
      restitution: z.number().min(0).max(1).optional(),
    },
    annotations: edit_tool,
    outputSchema: output_schemas.generic,
  },
  async (args) => tool_result(
    await bind_mesh_physics(args),
  ),
);

register_local_tool(
  "entity_create_primitive_batch",
  {
    title: "entity create primitive batch",
    description: "Create many primitive render entities with static matching collision by default. Preferred for blockouts, rooms, props, and repeated geometry; keep count under 64.",
    inputSchema: {
      items: z.array(z.object(primitive_create_args)).min(1).max(64),
    },
    outputSchema: output_schemas.batch_receipt,
    annotations: edit_tool,
  },
  async ({ items }) => {
    const args = { count: items.length };
    const keys = Object.keys(primitive_create_args);
    for (let i = 0; i < items.length; i++) {
      for (const key of keys) {
        if (items[i][key] !== undefined && items[i][key] !== null) {
          args[`item_${i}_${key}`] = items[i][key];
        }
      }
    }

    const result = await send_engine_command("entity_create_primitive_batch", args);
    return tool_result({
      ...result,
      created_count: result.created_count ?? result.created?.length ?? 0,
    });
  },
);

register_local_tool(
  "compound_create",
  {
    title: "compound create",
    description: "Create an editable compound object from parametric or cached mesh parts with per-part materials, transforms, and static convex collision by default. Optionally save the hierarchy as a prefab.",
    inputSchema: {
      name: z.string(),
      parent_id: z.string().optional(),
      position: vector3.optional(),
      rotation_euler: vector3.optional(),
      scale: vector3.optional(),
      asset_directory: z.string().optional().describe("generated mesh directory, always constrained to the active world resource directory"),
      prefab_path: z.string().optional(),
      parts: z.array(
        z.object(compound_part_args),
      ).min(1).max(64),
    },
    outputSchema: output_schemas.compound_create,
    annotations: edit_tool,
  },
  async ({
    name,
    parent_id,
    position,
    rotation_euler,
    scale,
    asset_directory = "project/world_resources",
    prefab_path,
    parts,
  }) => {
    const root_result = await send_engine_command(
      "entity_create_empty",
      { name, parent_id },
    );
    if (!root_result.ok)
    {
      return tool_result(root_result);
    }

    const root = root_result.entity;
    if (position || rotation_euler || scale)
    {
      const transform_result = await send_engine_command(
        "entity_set_transform",
        {
          id: root.id,
          position,
          rotation_euler,
          scale,
        },
      );
      if (!transform_result.ok)
      {
        return tool_result({
          ...transform_result,
          root,
          completed_parts: [],
        });
      }
    }

    const completed_parts = [];
    const directory = asset_directory.replace(/[\\/]+$/g, "");
    for (let i = 0; i < parts.length; i++)
    {
      const part = parts[i];
      if (Boolean(part.mesh) === Boolean(part.shape))
      {
        return tool_result({
          ok: false,
          error: "each compound part requires exactly one of mesh or shape",
          failed_index: i,
          root,
          completed_parts,
        });
      }

      let mesh = part.mesh;
      let generated = null;
      if (part.shape)
      {
        const signature = {
          shape: part.shape,
          size: part.size,
          opening_size: part.opening_size,
          opening_center: part.opening_center,
          openings: part.openings,
          opening_count: part.opening_count,
          opening_sizes: part.opening_sizes,
          opening_centers: part.opening_centers,
          radius: part.radius,
          bevel: part.bevel,
          segments: part.segments,
          profile: part.profile,
          depth: part.depth,
          height: part.height,
          major_radius: part.major_radius,
          minor_radius: part.minor_radius,
          minor_segments: part.minor_segments,
          bevel_segments: part.bevel_segments,
          path_points: part.path_points,
          loft_profiles: part.loft_profiles,
          loft_profile_points: part.loft_profile_points,
          sweep_scales: part.sweep_scales,
          sweep_twists_degrees:
            part.sweep_twists_degrees,
          thickness: part.thickness,
          border: part.border,
          inset: part.inset,
          scale_start: part.scale_start,
          scale_end: part.scale_end,
          grid_points: part.grid_points,
          extent: part.extent,
          petal_count: part.petal_count,
          petal_segments: part.petal_segments,
          modifier_pivot: part.modifier_pivot,
          taper_axis: part.taper_axis,
          taper_start: part.taper_start,
          taper_end: part.taper_end,
          bend_axis: part.bend_axis,
          bend_radial_axis: part.bend_radial_axis,
          bend_degrees: part.bend_degrees,
          mirror_axis: part.mirror_axis,
          mirror_plane: part.mirror_plane,
          shell_thickness: part.shell_thickness,
          linear_count: part.linear_count,
          linear_step: part.linear_step,
          radial_count: part.radial_count,
          radial_axis: part.radial_axis,
          radial_step_degrees: part.radial_step_degrees,
          uv_projection: part.uv_projection,
          uv_axis: part.uv_axis,
          uv_scale: part.uv_scale,
          uv_offset: part.uv_offset,
          uv_split_seams: part.uv_split_seams,
        };
        try
        {
          validate_parametric_mesh_args(signature);
        }
        catch (error)
        {
          return tool_result({
            ok: false,
            error: error.message,
            code: "invalid_arguments",
            failed_index: i,
            root,
            completed_parts,
          });
        }

        const uses_generated_path = !part.mesh_path;
        const path = part.mesh_path ??
          `${directory}/${safe_asset_name(name)}_${i}_${safe_asset_name(part.name)}_${short_hash(signature)}.mesh`;
        generated = await send_engine_command(
          "mesh_generate",
          {
            ...signature,
            path,
            reuse_existing: uses_generated_path ||
              Boolean(part.reuse_existing),
          },
        );
        if (!generated.ok)
        {
          return tool_result({
            ...generated,
            failed_index: i,
            root,
            completed_parts,
          });
        }
        mesh = generated.resource?.path ?? path;
      }

      const child_result = await send_engine_command(
        "entity_create_empty",
        {
          name: part.name,
          parent_id: root.id,
        },
      );
      if (!child_result.ok)
      {
        return tool_result({
          ...child_result,
          failed_index: i,
          root,
          completed_parts,
        });
      }

      const child = child_result.entity;
      if (
        part.position ||
        part.rotation_euler ||
        part.scale
      )
      {
        const transform_result = await send_engine_command(
          "entity_set_transform",
          {
            id: child.id,
            position: part.position,
            rotation_euler: part.rotation_euler,
            scale: part.scale,
          },
        );
        if (!transform_result.ok)
        {
          return tool_result({
            ...transform_result,
            failed_index: i,
            root,
            completed_parts,
          });
        }
      }

      const render_result = part.with_physics === false
        ? await send_engine_command(
          "render_set_mesh",
          {
            id: child.id,
            mesh,
            material: part.material,
          },
        )
        : await bind_mesh_physics({
          id: child.id,
          mesh,
          material: part.material,
          body_type: part.body_type ?? "mesh_convex",
          static: part.physics_static ?? true,
          kinematic: part.physics_kinematic ?? false,
          mass: part.physics_mass ?? 1,
          friction: part.physics_friction ?? 0.5,
          restitution: part.physics_restitution ?? 0,
        });
      if (!render_result.ok)
      {
        return tool_result({
          ...render_result,
          failed_index: i,
          root,
          completed_parts,
        });
      }

      completed_parts.push({
        entity: child,
        mesh,
        generated,
      });
    }

    let prefab = null;
    if (prefab_path)
    {
      prefab = await send_engine_command(
        "prefab_save",
        {
          id: root.id,
          path: prefab_path,
        },
      );
      if (!prefab.ok)
      {
        return tool_result({
          ...prefab,
          root,
          completed_parts,
        });
      }
    }

    return tool_result({
      ok: true,
      root,
      completed_parts,
      completed_count: completed_parts.length,
      prefab,
    });
  },
);

register_local_tool(
  "construction_grammar_suggest",
  {
    title: "construction grammar suggest",
    description: "Rank reusable construction assemblies for a semantic purpose before creating geometry.",
    inputSchema: {
      purpose: z.string().min(1),
      limit: z.number().int().min(1).max(8).optional(),
    },
    annotations: read_only,
  },
  async ({ purpose, limit }) => tool_result({
    ok: true,
    suggestions: suggest_construction_grammars(
      purpose,
      limit ?? 5,
    ),
  }),
);

register_local_tool(
  "construction_grammar_create",
  {
    title: "construction grammar create",
    description: "Create an editable reusable assembly from a construction grammar, including openings, roofs, circulation, frames, facades, room shells, storefront bays, warehouse bays, and boarding gates.",
    inputSchema: {
      grammar: z.enum(construction_grammar_names),
      name: z.string().min(1),
      parent_id: z.string().optional(),
      position: vector3.optional(),
      rotation_euler: vector3.optional(),
      scale: vector3.optional(),
      size: positive_vector3.optional(),
      rows: z.number().int().min(1).max(5).optional(),
      columns: z.number().int().min(1).max(8).optional(),
      count: z.number().int().min(1).max(32).optional(),
      spacing: z.number().positive().optional(),
      thickness: z.number().positive().optional(),
      pitch_degrees: z.number().min(8).max(65).optional(),
      step_count: z.number().int().min(2).max(32).optional(),
      support_style: z.enum([
        "square",
        "round",
      ]).optional(),
      path_points: z.array(vector3).min(2).max(32).optional(),
      snap_mode: z.enum([
        "floor",
        "wall",
        "ceiling",
        "surface",
      ]).optional(),
      snap_target: vector3.optional(),
      snap_offset: z.number().optional(),
      align_to_surface: z.boolean().optional(),
      primary_material: z.string().optional(),
      secondary_material: z.string().optional(),
      accent_material: z.string().optional(),
      glass_material: z.string().optional(),
      emissive_material: z.string().optional(),
      asset_directory: z.string().optional(),
      prefab_path: z.string().optional(),
    },
    outputSchema: output_schemas.construction_grammar,
    annotations: edit_tool,
  },
  async (args) => {
    let grammar;
    try
    {
      grammar = build_construction_grammar(args);
    }
    catch (error)
    {
      return tool_result(
        structured_error(
          error.message,
          {
            code: "invalid_arguments",
          },
        ),
      );
    }

    const compound_tool = tool_registry.get(
      "compound_create",
    );
    const compound_result = await compound_tool.handler({
      name: args.name,
      parent_id: args.parent_id,
      position: args.position,
      rotation_euler: args.rotation_euler,
      scale: args.scale,
      asset_directory: args.asset_directory,
      prefab_path: args.prefab_path,
      parts: grammar.parts,
    });
    const compound =
      compound_result.structuredContent ?? {
        ok: false,
        error: "construction grammar compound result is missing",
      };
    let snap = null;
    if (
      compound.ok &&
      compound.root?.id &&
      args.snap_mode
    )
    {
      snap = await send_engine_command(
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
        return tool_result({
          ...snap,
          root: compound.root,
          completed_parts:
            compound.completed_parts,
          completed_count:
            compound.completed_count,
          grammar: grammar.metadata,
        });
      }
    }
    return tool_result({
      ...compound,
      grammar: grammar.metadata,
      snap,
    });
  },
);

register_local_tool(
  "detail_pattern_create",
  {
    title: "detail pattern create",
    description: "Create reusable high-frequency scene details as one editable compound object. Patterns include keyboard keys, drawers, slats, books, buttons, cables, cushions, and wall trim.",
    inputSchema: {
      pattern: z.enum([
        "keyboard_keys",
        "drawers",
        "slats",
        "books",
        "buttons",
        "cable",
        "cushions",
        "wall_trim",
      ]),
      name: z.string(),
      parent_id: z.string().optional(),
      position: vector3.optional(),
      rotation_euler: vector3.optional(),
      count: z.number().int().min(1).max(64).optional(),
      rows: z.number().int().min(1).max(8).optional(),
      columns: z.number().int().min(1).max(16).optional(),
      spacing: z.number().positive().optional(),
      size: vector3.optional(),
      path_points: z.array(vector3).min(2).max(64).optional(),
      material: z.string().optional(),
      accent_material: z.string().optional(),
      asset_directory: z.string().optional(),
      prefab_path: z.string().optional(),
    },
    outputSchema: output_schemas.compound_create,
    annotations: edit_tool,
  },
  async (args) => {
    const parts = [];
    const count = args.count ?? 6;
    const rows = args.rows ?? 5;
    const columns = args.columns ?? 12;
    const spacing = args.spacing ?? 0.004;
    const directory = (
      args.asset_directory ??
      "project/world_resources"
    ).replace(/[\\/]+$/g, "");
    const base_name = safe_asset_name(args.name);

    if (args.pattern === "keyboard_keys")
    {
      const key_size = args.size ?? [0.018, 0.008, 0.018];
      const total = Math.min(64, rows * columns);
      const mesh_path =
        `${directory}/${base_name}_key.mesh`;
      for (let index = 0; index < total; index++)
      {
        const row = Math.floor(index / columns);
        const column = index % columns;
        const row_columns = Math.min(
          columns,
          total - row * columns,
        );
        parts.push({
          name: `key_${row}_${column}`,
          shape: "rounded_box",
          mesh_path,
          reuse_existing: true,
          size: key_size,
          radius: Math.min(...key_size) * 0.18,
          segments: 2,
          position: [
            (
              column -
              (row_columns - 1) * 0.5 +
              row * 0.18
            ) * (key_size[0] + spacing),
            0,
            (
              row -
              (rows - 1) * 0.5
            ) * (key_size[2] + spacing),
          ],
          material: args.material,
        });
      }
    }
    else if (args.pattern === "drawers")
    {
      const drawer_size = args.size ?? [0.5, 0.18, 0.025];
      const mesh_path =
        `${directory}/${base_name}_drawer.mesh`;
      const knob_path =
        `${directory}/${base_name}_knob.mesh`;
      for (let i = 0; i < Math.min(count, 24); i++)
      {
        const y = i * (drawer_size[1] + spacing);
        parts.push({
          name: `drawer_${i}`,
          shape: "inset_panel",
          mesh_path,
          reuse_existing: true,
          size: drawer_size,
          border: Math.min(
            drawer_size[0],
            drawer_size[1],
          ) * 0.08,
          inset: drawer_size[2] * 0.2,
          position: [0, y, 0],
          material: args.material,
        });
        parts.push({
          name: `drawer_knob_${i}`,
          shape: "rounded_cylinder",
          mesh_path: knob_path,
          reuse_existing: true,
          size: [0.025, 0.03, 0.025],
          radius: 0.0125,
          height: 0.03,
          bevel: 0.002,
          segments: 12,
          position: [0, y, drawer_size[2] * 0.8],
          rotation_euler: [90, 0, 0],
          material: args.accent_material ?? args.material,
        });
      }
    }
    else if (
      args.pattern === "slats" ||
      args.pattern === "wall_trim"
    )
    {
      const part_size = args.size ?? (
        args.pattern === "slats"
          ? [0.04, 1.0, 0.025]
          : [1.0, 0.08, 0.025]
      );
      const mesh_path =
        `${directory}/${base_name}_${args.pattern}.mesh`;
      for (let i = 0; i < count; i++)
      {
        parts.push({
          name: `${args.pattern}_${i}`,
          shape: "beveled_box",
          mesh_path,
          reuse_existing: true,
          size: part_size,
          bevel: Math.min(...part_size) * 0.12,
          position: args.pattern === "slats"
            ? [
                (
                  i -
                  (count - 1) * 0.5
                ) * (part_size[0] + spacing),
                0,
                0,
              ]
            : [
                (
                  i -
                  (count - 1) * 0.5
                ) * (part_size[0] + spacing),
                0,
                0,
              ],
          material: args.material,
        });
      }
    }
    else if (args.pattern === "books")
    {
      for (let i = 0; i < count; i++)
      {
        const height = 0.18 + (i % 4) * 0.015;
        const width = 0.025 + (i % 3) * 0.006;
        parts.push({
          name: `book_${i}`,
          shape: "beveled_box",
          size: [width, height, 0.13],
          bevel: 0.002,
          position: [
            (
              i -
              (count - 1) * 0.5
            ) * (0.04 + spacing),
            height * 0.5,
            0,
          ],
          rotation_euler: [
            0,
            0,
            (i % 5 === 0 ? -4 : 0),
          ],
          material: i % 2 === 0
            ? args.material
            : args.accent_material ?? args.material,
        });
      }
    }
    else if (args.pattern === "buttons")
    {
      const button_size = args.size ?? [0.018, 0.008, 0.018];
      const total = Math.min(64, rows * columns);
      const mesh_path =
        `${directory}/${base_name}_button.mesh`;
      for (let i = 0; i < total; i++)
      {
        const row = Math.floor(i / columns);
        const column = i % columns;
        parts.push({
          name: `button_${row}_${column}`,
          shape: "rounded_cylinder",
          mesh_path,
          reuse_existing: true,
          size: button_size,
          radius: button_size[0] * 0.5,
          height: button_size[1],
          bevel: button_size[1] * 0.18,
          segments: 12,
          position: [
            (
              column -
              (columns - 1) * 0.5
            ) * (button_size[0] + spacing),
            0,
            (
              row -
              (rows - 1) * 0.5
            ) * (button_size[2] + spacing),
          ],
          material: args.material,
        });
      }
    }
    else if (args.pattern === "cable")
    {
      if (!args.path_points)
      {
        return tool_result(
          structured_error(
            "cable requires path_points",
            { code: "invalid_arguments" },
          ),
        );
      }
      parts.push({
        name: "cable",
        shape: "pipe",
        path_points: args.path_points,
        radius: (args.size ?? [0.012, 0.012, 0.012])[0] * 0.5,
        segments: 10,
        material: args.material,
      });
    }
    else if (args.pattern === "cushions")
    {
      const cushion_size = args.size ?? [0.45, 0.12, 0.45];
      const mesh_path =
        `${directory}/${base_name}_cushion.mesh`;
      for (let i = 0; i < count; i++)
      {
        parts.push({
          name: `cushion_${i}`,
          shape: "rounded_box",
          mesh_path,
          reuse_existing: true,
          size: cushion_size,
          radius: Math.min(...cushion_size) * 0.42,
          segments: 6,
          position: [
            (
              i -
              (count - 1) * 0.5
            ) * (cushion_size[0] + spacing),
            0,
            0,
          ],
          material: args.material,
        });
      }
    }

    if (parts.length === 0 || parts.length > 64)
    {
      return tool_result(
        structured_error(
          "detail pattern produced an invalid part count",
          { code: "invalid_arguments" },
        ),
      );
    }

    const compound_tool = tool_registry.get("compound_create");
    return compound_tool.handler({
      name: args.name,
      parent_id: args.parent_id,
      position: args.position,
      rotation_euler: args.rotation_euler,
      asset_directory: args.asset_directory,
      prefab_path: args.prefab_path,
      parts,
    });
  },
);

register_tool(
  server,
  "entity_update",
  "Rename, activate, deactivate, reparent, or retag an entity in edit mode. Use parent_id root to detach. tags_mode defaults to replace; use merge to preserve semantic identity.",
  {
    id: z.string(),
    name: z.string().optional(),
    active: z.boolean().optional(),
    parent_id: z.string().optional(),
    tags: z.string().optional(),
    tags_mode: z.enum([
      "replace",
      "merge",
    ]).optional(),
  },
  "entity_update",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_tool(
  server,
  "entity_clone",
  "Clone an entity hierarchy in edit mode, optionally renaming, reparenting, and selecting the clone.",
  {
    id: z.string(),
    name: z.string().optional(),
    parent_id: z.string().optional(),
    select: z.boolean().optional(),
  },
  "entity_clone",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_tool(
  server,
  "entity_move_index",
  "Move an entity to a sibling/root index in edit mode.",
  {
    id: z.string(),
    index: z.number().int().min(0),
  },
  "entity_move_index",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_tool(
  server,
  "viewport_frame",
  "Frame the complete renderable hierarchy of the current selection or a specific entity. Fits every subtree AABB corner to the horizontal and vertical camera FOV with explicit padding.",
  {
    id: z.string().optional(),
    view: z.enum([
      "perspective",
      "front",
      "back",
      "left",
      "right",
      "top",
    ]).optional(),
    padding: z.number().min(1).max(4).optional(),
  },
  "viewport_frame",
  { annotations: edit_tool, outputSchema: output_schemas.camera_snapshot },
);

register_tool(
  server,
  "entity_delete",
  "Delete an entity in edit mode.",
  {
    id: z.string(),
  },
  "entity_delete",
  { annotations: destructive_tool, outputSchema: output_schemas.delete_receipt },
);

register_tool(
  server,
  "entity_delete_children",
  "Delete all direct children of an entity immediately in edit mode and return any remaining direct children.",
  {
    id: z.string(),
  },
  "entity_delete_children",
  { annotations: destructive_tool, outputSchema: output_schemas.delete_receipt },
);

register_tool(
  server,
  "entity_select",
  "Select an entity by id in edit mode.",
  {
    id: z.string(),
  },
  "entity_select",
  { annotations: edit_tool, outputSchema: output_schemas.selection_get },
);

register_tool(
  server,
  "entity_set_transform",
  "Set an entity local transform in edit mode.",
  {
    id: z.string(),
    position: vector3.optional(),
    rotation: quaternion.optional(),
    rotation_euler: vector3.optional(),
    scale: vector3.optional(),
  },
  "entity_set_transform",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_local_tool(
  "entity_set_transform_batch",
  {
    title: "entity set transform batch",
    description: "Set many entity local transforms in edit mode through one native engine batch command.",
    inputSchema: {
      items: z.array(z.object(transform_set_args)).min(1).max(64),
    },
    outputSchema: output_schemas.transform_batch_receipt,
    annotations: edit_tool,
  },
  async ({ items }) => {
    const args = { count: items.length };
    const keys = Object.keys(transform_set_args);
    for (let i = 0; i < items.length; i++) {
      for (const key of keys) {
        if (items[i][key] !== undefined && items[i][key] !== null) {
          args[`item_${i}_${key}`] = items[i][key];
        }
      }
    }

    const result = await send_engine_command("entity_set_transform_batch", args);
    return tool_result({
      ...result,
      updated_count: result.updated_count ?? result.updated?.length ?? 0,
    });
  },
);

register_tool(
  server,
  "entity_add_component",
  "Add a component to an entity in edit mode.",
  {
    id: z.string(),
    type: component_type,
  },
  "entity_add_component",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_tool(
  server,
  "entity_remove_component",
  "Remove a component from an entity in edit mode.",
  {
    id: z.string(),
    type: component_type,
  },
  "entity_remove_component",
  { annotations: destructive_tool, outputSchema: output_schemas.entity },
);

register_tool(
  server,
  "entity_render_materials",
  "Read render material names for an entity and optionally its descendants. Use before destructive rebuilds that must preserve materials.",
  {
    id: z.string(),
    include_descendants: z.boolean().optional(),
  },
  "entity_render_materials",
  { annotations: read_only, outputSchema: output_schemas.entity_render_materials },
);

register_tool(
  server,
  "resource_list",
  "List cached engine resources by type, including names, paths, states, and flags.",
  {
    type: resource_type.optional(),
    limit: z.number().int().min(1).max(5000).optional(),
    offset: z.number().int().min(0).optional(),
  },
  "resource_list",
  { annotations: read_only, outputSchema: output_schemas.resource_list },
);

register_tool(
  server,
  "resource_load",
  "Load a resource into the cache by type and path.",
  {
    type: z.enum(["texture", "material", "mesh", "animation"]),
    path: z.string(),
    flags: z.number().int().min(0).optional(),
  },
  "resource_load",
  { annotations: edit_tool, outputSchema: output_schemas.resource_receipt },
);

register_tool(
  server,
  "resource_reload",
  "Reload a cached resource by name or path.",
  {
    name: z.string().optional(),
    path: z.string().optional(),
    type: resource_type.optional(),
  },
  "resource_reload",
  { annotations: edit_tool, outputSchema: output_schemas.resource_receipt },
);

register_tool(
  server,
  "resource_save",
  "Save a cached resource to its current path or a new save_path.",
  {
    name: z.string().optional(),
    path: z.string().optional(),
    type: resource_type.optional(),
    save_path: z.string().optional(),
  },
  "resource_save",
  { annotations: edit_tool, outputSchema: output_schemas.resource_receipt },
);

register_tool(
  server,
  "resource_remove",
  "Remove a cached resource by name or path without deleting the file from disk.",
  {
    name: z.string().optional(),
    path: z.string().optional(),
    type: resource_type.optional(),
  },
  "resource_remove",
  { annotations: destructive_tool, outputSchema: output_schemas.resource_receipt },
);

register_tool(
  server,
  "material_get",
  "Inspect one cached material by name or path, including all scalar properties and texture slots.",
  {
    name: z.string().optional(),
    path: z.string().optional(),
  },
  "material_get",
  { annotations: read_only, outputSchema: output_schemas.material },
);

register_tool(
  server,
  "material_create",
  "Create, save, and cache a new material file in edit mode.",
  {
    path: z.string(),
    name: z.string().optional(),
  },
  "material_create",
  { annotations: edit_tool, outputSchema: output_schemas.material },
);

register_tool(
  server,
  "material_set_property",
  "Set one scalar material property in edit mode. Use material_get to inspect valid property names.",
  {
    name: z.string().optional(),
    path: z.string().optional(),
    property: z.string(),
    value: z.number(),
  },
  "material_set_property",
  { annotations: edit_tool, outputSchema: output_schemas.material },
);

register_tool(
  server,
  "material_set_texture",
  "Set one material texture slot in edit mode by texture type, path, and optional slot.",
  {
    name: z.string().optional(),
    path: z.string().optional(),
    texture_type: material_texture_type,
    texture_path: z.string(),
    slot: z.number().int().min(0).max(3).optional(),
  },
  "material_set_texture",
  { annotations: edit_tool, outputSchema: output_schemas.material },
);

register_tool(
  server,
  "material_apply_preset",
  "Apply a complete engine paint or surface preset to a cached material. Paint presets accept a linear RGBA color.",
  {
    name: z.string().optional(),
    path: z.string().optional(),
    kind: z.enum(["paint", "surface"]),
    preset: z.enum([
      "gloss_solid",
      "metallic",
      "satin",
      "matte",
      "pearl",
      "candy",
      "chameleon",
      "glass_clear",
      "glass_tinted",
      "headlight_lens",
      "taillight_lens",
      "rubber",
      "carbon_fiber",
      "chrome",
      "polished_metal",
      "brake_disc",
      "leather",
      "black_plastic",
      "emissive_red",
      "emissive_white",
    ]),
    color: vector4.optional(),
  },
  "material_apply_preset",
  { annotations: edit_tool, outputSchema: output_schemas.material },
);

register_tool(
  server,
  "material_semantic_create",
  "Create or update one reusable semantic material. Colors are linear RGBA. Prefer material_palette_create for a coordinated room palette.",
  {
    path: z.string(),
    semantic: semantic_material,
    color: vector4.optional(),
  },
  "material_semantic_create",
  { annotations: edit_tool, outputSchema: output_schemas.material },
);

register_local_tool(
  "material_palette_create",
  {
    title: "material palette create",
    description: "Create a coordinated reusable material palette for a generated scene. Returns material names and paths ready for compound_create parts.",
    inputSchema: {
      theme: z.enum([
        "cozy",
        "neutral",
        "cool",
        "vibrant",
        "industrial",
        "retail",
        "aviation",
      ]).optional(),
      directory: z.string().optional(),
      prefix: z.string().optional(),
      semantics: z.array(semantic_material).min(1).max(17).optional(),
      accent_ratio: z.number().min(0.05).max(0.35).optional(),
      texture_scale: z.number().min(0.1).max(100).optional(),
      roughness_bias: z.number().min(-0.5).max(0.5).optional(),
      wear: z.number().min(0).max(1).optional(),
    },
    outputSchema: output_schemas.generic,
    annotations: edit_tool,
  },
  async ({
    theme = "cozy",
    directory = "project/world_resources",
    prefix = "palette",
    accent_ratio = 0.12,
    texture_scale = 1,
    roughness_bias = 0,
    wear = 0,
    semantics = [
      "painted_wall",
      "wood",
      "black_plastic",
      "fabric",
      "metal",
      "glass",
      "screen",
      "screen_on",
      "emissive",
      "concrete",
      "painted_metal",
    ],
  }) => {
    const palette = [];
    const base_directory = directory.replace(/[\\/]+$/g, "");
    for (const semantic of semantics)
    {
      const srgb = palette_themes[theme][semantic];
      const result = await send_engine_command(
        "material_semantic_create",
        {
          path: `${base_directory}/${safe_asset_name(prefix)}_${semantic}.xml`,
          semantic,
          color: srgb ? srgb_to_linear(srgb) : undefined,
        },
      );
      if (!result.ok)
      {
        return tool_result({
          ...result,
          palette,
          failed_semantic: semantic,
        });
      }
      palette.push({
        semantic,
        material: result.material,
      });
      const finish = semantic_finish_rules[semantic] ?? {};
      const properties = {
        ...finish,
        roughness: finish.roughness === undefined
          ? undefined
          : Math.max(
            0,
            Math.min(
              1,
              finish.roughness + roughness_bias,
            ),
          ),
        texture_tiling_x: texture_scale,
        texture_tiling_y: texture_scale,
        color_variation_from_instance:
          wear * 0.08,
      };
      for (const [property, value] of Object.entries(properties))
      {
        if (value === undefined)
        {
          continue;
        }
        const configured = await send_engine_command(
          "material_set_property",
          {
            path:
              `${base_directory}/${safe_asset_name(prefix)}_${semantic}.xml`,
            property,
            value,
          },
        );
        if (!configured.ok)
        {
          return tool_result({
            ...configured,
            palette,
            failed_semantic: semantic,
            failed_property: property,
          });
        }
      }
    }

    return tool_result({
      ok: true,
      theme,
      palette,
      count: palette.length,
      art_direction: {
        accent_ratio,
        texture_scale,
        roughness_bias,
        wear,
      },
    });
  },
);

register_tool(
  server,
  "prefab_types",
  "List registered code prefab type names.",
  {},
  "prefab_types",
  { annotations: read_only, outputSchema: output_schemas.prefab_types },
);

register_tool(
  server,
  "prefab_save",
  "Save an entity hierarchy as a .prefab file in edit mode.",
  {
    id: z.string(),
    path: z.string(),
  },
  "prefab_save",
  { annotations: edit_tool, outputSchema: output_schemas.prefab_receipt },
);

register_tool(
  server,
  "prefab_load",
  "Load a .prefab file into an existing parent entity or a new root entity in edit mode.",
  {
    path: z.string(),
    parent_id: z.string().optional(),
    name: z.string().optional(),
  },
  "prefab_load",
  { annotations: edit_tool, outputSchema: output_schemas.prefab_receipt },
);

register_tool(
  server,
  "component_get",
  "Read editable properties and registered raw members for a component on an entity.",
  {
    id: z.string(),
    type: component_type,
  },
  "component_get",
  { annotations: read_only, outputSchema: output_schemas.component_get },
);

register_tool(
  server,
  "component_set",
  "Set one component property or registered raw member on an entity. Vector, color, bounding box, quaternion, and matrix values are arrays.",
  {
    id: z.string(),
    type: component_type,
    property: z.string(),
    value: component_value,
  },
  "component_set",
  {
    annotations: edit_tool,
    outputSchema: output_schemas.component_set,
  },
);

register_tool(
  server,
  "component_set_batch",
  "Set many component properties or registered raw members on one entity component in a single engine command.",
  {
    id: z.string(),
    type: component_type,
    items: z.array(z.object({
      property: z.string(),
      value: component_value,
    })).min(1).max(128),
  },
  "component_set_batch",
  {
    annotations: edit_tool,
    outputSchema: output_schemas.component_set_batch,
    map_args: (args) => {
      const mapped = {
        id: args.id,
        type: args.type,
        count: args.items.length,
      };
      for (let i = 0; i < args.items.length; i++)
      {
        mapped[`property_${i}`] = args.items[i].property;
        mapped[`value_${i}`] = args.items[i].value;
      }
      return mapped;
    },
  },
);

register_tool(
  server,
  "component_action",
  [
    "Invoke deterministic component methods that are not simple property writes.",
    "Supported actions include terrain generate; spline generate_road_mesh, clear_road_mesh, spawn_instances, clear_instances; particle_system apply_preset and trigger_burst; physics apply_force and vehicle utility actions; audio_source play and stop; light fit_to_mesh; camera focus_selected. Camera focus_selected is for one renderable, not hierarchy visual review; use viewport_frame for scenes.",
  ].join(" "),
  {
    id: z.string(),
    type: component_type,
    action: z.enum([
      "generate",
      "generate_road_mesh",
      "clear_road_mesh",
      "spawn_instances",
      "clear_instances",
      "apply_preset",
      "trigger_burst",
      "apply_force",
      "sync_wheel_offsets",
      "reset_tire_wear",
      "shift_up",
      "shift_down",
      "shift_to_neutral",
      "draw_debug_visualization",
      "play",
      "stop",
      "fit_to_mesh",
      "focus_selected",
    ]),
    preset: z.string().optional(),
    value: z.union([z.string(), z.number()]).optional(),
    count: z.number().optional(),
    force: vector3.optional(),
    mode: z.enum(["constant", "force", "impulse"]).optional(),
  },
  "component_action",
  { annotations: edit_tool, outputSchema: output_schemas.component_action },
);

register_tool(
  server,
  "renderer_debug_get",
  "Read friendly renderer debug overlay and visualization options.",
  {},
  "renderer_debug_get",
  { annotations: read_only, outputSchema: output_schemas.renderer_debug },
);

register_tool(
  server,
  "renderer_debug_set",
  "Set a friendly renderer debug overlay or visualization option.",
  {
    option: z.enum(["aabb", "picking_ray", "grid", "transform_handle", "selection_outline", "entity_icons", "performance_metrics", "physics", "wireframe", "meshlet_visualize", "cluster_visualize"]),
    value: z.union([z.boolean(), z.number(), z.string()]),
  },
  "renderer_debug_set",
  { annotations: edit_tool, outputSchema: output_schemas.renderer_debug },
);

register_tool(
  server,
  "physics_state",
  "Inspect rigid body and vehicle physics state for one entity.",
  {
    id: z.string(),
  },
  "physics_state",
  { annotations: read_only, outputSchema: output_schemas.physics_state },
);

register_tool(
  server,
  "vehicle_list",
  "List drivable cars in the world with occupancy, mcp control flag, view, position, and speed.",
  {},
  "vehicle_list",
  { annotations: read_only },
);

register_tool(
  server,
  "vehicle_get",
  "Read live car status: occupancy, mcp control, camera view, pedals, gear, rpm, and speed. Omit id to use the occupied car or the only car in the world.",
  {
    id: z.string().optional(),
  },
  "vehicle_get",
  { annotations: read_only },
);

register_tool(
  server,
  "vehicle_enter",
  "Enter a drivable car in play mode (same as E). Enables chase cam and by default takes mcp pedal ownership so keyboard does not overwrite agent input.",
  {
    id: z.string().optional(),
    mcp_controlled: z.boolean().optional(),
  },
  "vehicle_enter",
  { annotations: edit_tool },
);

register_tool(
  server,
  "vehicle_exit",
  "Exit the occupied car in play mode when slow enough (same as E). Clears mcp pedal ownership.",
  {
    id: z.string().optional(),
  },
  "vehicle_exit",
  { annotations: edit_tool },
);

register_tool(
  server,
  "vehicle_set_input",
  [
    "Set car pedals while occupied in play mode.",
    "throttle and brake are 0 to 1, steering is -1 left to 1 right, handbrake is 0 to 1.",
    "Automatically marks the car mcp_controlled so keyboard zeros do not overwrite these values.",
    "Human keyboard map for reference: Arrow Up gas, Arrow Down brake, Arrow Left/Right steer, Space handbrake, E enter/exit, V cycle view, R reset, L1/R1 shift.",
  ].join(" "),
  {
    id: z.string().optional(),
    throttle: z.number().min(0).max(1).optional(),
    brake: z.number().min(0).max(1).optional(),
    steering: z.number().min(-1).max(1).optional(),
    handbrake: z.number().min(0).max(1).optional(),
  },
  "vehicle_set_input",
  { annotations: edit_tool },
);

register_tool(
  server,
  "vehicle_shift",
  "Shift gears on the occupied car: up, down, or neutral.",
  {
    id: z.string().optional(),
    action: z.enum(["up", "down", "neutral"]),
  },
  "vehicle_shift",
  { annotations: edit_tool },
);

register_tool(
  server,
  "vehicle_reset",
  "Reset the car to its spawn pose and clear pedals with handbrake on (same as R).",
  {
    id: z.string().optional(),
  },
  "vehicle_reset",
  { annotations: edit_tool },
);

register_tool(
  server,
  "vehicle_set_view",
  "Set or cycle the in-car camera view: chase, hood, wheel, or cycle (same as V).",
  {
    id: z.string().optional(),
    view: z.enum(["chase", "hood", "wheel", "cycle", "next"]),
  },
  "vehicle_set_view",
  { annotations: edit_tool },
);

register_tool(
  server,
  "vehicle_telemetry",
  [
    "Read car_telemetry.csv from the engine working directory (binary folder when launched from there).",
    "Returns the absolute path plus the csv header and the last max_rows data rows for handling diagnosis.",
    "Not an Excel file; it is a per-physics-tick csv written while a drivable car simulates with log_to_file enabled.",
  ].join(" "),
  {
    max_rows: z.number().int().min(1).max(5000).optional(),
    include_csv: z.boolean().optional(),
  },
  "vehicle_telemetry",
  { annotations: read_only },
);

register_tool(
  server,
  "execute_lua",
  [
    "Run one focused Lua script inside the engine using known bindings (World, Entity, Renderable, Light, MeshType, LightType, ComponentType, WorldHelpers, etc.).",
    "Do not use this for API discovery, pairs/next probing, method listing, or exploratory scripts.",
    "For blockouts and repeated primitives prefer entity_create_primitive_batch; for lights prefer entity_create_light.",
    "Use this only when a native batch tool cannot express the edit. Keep the script bounded, use print(...) for diagnostics, and return a short summary string.",
  ].join(" "),
  {
    code: z.string().optional(),
    script: z.string().optional(),
  },
  "execute_lua",
  {
    annotations: destructive_tool,
    outputSchema: output_schemas.lua_result,
    map_args: (args) => {
      const code = args.code ?? args.script;
      if (code === undefined) {
        throw new Error("missing code");
      }
      return { code };
    },
  },
);

function register_text_resource(name, uri, title, description, read_text, mime_type = "text/markdown") {
  const resource = {
    name,
    uri,
    title,
    description,
    mimeType: mime_type,
  };
  resource_registry.set(name, resource);

  if (typeof server.registerResource === "function") {
    server.registerResource(name, uri, {
      title,
      description,
      mimeType: mime_type,
    }, async (resource_uri) => {
      const text = await read_text();
      return {
        contents: [
          {
            uri: resource_uri.href ?? uri,
            mimeType: mime_type,
            text,
          },
        ],
      };
    });
  }
}

register_text_resource(
  "engine_overview",
  "spartan://engine/overview",
  "Spartan Engine MCP Overview",
  "Architecture and usage notes for the Spartan MCP bridge.",
  () => engine_overview,
);

register_text_resource(
  "edit_rules",
  "spartan://engine/edit-rules",
  "Spartan MCP Edit Rules",
  "Safety and edit-mode rules for Spartan MCP tools.",
  () => edit_rules,
);

register_text_resource(
  "component_schemas",
  "spartan://engine/component-schemas",
  "Spartan Component Schemas",
  "Editable component property names and notes.",
  () => component_schema_markdown(),
);

register_text_resource(
  "parametric_modeling",
  "spartan://engine/parametric-modeling",
  "Spartan Parametric Modeling",
  "Shape selection, dimensions, budgets, and furniture patterns for procedural modeling.",
  () => parametric_modeling_guide,
);

register_text_resource(
  "scene_planning",
  "spartan://engine/scene-planning",
  "Spartan Generic Scene Planning",
  "Environment-agnostic semantic planning, scale, placement, lighting, and correction workflow.",
  () => scene_planning_guide,
);

register_text_resource(
  "construction_grammars",
  "spartan://engine/construction-grammars",
  "Spartan Construction Grammars",
  "Generic medium-scale assemblies for openings, roofs, circulation, structure, panels, supports, signs, cables, and facades.",
  () => construction_grammar_guide,
);

register_text_resource(
  "world_current",
  "spartan://world/current",
  "Current Spartan World",
  "Live world summary from the engine bridge.",
  async () => JSON.stringify(await send_engine_command("world_summary"), null, 2),
  "application/json",
);

register_text_resource(
  "console_recent",
  "spartan://console/recent",
  "Recent Spartan Console",
  "Recent engine console output.",
  async () => JSON.stringify(await send_engine_command("console_read", { limit: 80 }), null, 2),
  "application/json",
);

register_text_resource(
  "source_access",
  "spartan://source",
  "Spartan Source Access",
  "Use search_codebase and read_source_file for project-relative source files under source/ and tools/mcp/spartan_engine/.",
  () => [
    "# Spartan Source Access",
    "",
    "Use search_codebase to locate files and read_source_file to read focused line ranges.",
    "The source reader accepts project-relative paths under source/ and tools/mcp/spartan_engine/.",
  ].join("\n"),
);

register_text_resource(
  "agent_memory",
  "spartan://agent/memory",
  "Spartan Agent Memory",
  "Shared memory for future Spartan agents, including engine facts, strategies, gotchas, and maintainer advice.",
  () => read_agent_memory(),
);

register_text_resource(
  "debug_log",
  "spartan://agent/debug-log",
  "Spartan MCP Debug Log",
  "Recent assistant prompts and engine command inputs/outputs for debugging MCP behavior.",
  () => read_debug_log(120),
  "application/jsonl",
);

function raw_shape_to_json_schema(shape) {
  return json_schema_from_raw_shape(shape);
}

function http_tool_list() {
  return [...tool_registry.values()].map((tool) => {
    const entry = {
      name: tool.name,
      title: tool.title,
      description: tool.description,
      inputSchema: raw_shape_to_json_schema(tool.inputSchema),
    };
    if (tool.outputSchema) {
      entry.outputSchema = raw_shape_to_json_schema(tool.outputSchema);
    }
    if (tool.annotations) {
      entry.annotations = tool.annotations;
    }
    return entry;
  });
}

function http_resource_list() {
  return [...resource_registry.values()].map((resource) => ({
    uri: resource.uri,
    name: resource.name,
    title: resource.title,
    description: resource.description,
    mimeType: resource.mimeType,
  }));
}

function http_resource_templates() {
  return [
    {
      uriTemplate: "spartan://source/{path}",
      name: "source_file",
      title: "Spartan Source File",
      description: "Read source files through the read_source_file tool using project-relative paths.",
      mimeType: "text/plain",
    },
  ];
}

async function read_resource(uri) {
  const resource = [...resource_registry.values()].find((entry) => entry.uri === uri);
  if (!resource) {
    return null;
  }

  if (uri === "spartan://world/current") {
    return JSON.stringify(await send_engine_command("world_summary"), null, 2);
  }
  if (uri === "spartan://console/recent") {
    return JSON.stringify(await send_engine_command("console_read", { limit: 80 }), null, 2);
  }
  if (uri === "spartan://engine/overview") {
    return engine_overview;
  }
  if (uri === "spartan://engine/edit-rules") {
    return edit_rules;
  }
  if (uri === "spartan://engine/component-schemas") {
    return component_schema_markdown();
  }
  if (uri === "spartan://engine/parametric-modeling") {
    return parametric_modeling_guide;
  }
  if (uri === "spartan://engine/scene-planning") {
    return scene_planning_guide;
  }
  if (uri === "spartan://engine/construction-grammars") {
    return construction_grammar_guide;
  }
  if (uri === "spartan://source") {
    return [
      "# Spartan Source Access",
      "",
      "Use search_codebase to locate files and read_source_file to read focused line ranges.",
      "The source reader accepts project-relative paths under source/ and tools/mcp/spartan_engine/.",
    ].join("\n");
  }
  if (uri === "spartan://agent/memory") {
    return read_agent_memory();
  }

  return null;
}

function json_rpc_result(id, result) {
  return {
    jsonrpc: "2.0",
    id,
    result,
  };
}

function json_rpc_error(id, code, message) {
  return {
    jsonrpc: "2.0",
    id: id ?? null,
    error: {
      code,
      message,
    },
  };
}

async function handle_http_rpc(message) {
  if (!message || typeof message !== "object" || message.jsonrpc !== "2.0") {
    return json_rpc_error(null, -32600, "invalid json-rpc request");
  }

  const has_id = Object.prototype.hasOwnProperty.call(message, "id");
  const id = message.id;
  const method = message.method;
  if (!has_id && method?.startsWith("notifications/")) {
    return null;
  }

  if (method === "initialize") {
    return json_rpc_result(id, {
      protocolVersion: "2025-06-18",
      capabilities: {
        tools: {
          listChanged: false,
        },
        resources: {},
        logging: {},
      },
      serverInfo: {
        name: "spartan-engine",
        title: "Spartan Engine MCP",
        version: "0.1.0",
      },
      instructions: "Use spartan_status first, search_codebase for source questions, and engine tools for live scene control.",
    });
  }

  if (method === "ping") {
    return json_rpc_result(id, {});
  }

  if (method === "tools/list") {
    return json_rpc_result(id, {
      tools: http_tool_list(),
    });
  }

  if (method === "resources/list") {
    return json_rpc_result(id, {
      resultType: "complete",
      resources: http_resource_list(),
    });
  }

  if (method === "resources/templates/list") {
    return json_rpc_result(id, {
      resultType: "complete",
      resourceTemplates: http_resource_templates(),
    });
  }

  if (method === "resources/read") {
    const uri = message.params?.uri;
    const resource = [...resource_registry.values()].find((entry) => entry.uri === uri);
    const text = await read_resource(uri);
    if (!resource || text === null) {
      return json_rpc_error(id, -32602, `resource not found: ${uri}`);
    }

    return json_rpc_result(id, {
      resultType: "complete",
      contents: [
        {
          uri,
          mimeType: resource.mimeType,
          text,
        },
      ],
    });
  }

  if (method === "tools/call") {
    const name = message.params?.name;
    const args = message.params?.arguments ?? {};
    const tool = tool_registry.get(name);
    if (!tool) {
      return json_rpc_error(id, -32602, `unknown tool: ${name}`);
    }

    try {
      const parsed = parse_raw_shape(tool.inputSchema, args);
      if (!parsed.ok) {
        return json_rpc_result(id, tool_result(parsed.error));
      }

      const result = await tool.handler(parsed.value);
      return json_rpc_result(id, result);
    } catch (error) {
      return json_rpc_result(id, tool_result(structured_error(error.message)));
    }
  }

  return json_rpc_error(id, -32601, `method not found: ${method}`);
}

function send_http_json(response, status_code, payload) {
  const text = JSON.stringify(payload);
  response.writeHead(status_code, {
    "content-type": "application/json",
    "content-length": Buffer.byteLength(text),
    "access-control-allow-origin": "*",
  });
  response.end(text);
}

function start_http_server() {
  const http_server = http.createServer((request, response) => {
    if (request.method === "OPTIONS") {
      response.writeHead(204, {
        "access-control-allow-origin": "*",
        "access-control-allow-methods": "GET,POST,OPTIONS",
        "access-control-allow-headers": "content-type,authorization,mcp-session-id",
      });
      response.end();
      return;
    }

    if (request.method === "GET" && request.url === "/health") {
      send_http_json(response, 200, {
        ok: true,
        server: "spartan-engine",
        endpoint: `http://127.0.0.1:${http_port}/mcp`,
        project_root,
        engine_host,
        engine_port,
        codebase: codebase.status(),
      });
      return;
    }

    if (request.method !== "POST" || request.url !== "/mcp") {
      send_http_json(response, 404, {
        ok: false,
        error: "not found",
      });
      return;
    }

    let body = "";
    request.on("data", (chunk) => {
      body += chunk.toString("utf8");
      if (body.length > 1024 * 1024) {
        request.destroy();
      }
    });

    request.on("end", async () => {
      let message;
      try {
        message = JSON.parse(body);
      } catch {
        send_http_json(response, 400, json_rpc_error(null, -32700, "parse error"));
        return;
      }

      const messages = Array.isArray(message) ? message : [message];
      const results = [];
      for (const entry of messages) {
        const result = await handle_http_rpc(entry);
        if (result) {
          results.push(result);
        }
      }

      if (results.length === 0) {
        response.writeHead(202);
        response.end();
      } else {
        send_http_json(response, 200, Array.isArray(message) ? results : results[0]);
      }
    });
  });

  http_server.listen(http_port, "127.0.0.1", () => {
    console.error(`spartan MCP HTTP listening on http://127.0.0.1:${http_port}/mcp`);
  });
  return http_server;
}

let http_server = null;
if (http_enabled) {
  http_server = start_http_server();
}

if (stdio_enabled) {
  const transport = new StdioServerTransport();
  await server.connect(transport);
}

process.on("beforeExit", () => {
  http_server?.close();
  engine.close();
});
