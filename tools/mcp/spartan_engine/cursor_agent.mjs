import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import {
  advanced_scene_tool_names,
  audit_scene_quality,
  infer_required_features,
  scene_quality_prompt_lines,
} from "./scene_quality.mjs";
import {
  audit_scene_layout,
  make_scene_plan_namespace,
} from "./scene_planning.mjs";
import {
  infer_design_template,
} from "./design_intelligence.mjs";
import { get_project_root } from "./shared_codebase.mjs";

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
  "lights_calibrate",
  "world_landmarks",
  "spline_create_road",
  "spline_set_control_points",
  "spline_connect",
  "spline_reroute",
  "spline_junction",
  "spline_decorate",
  "district_blockout",
  "city_blockout",
  "entity_create_primitive",
  "entity_create_primitive_batch",
  "entity_update",
  "entity_delete",
  "entity_delete_children",
  "entity_clone",
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
for (const tool_name of advanced_scene_tool_names)
{
  engine_tool_names.add(tool_name);
}

let cached_agent = null;
let cached_agent_key = "";
let agent_run_queue = Promise.resolve();

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

function object_contains(
  value,
  predicate,
  seen = new Set(),
  depth = 0,
) {
  if (
    value === undefined ||
    value === null ||
    typeof value !== "object" ||
    seen.has(value) ||
    depth > 8
  )
  {
    return false;
  }
  seen.add(value);
  if (predicate(value))
  {
    return true;
  }
  if (Array.isArray(value))
  {
    return value.some((entry) =>
      object_contains(
        entry,
        predicate,
        seen,
        depth + 1,
      ),
    );
  }
  return Object.values(value).some((entry) =>
    object_contains(
      entry,
      predicate,
      seen,
      depth + 1,
    ),
  );
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

function is_named_tool_event(value, tool_name) {
  if (!is_tool_event(value))
  {
    return false;
  }

  return value_contains(value, (text) =>
    text.toLowerCase().replaceAll("-", "_").includes(
      tool_name,
    ),
  );
}

function build_prompt(prompt, snapshot, intent = null) {
  const lines = [
    "You are controlling Spartan Engine through the spartan_engine MCP tools.",
    "Read agent_memory_read early when available, and treat it as project advice rather than absolute truth.",
    "For engine-control requests, use Spartan MCP tools first and group repetitive calls without sacrificing completeness or visual quality.",
    "For source-code questions, use search_codebase first, then read_source_file for focused line ranges.",
    "Use search_capabilities and get_capability_details when you are unsure which engine tool or resource to use.",
    "Use spartan_status when you need to know whether the MCP bridge, engine, or codebase index is ready.",
    "Use debug_log_read when diagnosing what commands the assistant sent to the engine and what came back.",
    "Use context_snapshot and entity_resolve instead of multiple separate read calls.",
    "Use camera_snapshot before camera-relative placement such as in front of camera, beside camera, or from camera.",
    "Use world_raycast for ground or surface-relative placement instead of assuming y=0 when precision matters.",
    "Before deleting or rebuilding existing geometry while preserving look, call entity_render_materials on the target parent and reuse material names in entity_create_primitive_batch or component_set.",
    "Use mesh_geometry_capabilities before deciding that requested procedural geometry is unavailable.",
    "Prefer concave extruded profiles, multi-opening walls, variable lofts or sweeps, shell thickness, and seam-split box UVs when they express the design better than stacked boxes.",
    "For multiple materials, split semantic surfaces into compound parts because one render entity owns one material.",
    "Every created renderable must have static collision. Use mesh_convex for generated or imported meshes and the matching box, sphere, capsule, or plane collider for standard primitives. Never leave collision coverage partial.",
    "Use entity_create_light for every light. Never hand-roll lights with entity_create_empty + entity_add_component light + component_set; that path leaves weak invisible lights.",
    "entity_create_light fully initializes the light: intensity is lux for directional and lumens otherwise. Visible blockout defaults are point/spot 8500, area 12000, directional 120000, plus range, angle, area size, shadows, and draw/shadow distances.",
    "Do not pass tiny intensities like 25-100 for blockout lights. If you omit intensity, the tool calibrates it. Only set calibrated false when you intentionally want a dim light.",
    "To calibrate existing scene lights, call lights_calibrate once. Do not write execute_lua or dozens of component_set calls for that.",
    "For city development: massing first, roads second. Use city_blockout / district_blockout for districts; never hand-place hundreds of cubes for a city.",
    "district_blockout presets: market, downtown/skyscrapers, park, industrial, residential, parking, plaza, gas_station. city_blockout lays several districts with corridor gaps and avoid_existing landmarks.",
    "Architect rules: leave corridors between districts for arterials; do not stamp on runway/existing landmarks; vary density by preset.",
    "Road pass after massing: world_landmarks -> arterial that skirts large districts -> spur branches to district edges -> spline_junction -> spline_decorate. Never triangle center-to-center through an airway.",
    "To fix an existing road that cuts through buildings or other roads, call spline_reroute on it. It skirts obstacles and redistributes lights/cameras/props along the new path without deleting them.",
    "Never drive through an airway/runway, dockyard footprint, or building mass. Approach district edges, not centers. Use via points when an arterial must go around a district.",
    "spline_decorate adds sidewalks, street lights, and roadside props. Never stop at bare undecorated lines.",
    "Never hand-build spline_point_* children. Do not search source code for city prompts. Do not invent Lua APIs.",
    "Use primitive-only single-area construction only when the user explicitly asks for a greybox. Normal environments require semantic planning, generated or compound geometry, materials, calibrated lighting, and correction audits.",
    "Do not use execute_lua for API discovery, pairs/next probing, method listing, or exploratory scripts. Those crash or hang the engine.",
    "Prefer entity_create_primitive_batch over execute_lua for repeated primitives. Use execute_lua only when a native batch tool cannot express the edit, and then only with one focused script that uses known bindings.",
    "Known Lua facts if you must use it: World.CreateEntity, World.GetEntityByName, World.GetEntityById(id_string), entity:SetParent, entity:AddComponent(ComponentType.Renderable|Light|...), Renderable:SetMesh(MeshType.Cube), Light:SetLightType(LightType.Point), never pairs() on World.GetEntities or GetChildren, use ForEachChild instead.",
    "When you learn a durable lesson, correction, recurring problem, or maintainer improvement idea, update agent memory concisely.",
    "After finished scene construction, call world_resources_clean so the managed world resources directory contains only live mesh, material, and texture dependencies.",
    "Do not reveal hidden chain of thought. Report only brief progress, blockers, and final results.",
  ];

  if (intent?.kind === "scene_rebuild" || intent?.live_scene_action)
  {
    lines.push(...scene_quality_prompt_lines(prompt, intent));
  }

  if (intent?.target_name)
  {
    lines.push(`Resolved parent entity name from the request: ${intent.target_name}. Call entity_find with exact matching first. If several entities share the name, use the first root-level match by id and never call entity_resolve by ambiguous name. Create the root with entity_create_empty only when no exact match exists, then parent all planned environment content under that entity id.`);
  }
  if (intent?.kind === "city_develop")
  {
    lines.push("This is a city-planning request. If the user wants districts/areas/blockout, use city_blockout or district_blockout first. If they want roads, plan an arterial that skirts large footprints and spur to edges — never a triangle through centers. Massing and roads can be separate passes.");
    if (Array.isArray(intent.landmarks) && intent.landmarks.length > 0)
    {
      lines.push(`Landmarks mentioned in the prompt: ${intent.landmarks.join(", ")}. Prefer these, but still scan world_landmarks and use their bounding boxes for edge approaches.`);
    }
  }
  if (intent?.kind === "scene_rebuild" || intent?.live_scene_action)
  {
    lines.push("This is a live scene construction request. Build a finished, visually reviewed scene under the requested parent. Do not search source code and do not invent Lua APIs.");
  }

  lines.push(
    "Engine state snapshot:",
    JSON.stringify(snapshot),
    "",
    "User request:",
    prompt,
  );
  return lines.join("\n");
}

async function run_cursor_fallback_serial({ prompt, api_key, model_id, engine_host, engine_port, run, timeout_ms, engine_first_timeout_ms, intent = null }) {
  const request_started_at_ms = Date.now();
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
  let visual_review_seen = false;
  const observe = async (event) => {
    last_activity_at = Date.now();
    const is_visual_review = is_named_tool_event(
      event,
      "scene_visual_review",
    );
    const successful_visual_review =
      is_visual_review &&
      object_contains(event, (value) =>
        value.ok === true &&
        Array.isArray(value.views) &&
        value.views.length >= 2 &&
        value.views.every((review) =>
          review?.camera?.ok === true &&
          review?.screenshot?.ok === true &&
          review?.screenshot?.ready === true,
        ),
      );
    visual_review_seen ||=
      successful_visual_review;
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
  const execute_agent_prompt = async (agent, prompt_text) => {
    last_activity_at = Date.now();
    cursor_run = await agent.send(prompt_text, {
      onStep: ({ step }) => {
        void observe(step);
      },
    });
    run.receipt("cursor run", { id: cursor_run.id });

    const stream_task = cursor_run.stream ? (async () => {
      for await (const event of cursor_run.stream())
      {
        await observe(event);
      }
    })().catch(() => {}) : Promise.resolve();

    const result = await Promise.race([
      cursor_run.wait(),
      new Promise((_, reject) => {
        idle_timer = setInterval(() => {
          if (Date.now() - last_activity_at >= timeout_ms)
          {
            reject(
              new Error(
                `Cursor produced no activity within ${timeout_ms}ms.`,
              ),
            );
          }
        }, 1000);
        idle_timer.unref?.();
      }),
    ]);
    if (idle_timer)
    {
      clearInterval(idle_timer);
      idle_timer = null;
    }
    await Promise.race([
      stream_task,
      new Promise((resolve) => setTimeout(resolve, 1000)),
    ]);
    return result;
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

      return execute_agent_prompt(
        agent,
        build_prompt(prompt, snapshot, intent),
      );
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

    const is_scene_construction =
      intent?.kind === "scene_rebuild" ||
      intent?.live_scene_action;
    if (!is_scene_construction || !intent?.target_name)
    {
      return {
        ok: true,
        text: cursor_result.result?.trim() || "Done.",
      };
    }

    const found = await run.stage(
      "Resolve Quality Root",
      "finding the completed scene hierarchy",
      () => run.tool(
        "entity_find",
        {
          name: intent.target_name,
          match: "exact",
          limit: 100,
        },
      ),
    );
    const matches = found.matches ?? [];
    const root_match =
      matches.find((match) => !match.parent_id) ??
      matches[0];
    const root_id = root_match?.id;
    if (!found.ok || !root_id)
    {
      return {
        ok: false,
        text:
          `Scene quality gate could not resolve root entity ${intent.target_name}.`,
      };
    }

    const audit_args = {
      id: root_id,
      required_features: infer_required_features(prompt),
      scene_type: infer_design_template(prompt),
    };
    const send_command = (name, args) =>
      run.tool(name, args);
    let audit = await run.stage(
      "Audit Scene Quality",
      "checking geometry, materials, features, and lighting",
      () => audit_scene_quality(
        send_command,
        audit_args,
      ),
    );
    const layout_audit_args = {
      id: root_id,
      root_name: intent.target_name,
      minimum_created_at_ms: request_started_at_ms,
    };
    const audit_current_layout = async () => {
      const current_world = await send_command(
        "world_summary",
        {},
      );
      return audit_scene_layout(
        send_command,
        {
          ...layout_audit_args,
          namespace: make_scene_plan_namespace({
            project_root: get_project_root(),
            engine_host,
            engine_port,
            world: current_world,
          }),
        },
      );
    };
    let layout_audit = await run.stage(
      "Audit Scene Layout",
      "checking scale, support, relationships, and lighting",
      audit_current_layout,
    );
    let final_result = cursor_result;

    for (
      let attempt = 1;
      attempt <= 2 &&
      (
        !audit.pass ||
        !layout_audit.pass ||
        !visual_review_seen
      );
      attempt++
    )
    {
      const correction_prompt = [
        "Perform a mandatory quality correction pass on the live Spartan Engine scene.",
        `Original request: ${prompt}`,
        `Root entity: ${intent.target_name}, id ${root_id}.`,
        `Quality audit: ${safe_json(audit, 3500)}`,
        `Layout audit: ${safe_json(layout_audit, 5000)}`,
        "If the generic scene plan is missing or invalid, call scene_plan_create first with realistic expected dimensions, zones, support modes, relationships, and lighting intent inferred from the original request.",
        "Call scene_visual_review on the root with perspective and top views, then inspect both images.",
        "Fix every failed scene_layout_audit and scene_quality_audit check, including every renderable listed by collision_coverage, plus the most visible weakness in the image.",
        "When audit issues contain recipe_ids, modify only those scene recipe nodes unless a shared support or material must change.",
        "Use generated or compound geometry, semantic palette materials, descriptive feature names, snapping, and calibrated lighting as needed.",
        "Preserve all good existing work and keep every addition under the root.",
        "Call scene_layout_audit and scene_quality_audit after corrections and do not report completion unless both pass.",
      ].join("\n");

      final_result = await run.stage(
        `Quality Correction ${attempt}`,
        "waiting for visual review and targeted corrections",
        () => execute_agent_prompt(
          agent,
          correction_prompt,
        ),
      );
      if (
        final_result.status === "error" ||
        final_result.status === "cancelled"
      )
      {
        const failure_message = await run_failure_message(
          cursor_run,
          final_result,
        );
        return {
          ok: false,
          text: `Scene was edited, but quality correction failed: ${failure_message}`,
        };
      }

      audit = await run.stage(
        `Verify Quality ${attempt}`,
        "rechecking the corrected scene",
        () => audit_scene_quality(
          send_command,
          audit_args,
        ),
      );
      layout_audit = await run.stage(
        `Verify Layout ${attempt}`,
        "rechecking scale, support, relationships, and lighting",
        audit_current_layout,
      );
    }

    if (
      !audit.pass ||
      !layout_audit.pass ||
      !visual_review_seen
    )
    {
      return {
        ok: false,
        text: [
          "Scene was edited, but the quality gate remains incomplete.",
          `Quality audit: ${safe_json(audit, 2500)}`,
          `Layout audit: ${safe_json(layout_audit, 3500)}`,
          `Visual review completed: ${visual_review_seen}.`,
          "Final-state audits are authoritative for plan and correction completion.",
        ].join("\n"),
      };
    }

    const resource_cleanup = await run.stage(
      "Clean World Resources",
      "removing unreferenced world assets",
      () => run.tool(
        "world_resources_clean",
        {},
      ),
    );
    return {
      ok: true,
      text: [
        final_result.result?.trim() ||
          cursor_result.result?.trim() ||
          "Done.",
        `Quality gates passed: content ${audit.score}/100, layout ${layout_audit.score}/100, visual review complete.`,
        resource_cleanup.ok
          ? `World resources cleaned: ${(resource_cleanup.removed ?? []).length} unused files removed.`
          : `World resource cleanup failed: ${resource_cleanup.error ?? "unknown error"}.`,
      ].join("\n"),
    };
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

export async function run_cursor_fallback(args) {
  const previous_run = agent_run_queue;
  let release_run;
  agent_run_queue = new Promise((resolve) => {
    release_run = resolve;
  });
  await previous_run;

  try
  {
    return await run_cursor_fallback_serial(args);
  }
  finally
  {
    release_run();
  }
}
