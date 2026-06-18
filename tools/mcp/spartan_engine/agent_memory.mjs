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
- Mutating scene tools require edit mode.
- \`execute_lua\` is the broad capability layer for procedural scene edits and is edit-mode guarded.
- \`context_snapshot\` is the fastest first read for engine status, world summary, and selection.
- \`component_get\` exposes editable properties for an entity component before calling \`component_set\`.

## Good Agent Strategies
- Start engine tasks with \`spartan_status\` or \`context_snapshot\`.
- Use \`search_capabilities\` and \`get_capability_details\` before guessing tool names.
- Resolve targets with \`entity_resolve\` before mutating named or selected entities.
- For repeated scene work, prefer \`entity_create_primitive_batch\` or one focused \`execute_lua\` script.
- For source questions, use \`search_codebase\`, then \`read_source_file\` for focused context.

## Gotchas
- World loading blocks many engine commands until loading completes.
- \`component_set\` supports editable properties only for supported component types and known property names.
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
- Consider viewport screenshot or render evidence tools so agents can verify visual scene edits.
- Consider request ids or framed messages for the C++ bridge if multiple clients become common.
- Consider async task reporting for long-running engine operations instead of blocking the main MCP request.

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
