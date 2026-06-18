#!/usr/bin/env node

import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { CodebaseIndex } from "./codebase_index.mjs";
import { EngineClient } from "./engine_client.mjs";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const project_root = path.resolve(__dirname, "../../..");

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
});

const codebase = new CodebaseIndex(project_root);
void codebase.ensure().catch((error) => {
  console.error(`spartan codebase indexing failed: ${error.message}`);
});

function send_engine_command(command, args = {}) {
  return engine.command(command, args, engine_timeout_ms);
}

function tool_result(result) {
  return {
    structuredContent: result,
    content: [
      {
        type: "text",
        text: JSON.stringify(result),
      },
    ],
    isError: !result.ok,
  };
}

const tool_registry = new Map();

function register_local_tool(name, config, handler) {
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
    annotations: options.annotations,
  }, async (args) => {
    const map_args = options.map_args ?? ((value) => value);
    const result = await send_engine_command(command, map_args(args));
    return tool_result(result);
  });
}

const vector3 = z.array(z.number()).length(3);
const quaternion = z.array(z.number()).length(4);
const vector4 = z.array(z.number()).length(4);
const mesh_type = z.enum(["cube", "quad", "plane", "sphere", "cylinder", "cone"]);
const body_type = z.enum(["box", "sphere", "plane", "capsule", "mesh", "mesh_convex", "controller", "vehicle", "cloth"]);
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
const component_value = z.union([z.string(), z.number(), z.boolean(), vector3, vector4]);
const primitive_create_args = {
  mesh: mesh_type.optional(),
  name: z.string().optional(),
  parent_id: z.string().optional(),
  position: vector3.optional(),
  rotation_euler: vector3.optional(),
  scale: vector3.optional(),
  with_physics: z.boolean().optional(),
  body_type: body_type.optional(),
  physics_static: z.boolean().optional(),
  physics_kinematic: z.boolean().optional(),
  physics_mass: z.number().optional(),
  physics_friction: z.number().optional(),
  physics_restitution: z.number().optional(),
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

register_local_tool("spartan_status", {
  title: "Spartan Status",
  description: "Return Spartan MCP bridge, engine, and codebase-index status.",
  inputSchema: {},
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
    engine: engine_status,
    codebase: codebase.status(),
  });
});

register_local_tool("search_codebase", {
  title: "Search Codebase",
  description: "Search Spartan source code using a local keyword index. Use this for source questions instead of filesystem tools.",
  inputSchema: {
    query: z.string(),
    top_k: z.number().int().min(1).max(25).optional(),
  },
  annotations: read_only,
}, async ({ query, top_k }) => {
  if (!query?.trim()) {
    return tool_result({
      ok: false,
      error: "query is required",
    });
  }

  const results = await codebase.search(query, top_k ?? 8);
  return tool_result({
    ok: true,
    ready: codebase.status().ready,
    query,
    results,
  });
});

register_tool(server, "ping", "Check that the Spartan live-control endpoint is reachable.", {}, "ping", {
  annotations: read_only,
});

