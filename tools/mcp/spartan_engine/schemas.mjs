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
  // loose objects keep additionalProperties open, engine results carry extra fields
  return z.looseObject({
    ok: z.boolean(),
    request_id: z.string().optional(),
    error: z.string().optional(),
    code: z.string().optional(),
    retryable: z.boolean().optional(),
    suggested_action: z.string().optional(),
    ...shape,
  });
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
    features: any_object.optional(),
    async_tasks: any_object.optional(),
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
  async_task: with_error_fields({
    task: any_object.optional(),
  }),
  async_task_list: with_error_fields({
    tasks: z.array(any_object).optional(),
  }),
  engine_status: with_error_fields({
    playing: z.boolean().optional(),
    paused: z.boolean().optional(),
    loading: z.boolean().optional(),
    fps: z.number().optional(),
    frame_ms: z.number().optional(),
  }),
  profiler_snapshot: with_error_fields({
    fps: z.number().optional(),
    frame_ms: z.number().optional(),
    cpu_ms: z.number().optional(),
    gpu_ms: z.number().optional(),
    frame_ms_last: z.number().optional(),
    cpu_stuttering: z.boolean().optional(),
    gpu_stuttering: z.boolean().optional(),
    update_interval_sec: z.number().optional(),
    visualized: z.boolean().optional(),
    rhi: any_object.optional(),
    time_block_count: z.number().int().optional(),
    time_blocks: z.array(any_object).optional(),
  }),
  camera_snapshot: with_error_fields({
    entity_id: z.string().optional(),
    entity_name: z.string().optional(),
    position: vector3.optional(),
    forward: vector3.optional(),
    right: vector3.optional(),
    up: vector3.optional(),
    view: z.string().optional(),
    target: vector3.optional(),
    distance: z.number().optional(),
    padding: z.number().optional(),
    bounding_box: any_object.optional(),
  }),
  screenshot_take: with_error_fields({
    path: z.string().optional(),
    ready: z.boolean().optional(),
    async: z.boolean().optional(),
    note: z.string().optional(),
  }),
  scene_visual_review: with_error_fields({
    context: any_object.optional(),
    camera: any_object.optional(),
    materials: any_object.nullable().optional(),
    renderer_debug: any_object.optional(),
    screenshot: any_object.optional(),
    views: z.array(any_object).optional(),
  }),
  scene_quality_audit: with_error_fields({
    pass: z.boolean().optional(),
    score: z.number().int().min(0).max(100).optional(),
    root: any_object.optional(),
    metrics: any_object.optional(),
    feature_results: z.array(any_object).optional(),
    checks: z.array(any_object).optional(),
    failed_checks: z.array(any_object).optional(),
    recommendations: z.array(z.string()).optional(),
  }),
  scene_plan: with_error_fields({
    pass: z.boolean().optional(),
    error_count: z.number().int().optional(),
    issues: z.array(any_object).optional(),
    plan: any_object.optional(),
  }),
  scene_layout_audit: with_error_fields({
    pass: z.boolean().optional(),
    score: z.number().int().min(0).max(100).optional(),
    root: any_object.optional(),
    issues: z.array(any_object).optional(),
    element_evidence: z.array(any_object).optional(),
    lighting: any_object.optional(),
    plan: any_object.optional(),
  }),
  world_summary: with_error_fields({
    name: z.string().optional(),
    file_path: z.string().optional(),
    path: z.string().optional(),
    description: z.string().optional(),
    entity_count: z.number().int().optional(),
    light_count: z.number().int().optional(),
    audio_source_count: z.number().int().optional(),
    time_of_day: z.number().optional(),
    wind: vector3.optional(),
    bounding_box: any_object.optional(),
    bounds: any_object.optional(),
  }),
  world_raycast: with_error_fields({
    hit: z.boolean().optional(),
    position: vector3.optional(),
    normal: vector3.optional(),
    distance: z.number().optional(),
    entity_id: z.string().optional(),
    entity_name: z.string().optional(),
  }),
  entity_snap: with_error_fields({
    mode: z.string().optional(),
    position: vector3.optional(),
    rotation: z.array(z.number()).length(4).optional(),
    hit: any_object.optional(),
    bounding_box: any_object.optional(),
  }),
  entity_spatial_snapshot: with_error_fields({
    root_id: z.string().optional(),
    root_name: z.string().optional(),
    entities: z.array(any_object).optional(),
    count: z.number().int().optional(),
    truncated: z.boolean().optional(),
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
  resource_receipt: with_error_fields({
    path: z.string().optional(),
    resource: any_object.optional(),
    removed: any_object.optional(),
  }),
  parametric_mesh: with_error_fields({
    reused: z.boolean().optional(),
    shape: z.string().optional(),
    vertex_count: z.number().int().optional(),
    index_count: z.number().int().optional(),
    resource: any_object.optional(),
  }),
  parametric_mesh_batch: with_error_fields({
    generated: z.array(any_object).optional(),
    generated_count: z.number().int().optional(),
    failed_index: z.number().int().optional(),
    failure: any_object.optional(),
  }),
  render_set_mesh: with_error_fields({
    entity: any_object.optional(),
    mesh: any_object.optional(),
    sub_mesh_index: z.number().int().optional(),
  }),
  compound_create: with_error_fields({
    root: any_object.optional(),
    completed_parts: z.array(any_object).optional(),
    completed_count: z.number().int().optional(),
    failed_index: z.number().int().optional(),
    prefab: any_object.nullable().optional(),
  }),
  construction_grammar: with_error_fields({
    root: any_object.optional(),
    completed_parts: z.array(any_object).optional(),
    completed_count: z.number().int().optional(),
    failed_index: z.number().int().optional(),
    prefab: any_object.nullable().optional(),
    grammar: any_object.optional(),
    snap: any_object.nullable().optional(),
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
  renderer_debug: with_error_fields({
    options: z.array(z.string()).optional(),
    values: any_object.optional(),
  }),
  physics_state: with_error_fields({
    entity: any_object.optional(),
    body_type: z.string().optional(),
    enabled: z.boolean().optional(),
    static: z.boolean().optional(),
    kinematic: z.boolean().optional(),
    vehicle: any_object.optional(),
  }),
  batch_receipt: with_error_fields({
    created_count: z.number().int().optional(),
    created: z.array(any_object).optional(),
    failed_index: z.number().int().optional(),
    failure: any_object.optional(),
  }),
  transform_batch_receipt: with_error_fields({
    updated_count: z.number().int().optional(),
    updated: z.array(any_object).optional(),
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

function is_zod_schema(value) {
  return Boolean(value?._zod);
}

export function json_schema_from_raw_shape(shape) {
  if (!is_zod_schema(shape) && shape?.type === "object" && shape.properties) {
    return shape;
  }

  if (typeof z.toJSONSchema === "function") {
    try {
      return z.toJSONSchema(is_zod_schema(shape) ? shape : z.object(shape ?? {}));
    } catch {
    }
  }

  return {
    type: "object",
    additionalProperties: true,
  };
}

export function parse_raw_shape(shape, args) {
  if (!shape || (!is_zod_schema(shape) && Object.keys(shape).length === 0)) {
    return { ok: true, value: {} };
  }

  const parsed = (is_zod_schema(shape) ? shape : z.object(shape)).safeParse(args ?? {});
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
