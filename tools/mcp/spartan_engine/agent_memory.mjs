import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
export const agent_memory_path = path.join(__dirname, "AGENT_MEMORY.md");

const initial_memory = `# Spartan Agent Memory

This file is shared memory for agents working on Spartan Engine. Keep it short, factual, and useful for future runs. Replace wrong notes instead of piling corrections on top of them.

## Engine Facts
- Engine MCP commands run through the C++ bridge on the engine main thread.
- Bridge requests carry request ids that are echoed in engine responses and debug logs.
- \`async_task_start\`, \`async_task_get\`, and \`async_task_list\` provide pollable background MCP tool execution.
- Mutating scene tools require edit mode.
- \`execute_lua\` is the broad capability layer for procedural scene edits and is edit-mode guarded.
- \`context_snapshot\` is the fastest first read for engine status, world summary, and selection.
- \`component_get\` exposes friendly properties, registered raw members, and metadata for ranges, units, enum values, side effects, recommended defaults, and read-only reasons.
- \`component_action\` invokes deterministic component methods that are not simple property writes.
- \`resource_list\` and \`material_get\` expose cached resources and material scalar/texture state.
- \`resource_load\`, \`resource_reload\`, \`resource_save\`, \`resource_remove\`, and \`material_create\` cover common resource lifecycle work.
- \`undo_redo\` routes editor undo and redo through the command stack.
- \`camera_set_view\`, \`viewport_frame\`, \`renderer_debug_get\`, \`renderer_debug_set\`, and \`physics_state\` cover common viewport/debug inspection.
- \`screenshot_take\` queues a renderer screenshot and can return the saved PNG as image content for visual inspection.

## Good Agent Strategies
- Start engine tasks with \`spartan_status\` or \`context_snapshot\`.
- Use \`debug_log_read\` after failures to inspect actual engine command inputs and outputs.
- Use \`search_capabilities\` and \`get_capability_details\` before guessing tool names.
- Use \`async_task_start\` for long-running tools, then poll with \`async_task_get\`.
- Resolve targets with \`entity_resolve\` before mutating named or selected entities.
- Use \`undo_redo\` instead of keyboard shortcuts for editor command-stack undo or redo.
- Use \`entity_find_by_component\` to locate all entities with a component type.
- Inspect \`component_get.property_metadata\` and \`component_get.member_metadata\` before writing unfamiliar component fields.
- Use \`component_set_batch\` for multiple property/member edits on one component.
- Use \`component_action\` before falling back to Lua for terrain, spline, particle, physics, audio, light, or camera actions.
- Use \`selection_update\`, \`entity_clone\`, \`entity_move_index\`, and prefab tools before using Lua for common editor hierarchy workflows.
- Use \`material_set_property\` and \`material_set_texture\` for material edits instead of custom Lua.
- Use resource lifecycle tools for asset cache load/reload/save/remove and new material creation.
- Use \`viewport_frame\` and \`camera_set_view\` before manual camera transform scripts.
- Use \`renderer_debug_set\` and \`physics_state\` for visual debugging and vehicle/rigid body inspection.
- Use \`screenshot_take\` when visual verification matters; it waits briefly for the async save and returns the image when ready.
- Before deleting or rebuilding geometry that should preserve look, call \`entity_render_materials\` on the target and reuse material names.
- Use \`entity_create_light\` for generic point, spot, directional, and area lights, and calibrate intensity, range, and area size to the scene scale.
- Use \`camera_snapshot\` before interpreting camera-relative placement.
- Use \`world_raycast\` for ground or surface-relative placement when possible.
- Simple live scene edits should use deterministic tools or fail fast; complex blockouts can use the higher-level scene path.
- Missing deterministic capabilities should be logged immediately under Problem Reports.
- Do not route delete plus rebuild prompts to \`entity_delete\`; preserve materials first, then rebuild through a complex scene path.
- User convention, \`physics <primitive>\` means dynamic non-static physics unless static, fixed, or immovable is explicitly requested.
- For repeated scene work, prefer \`entity_create_primitive_batch\` or one focused \`execute_lua\` script.
- For source questions, use \`search_codebase\`, then \`read_source_file\` for focused context.

## Gotchas
- World loading blocks many engine commands until loading completes.
- \`component_set\` supports friendly properties and registered raw component member names for all component types; metadata is advisory and the engine still validates writes.
- Long Lua scripts run on the main thread, so they should do a bounded amount of work and return a short summary.
- Tool errors are advisory data for recovery, not transport failures.

## Verified Patterns
- A parent entity plus a single batch or Lua script is usually better than many individual entity tool calls.
- A small receipt after each meaningful engine action helps the editor assistant UI stay understandable.

## Corrections
- Add corrections here when a previous note turns out to be wrong or incomplete.

## Advice To Future Agents
- Treat this file as advice, not absolute truth.
- Update this file only when a durable lesson was learned.
- Prefer replacing stale bullets over appending duplicates.
- Keep entries concise and tied to observed behavior.

## Advice To Maintainers
- Add native engine tools when agents repeatedly need the same multi-step command sequence.
- Keep MCP schemas close to engine component metadata so tool descriptions do not drift.

## Problem Reports
- Add specific recurring friction here, with the file/tool involved and why it matters.
`;

export async function ensure_agent_memory() {
  try {
    await fs.access(agent_memory_path);
  } catch {
    await fs.writeFile(agent_memory_path, initial_memory, "utf8");
  }
}

export async function read_agent_memory() {
  await ensure_agent_memory();
  return fs.readFile(agent_memory_path, "utf8");
}

export async function write_agent_memory(text) {
  const value = String(text ?? "").trimEnd();
  if (!value.startsWith("# Spartan Agent Memory")) {
    throw new Error("memory must start with # Spartan Agent Memory");
  }
  if (value.length > 32000) {
    throw new Error("memory is too large, prune stale notes before saving");
  }

  await fs.writeFile(agent_memory_path, `${value}\n`, "utf8");
  return read_agent_memory();
}

export async function append_agent_memory(section, note) {
  const section_name = String(section ?? "").trim();
  const note_text = String(note ?? "").trim();
  if (!section_name || !note_text) {
    throw new Error("section and note are required");
  }

  const memory = await read_agent_memory();
  const heading = `## ${section_name}`;
  const bullet = note_text.startsWith("- ") ? note_text : `- ${note_text}`;
  if (memory.includes(bullet)) {
    return memory;
  }

  const heading_index = memory.indexOf(heading);
  if (heading_index === -1) {
    return write_agent_memory(`${memory.trimEnd()}\n\n${heading}\n${bullet}\n`);
  }

  const next_heading_index = memory.indexOf("\n## ", heading_index + heading.length);
  if (next_heading_index === -1) {
    return write_agent_memory(`${memory.trimEnd()}\n${bullet}\n`);
  }

  const before = memory.slice(0, next_heading_index).trimEnd();
  const after = memory.slice(next_heading_index);
  return write_agent_memory(`${before}\n${bullet}\n${after.trimEnd()}\n`);
}
