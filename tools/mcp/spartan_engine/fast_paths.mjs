import { get_shared_codebase } from "./shared_codebase.mjs";

const codebase = get_shared_codebase();

function entity_label(entity) {
  return entity?.name ? `${entity.name} (${entity.id})` : String(entity?.id ?? "unknown");
}

function as_vector3(value) {
  if (!Array.isArray(value) || value.length !== 3) {
    return null;
  }

  const vector = value.map((component) => Number(component));
  return vector.every(Number.isFinite) ? vector : null;
}

async function read_camera_snapshot(run) {
  const snapshot = await run.stage("Read Context", "reading scene context for deterministic placement", () => run.tool("context_snapshot", {}, 5000));
  if (!snapshot.ok) {
    throw new Error(snapshot.error ?? "context_snapshot failed");
  }

  run.receipt("scene context", {
    status: snapshot.status,
    world: snapshot.world,
    camera: snapshot.camera,
  });

  if (snapshot.camera?.ok) {
    return snapshot.camera;
  }

  const camera = await run.stage("Read Camera", "reading focused camera pose", () => run.tool("camera_snapshot", {}, 5000));
  if (!camera.ok) {
    if (String(camera.error ?? "").toLowerCase().includes("unknown command")) {
      throw new Error("camera-relative placement needs the native camera_snapshot command. Rebuild Spartan Engine so the C++ MCP bridge exposes it.");
    }
    throw new Error(camera.error ?? "camera_snapshot failed");
  }

  return camera;
}

async function place_above_ground(run, position, height, { use_raycast = false } = {}) {
  if (!use_raycast)
  {
    const placed = [position[0], height, position[2]];
    run.receipt("ground placement", {
      mode: "world y",
      position: placed,
    });
    return placed;
  }

  const ray_origin = [position[0], Math.max(position[1] + 500.0, height + 500.0), position[2]];
  const raycast = await run.stage("Ground Placement", "raycasting ground under target", () => run.tool("world_raycast", {
    origin: ray_origin,
    direction: [0, -1, 0],
    max_distance: 1000,
  }, 3000));

  if (raycast.ok && raycast.hit) {
    const hit_position = as_vector3(raycast.position);
    if (hit_position) {
      const grounded = [position[0], hit_position[1] + height, position[2]];
      run.receipt("ground placement", {
        hit: raycast.entity_name ?? raycast.entity_id ?? "static world",
        position: grounded,
      });
      return grounded;
    }
  }

  const fallback = [position[0], height, position[2]];
  run.receipt("ground placement", {
    fallback: raycast.ok ? "no ground hit" : (raycast.error ?? "world_raycast unavailable"),
    position: fallback,
  });
  return fallback;
}

async function resolve_target(run, intent, { allow_create = false } = {}) {
  const args = intent.use_selected ? { selected: true } : { name: intent.target_name };
  if (!intent.use_selected && !intent.target_name) {
    throw new Error("I could not infer which entity to target.");
  }

  let result = await run.tool("entity_resolve", args);
  if (!result.ok && allow_create && intent.target_name) {
    result = await run.tool("entity_create_empty", { name: intent.target_name });
    if (result.ok) {
      return result.entity;
    }
  }

  if (!result.ok) {
    throw new Error(result.error ?? "target entity could not be resolved");
  }

  return result.entity;
}

async function run_delete_children(run, intent) {
  const entity = await run.stage("Resolve Target", "finding the entity to clear", async () => {
    const entity = await resolve_target(run, intent);
    run.receipt("target resolved", {
      id: entity.id,
      name: entity.name,
      source: intent.use_selected ? "selection" : "name",
    });
    return entity;
  });

  const deletion = await run.stage("Apply Changes", `deleting children of ${entity.name ?? entity.id}`, async () => {
    const deletion = await run.tool("entity_delete_children", { id: entity.id }, 10000);
    if (!deletion.ok) {
      throw new Error(deletion.error ?? "entity_delete_children failed");
    }

    run.receipt("children deleted", {
      id: deletion.id ?? entity.id,
      name: deletion.name ?? entity.name,
      deleted_count: deletion.deleted_count ?? 0,
      remaining_count: deletion.remaining_count ?? 0,
    });

    return deletion;
  });

  await run.stage("Verify", "checking that no direct children remain", async () => {
    if (Number(deletion.remaining_count ?? 0) !== 0) {
      const remaining = Array.isArray(deletion.remaining_children) ? deletion.remaining_children.join(", ") : "unknown";
      throw new Error(`stopped because ${deletion.remaining_count} children remain: ${remaining}`);
    }
  });

  return `Deleted ${deletion.deleted_count ?? 0} children from ${deletion.name ?? entity.name}.`;
}

