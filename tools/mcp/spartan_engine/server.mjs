#!/usr/bin/env node

import net from "node:net";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

const defaults = {
  host: process.env.SPARTAN_ENGINE_HOST ?? "127.0.0.1",
  port: Number.parseInt(process.env.SPARTAN_ENGINE_PORT ?? "47777", 10),
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

if (!Number.isInteger(engine_port) || engine_port <= 0 || engine_port > 65535) {
  console.error("invalid spartan engine port");
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

    socket.setTimeout(10000);

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
      finish({ ok: false, error: "engine request timed out" });
    });

    socket.on("error", (error) => {
      finish({ ok: false, error: error.message });
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
        text: JSON.stringify(result, null, 2),
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

register_tool(server, "entity_list", "List entities with ids, names, transforms, and component names.", {}, "entity_list");

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

const transport = new StdioServerTransport();
await server.connect(transport);
