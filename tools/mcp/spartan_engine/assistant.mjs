#!/usr/bin/env node

import net from "node:net";
import { append_agent_memory } from "./agent_memory.mjs";
import { append_debug_log } from "./debug_log.mjs";
import { EngineClient } from "./engine_client.mjs";
import { run_cursor_fallback, list_models, dispose_cached_agent } from "./cursor_agent.mjs";
import { run_fast_path } from "./fast_paths.mjs";
import { route_intent } from "./intent_router.mjs";
import { make_run_id, parse_key_payload, parse_line, parse_prompt_payload, send_event, send_line } from "./protocol.mjs";

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
const engine_first_timeout_ms = Number.parseInt(process.env.SPARTAN_ASSISTANT_ENGINE_FIRST_TIMEOUT_MS ?? "60000", 10);
const read_only_mode = process.argv.includes("--read-only") || ["1", "true", "yes", "on"].includes(String(process.env.SPARTAN_ASSISTANT_READ_ONLY ?? process.env.SPARTAN_MCP_READ_ONLY ?? "").toLowerCase());
const mutating_tools = new Set([
  "engine_set_mode",
  "cvar_set",
  "world_load",
  "world_save",
  "world_set_environment",
  "entity_create_empty",
  "entity_create_light",
  "entity_create_primitive",
  "entity_create_primitive_batch",
  "entity_update",
  "entity_delete",
  "entity_delete_children",
  "entity_select",
  "entity_set_transform",
  "entity_add_component",
  "entity_remove_component",
  "component_set",
  "execute_lua",
]);

if (!Number.isInteger(assistant_port) || assistant_port <= 0 || assistant_port > 65535) {
  console.error("invalid assistant port");
  process.exit(1);
}

if (!Number.isInteger(engine_port) || engine_port <= 0 || engine_port > 65535) {
  console.error("invalid engine port");
  process.exit(1);
}

const engine = new EngineClient({
  host: engine_host,
  port: engine_port,
  timeout_ms: context_timeout_ms,
  source: "assistant",
});
void append_debug_log({
  type: "assistant_started",
  source: "assistant",
  port: assistant_port,
  engine_host,
  engine_port,
  client_features: {
    idle_socket_reuse: true,
    idle_close_ms: 250,
    command_timeout_names: true,
  },
});

const active_runs = new Map();
let prompt_chain = Promise.resolve();

function short_text(value, max_length = 180) {
  const text = String(value ?? "").replace(/\s+/g, " ").trim();
  return text.length <= max_length ? text : `${text.slice(0, max_length - 3)}...`;
}

function capability_gap_note(prompt, detail) {
  return `Capability gap: ${short_text(detail)} Prompt: "${short_text(prompt)}"`;
}

function sanitize_tool_args(args) {
  if (!args) {
    return {};
  }

  const sanitized = { ...args };
  if (typeof sanitized.code === "string") {
    sanitized.code = `${sanitized.code.length} lua chars`;
  }
  return sanitized;
}

class AssistantRun {
  constructor(socket, prompt) {
    this.socket = socket;
    this.prompt = prompt;
    this.id = make_run_id();
    this.started_at = Date.now();
    this.stage_index = 0;
    this.tool_index = 0;
    this.cancelled = false;
    this.current_stage_id = "";
  }

  elapsed_ms() {
    return Date.now() - this.started_at;
  }

  event(type, data = {}) {
    send_event(this.socket, {
      type,
      run_id: this.id,
      elapsed_ms: this.elapsed_ms(),
      ...data,
    });
  }

  start(intent) {
    this.event("run_started", {
      prompt: this.prompt,
      intent: intent.kind,
      confidence: intent.confidence,
    });
  }

  cancel(reason = "cancelled by user") {
    if (this.cancelled) {
      return;
    }

    this.cancelled = true;
    this.event("run_cancelled", { summary: reason });
  }

  throw_if_cancelled() {
    if (this.cancelled) {
      throw new Error("Run cancelled.");
    }
  }

  async stage(title, status, action) {
    this.throw_if_cancelled();
    const stage_id = `stage_${++this.stage_index}`;
    const previous_stage_id = this.current_stage_id;
    this.current_stage_id = stage_id;
    const started_at = Date.now();
    this.event("stage_started", {
      stage_id,
      title,
      status,
    });

    try {
      const result = await action();
      this.throw_if_cancelled();
      this.event("stage_finished", {
        stage_id,
        title,
        status: "done",
        duration_ms: Date.now() - started_at,
      });
      return result;
    } catch (error) {
      this.event("stage_finished", {
        stage_id,
        title,
        status: "failed",
        duration_ms: Date.now() - started_at,
        error: error.message,
      });
      throw error;
    } finally {
      this.current_stage_id = previous_stage_id;
    }
  }

  async tool(name, args = {}, timeout_ms = context_timeout_ms) {
    this.throw_if_cancelled();
    if (read_only_mode && mutating_tools.has(name)) {
      const result = {
        ok: false,
        error: "assistant is running in read-only mode",
        code: "read_only_mode",
        retryable: false,
        suggested_action: "restart the assistant without read-only mode to enable mutating tools",
      };
      this.event("tool_finished", {
        tool_id: `tool_${++this.tool_index}`,
        stage_id: this.current_stage_id,
        name,
        ok: false,
        duration_ms: 0,
        result,
      });
      return result;
    }

    const tool_id = `tool_${++this.tool_index}`;
    const started_at = Date.now();
    this.event("tool_started", {
      tool_id,
      stage_id: this.current_stage_id,
      name,
      args: sanitize_tool_args(args),
    });

    const result = await engine.command(name, args, timeout_ms);
    this.event("tool_finished", {
      tool_id,
      stage_id: this.current_stage_id,
      name,
      ok: Boolean(result.ok),
      duration_ms: Date.now() - started_at,
      result: summarize_tool_result(result),
    });
    if (!result.ok && String(result.error ?? "").toLowerCase().includes("unknown command")) {
      await this.report_capability_gap(`Native MCP command or tool \`${name}\` is missing from the engine bridge or Node registry.`);
    }
    if (!result.ok && (String(result.code ?? "") === "engine_timeout" || String(result.error ?? "").toLowerCase().includes("timed out"))) {
      await this.report_capability_gap(`Native MCP command \`${name}\` timed out. Error: ${result.error ?? "timeout"}`);
    }
    this.throw_if_cancelled();
    return result;
  }

