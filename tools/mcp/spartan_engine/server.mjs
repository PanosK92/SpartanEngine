#!/usr/bin/env node

import net from "node:net";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

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

function protocol_value(value) {
  if (Array.isArray(value)) {
    return value.join(",");
  }

  if (typeof value === "boolean") {
    return value ? "true" : "false";
  }

  return String(value);
}

function command_line(command, args = {}) {
  const parts = [command];
  for (const [key, value] of Object.entries(args)) {
    if (value === undefined || value === null) {
      continue;
    }

    parts.push(`${key}=${encodeURIComponent(protocol_value(value))}`);
  }

  return `${parts.join(" ")}\n`;
}

function send_engine_command(command, args = {}) {
  return new Promise((resolve) => {
    const socket = net.createConnection({ host: engine_host, port: engine_port });
    let buffer = "";
    let finished = false;

    function finish(result) {
      if (finished) {
        return;
      }

      finished = true;
      socket.destroy();
      resolve(result);
    }

    socket.setTimeout(engine_timeout_ms);

    socket.on("connect", () => {
      socket.write(command_line(command, args));
    });

    socket.on("data", (chunk) => {
      buffer += chunk.toString("utf8");
      const newline = buffer.indexOf("\n");
      if (newline === -1) {
        return;
      }

      const line = buffer.slice(0, newline).trim();
      try {
        finish(JSON.parse(line));
      } catch (error) {
        finish({ ok: false, error: `invalid engine response, ${error.message}` });
      }
    });

    socket.on("timeout", () => {
      finish({ ok: false, error: `engine request timed out after ${engine_timeout_ms}ms` });
    });

    socket.on("error", (error) => {
      finish({ ok: false, error: `engine connection failed, ${error.message}` });
    });

    socket.on("end", () => {
      if (!finished) {
        finish({ ok: false, error: "engine connection closed" });
      }
    });
  });
}

function tool_result(result) {
  return {
    content: [
      {
        type: "text",
        text: JSON.stringify(result),
      },
    ],
    isError: !result.ok,
  };
}

function register_tool(server, name, description, schema, command, map_args = (args) => args) {
  server.tool(name, description, schema, async (args) => {
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

register_tool(server, "ping", "Check that the Spartan live-control endpoint is reachable.", {}, "ping");

register_tool(server, "engine_status", "Read editor/runtime status, frame metrics, and loading state.", {}, "engine_status");

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
);

register_tool(server, "cvar_list", "List registered console variables.", {}, "cvar_list");

register_tool(
  server,
  "cvar_get",
  "Read a console variable by name.",
  {
    name: z.string(),
  },
  "cvar_get",
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
);

register_tool(server, "world_summary", "Read the current world name, path, counts, environment, and bounds.", {}, "world_summary");

register_tool(
  server,
  "world_load",
  "Load an absolute .world file path.",
  {
    path: z.string(),
  },
  "world_load",
);

register_tool(
  server,
  "world_save",
  "Save the current world, optionally to an absolute .world file path.",
  {
    path: z.string().optional(),
  },
  "world_save",
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
);

register_tool(
  server,
  "entity_get",
  "Read a single entity by id.",
  {
    id: z.string(),
  },
  "entity_get",
);

register_tool(server, "selection_get", "Read the selected entity ids.", {}, "selection_get");

register_tool(server, "component_types", "List valid Spartan component type names.", {}, "component_types");

register_tool(server, "primitive_types", "List valid Spartan built-in primitive mesh names and aliases.", {}, "primitive_types");

register_tool(
  server,
  "entity_create_empty",
  "Create an empty entity in edit mode.",
  {
    name: z.string().optional(),
    parent_id: z.string().optional(),
  },
  "entity_create_empty",
);

register_tool(
  server,
  "entity_create_primitive",
  "Create a primitive render entity in edit mode, optionally with transform, parent, and physics.",
  primitive_create_args,
  "entity_create_primitive",
);

server.tool(
  "entity_create_primitive_batch",
  "Create many primitive render entities in edit mode. Use this instead of repeated single creates for layouts.",
  {
    items: z.array(z.object(primitive_create_args)).min(1).max(64),
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
);

register_tool(
  server,
  "entity_delete",
  "Delete an entity in edit mode.",
  {
    id: z.string(),
  },
  "entity_delete",
);

register_tool(
  server,
  "entity_delete_children",
  "Delete all direct children of an entity in edit mode.",
  {
    id: z.string(),
  },
  "entity_delete_children",
);

register_tool(
  server,
  "entity_select",
  "Select an entity by id in edit mode.",
  {
    id: z.string(),
  },
  "entity_select",
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
);

const transport = new StdioServerTransport();
await server.connect(transport);
