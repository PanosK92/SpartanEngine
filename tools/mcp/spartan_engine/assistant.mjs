#!/usr/bin/env node

import net from "node:net";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const agent_cwd = __dirname;
const require_from_helper = createRequire(import.meta.url);
const { Agent, Cursor, CursorAgentError } = require_from_helper("@cursor/sdk");

function read_arg(name, fallback) {
  const prefix = `--${name}=`;
  const match = process.argv.find((arg) => arg.startsWith(prefix));
  return match ? match.slice(prefix.length) : fallback;
}

const assistant_port = Number.parseInt(read_arg("port", process.env.SPARTAN_ASSISTANT_PORT ?? "47778"), 10);
const engine_port = Number.parseInt(read_arg("engine-port", process.env.SPARTAN_ENGINE_PORT ?? "47777"), 10);
const engine_host = read_arg("engine-host", process.env.SPARTAN_ENGINE_HOST ?? "127.0.0.1");
const run_timeout_ms = Number.parseInt(process.env.SPARTAN_ASSISTANT_RUN_TIMEOUT_MS ?? "180000", 10);
const context_timeout_ms = Number.parseInt(process.env.SPARTAN_ASSISTANT_CONTEXT_TIMEOUT_MS ?? "2500", 10);

if (!Number.isInteger(assistant_port) || assistant_port <= 0 || assistant_port > 65535) {
  console.error("invalid assistant port");
  process.exit(1);
}

if (!Number.isInteger(engine_port) || engine_port <= 0 || engine_port > 65535) {
  console.error("invalid engine port");
  process.exit(1);
}

function encode_value(value) {
  return encodeURIComponent(String(value));
}

function decode_value(value) {
  try {
    return decodeURIComponent(value);
  } catch {
    return value;
  }
}

function parse_line(line) {
  const separator = line.indexOf(" ");
  if (separator === -1) {
    return { command: line, value: "" };
  }

  return {
    command: line.slice(0, separator),
    value: line.slice(separator + 1),
  };
}

function parse_prompt_payload(value) {
  const params = new URLSearchParams(value);
  const prompt = params.get("prompt");
  if (prompt !== null) {
    return {
      prompt,
      api_key: (params.get("api_key") ?? process.env.CURSOR_API_KEY ?? "").trim(),
      model_id: (params.get("model") ?? "auto").trim() || "auto",
    };
  }

  return {
    prompt: decode_value(value),
    api_key: (process.env.CURSOR_API_KEY ?? "").trim(),
    model_id: "auto",
  };
}

function parse_key_payload(value) {
  const params = new URLSearchParams(value);
  return (params.get("api_key") ?? process.env.CURSOR_API_KEY ?? "").trim();
}

