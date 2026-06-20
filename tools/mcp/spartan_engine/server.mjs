#!/usr/bin/env node

import http from "node:http";
import fs from "node:fs/promises";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { EngineClient } from "./engine_client.mjs";
import { append_debug_log, debug_log_path, read_debug_log } from "./debug_log.mjs";
import { get_project_root, get_shared_codebase } from "./shared_codebase.mjs";
import { component_schema_markdown, edit_rules, engine_overview, search_capability_catalog } from "./knowledge.mjs";
import { json_schema_from_raw_shape, normalize_result, output_schemas, parse_raw_shape, structured_error } from "./schemas.mjs";
import { agent_memory_path, append_agent_memory, ensure_agent_memory, read_agent_memory, write_agent_memory } from "./agent_memory.mjs";

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

function send_engine_command(command, args = {}) {
  return engine.command(command, args, engine_timeout_ms);
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
  const tool = {
    name,
    title: config.title ?? name.replaceAll("_", " "),
    description: config.description ?? "",
    inputSchema: input_schema,
    outputSchema: config.outputSchema,
    annotations: config.annotations,
    handler,
  };

  tool_registry.set(name, tool);
  server.registerTool(name, {
    title: tool.title,
    description: tool.description,
    inputSchema: input_schema,
    outputSchema: tool.outputSchema,
    annotations: tool.annotations,
  }, handler);
}

function register_tool(server, name, description, schema, command, options = {}) {
  register_local_tool(name, {
    title: options.title ?? name.replaceAll("_", " "),
    description,
    inputSchema: schema,
    outputSchema: options.outputSchema ?? output_schemas.generic,
    annotations: options.annotations,
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
const quaternion = z.array(z.number()).length(4);
const vector4 = z.array(z.number()).length(4);
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
]);
const component_value = z.union([z.string(), z.number(), z.boolean(), numeric_array]);
const light_type = z.enum(["directional", "point", "spot", "area"]);
const primitive_create_args = {
  mesh: mesh_type.optional(),
  name: z.string().optional(),
  parent_id: z.string().optional(),
  position: vector3.optional(),
  rotation_euler: vector3.optional(),
  scale: vector3.optional(),
  material: z.string().optional(),
  with_physics: z.boolean().optional(),
  body_type: body_type.optional(),
  physics_static: z.boolean().optional(),
  physics_kinematic: z.boolean().optional(),
  physics_mass: z.number().optional(),
  physics_friction: z.number().optional(),
  physics_restitution: z.number().optional(),
};

function calibrated_light_defaults(type)
{
  if (type === "area")
  {
    return {
      light_type: "area",
      color: [1.0, 0.93, 0.82, 1.0],
      temperature: 3200,
      intensity: 5000,
      range: 24,
      area_width: 5,
      area_height: 3,
      shadows: true,
    };
  }

  if (type === "spot")
  {
    return {
      light_type: "spot",
      color: [1.0, 0.94, 0.84, 1.0],
      temperature: 3500,
      intensity: 2500,
      range: 24,
      angle_degrees: 45,
      shadows: true,
    };
  }

  if (type === "directional")
  {
    return {
      light_type: "directional",
      color: [1.0, 0.96, 0.9, 1.0],
      temperature: 5500,
      intensity: 8,
      shadows: true,
    };
  }

  return {
    light_type: "point",
    color: [1.0, 0.92, 0.78, 1.0],
    temperature: 3200,
    intensity: 1200,
    range: 14,
    shadows: true,
  };
}

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
  "Set the editor camera position and rotation, or look at a target point.",
  {
    position: vector3.optional(),
    rotation_euler: vector3.optional(),
    target: vector3.optional(),
  },
  "camera_set_view",
  { annotations: edit_tool, outputSchema: output_schemas.camera_snapshot },
);

