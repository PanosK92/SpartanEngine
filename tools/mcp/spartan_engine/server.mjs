#!/usr/bin/env node

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { EngineClient } from "./engine_client.mjs";

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

if (!Number.isInteger(engine_port) || engine_port <= 0 || engine_port > 65535) {
  console.error("invalid spartan engine port");
  process.exit(1);
}

if (!Number.isInteger(engine_timeout_ms) || engine_timeout_ms <= 0) {
  console.error("invalid spartan engine timeout");
  process.exit(1);
}

const engine = new EngineClient({
  host: engine_host,
  port: engine_port,
  timeout_ms: engine_timeout_ms,
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

function register_tool(server, name, description, schema, command, options = {}) {
  server.registerTool(name, {
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

server.registerTool(
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

const transport = new StdioServerTransport();
await server.connect(transport);

process.on("beforeExit", () => {
  engine.close();
});