function with_timeout(promise, timeout_ms, message) {
  let timeout_id;
  return Promise.race([
    promise,
    new Promise((_, reject) => {
      timeout_id = setTimeout(() => reject(new Error(message)), timeout_ms);
    }),
  ]).finally(() => clearTimeout(timeout_id));
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

function send_engine_command(command, args = {}, timeout_ms = context_timeout_ms) {
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

    socket.setTimeout(timeout_ms);

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
      finish({ ok: false, error: `engine context request timed out after ${timeout_ms}ms` });
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

async function list_models(api_key) {
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
    if (error instanceof CursorAgentError) {
      return { ok: false, text: `Cursor model list failed: ${error.message}` };
    }

    return { ok: false, text: `Cursor model list failed: ${error.message}` };
  }
}

let cached_agent = null;
let cached_agent_key = "";
let prompt_chain = Promise.resolve();

function agent_key(api_key, model_id) {
  return JSON.stringify({ api_key, model_id, engine_host, engine_port });
}

async function dispose_agent(agent) {
  if (!agent) {
    return;
  }

  if (agent?.[Symbol.asyncDispose]) {
    await agent[Symbol.asyncDispose]();
  } else if (agent?.close) {
    await agent.close();
  }
}

async function dispose_cached_agent() {
  const agent = cached_agent;
  cached_agent = null;
  cached_agent_key = "";
  await dispose_agent(agent);
}

async function get_agent(api_key, model_id, send_activity) {
  const key = agent_key(api_key, model_id);
  if (cached_agent && cached_agent_key === key) {
    return cached_agent;
  }

  await dispose_cached_agent();
  send_activity("starting cursor agent");
  cached_agent = await Agent.create({
    apiKey: api_key,
    model: { id: model_id },
    local: { cwd: agent_cwd, settingSources: [] },
    mcpServers: {
      spartan_engine: {
        type: "stdio",
        command: "node",
        args: [path.join(__dirname, "server.mjs"), `--host=${engine_host}`, `--port=${engine_port}`],
        cwd: __dirname,
      },
    },
  });
  cached_agent_key = key;
  return cached_agent;
}

function activity_from_event(event) {
  if (!event || typeof event !== "object") {
    return "";
  }

  if (event.type === "assistant") {
    return "writing response";
  }

  const name = event.name ?? event.toolName ?? event.tool_name ?? event.command ?? "";
  if (name) {
    return `running ${name}`;
  }

  if (event.type) {
    return `cursor ${String(event.type).replaceAll("_", " ")}`;
  }

  return "";
}

async function stream_run(run, send_activity) {
  if (!run?.stream) {
    return;
  }

  let last_activity = "";
  for await (const event of run.stream()) {
    const activity = activity_from_event(event);
    if (activity && activity !== last_activity) {
      send_activity(activity);
      last_activity = activity;
    }
  }
}

async function build_context_snapshot(send_activity) {
  send_activity("reading engine state");
  const status = await send_engine_command("engine_status");
  const world = await send_engine_command("world_summary");
  const selection = await send_engine_command("selection_get");

  return [
    "Current Spartan Engine state snapshot:",
    JSON.stringify({ status, world, selection }),
    "",
  ].join("\n");
}

function build_prompt(prompt, snapshot) {
  return [
    "You are controlling Spartan Engine through the spartan_engine MCP tools.",
    "Use these tools for any request that needs engine state or changes. Do not use workspace tools (grep, read, glob, shell, file editing) unless the user explicitly asks about source code.",
    "Engine edits require edit mode. If a tool reports the engine is playing or loading, report that blocker instead of guessing.",
    "",
    "SPEED RULE: every tool call is a slow round trip, so do the whole task in as few calls as possible.",
    "For anything beyond a single trivial edit (locating plus clearing plus building, grids, rows, repeated props, a whole playground), do it ALL in ONE execute_lua call. Do not chain entity_find, entity_delete, and create calls for these.",
    "Use the granular tools (entity_find, entity_create_primitive, component_set, etc.) only for a single tiny one-off change or a quick read.",
    "",
    "execute_lua runs Lua on the engine main thread. Verified bindings:",
    "  World.GetEntities() returns an array; World.CreateEntity() returns a new entity; World.RemoveEntity(e) deletes e and its descendants.",
    "  entity methods: GetName(), SetName(s), GetChildren() (array), GetParent(), SetParent(e), Clone(), SetPosition(Vector3(x,y,z)), SetScale(Vector3(x,y,z)), AddComponent(type), GetComponent(type), RemoveComponent(type).",
    "  Vector3(x,y,z) constructs a vector.",
    "  Component types are ComponentType.Renderable, ComponentType.Physics, ComponentType.Light, ComponentType.AudioSource. Note it is Renderable, not Render.",
    "  local r = e:AddComponent(ComponentType.Renderable); r:SetMesh(MeshType.Cube | Quad | Sphere | Cylinder | Cone); r:SetDefaultMaterial().",
    "  Color an object: local m = Material.New(); m:SetColor(red, green, blue, alpha); r:SetMaterial(m).",
    "  local p = e:AddComponent(ComponentType.Physics); p:SetBodyType(BodyType.Box | Sphere | Capsule | Plane); p:SetStatic(true or false); p:SetMass(n).",
    "  print(...) goes to the engine log; finish the script with return \"a short summary\".",
    "",
    "Typical compound build: scan World.GetEntities() for the entity whose GetName() matches the target, World.RemoveEntity each of its children, then loop to create the new colored, physics-enabled props and SetParent them under the target.",
    "Assign physics by role: floors, roads, walls, ramps, barriers, and platforms are static colliders; loose props like cones and balls are dynamic with SetStatic(false) and a small mass.",
    "Do one focused first pass, then stop and report what you built. Never run open-ended loops.",
    "Never invent entity ids, names, paths, or cvars. In compound requests resolve its/their to the named entity, not the current selection.",
    "Keep replies short: say what you changed or what blocked you.",
    "",
    snapshot,
    "User request:",
    prompt,
  ].join("\n");
}

async function run_prompt(prompt, api_key, model_id, send_activity) {
  if (!api_key) {
    return {
      ok: false,
      text:
        "Cursor API key is missing. Paste it into the MCP Assistant window first.",
    };
  }

  let run;
  try {
    const agent = await get_agent(api_key, model_id, send_activity);
    const snapshot = await build_context_snapshot(send_activity);

    send_activity("waiting for cursor");
    run = await agent.send(build_prompt(prompt, snapshot));
    const stream_task = stream_run(run, send_activity).catch(() => {});
    const result = await with_timeout(
      run.wait(),
      run_timeout_ms,
      `Cursor did not return a final message within ${run_timeout_ms}ms.`,
    );
    await Promise.race([stream_task, new Promise((resolve) => setTimeout(resolve, 1000))]);

    if (result.status === "error") {
      return { ok: false, text: `Cursor run failed: ${result.id}` };
    }
    if (result.status === "cancelled") {
      return { ok: false, text: "Cursor run was cancelled." };
    }

    return { ok: true, text: result.result?.trim() || "Done." };
  } catch (error) {
    if (error.message?.includes("within")) {
      if (run?.supports?.("cancel")) {
        await run.cancel();
      }

      return { ok: false, text: error.message };
    }

    if (error instanceof CursorAgentError) {
      return { ok: false, text: `Cursor startup failed: ${error.message}` };
    }

    return { ok: false, text: `Assistant failed: ${error.message}` };
  }
}

function send_line(socket, status, text) {
  if (socket.destroyed) {
    return;
  }

  try {
    socket.write(`${status} ${encode_value(text)}\n`);
  } catch {
  }
}

function make_activity_sender(socket) {
  let last_activity = "";
  return (activity) => {
    if (!activity || activity === last_activity || socket.destroyed) {
      return;
    }

    last_activity = activity;
    send_line(socket, "activity", activity);
  };
}

const server = net.createServer((socket) => {
  let buffer = "";

  socket.on("data", (chunk) => {
    buffer += chunk.toString("utf8");

    let newline = buffer.indexOf("\n");
    while (newline !== -1) {
      const line = buffer.slice(0, newline).trim();
      buffer = buffer.slice(newline + 1);

      if (line.length > 0) {
        const request = parse_line(line);
        if (request.command === "models") {
          const api_key = parse_key_payload(request.value);
          list_models(api_key).then((result) => {
            send_line(socket, result.ok ? "ok" : "error", result.text);
          });
        } else if (request.command !== "prompt") {
          send_line(socket, "error", `unknown command: ${request.command}`);
        } else {
          const payload = parse_prompt_payload(request.value);
          const send_activity = make_activity_sender(socket);
          prompt_chain = prompt_chain
            .then(() => run_prompt(payload.prompt, payload.api_key, payload.model_id, send_activity))
            .catch((error) => ({ ok: false, text: `Assistant failed: ${error.message}` }));
          prompt_chain.then((result) => {
            send_line(socket, result.ok ? "ok" : "error", result.text);
          });
        }
      }

      newline = buffer.indexOf("\n");
    }
  });
});

server.listen(assistant_port, "127.0.0.1", () => {
  console.error(`spartan assistant listening on 127.0.0.1:${assistant_port}`);
});

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => {
    dispose_cached_agent().finally(() => process.exit(0));
  });
}

process.on("beforeExit", () => {
  void dispose_cached_agent();
});