  receipt(title, data = {}) {
    this.event("receipt", {
      title,
      data,
    });
  }

  finish(summary) {
    this.event("run_finished", {
      summary,
      duration_ms: this.elapsed_ms(),
    });
  }

  fail(summary) {
    this.event("run_failed", {
      summary,
      duration_ms: this.elapsed_ms(),
    });
  }

  async report_capability_gap(detail) {
    const note = capability_gap_note(this.prompt, detail);
    try {
      await append_agent_memory("Problem Reports", note);
      this.receipt("capability gap logged", {
        section: "Problem Reports",
        note,
      });
    } catch (error) {
      this.receipt("capability gap log failed", {
        error: error.message,
        note,
      });
    }
  }
}

function summarize_tool_result(result) {
  if (!result || typeof result !== "object") {
    return {};
  }

  if (!result.ok) {
    return { error: result.error ?? "failed" };
  }

  const summary = { ok: true };
  for (const key of ["id", "name", "deleted_count", "remaining_count", "created_count", "result", "source"]) {
    if (Object.prototype.hasOwnProperty.call(result, key)) {
      summary[key] = result[key];
    }
  }
  if (result.entity) {
    summary.entity = {
      id: result.entity.id,
      name: result.entity.name,
    };
  }
  if (Array.isArray(result.selected_ids)) {
    summary.selected_ids = result.selected_ids;
  }
  return summary;
}

function start_heartbeat(run, get_phase) {
  const interval = setInterval(() => {
    run.event("heartbeat", {
      stage_id: run.current_stage_id,
      status: get_phase() || "working",
    });
  }, 15000);
  interval.unref?.();
  return () => clearInterval(interval);
}

async function execute_prompt(socket, payload) {
  const run = new AssistantRun(socket, payload.prompt);
  active_runs.set(run.id, run);

  let phase = "starting";
  const stop_heartbeat = start_heartbeat(run, () => phase);
  const intent = route_intent(payload.prompt);
  run.start(intent);
  void append_debug_log({
    type: "assistant_prompt",
    source: "assistant",
    run_id: run.id,
    prompt: payload.prompt,
    intent,
  });

  try {
    let summary = null;
    await run.stage("Understand", "classifying the request", async () => {
      phase = `using ${intent.kind} path`;
      run.receipt("route selected", {
        intent: intent.kind,
        confidence: intent.confidence,
      });
    });

    phase = "running tool-first path";
    summary = await run_fast_path(run, intent, payload.prompt);

    if (summary === null) {
      if (intent.allow_cursor_fallback === false) {
        await run.report_capability_gap(`No deterministic scene operation matched intent ${intent.kind}. Add a native MCP tool or generic operation instead of using Cursor fallback.`);
        throw new Error("No deterministic Spartan scene operation matched this request, and live scene edits are not allowed to fall back to Cursor.");
      }

      await run.stage("Escalate", "no direct tool path matched, starting Cursor only now", async () => {
        run.receipt("cursor fallback", {
          reason: "no deterministic tool path matched this request",
          intent: intent.kind,
        });
      });

      phase = "using cursor fallback";
      const result = await run_cursor_fallback({
        prompt: payload.prompt,
        api_key: payload.api_key,
        model_id: payload.model_id,
        engine_host,
        engine_port,
        run,
        timeout_ms: run_timeout_ms,
        engine_first_timeout_ms,
      });
      if (!result.ok) {
        throw new Error(result.text);
      }
      summary = result.text;
    }

    run.finish(summary || "Done.");
    return { ok: true, text: summary || "Done." };
  } catch (error) {
    const text = error.message || "Assistant failed.";
    if (run.cancelled) {
      return { ok: false, text: text === "Run cancelled." ? "Run cancelled." : text };
    }

    run.fail(text);
    return { ok: false, text };
  } finally {
    stop_heartbeat();
    active_runs.delete(run.id);
  }
}

function cancel_active_run(reason) {
  let cancelled = false;
  for (const run of active_runs.values()) {
    run.cancel(reason);
    cancelled = true;
  }
  return cancelled;
}

const server = net.createServer((socket) => {
  let buffer = "";
  let owns_prompt = false;

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
        } else if (request.command === "cancel") {
          const cancelled = cancel_active_run("cancelled by user");
          send_line(socket, cancelled ? "ok" : "error", cancelled ? "Cancellation requested." : "No active run.");
        } else if (request.command !== "prompt") {
          send_line(socket, "error", `unknown command: ${request.command}`);
        } else {
          owns_prompt = true;
          const payload = parse_prompt_payload(request.value);
          prompt_chain = prompt_chain
            .then(() => execute_prompt(socket, payload))
            .catch((error) => ({ ok: false, text: `Assistant failed: ${error.message}` }));
          prompt_chain.then((result) => {
            send_line(socket, result.ok ? "ok" : "error", result.text);
          });
        }
      }

      newline = buffer.indexOf("\n");
    }
  });

  socket.on("close", () => {
    if (owns_prompt) {
      cancel_active_run("client disconnected");
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
  engine.close();
  void dispose_cached_agent();
});