async function run_delete_entity(run, intent) {
  const entity = await run.stage("Resolve Target", "finding the entity to delete", async () => {
    const entity = await resolve_target(run, intent);
    run.receipt("target resolved", {
      id: entity.id,
      name: entity.name,
      source: intent.use_selected ? "selection" : "name",
    });
    return entity;
  });

  const deletion = await run.stage("Apply Changes", `deleting ${entity.name ?? entity.id}`, async () => {
    const deletion = await run.tool("entity_delete", { id: entity.id }, 10000);
    if (!deletion.ok) {
      throw new Error(deletion.error ?? "entity_delete failed");
    }

    run.receipt("entity deleted", {
      id: deletion.deleted_id ?? entity.id,
      name: entity.name,
    });
    return deletion;
  });

  return `Deleted ${entity_label(entity)}.`;
}

async function run_simple_read(run, prompt) {
  const value = prompt.toLowerCase();
  if (value.includes("primitive")) {
    const result = await run.stage("Read Engine", "reading primitive types", () => run.tool("primitive_types"));
    if (!result.ok) {
      throw new Error(result.error ?? "primitive_types failed");
    }

    run.receipt("primitive types", result);
    return JSON.stringify(result.primitive_types ?? result, null, 2);
  }

  if (value.includes("component")) {
    const result = await run.stage("Read Engine", "reading component types", () => run.tool("component_types"));
    if (!result.ok) {
      throw new Error(result.error ?? "component_types failed");
    }

    run.receipt("component types", result);
    return JSON.stringify(result.component_types ?? result, null, 2);
  }

  if (value.includes("selected") || value.includes("selection")) {
    const selection = await run.stage("Read Engine", "reading selected entities", () => run.tool("selection_get"));
    if (!selection.ok) {
      throw new Error(selection.error ?? "selection_get failed");
    }

    run.receipt("selection", selection);
    if (!Array.isArray(selection.selected_ids) || selection.selected_ids.length === 0) {
      return "Nothing is selected.";
    }
    if (selection.selected_ids.length > 1) {
      return `Selected entities: ${selection.selected_ids.join(", ")}.`;
    }

    const entity = await run.stage("Inspect Selection", "reading selected entity details", () => run.tool("entity_get", { id: selection.selected_ids[0] }));
    if (!entity.ok) {
      throw new Error(entity.error ?? "entity_get failed");
    }

    run.receipt("selected entity", entity.entity ?? entity);
    return `Selected entity: ${entity_label(entity.entity)}.`;
  }

  const snapshot = await run.stage("Read Engine", "reading world and engine state", () => run.tool("context_snapshot"));
  if (!snapshot.ok) {
    throw new Error(snapshot.error ?? "context_snapshot failed");
  }

  run.receipt("context snapshot", {
    status: snapshot.status,
    world: snapshot.world,
    selection: snapshot.selection,
  });

  const world = snapshot.world ?? {};
  const status = snapshot.status ?? {};
  return [
    `World: ${world.name ?? "unknown"} with ${world.entity_count ?? "unknown"} entities.`,
    `Engine: ${status.playing ? "playing" : "edit mode"}, ${status.loading ? "loading" : "ready"}.`,
  ].join(" ");
}

function default_body_type_for_mesh(mesh)
{
  if (mesh === "sphere")
  {
    return "sphere";
  }
  if (mesh === "quad")
  {
    return "plane";
  }
  if (mesh === "cylinder")
  {
    return "capsule";
  }
  return "box";
}

