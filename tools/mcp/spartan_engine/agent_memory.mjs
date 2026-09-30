/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
export const agent_memory_path = path.join(__dirname, "AGENT_MEMORY.md");
export const agent_memory_archive_path = path.join(__dirname, "AGENT_MEMORY_ARCHIVE.md");
export const agent_memory_max_chars = 24000;
export const agent_memory_note_max_chars = 1200;
// core guidance every session needs, never auto archived
const pinned_sections = new Set(["Engine Facts", "Good Agent Strategies", "Gotchas", "Advice To Maintainers"]);
const agent_memory_lock_path = `${agent_memory_path}.lock`;
const agent_memory_lock_owner_path = path.join(
  agent_memory_lock_path,
  "owner",
);
const lock_retry_ms = 50;
const lock_stale_ms = 10000;
const lock_timeout_ms = 12000;

const initial_memory = `# Spartan Agent Memory

This file is shared memory for agents working on Spartan Engine. Keep it short, factual, and useful for future runs. Replace wrong notes instead of piling corrections on top of them.

## Engine Facts
- Engine MCP commands run through the C++ bridge on the engine main thread.
- Bridge requests carry request ids that are echoed in engine responses and debug logs.
- \`async_task_start\`, \`async_task_get\`, and \`async_task_list\` provide pollable background MCP tool execution.
- Mutating scene tools require edit mode.
- \`execute_lua\` is available for focused procedural edits, but native batch tools are preferred for blockouts; exploratory Lua API probing has crashed the engine.
- Lua can sample splines via \`entity:GetComponent(ComponentType.Spline)\` with \`GetPoint(t)\`, \`GetTangent(t)\`, \`GetLength()\`, and can add cameras via \`entity:AddComponent(ComponentType.Camera)\`.
- \`World.GetEntities()\`, \`World.GetEntitiesLights()\`, and \`entity:GetChildren()\` return 1-based Lua tables; prefer \`ForEachChild\` for iteration.
- \`context_snapshot\` is the fastest first read for engine status, world summary, and selection.
- \`component_get\` exposes friendly properties, registered raw members, and metadata for ranges, units, enum values, side effects, recommended defaults, and read-only reasons.
- \`component_action\` invokes deterministic component methods that are not simple property writes.
- \`resource_list\` and \`material_get\` expose cached resources and material scalar/texture state.
- \`resource_load\`, \`resource_reload\`, \`resource_save\`, \`resource_remove\`, and \`material_create\` cover common resource lifecycle work.
- \`world_save\` prunes unreferenced files from the current world resources directory, and \`world_resources_clean\` returns an explicit cleanup receipt.
- \`undo_redo\` routes editor undo and redo through the command stack.
- \`viewport_frame\` frames complete descendant bounds from perspective, front, back, left, right, or top views without keyboard focus. Use \`camera_set_view\` for custom poses.
- \`screenshot_take\` queues a renderer screenshot and can return the saved PNG as image content for visual inspection.
- The editor sequencer (shots, spline followers, physics drives) is controlled with \`sequencer_get\`, \`sequencer_set\`, \`sequencer_playback\`, \`sequencer_event_*\`, \`sequencer_spline_*\`, \`sequencer_drive_*\` and \`sequencer_render\`; \`camera\` accepts an entity id or name, events re-sort by time, and state auto-saves to \`sequencer_<world>.xml\` under the project directory.

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
- Use \`viewport_frame\` with an explicit review angle before \`camera_set_view\` or manual camera transform scripts.
- Use \`renderer_debug_set\` and \`physics_state\` for visual debugging and vehicle/rigid body inspection.
- Use \`scene_visual_review\` when visual verification matters; it captures perspective and top views by default and returns both images when ready.
- Before deleting or rebuilding geometry that should preserve look, call \`entity_render_materials\` on the target and reuse material names.
- Use \`entity_create_light\` for every light; it fully initializes intensity, range, angle, area size, shadows, and distances. Never hand-roll lights with empty + add component + component_set.
- Light intensity is lux for directional and lumens otherwise. Visible blockout defaults: point/spot 8500, area 12000, directional 120000. Values like 25-100 are invisible.
- Use \`lights_calibrate\` to fix existing scene lights in one call; specialty car lights stay dim, blockout lights get lifted.
- For city massing: \`city_blockout\` / \`district_blockout\`. For city roads: scan \`world_landmarks\` and bounding boxes, invent an arterial that skirts large districts, spur to edges, \`spline_junction\`, then \`spline_decorate\`. Use \`spline_reroute\` to fix roads that cut through geometry while keeping lights/cameras. Never triangle center-to-center through an airway. Never hand-build \`spline_point_*\` children.
- Use \`camera_snapshot\` before interpreting camera-relative placement.
- Use \`world_raycast\` for ground or surface-relative placement when possible.
- Simple live scene edits should use deterministic tools; anything unmatched falls back to the Cursor agent with the engine MCP tools.
- Scene construction prompts such as \`build a level\`, \`make rooms\`, \`backrooms\`, or \`liminal space\` are live scene edits, not source-code search requests.
- Recurring gaps worth a dedicated fast path should be logged under Advice To Maintainers.
- Do not route delete plus rebuild prompts to \`entity_delete\`; preserve materials first, then rebuild through a complex scene path.
- User convention, \`physics <primitive>\` means dynamic non-static physics unless static, fixed, or immovable is explicitly requested.
- For repeated scene work, prefer \`entity_create_primitive_batch\` or one focused \`execute_lua\` script.
- For blockouts, resolve or create the parent first, then build with \`entity_create_primitive_batch\` and \`entity_create_light\`; do not probe Lua APIs.
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
- Add specific unresolved friction here, with the file or tool involved and why it matters.
`;