register_tool(server, "engine_status", "Read editor/runtime status, frame metrics, and loading state.", {}, "engine_status", {
  annotations: read_only,
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
  { annotations: edit_tool },
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
  { annotations: read_only },
);

register_tool(server, "world_summary", "Read the current world name, path, counts, environment, and bounds.", {}, "world_summary", {
  annotations: read_only,
});

register_tool(server, "context_snapshot", "Read engine status, world summary, and current selection in one native request.", {}, "context_snapshot", {
  annotations: read_only,
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
  { annotations: edit_tool },
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
  { annotations: edit_tool },
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
  { annotations: read_only },
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
  { annotations: read_only },
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
  { annotations: read_only },
);

register_tool(
  server,
  "entity_get",
  "Read a single entity by id.",
  {
    id: z.string(),
  },
  "entity_get",
  { annotations: read_only },
);

register_tool(server, "selection_get", "Read the selected entity ids.", {}, "selection_get", {
  annotations: read_only,
});

register_tool(server, "component_types", "List valid Spartan component type names.", {}, "component_types", {
  annotations: read_only,
});

register_tool(server, "primitive_types", "List valid Spartan built-in primitive mesh names and aliases.", {}, "primitive_types", {
  annotations: read_only,
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
  { annotations: edit_tool },
);

register_tool(
  server,
  "entity_create_primitive",
  "Create a primitive render entity in edit mode, optionally with transform, parent, and physics.",
  primitive_create_args,
  "entity_create_primitive",
  { annotations: edit_tool },
);

register_local_tool(
  "entity_create_primitive_batch",
  {
    title: "entity create primitive batch",
    description: "Create many primitive render entities in edit mode. Uses one persistent engine connection but still applies each primitive as a separate engine command.",
    inputSchema: {
      items: z.array(z.object(primitive_create_args)).min(1).max(64),
    },
    annotations: edit_tool,
  },
  async ({ items }) => {
    const created = [];
    for (const item of items) {
      const result = await send_engine_command("entity_create_primitive", item);
      if (!result.ok) {
        return tool_result({
          ok: false,
          error: result.error ?? "failed to create primitive",
          created_count: created.length,
          created,
          failed_item: item,
        });
      }

      created.push({
        id: result.entity?.id,
        name: result.entity?.name,
      });
    }

    return tool_result({
      ok: true,
      created_count: created.length,
      created,
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
  { annotations: edit_tool },
);

register_tool(
  server,
  "entity_delete",
  "Delete an entity in edit mode.",
  {
    id: z.string(),
  },
  "entity_delete",
  { annotations: destructive_tool },
);

register_tool(
  server,
  "entity_delete_children",
  "Delete all direct children of an entity immediately in edit mode and return any remaining direct children.",
  {
    id: z.string(),
  },
  "entity_delete_children",
  { annotations: destructive_tool },
);

register_tool(
  server,
  "entity_select",
  "Select an entity by id in edit mode.",
  {
    id: z.string(),
  },
  "entity_select",
  { annotations: edit_tool },
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
  { annotations: edit_tool },
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
  { annotations: edit_tool },
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
  { annotations: destructive_tool },
);

register_tool(
  server,
  "component_get",
  "Read editable properties for a component on an entity.",
  {
    id: z.string(),
    type: component_type,
  },
  "component_get",
  { annotations: read_only },
);

register_tool(
  server,
  "component_set",
  "Set one editable component property on an entity. Vector and color values are arrays.",
  {
    id: z.string(),
    type: component_type,
    property: z.string(),
    value: component_value,
  },
  "component_set",
  { annotations: edit_tool },
);

register_tool(
  server,
  "execute_lua",
  [
    "Run a Lua script inside the engine in a single call, using the engine's Lua bindings (World, Entity, Render, Physics, Light, ParticleSystem, WorldHelpers, Timer, ComponentType, etc.).",
    "Best for procedural or multi-step scene work: write ONE script with loops and math (grids, repeated props, whole playgrounds) instead of many individual tool calls.",
    "Runs on the engine main thread. Use print(...) for diagnostics (read it back with console_read) and return a short summary string describing what you built.",
  ].join(" "),
  {
    code: z.string(),
  },
  "execute_lua",
  { annotations: destructive_tool },
);

function is_zod_optional(schema) {
  return schema?._def?.typeName === "ZodOptional" || schema?._def?.typeName === "ZodDefault";
}

function zod_to_json_schema(schema) {
  if (!schema?._def) {
    return schema && typeof schema === "object" ? schema : {};
  }

  const type_name = schema._def.typeName;
  if (type_name === "ZodOptional" || type_name === "ZodDefault") {
    return zod_to_json_schema(schema._def.innerType);
  }
  if (type_name === "ZodString") {
    return { type: "string" };
  }
  if (type_name === "ZodBoolean") {
    return { type: "boolean" };
  }
  if (type_name === "ZodNumber") {
    const is_integer = Array.isArray(schema._def.checks) && schema._def.checks.some((check) => check.kind === "int");
    return { type: is_integer ? "integer" : "number" };
  }
  if (type_name === "ZodEnum") {
    return { type: "string", enum: schema._def.values };
  }
  if (type_name === "ZodArray") {
    return { type: "array", items: zod_to_json_schema(schema._def.type ?? schema._def.element) };
  }
  if (type_name === "ZodObject") {
    const shape = typeof schema._def.shape === "function" ? schema._def.shape() : schema._def.shape;
    return raw_shape_to_json_schema(shape);
  }
  if (type_name === "ZodUnion") {
    return { anyOf: schema._def.options.map((option) => zod_to_json_schema(option)) };
  }

  return {};
}

function raw_shape_to_json_schema(shape) {
  if (shape?.type === "object" && shape.properties) {
    return shape;
  }

  const properties = {};
  const required = [];
  for (const [name, schema] of Object.entries(shape ?? {})) {
    properties[name] = zod_to_json_schema(schema);
    if (!is_zod_optional(schema)) {
      required.push(name);
    }
  }

  const json_schema = {
    type: "object",
    properties,
  };
  if (required.length > 0) {
    json_schema.required = required;
  }
  return json_schema;
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
      entry.outputSchema = tool.outputSchema;
    }
    if (tool.annotations) {
      entry.annotations = tool.annotations;
    }
    return entry;
  });
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

  if (method === "tools/call") {
    const name = message.params?.name;
    const args = message.params?.arguments ?? {};
    const tool = tool_registry.get(name);
    if (!tool) {
      return json_rpc_error(id, -32602, `unknown tool: ${name}`);
    }

    try {
      const result = await tool.handler(args);
      return json_rpc_result(id, result);
    } catch (error) {
      return json_rpc_result(id, tool_result({
        ok: false,
        error: error.message,
      }));
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
