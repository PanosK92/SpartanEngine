import { car_playground_lua } from "./recipes/playground.mjs";

function entity_label(entity) {
  return entity?.name ? `${entity.name} (${entity.id})` : String(entity?.id ?? "unknown");
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

async function run_car_playground(run, intent) {
  const entity = await run.stage("Resolve Target", "finding the playground parent", async () => {
    const entity = await resolve_target(run, intent, { allow_create: true });
    run.receipt("target resolved", {
      id: entity.id,
      name: entity.name,
      source: intent.use_selected ? "selection" : "name",
    });
    return entity;
  });

  const deletion = await run.stage("Clear Target", `deleting old children under ${entity.name ?? entity.id}`, async () => {
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

  await run.stage("Verify Clear", "checking the target is empty", async () => {
    if (Number(deletion.remaining_count ?? 0) !== 0) {
      throw new Error(`stopped because ${deletion.remaining_count} children remain`);
    }
  });

  const result = await run.stage("Build Blockout", "creating the car playground in one engine script", async () => {
    const script = car_playground_lua(entity.name);
    const result = await run.tool("execute_lua", { code: script }, 30000);
    if (!result.ok) {
      throw new Error(result.error ?? "execute_lua failed");
    }

    run.receipt("playground built", {
      id: entity.id,
      name: entity.name,
      result: result.result ?? "done",
    });

    return result;
  });

  await run.stage("Verify Build", "checking the playground parent after build", async () => {
    const verification = await run.tool("entity_get", { id: entity.id });
    if (!verification.ok) {
      throw new Error(verification.error ?? "entity_get failed");
    }

    run.receipt("verification", {
      id: entity.id,
      name: entity.name,
      child_count: Array.isArray(verification.entity?.children) ? verification.entity.children.length : "unknown",
    });
  });

  return result.result ?? `Built car playground under ${entity.name}.`;
}

export async function run_fast_path(run, intent, prompt) {
  if (intent.kind === "simple_read") {
    return run_simple_read(run, prompt);
  }

  if (intent.kind === "delete_children") {
    return run_delete_children(run, intent);
  }

  if (intent.kind === "car_playground") {
    return run_car_playground(run, intent);
  }

  return null;
}