function wait(delay_ms)
{
  return new Promise((resolve) =>
  {
    setTimeout(resolve, delay_ms);
  });
}

async function with_agent_memory_lock(operation)
{
  const started_at = Date.now();
  const owner =
    `${process.pid}.${Date.now()}.` +
    Math.random().toString(16).slice(2);

  while (true)
  {
    try
    {
      await fs.mkdir(agent_memory_lock_path);
      try
      {
        await fs.writeFile(
          agent_memory_lock_owner_path,
          owner,
          "utf8",
        );
      }
      catch (error)
      {
        await fs.rm(
          agent_memory_lock_path,
          {
            recursive: true,
            force: true,
          },
        );
        throw error;
      }
      break;
    }
    catch (error)
    {
      if (error.code !== "EEXIST")
      {
        throw error;
      }

      try
      {
        const stats = await fs.stat(agent_memory_lock_path);
        if (Date.now() - stats.mtimeMs >= lock_stale_ms)
        {
          await fs.rm(
            agent_memory_lock_path,
            {
              recursive: true,
              force: true,
            },
          );
          continue;
        }
      }
      catch (stat_error)
      {
        if (stat_error.code !== "ENOENT")
        {
          throw stat_error;
        }
        continue;
      }

      if (Date.now() - started_at >= lock_timeout_ms)
      {
        throw new Error("timed out waiting for agent memory lock");
      }
      await wait(lock_retry_ms);
    }
  }

  try
  {
    return await operation();
  }
  finally
  {
    try
    {
      const active_owner = await fs.readFile(
        agent_memory_lock_owner_path,
        "utf8",
      );
      if (active_owner === owner)
      {
        await fs.rm(
          agent_memory_lock_path,
          {
            recursive: true,
            force: true,
          },
        );
      }
    }
    catch
    {
    }
  }
}

export async function ensure_agent_memory()
{
  try
  {
    await fs.access(agent_memory_path);
  }
  catch
  {
    try
    {
      await fs.writeFile(
        agent_memory_path,
        initial_memory,
        {
          encoding: "utf8",
          flag: "wx",
        },
      );
    }
    catch (error)
    {
      if (error.code !== "EEXIST")
      {
        throw error;
      }
    }
  }
}

export async function read_agent_memory()
{
  await ensure_agent_memory();
  return fs.readFile(agent_memory_path, "utf8");
}

function to_lf(text)
{
  return String(text ?? "").replace(/\r\n/g, "\n");
}

// keeps whatever line ending the file already uses so appends never mix CRLF and LF
async function agent_memory_eol()
{
  try
  {
    return (await fs.readFile(agent_memory_path, "utf8")).includes("\r\n") ? "\r\n" : "\n";
  }
  catch
  {
    return "\n";
  }
}

async function write_agent_memory_unlocked(text)
{
  const value = to_lf(text).trimEnd();
  if (!value.startsWith("# Spartan Agent Memory"))
  {
    throw new Error("memory must start with # Spartan Agent Memory");
  }
  if (value.length > agent_memory_max_chars)
  {
    throw new Error(`memory is ${value.length} chars, the limit is ${agent_memory_max_chars}; prune stale notes or move old ones to ${path.basename(agent_memory_archive_path)}`);
  }

  const eol = await agent_memory_eol();
  await fs.writeFile(agent_memory_path, `${value}\n`.replace(/\n/g, eol), "utf8");
  return read_agent_memory();
}

export async function write_agent_memory(text)
{
  return with_agent_memory_lock(
    () => write_agent_memory_unlocked(text),
  );
}

function find_section(memory, heading)
{
  const escaped = heading.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const match = new RegExp(`^${escaped}[ \\t]*$`, "m").exec(memory);
  if (!match)
  {
    return null;
  }
  const body_start = match.index + match[0].length;
  const next = memory.indexOf("\n## ", body_start);
  return {
    start: match.index,
    body_start,
    end: next === -1 ? memory.length : next,
  };
}

function split_bullets(body)
{
  const bullets = [];
  for (const line of body.split("\n"))
  {
    if (line.startsWith("- ") || bullets.length === 0)
    {
      bullets.push(line);
    }
    else
    {
      bullets[bullets.length - 1] += `\n${line}`;
    }
  }
  return bullets.map((bullet) => bullet.trimEnd()).filter((bullet) => bullet.startsWith("- "));
}