async function apply_primitive_physics(run, entity, intent)
{
  const id = entity?.id;
  if (!id)
  {
    throw new Error("entity_create_primitive returned no entity id for physics setup");
  }

  const add_physics = await run.stage("Add Physics", `adding physics to ${entity.name ?? id}`, () => run.tool("entity_add_component", { id, type: "physics" }, 5000));
  if (!add_physics.ok)
  {
    throw new Error(add_physics.error ?? "entity_add_component failed");
  }

  const body_type = intent.body_type ?? default_body_type_for_mesh(intent.mesh);
  const configured = await run.stage("Configure Physics", `setting ${body_type} body type`, () => run.tool("component_set", {
    id,
    type: "physics",
    property: "body_type",
    value: body_type,
  }, 5000));
  if (!configured.ok)
  {
    throw new Error(configured.error ?? "component_set failed");
  }

  let is_static = undefined;
  if (typeof intent.physics_static === "boolean")
  {
    is_static = intent.physics_static;
    const static_result = await run.stage("Configure Physics", `setting static ${is_static}`, () => run.tool("component_set", {
      id,
      type: "physics",
      property: "static",
      value: is_static,
    }, 5000));
    if (!static_result.ok)
    {
      throw new Error(static_result.error ?? "component_set static failed");
    }
  }

  run.receipt("physics configured", {
    id,
    name: entity.name,
    body_type,
    static: is_static,
  });
  return body_type;
}

async function run_create_primitive(run, intent) {
  const args = {
    mesh: intent.mesh,
    name: intent.name,
  };

  let position = Array.isArray(intent.position) ? [...intent.position] : undefined;
  if (Number.isFinite(intent.camera_forward_distance)) {
    const camera = await read_camera_snapshot(run);
    const camera_position = as_vector3(camera.position);
    const camera_forward = as_vector3(camera.forward);
    if (!camera_position || !camera_forward) {
      throw new Error("camera_snapshot returned an invalid transform");
    }

    position = [
      Number(camera_position[0]) + Number(camera_forward[0]) * intent.camera_forward_distance,
      Number(camera_position[1]) + Number(camera_forward[1]) * intent.camera_forward_distance,
      Number(camera_position[2]) + Number(camera_forward[2]) * intent.camera_forward_distance,
    ];
    run.receipt("camera placement", {
      camera: camera.entity_name ?? camera.entity_id,
      distance: intent.camera_forward_distance,
      position,
    });
  }

  if (Number.isFinite(intent.height_above_ground)) {
    position = position ?? [0, 0, 0];
    position = await place_above_ground(run, position, intent.height_above_ground, { use_raycast: Boolean(intent.use_ground_raycast) });
  }

  if (Array.isArray(position)) {
    args.position = position;
  }

  run.receipt("create arguments", args);
  const result = await run.stage("Create Primitive", `creating ${intent.name}`, () => run.tool("entity_create_primitive", args, 10000));
  if (!result.ok) {
    throw new Error(result.error ?? "entity_create_primitive failed");
  }

  let physics_body_type = undefined;
  if (intent.with_physics)
  {
    physics_body_type = await apply_primitive_physics(run, result.entity, intent);
  }

  run.receipt("primitive created", {
    id: result.entity?.id,
    name: result.entity?.name,
    mesh: intent.mesh,
    physics: Boolean(intent.with_physics),
    body_type: physics_body_type,
  });

  return `Created ${result.entity?.name ?? intent.name}.`;
}

function format_log_entry(entry) {
  return `[${entry.type ?? "info"}] ${entry.text ?? ""}`.trim();
}

async function run_mcp_status(run) {
  const engine_status = await run.stage("Check Engine", "reading live engine status", () => run.tool("engine_status", {}, 2500));
  const index_status = codebase.status();
  run.receipt("assistant status", {
    engine_ok: Boolean(engine_status.ok),
    engine: engine_status,
    codebase: index_status,
  });

  const engine_text = engine_status.ok ? "engine connected" : `engine unavailable, ${engine_status.error ?? "unknown error"}`;
  const index_text = index_status.ready ? `code index ready with ${index_status.chunks} chunks` : `code index ${index_status.indexing ? "indexing" : "not ready"}`;
  return `MCP assistant status: ${engine_text}; ${index_text}.`;
}

