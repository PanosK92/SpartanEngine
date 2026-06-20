import { z } from "zod";

const any_object = z.record(z.string(), z.any());
const vector3 = z.array(z.number()).length(3);
const source_match = z.object({
  score: z.number(),
  path: z.string(),
  start_line: z.number().int(),
  end_line: z.number().int(),
  content: z.string(),
});

function with_error_fields(shape = {}) {
  return {
    ok: z.boolean(),
    error: z.string().optional(),
    code: z.string().optional(),
    retryable: z.boolean().optional(),
    suggested_action: z.string().optional(),
    ...shape,
  };
}

export const output_schemas = {
  generic: with_error_fields(),
  spartan_status: with_error_fields({
    server: z.string().optional(),
    transport: z.string().optional(),
    http_endpoint: z.string().nullable().optional(),
    project_root: z.string().optional(),
    engine_host: z.string().optional(),
    engine_port: z.number().int().optional(),
    read_only_mode: z.boolean().optional(),
    engine: any_object.optional(),
    codebase: any_object.optional(),
  }),
  search_codebase: with_error_fields({
    ready: z.boolean().optional(),
    query: z.string().optional(),
    results: z.array(source_match).optional(),
  }),
  read_source_file: with_error_fields({
    path: z.string().optional(),
    start_line: z.number().int().optional(),
    end_line: z.number().int().optional(),
    total_lines: z.number().int().optional(),
    text: z.string().optional(),
  }),
  search_capabilities: with_error_fields({
    query: z.string().optional(),
    matches: z.array(any_object).optional(),
  }),
  get_capability_details: with_error_fields({
    name: z.string().optional(),
    kind: z.string().optional(),
    tool: any_object.optional(),
    resource: any_object.optional(),
  }),
  agent_memory: with_error_fields({
    path: z.string().optional(),
    memory: z.string().optional(),
  }),
  debug_log: with_error_fields({
    path: z.string().optional(),
    log: z.string().optional(),
  }),
  engine_status: with_error_fields({
    playing: z.boolean().optional(),
    paused: z.boolean().optional(),
    loading: z.boolean().optional(),
    fps: z.number().optional(),
    frame_ms: z.number().optional(),
  }),
  camera_snapshot: with_error_fields({
    entity_id: z.string().optional(),
    entity_name: z.string().optional(),
    position: vector3.optional(),
    forward: vector3.optional(),
    right: vector3.optional(),
    up: vector3.optional(),
  }),
  world_summary: with_error_fields({
    name: z.string().optional(),
    path: z.string().optional(),
    entity_count: z.number().int().optional(),
    time_of_day: z.number().optional(),
    wind: vector3.optional(),
    bounds: any_object.optional(),
  }),
  world_raycast: with_error_fields({
    hit: z.boolean().optional(),
    position: vector3.optional(),
    entity_id: z.string().optional(),
    entity_name: z.string().optional(),
  }),
  context_snapshot: with_error_fields({
    status: any_object.optional(),
    world: any_object.optional(),
    selection: any_object.optional(),
    camera: any_object.optional(),
  }),
  console_read: with_error_fields({
    entries: z.array(any_object).optional(),
    count: z.number().int().optional(),
    truncated: z.boolean().optional(),
  }),
  entity: with_error_fields({
    entity: any_object.optional(),
    source: z.string().optional(),
  }),
  entity_list: with_error_fields({
    total: z.number().int().optional(),
    offset: z.number().int().optional(),
    count: z.number().int().optional(),
    truncated: z.boolean().optional(),
    entities: z.array(any_object).optional(),
  }),
  entity_find: with_error_fields({
    matches: z.array(any_object).optional(),
    truncated: z.boolean().optional(),
  }),
  entity_render_materials: with_error_fields({
    id: z.string().optional(),
    name: z.string().optional(),
    materials: z.array(any_object).optional(),
  }),
  selection_get: with_error_fields({
    selected_ids: z.array(z.string()).optional(),
  }),
  component_types: with_error_fields({
    component_types: z.array(z.string()).optional(),
  }),
  primitive_types: with_error_fields({
    primitive_types: z.array(any_object).optional(),
  }),
  resource_list: with_error_fields({
    type: z.string().optional(),
    offset: z.number().int().optional(),
    count: z.number().int().optional(),
    total: z.number().int().optional(),
    truncated: z.boolean().optional(),
    resources: z.array(any_object).optional(),
  }),
  material: with_error_fields({
    material: any_object.optional(),
  }),
  prefab_types: with_error_fields({
    types: z.array(z.string()).optional(),
  }),
  prefab_receipt: with_error_fields({
    path: z.string().optional(),
    entity: any_object.optional(),
  }),
  component_get: with_error_fields({
    component: any_object.optional(),
  }),
  component_set: with_error_fields({
    component: any_object.optional(),
  }),
  component_set_batch: with_error_fields({
    updated_count: z.number().int().optional(),
    failed_index: z.number().int().optional(),
    component: any_object.optional(),
  }),
  component_action: with_error_fields({
    entity: any_object.optional(),
    type: z.string().optional(),
    action: z.string().optional(),
    result: any_object.optional(),
    component: any_object.optional(),
  }),
  batch_receipt: with_error_fields({
    created_count: z.number().int().optional(),
    created: z.array(any_object).optional(),
    failed_index: z.number().int().optional(),
    failure: any_object.optional(),
  }),
  delete_receipt: with_error_fields({
    id: z.string().optional(),
    name: z.string().optional(),
    deleted_id: z.string().optional(),
    deleted_count: z.number().int().optional(),
    remaining_count: z.number().int().optional(),
    remaining_children: z.array(z.string()).optional(),
  }),
  lua_result: with_error_fields({
    result: z.string().optional(),
  }),
};

