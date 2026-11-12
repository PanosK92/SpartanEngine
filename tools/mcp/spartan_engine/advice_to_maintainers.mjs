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
export const advice_path = path.join(__dirname, "ADVICE_TO_MAINTAINERS.md");
export const advice_note_max_chars = 600;
// the kind an agent passes and the section its item lands in
export const advice_kinds = {
  bug: "Bugs",
  ux: "UX Improvements",
};
const advice_title = "# Advice To Maintainers";
const advice_lock_path = `${advice_path}.lock`;
const advice_lock_owner_path = path.join(advice_lock_path, "owner");
const lock_retry_ms = 50;
const lock_stale_ms = 10000;
const lock_timeout_ms = 12000;

const initial_advice = `${advice_title}

What the AI agents working on Spartan Engine ask the maintainers to fix. One line per item: where it happens, what goes wrong or is missing, and what would fix it. Agents add items with \`advice_to_maintainers_append\` and remove the ones they have verified as fixed with \`advice_to_maintainers_resolve\`. This is not agent memory: no how-tos, no lessons, no session notes.

## ${advice_kinds.bug}

## ${advice_kinds.ux}
`;

function wait(delay_ms)
{
  return new Promise((resolve) =>
  {
    setTimeout(resolve, delay_ms);
  });
}

// several mcp servers write the same file, a lock directory keeps their edits from overwriting each other
async function with_advice_lock(operation)
{
  const started_at = Date.now();
  const owner = `${process.pid}.${Date.now()}.` + Math.random().toString(16).slice(2);

  while (true)
  {
    try
    {
      await fs.mkdir(advice_lock_path);
      try
      {
        await fs.writeFile(advice_lock_owner_path, owner, "utf8");
      }
      catch (error)
      {
        await fs.rm(advice_lock_path, { recursive: true, force: true });
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
        const stats = await fs.stat(advice_lock_path);
        if (Date.now() - stats.mtimeMs >= lock_stale_ms)
        {
          await fs.rm(advice_lock_path, { recursive: true, force: true });
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
        throw new Error("timed out waiting for the advice to maintainers lock");
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
      const active_owner = await fs.readFile(advice_lock_owner_path, "utf8");
      if (active_owner === owner)
      {
        await fs.rm(advice_lock_path, { recursive: true, force: true });
      }
    }
    catch
    {
    }
  }
}

export async function ensure_advice()
{
  try
  {
    await fs.access(advice_path);
  }
  catch
  {
    try
    {
      await fs.writeFile(advice_path, initial_advice, { encoding: "utf8", flag: "wx" });
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

export async function read_advice()
{
  await ensure_advice();
  return fs.readFile(advice_path, "utf8");
}

// the file keeps whatever line ending it already has
async function write_advice_lines(lines, eol)
{
  await fs.writeFile(advice_path, `${lines.join("\n").trimEnd()}\n`.replace(/\n/g, eol), "utf8");
}

async function read_advice_lines()
{
  const text = await read_advice();
  return {
    lines: text.replace(/\r\n/g, "\n").trimEnd().split("\n"),
    eol: text.includes("\r\n") ? "\r\n" : "\n",
  };
}

function is_item(line)
{
  return line.startsWith("- ");
}

function count_items(lines)
{
  return lines.filter(is_item).length;
}

// the same request reported on another day is still the same request
function item_key(line)
{
  return line.replace(/^-\s+/, "").replace(/^\d{4}-\d{2}-\d{2}:?\s*/, "").replace(/\s+/g, " ").trim().toLowerCase();
}

function today()
{
  const now = new Date();
  const pad = (value) => String(value).padStart(2, "0");
  return `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`;
}

export function advice_kind(value)
{
  const kind = String(value ?? "").trim().toLowerCase();
  if (!Object.hasOwn(advice_kinds, kind))
  {
    throw new Error(`kind must be one of: ${Object.keys(advice_kinds).join(", ")}`);
  }
  return kind;
}

export async function append_advice(kind_value, note)
{
  const kind = advice_kind(kind_value);
  const section = advice_kinds[kind];
  const text = String(note ?? "").replace(/^-\s+/, "").replace(/\s+/g, " ").trim();
  if (!text)
  {
    throw new Error("note is required");
  }
  if (text.length > advice_note_max_chars)
  {
    throw new Error(`note is ${text.length} chars, keep one item under ${advice_note_max_chars}: where it happens, what goes wrong, what would fix it`);
  }
  const item = /^\d{4}-\d{2}-\d{2}\b/.test(text) ? `- ${text}` : `- ${today()} ${text}`;

  return with_advice_lock(async () =>
  {
    const { lines, eol } = await read_advice_lines();
    const key = item_key(item);
    const existing = lines.find((line) => is_item(line) && item_key(line) === key);
    if (existing)
    {
      return { kind, section, item: existing.slice(2), duplicate: true, items: count_items(lines) };
    }

    const heading = `## ${section}`;
    let start = lines.findIndex((line) => line.trim() === heading);
    if (start === -1)
    {
      lines.push("", heading);
      start = lines.length - 1;
    }
    let end = lines.findIndex((line, index) => index > start && line.startsWith("## "));
    if (end === -1)
    {
      end = lines.length;
    }
    // the new item goes right after the last line of the section that is not blank
    let insert_at = end;
    while (insert_at > start + 1 && lines[insert_at - 1].trim() === "")
    {
      insert_at--;
    }
    lines.splice(insert_at, 0, item);
    if (insert_at + 1 < lines.length && lines[insert_at + 1].startsWith("## "))
    {
      lines.splice(insert_at + 1, 0, "");
    }

    await write_advice_lines(lines, eol);
    return { kind, section, item: item.slice(2), duplicate: false, items: count_items(lines) };
  });
}

// removes the one item that contains the text, for requests that were fixed and verified
export async function resolve_advice(match)
{
  const needle = String(match ?? "").replace(/\s+/g, " ").trim().toLowerCase();
  if (!needle)
  {
    throw new Error("match is required, pass text that appears in exactly one item");
  }

  return with_advice_lock(async () =>
  {
    const { lines, eol } = await read_advice_lines();
    const hits = [];
    lines.forEach((line, index) =>
    {
      if (is_item(line) && line.replace(/\s+/g, " ").toLowerCase().includes(needle))
      {
        hits.push(index);
      }
    });
    if (hits.length === 0)
    {
      throw new Error("no item contains that text");
    }
    if (hits.length > 1)
    {
      const shown = hits.slice(0, 5).map((index) => lines[index].slice(2, 122)).join(" | ");
      throw new Error(`${hits.length} items contain that text, pass more of the one you mean: ${shown}`);
    }

    const [resolved] = lines.splice(hits[0], 1);
    await write_advice_lines(lines, eol);
    return { resolved: resolved.slice(2), items: count_items(lines) };
  });
}