register_local_tool("screenshot_take", {
  title: "screenshot take",
  description: "Request a renderer screenshot and return the target PNG path. If the async save completes quickly, the PNG is also returned as image content.",
  inputSchema: {
    path: z.string().optional(),
    wait_ms: z.number().int().min(0).max(10000).optional(),
  },
  outputSchema: output_schemas.screenshot_take,
  annotations: read_only,
}, async (args) => {
  const result = await send_engine_command("screenshot_take", { path: args.path });
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
      image_buffer = await fs.readFile(result.path);
      break;
    }
    catch
    {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
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
  "Save the current world, optionally to an absolute .world file path.",
  {
    path: z.string().optional(),
  },
  "world_save",
  { annotations: edit_tool, outputSchema: output_schemas.generic },
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
  "Cast a ray against static world physics and return the hit position and entity.",
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
  "Find entities by name without listing the whole scene.",
  {
    name: z.string(),
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
    type: component_type,
    limit: z.number().int().min(1).max(1000).optional(),
    offset: z.number().int().min(0).optional(),
  },
  "entity_find_by_component",
  { annotations: read_only, outputSchema: output_schemas.entity_list },
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
  "Read a single entity by id.",
  {
    id: z.string(),
  },
  "entity_get",
  { annotations: read_only, outputSchema: output_schemas.entity },
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
  },
  "entity_create_empty",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_local_tool(
  "entity_create_light",
  {
    title: "entity create light",
    description: "Create a light entity in edit mode and configure common light properties in one generic operation. By default, omitted light values are calibrated to usable per-type values; pass calibrated false to leave engine defaults.",
    inputSchema: {
      name: z.string().optional(),
      parent_id: z.string().optional(),
      position: vector3.optional(),
      rotation_euler: vector3.optional(),
      scale: vector3.optional(),
      light_type: light_type.optional(),
      color: vector4.optional(),
      temperature: z.number().optional(),
      intensity: z.number().optional(),
      range: z.number().optional(),
      angle_degrees: z.number().optional(),
      area_width: z.number().optional(),
      area_height: z.number().optional(),
      shadows: z.boolean().optional(),
      volumetric: z.boolean().optional(),
      draw_distance: z.number().optional(),
      shadow_distance: z.number().optional(),
      volumetric_distance: z.number().optional(),
      calibrated: z.boolean().optional(),
    },
    outputSchema: output_schemas.entity,
    annotations: edit_tool,
  },
  async (args) => {
    const create_args = {
      name: args.name ?? "light",
    };
    if (args.parent_id)
    {
      create_args.parent_id = args.parent_id;
    }

    const created = await send_engine_command("entity_create_empty", create_args);
    if (!created.ok)
    {
      return tool_result(created);
    }

    const id = created.entity?.id;
    if (!id)
    {
      return tool_result(structured_error("entity_create_empty returned no entity id", { code: "engine_invalid_response" }));
    }

    const transform_args = { id };
    for (const key of ["position", "rotation_euler", "scale"])
    {
      if (args[key] !== undefined)
      {
        transform_args[key] = args[key];
      }
    }
    if (Object.keys(transform_args).length > 1)
    {
      const transformed = await send_engine_command("entity_set_transform", transform_args);
      if (!transformed.ok)
      {
        return tool_result(transformed);
      }
    }

    const added = await send_engine_command("entity_add_component", { id, type: "light" });
    if (!added.ok)
    {
      return tool_result(added);
    }

    const effective_light_type = args.light_type ?? "point";
    const defaults = args.calibrated === false ? {} : calibrated_light_defaults(effective_light_type);
    const properties = {
      light_type: args.light_type ?? defaults.light_type,
      color: args.color ?? defaults.color,
      temperature: args.temperature ?? defaults.temperature,
      intensity: args.intensity ?? defaults.intensity,
      range: args.range ?? defaults.range,
      angle_degrees: args.angle_degrees ?? defaults.angle_degrees,
      area_width: args.area_width ?? defaults.area_width,
      area_height: args.area_height ?? defaults.area_height,
      shadows: args.shadows ?? defaults.shadows,
      volumetric: args.volumetric ?? defaults.volumetric,
      draw_distance: args.draw_distance ?? defaults.draw_distance,
      shadow_distance: args.shadow_distance ?? defaults.shadow_distance,
      volumetric_distance: args.volumetric_distance ?? defaults.volumetric_distance,
    };

    for (const [property, value] of Object.entries(properties))
    {
      if (value === undefined || value === null)
      {
        continue;
      }

      const configured = await send_engine_command("component_set", { id, type: "light", property, value });
      if (!configured.ok)
      {
        return tool_result(configured);
      }
    }

    const final_entity = await send_engine_command("entity_get", { id });
    return tool_result(final_entity.ok ? final_entity : created);
  },
);

register_tool(
  server,
  "entity_create_primitive",
  "Create a primitive render entity in edit mode, optionally with transform, parent, and physics.",
  primitive_create_args,
  "entity_create_primitive",
  { annotations: edit_tool, outputSchema: output_schemas.entity },
);

register_local_tool(
  "entity_create_primitive_batch",
  {
    title: "entity create primitive batch",
    description: "Create many primitive render entities in edit mode through one native engine batch command.",
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

register_tool(
  server,
  "entity_update",
  "Rename, activate, deactivate, or reparent an entity in edit mode. Use parent_id root to detach.",
  {
    id: z.string(),
    name: z.string().optional(),
    active: z.boolean().optional(),
    parent_id: z.string().optional(),
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
  "Frame the current selection or a specific entity in the editor camera.",
  {
    id: z.string().optional(),
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
    "Supported actions include terrain generate; spline generate_road_mesh, clear_road_mesh, spawn_instances, clear_instances; particle_system apply_preset and trigger_burst; physics apply_force and vehicle utility actions; audio_source play and stop; light fit_to_mesh; camera focus_selected.",
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
    option: z.enum(["aabb", "picking_ray", "grid", "transform_handle", "selection_outline", "lights", "audio_sources", "performance_metrics", "physics", "wireframe", "meshlet_visualize", "cluster_visualize"]),
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
  "execute_lua",
  [
    "Run a Lua script inside the engine in a single call, using the engine's Lua bindings (World, Entity, Render, Physics, Light, ParticleSystem, WorldHelpers, Timer, ComponentType, etc.).",
    "Best for procedural or multi-step scene work: write ONE script with loops and math (grids, repeated props, whole layouts) instead of many individual tool calls.",
    "Runs on the engine main thread. Use print(...) for diagnostics (read it back with console_read) and return a short summary string describing what you built.",
  ].join(" "),
  {
    code: z.string(),
  },
  "execute_lua",
  { annotations: destructive_tool, outputSchema: output_schemas.lua_result },
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