async function run_console_read(run, intent) {
  const args = {
    limit: 40,
  };
  if (intent.minimum_type) {
    args.minimum_type = intent.minimum_type;
  }

  const result = await run.stage("Read Console", "reading recent engine logs directly", () => run.tool("console_read", args, 5000));
  if (!result.ok) {
    throw new Error(result.error ?? "console_read failed");
  }

  const entries = Array.isArray(result.entries) ? result.entries : [];
  run.receipt("console entries", {
    count: entries.length,
    minimum_type: intent.minimum_type ?? "info",
  });

  if (entries.length === 0) {
    return "No matching console entries.";
  }

  return entries.slice(-12).map(format_log_entry).join("\n");
}

async function run_engine_mode(run, intent) {
  if (!intent.mode) {
    throw new Error("I could not infer which engine mode to set.");
  }

  const result = await run.stage("Set Engine Mode", `switching engine to ${intent.mode}`, () => run.tool("engine_set_mode", { mode: intent.mode }, 5000));
  if (!result.ok) {
    throw new Error(result.error ?? "engine_set_mode failed");
  }

  run.receipt("engine mode", {
    mode: intent.mode,
    result,
  });
  return `Engine mode set to ${intent.mode}.`;
}

async function run_calibrate_lights(run, intent) {
  const args = {};
  if (intent.target_name || intent.use_selected)
  {
    const entity = await run.stage("Resolve Parent", "resolving optional light parent", () => resolve_target(run, intent));
    args.parent_id = entity.id;
    run.receipt("light parent", {
      id: entity.id,
      name: entity.name,
    });
  }

  const result = await run.stage("Calibrate Lights", "applying photometric defaults to scene lights", () => run.tool("lights_calibrate", args, 60000));
  if (!result.ok)
  {
    throw new Error(result.error ?? "lights_calibrate failed");
  }

  run.receipt("lights calibrated", {
    updated_count: result.updated_count ?? 0,
    skipped_count: result.skipped_count ?? 0,
    role_counts: result.role_counts ?? {},
  });

  const roles = result.role_counts && typeof result.role_counts === "object"
    ? Object.entries(result.role_counts).map(([role, count]) => `${role}=${count}`).join(", ")
    : "";
  const role_text = roles ? ` (${roles})` : "";
  return `Calibrated ${result.updated_count ?? 0} lights${role_text}.`;
}

async function run_source_code_search(run, prompt) {
  const results = await run.stage("Search Codebase", "searching the local source index directly", async () => codebase.search(prompt, 8));
  run.receipt("source matches", {
    query: prompt,
    count: results.length,
    results: results.slice(0, 5).map((result) => ({
      path: result.path,
      start_line: result.start_line,
      end_line: result.end_line,
      score: result.score,
    })),
  });

  if (results.length === 0) {
    return "I searched the local source index and found no strong matches.";
  }

  const lines = results.slice(0, 5).map((result) => {
    return `${result.path}:${result.start_line}-${result.end_line} score ${result.score}`;
  });
  return `Top source matches:\n${lines.join("\n")}`;
}

export async function run_fast_path(run, intent, prompt) {
  if (intent.kind === "mcp_status") {
    return run_mcp_status(run);
  }

  if (intent.kind === "console_read") {
    return run_console_read(run, intent);
  }

  if (intent.kind === "engine_mode") {
    return run_engine_mode(run, intent);
  }

  if (intent.kind === "source_code") {
    return run_source_code_search(run, prompt);
  }

  if (intent.kind === "simple_read") {
    return run_simple_read(run, prompt);
  }

  if (intent.kind === "create_primitive") {
    return run_create_primitive(run, intent);
  }

  if (intent.kind === "delete_children") {
    return run_delete_children(run, intent);
  }

  if (intent.kind === "delete_entity") {
    return run_delete_entity(run, intent);
  }

  if (intent.kind === "calibrate_lights")
  {
    return run_calibrate_lights(run, intent);
  }

  // city road / district prompts always escalate so the agent can plan, decorate, and design
  return null;
}