export function error_metadata(message) {
  const text = String(message ?? "").toLowerCase();
  if (text.includes("loading")) {
    return {
      code: "engine_loading",
      retryable: true,
      suggested_action: "wait for loading to finish and retry the same request",
    };
  }
  if (text.includes("edit mode") || text.includes("requires edit")) {
    return {
      code: "edit_mode_required",
      retryable: false,
      suggested_action: "switch the engine to edit mode before calling this mutating tool",
    };
  }
  if (text.includes("not found") || text.includes("nothing selected") || text.includes("multiple entities")) {
    return {
      code: "target_resolution_failed",
      retryable: false,
      suggested_action: "call entity_find, selection_get, or entity_resolve with a more precise target",
    };
  }
  if (text.includes("timeout") || text.includes("timed out") || text.includes("did not answer")) {
    return {
      code: "engine_timeout",
      retryable: true,
      suggested_action: "retry once, then reduce the operation size or use execute_lua for a single batched edit",
    };
  }
  if (text.includes("read-only")) {
    return {
      code: "read_only_mode",
      retryable: false,
      suggested_action: "restart the MCP server without read-only mode to enable mutating tools",
    };
  }
  return {
    code: "tool_error",
    retryable: false,
    suggested_action: "inspect the tool arguments and current engine state before retrying",
  };
}

export function structured_error(message, extra = {}) {
  const metadata = error_metadata(message);
  return {
    ok: false,
    error: String(message ?? "tool failed"),
    ...metadata,
    ...extra,
  };
}

export function normalize_result(result) {
  if (!result || typeof result !== "object") {
    return structured_error("tool returned an invalid result");
  }
  if (result.ok) {
    return result;
  }

  const metadata = error_metadata(result.error ?? "tool failed");
  return {
    ...result,
    error: result.error ?? "tool failed",
    code: result.code ?? metadata.code,
    retryable: result.retryable ?? metadata.retryable,
    suggested_action: result.suggested_action ?? metadata.suggested_action,
  };
}

export function json_schema_from_raw_shape(shape) {
  if (shape?.type === "object" && shape.properties) {
    return shape;
  }

  if (typeof z.toJSONSchema === "function") {
    try {
      return z.toJSONSchema(z.object(shape ?? {}));
    } catch {
    }
  }

  return {
    type: "object",
    additionalProperties: true,
  };
}

export function parse_raw_shape(shape, args) {
  if (!shape || Object.keys(shape).length === 0) {
    return { ok: true, value: {} };
  }

  const parsed = z.object(shape).safeParse(args ?? {});
  if (parsed.success) {
    return { ok: true, value: parsed.data };
  }

  const issues = parsed.error.issues.map((issue) => `${issue.path.join(".") || "arguments"} ${issue.message}`);
  return {
    ok: false,
    error: structured_error(`invalid tool arguments, ${issues.join("; ")}`, {
      code: "invalid_arguments",
      retryable: false,
      suggested_action: "call get_capability_details for the tool schema and retry with valid arguments",
    }),
  };
}
