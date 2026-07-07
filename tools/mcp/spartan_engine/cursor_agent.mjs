import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

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
  "world_load",
  "world_save",
  "world_set_environment",
  "world_raycast",
  "entity_list",
  "entity_find",
  "entity_resolve",
  "entity_get",
  "selection_get",
  "entity_create_empty",
  "entity_create_light",
  "entity_create_primitive",
  "entity_create_primitive_batch",
  "entity_update",
  "entity_delete",
  "entity_delete_children",
  "entity_select",
  "entity_set_transform",
  "entity_render_materials",
  "component_types",
  "primitive_types",
  "entity_add_component",
  "entity_remove_component",
  "component_get",
  "component_set",
  "execute_lua",
]);

let cached_agent = null;
let cached_agent_key = "";

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
    local: { cwd: __dirname, settingSources: [] },
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

function build_prompt(prompt, snapshot) {
  return [
    "You are controlling Spartan Engine through the spartan_engine MCP tools.",
    "Read agent_memory_read early when available, and treat it as project advice rather than absolute truth.",
    "For engine-control requests, use Spartan MCP tools first and keep tool calls minimal.",
    "For source-code questions, use search_codebase first, then read_source_file for focused line ranges.",
    "Use search_capabilities and get_capability_details when you are unsure which engine tool or resource to use.",
    "Use spartan_status when you need to know whether the MCP bridge, engine, or codebase index is ready.",
    "Use debug_log_read when diagnosing what commands the assistant sent to the engine and what came back.",
    "Use context_snapshot and entity_resolve instead of multiple separate read calls.",
    "Use camera_snapshot before camera-relative placement such as in front of camera, beside camera, or from camera.",
    "Use world_raycast for ground or surface-relative placement instead of assuming y=0 when precision matters.",
    "Before deleting or rebuilding existing geometry while preserving look, call entity_render_materials on the target parent and reuse material names in entity_create_primitive_batch or component_set.",
    "Use entity_create_light for lights, including area lights on ceilings, and set intensity, range, and area size to fit the room or blockout scale instead of leaving weak defaults.",
    "For repeated scene work, use one execute_lua script or a native batch tool.",
    "When you learn a durable lesson, correction, recurring problem, or maintainer improvement idea, update agent memory concisely.",
    "Do not reveal hidden chain of thought. Report only brief progress, blockers, and final results.",
    "Engine state snapshot:",
    JSON.stringify(snapshot),
    "",
    "User request:",
    prompt,
  ].join("\n");
}

export async function run_cursor_fallback({ prompt, api_key, model_id, engine_host, engine_port, run, timeout_ms, engine_first_timeout_ms }) {
  if (!api_key) {
    return {
      ok: false,
      text: "Cursor API key is missing. Paste it into the MCP Assistant window first.",
    };
  }

  let cursor_run = null;
  let engine_tool_seen = false;
  let cancel_message = "";
  let guard_timer = null;
  let idle_timer = null;
  let last_activity_at = Date.now();
  const observe = async (event) => {
    last_activity_at = Date.now();
    if (!engine_tool_seen && is_engine_tool_event(event)) {
      engine_tool_seen = true;
      run.event("stage_note", { text: "engine tool interaction confirmed" });
    } else if (!engine_tool_seen && is_tool_event(event)) {
      cancel_message = "cancelled, first tool activity was not a Spartan engine tool";
      if (cursor_run?.supports?.("cancel")) {
        await cursor_run.cancel();
      }
    }

    const activity = activity_from_event(event);
    if (activity) {
      run.event("stage_note", { text: activity });
    }
  };

  try {
    const agent = await run.stage("Prepare Cursor", "starting or reusing the Cursor agent", () => get_agent({ api_key, model_id, engine_host, engine_port, run }));
    const snapshot = await run.stage("Read Context", "reading engine state for Cursor", () => run.tool("context_snapshot"));
    const cursor_result = await run.stage("Plan And Act", "waiting for Cursor to use Spartan tools", async () => {
      // thinking and streaming count as activity, only a fully silent run that never touched an engine tool gets cancelled
      guard_timer = setInterval(() => {
        if (engine_tool_seen || cancel_message) {
          clearInterval(guard_timer);
          return;
        }
        if (Date.now() - last_activity_at < engine_first_timeout_ms) {
          return;
        }

        cancel_message = `cancelled, no Spartan engine tool was used and the run was silent for ${engine_first_timeout_ms}ms`;
        run.event("stage_note", { text: cancel_message });
        if (cursor_run?.supports?.("cancel")) {
          void cursor_run.cancel();
        }
      }, 1000);
      guard_timer.unref?.();

      cursor_run = await agent.send(build_prompt(prompt, snapshot), {
        onStep: ({ step }) => {
          void observe(step);
        },
      });
      run.receipt("cursor run", { id: cursor_run.id });

      const stream_task = cursor_run.stream ? (async () => {
        for await (const event of cursor_run.stream()) {
          await observe(event);
        }
      })().catch(() => {}) : Promise.resolve();

      // an active run is never killed, only one that stopped producing events for timeout_ms
      const result = await Promise.race([
        cursor_run.wait(),
        new Promise((_, reject) => {
          idle_timer = setInterval(() => {
            if (Date.now() - last_activity_at >= timeout_ms) {
              reject(new Error(`Cursor produced no activity within ${timeout_ms}ms.`));
            }
          }, 1000);
          idle_timer.unref?.();
        }),
      ]);
      await Promise.race([stream_task, new Promise((resolve) => setTimeout(resolve, 1000))]);
      return result;
    });

    if (cursor_result.status === "error") {
      const failure_message = await run_failure_message(cursor_run, cursor_result);
      run.receipt("cursor failure", {
        id: cursor_result.id ?? cursor_run?.id,
        status: cursor_result.status,
        detail: compact_line(failure_message),
      });
      await dispose_cached_agent();
      return { ok: false, text: failure_message };
    }

    if (cursor_result.status === "cancelled" || cancel_message) {
      return { ok: false, text: cancel_message || "Cursor run was cancelled." };
    }

    return { ok: true, text: cursor_result.result?.trim() || "Done." };
  } catch (error) {
    if (cursor_run?.supports?.("cancel") && error.message?.includes("within")) {
      await cursor_run.cancel();
    }

    if (error instanceof CursorAgentError) {
      return { ok: false, text: `Cursor startup failed: ${error.message}` };
    }

    return { ok: false, text: `Assistant failed: ${error.message}` };
  } finally {
    if (guard_timer) {
      clearTimeout(guard_timer);
    }
    if (idle_timer) {
      clearInterval(idle_timer);
    }
  }
}
