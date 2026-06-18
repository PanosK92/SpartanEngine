#!/usr/bin/env node

import net from "node:net";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const workspace_root = path.resolve(__dirname, "../../..");
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
  return Promise.race([
    promise,
    new Promise((_, reject) => {
      setTimeout(() => reject(new Error(message)), timeout_ms);
    }),
  ]);
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

async function run_prompt(prompt, api_key, model_id) {
  if (!api_key) {
    return {
      ok: false,
      text:
        "Cursor API key is missing. Paste it into the MCP Assistant window first.",
    };
  }

  let agent;
  try {
    agent = await Agent.create({
      apiKey: api_key,
      model: { id: model_id },
      local: { cwd: workspace_root },
      mcpServers: {
        spartan_engine: {
          type: "stdio",
          command: "node",
          args: [path.join(__dirname, "server.mjs"), `--host=${engine_host}`, `--port=${engine_port}`],
          cwd: __dirname,
        },
      },
    });

    const run = await agent.send(
      [
        "You are controlling Spartan Engine through MCP tools.",
        "Use the spartan_engine MCP tools whenever the user's request needs engine state or engine actions.",
        "Keep replies concise and describe what you changed or what blocked you.",
        "",
        prompt,
      ].join("\n"),
    );

    const result = await with_timeout(
      run.wait(),
      run_timeout_ms,
      "Cursor did the work but did not return a final message before timeout.",
    );
    if (result.status === "error") {
      return { ok: false, text: `Cursor run failed: ${result.id}` };
    }
    if (result.status === "cancelled") {
      return { ok: false, text: "Cursor run was cancelled." };
    }

    return { ok: true, text: result.result?.trim() || "Done." };
  } catch (error) {
    if (error.message?.includes("before timeout")) {
      return {
        ok: true,
        text: "I started the work, but Cursor did not return a final message before the timeout. Check the engine state and console.",
      };
    }

    if (error instanceof CursorAgentError) {
      return { ok: false, text: `Cursor startup failed: ${error.message}` };
    }

    return { ok: false, text: `Assistant failed: ${error.message}` };
  } finally {
    if (agent?.[Symbol.asyncDispose]) {
      await agent[Symbol.asyncDispose]();
    } else if (agent?.close) {
      await agent.close();
    }
  }
}

function send_line(socket, status, text) {
  socket.write(`${status} ${encode_value(text)}\n`);
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
          run_prompt(payload.prompt, payload.api_key, payload.model_id).then((result) => {
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