async function archive_bullets(bullets)
{
  let archive = "";
  try
  {
    archive = to_lf(await fs.readFile(agent_memory_archive_path, "utf8"));
  }
  catch
  {
    archive = "# Spartan Agent Memory Archive\n\nOlder lessons moved out of AGENT_MEMORY.md when it reached its size limit. Search it with agent_memory_read {archive_query}.\n";
  }
  await fs.writeFile(agent_memory_archive_path, `${archive.trimEnd()}\n${bullets.join("\n")}\n`, "utf8");
}

function parse_sections(memory)
{
  const first = memory.indexOf("\n## ");
  const header = (first === -1 ? memory : memory.slice(0, first)).trimEnd();
  const sections = [];
  if (first !== -1)
  {
    for (const chunk of memory.slice(first + 1).split(/\n(?=## )/))
    {
      const newline = chunk.indexOf("\n");
      const heading = (newline === -1 ? chunk : chunk.slice(0, newline)).replace(/^##\s*/, "").trim();
      const body = newline === -1 ? "" : chunk.slice(newline + 1);
      sections.push({ heading, bullets: split_bullets(`\n${body}`) });
    }
  }
  return { header, sections };
}

function build_memory({ header, sections })
{
  const parts = [header];
  for (const section of sections)
  {
    if (section.bullets.length === 0 && !pinned_sections.has(section.heading))
    {
      continue;
    }
    parts.push(`## ${section.heading}\n${section.bullets.join("\n")}`);
  }
  return parts.join("\n\n");
}

// moves notes to the archive until the memory fits so appends never get rejected for size:
// dated notes oldest first, then undated notes of topic sections, pinned sections stay
async function fit_agent_memory(memory, keep_bullet)
{
  if (memory.length <= agent_memory_max_chars)
  {
    return { memory, archived: [] };
  }

  const parsed = parse_sections(memory);
  const candidates = [];
  let order = 0;
  for (const section of parsed.sections)
  {
    if (pinned_sections.has(section.heading))
    {
      continue;
    }
    for (const bullet of section.bullets)
    {
      if (bullet === keep_bullet)
      {
        continue;
      }
      const date = /^- (\d{4}-\d{2}-\d{2})/.exec(bullet)?.[1] ?? "9999-99-99";
      candidates.push({ section, bullet, date, order: order++ });
    }
  }
  candidates.sort((a, b) => a.date.localeCompare(b.date) || a.order - b.order);

  const archived = [];
  let current = memory;
  for (const candidate of candidates)
  {
    if (current.length <= agent_memory_max_chars)
    {
      break;
    }
    candidate.section.bullets.splice(candidate.section.bullets.indexOf(candidate.bullet), 1);
    archived.push(`- [${candidate.section.heading}] ${candidate.bullet.slice(2)}`);
    current = build_memory(parsed);
  }
  if (current.length > agent_memory_max_chars)
  {
    throw new Error(`memory is ${current.length} chars even after archiving every topic note, the limit is ${agent_memory_max_chars}; shorten the pinned sections (${[...pinned_sections].join(", ")}) with agent_memory_replace`);
  }

  await archive_bullets(archived);
  return { memory: current, archived };
}

export async function append_agent_memory(section, note)
{
  const section_name = String(section ?? "").trim();
  const note_text = String(note ?? "").trim();
  if (!section_name || !note_text)
  {
    throw new Error("section and note are required");
  }
  if (note_text.length > agent_memory_note_max_chars)
  {
    throw new Error(`note is ${note_text.length} chars, keep one note under ${agent_memory_note_max_chars}: state the rule and where it lives, drop the story`);
  }

  return with_agent_memory_lock(async () =>
  {
    const memory = to_lf(await read_agent_memory());
    const heading = `## ${section_name}`;
    const bullet = note_text.startsWith("- ")
      ? note_text
      : `- ${note_text}`;
    if (memory.includes(bullet))
    {
      return { section: section_name, duplicate: true, chars: memory.trimEnd().length, limit: agent_memory_max_chars, archived: [] };
    }

    let updated;
    const existing = find_section(memory, heading);
    if (!existing)
    {
      updated = `${memory.trimEnd()}\n\n${heading}\n${bullet}\n`;
    }
    else
    {
      const before = memory.slice(0, existing.end).trimEnd();
      const after = memory.slice(existing.end);
      updated = `${before}\n${bullet}\n${after.trimEnd()}\n`;
    }

    const fitted = await fit_agent_memory(updated.trimEnd(), bullet);
    const written = await write_agent_memory_unlocked(fitted.memory);
    return {
      section: section_name,
      duplicate: false,
      chars: to_lf(written).trimEnd().length,
      limit: agent_memory_max_chars,
      archived: fitted.archived.map((note) => note.slice(0, 120)),
    };
  });
}

export async function search_agent_memory_archive(query)
{
  let archive = "";
  try
  {
    archive = to_lf(await fs.readFile(agent_memory_archive_path, "utf8"));
  }
  catch
  {
    return [];
  }
  const terms = String(query ?? "").toLowerCase().split(/\s+/).filter(Boolean);
  return split_bullets(archive).filter((bullet) =>
  {
    const text = bullet.toLowerCase();
    return terms.every((term) => text.includes(term));
  });
}
