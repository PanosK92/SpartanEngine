import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

export const debug_log_path = path.join(__dirname, "MCP_DEBUG.jsonl");
const max_log_bytes = 4 * 1024 * 1024;
const max_string_length = 1200;

function truncate_string(value, max_length = max_string_length) {
  if (value.length <= max_length) {
    return value;
  }
  return `${value.slice(0, max_length - 32)}...<truncated ${value.length} chars>`;
}

function sanitize_value(value, depth = 0) {
  if (depth > 5) {
    return "<max depth>";
  }
  if (typeof value === "string") {
    return truncate_string(value);
  }
  if (typeof value === "number" || typeof value === "boolean" || value === null || value === undefined) {
    return value;
  }
  if (Array.isArray(value)) {
    const items = value.slice(0, 32).map((item) => sanitize_value(item, depth + 1));
    if (value.length > 32) {
      items.push(`<truncated ${value.length - 32} items>`);
    }
    return items;
  }
  if (typeof value === "object") {
    const result = {};
    for (const [key, item] of Object.entries(value)) {
      if (key === "api_key") {
        result[key] = "<redacted>";
      } else if (key === "code" && typeof item === "string") {
        result[key] = `<${item.length} chars>`;
      } else if (
        key === "property_metadata" ||
        key === "member_metadata"
      ) {
        result[key] = `<omitted ${item?.length ?? 0} items>`;
      } else {
        result[key] = sanitize_value(item, depth + 1);
      }
    }
    return result;
  }
  return String(value);
}

async function rotate_if_needed() {
  try {
    const stats = await fs.stat(debug_log_path);
    if (stats.size <= max_log_bytes) {
      return;
    }

    const rotated_path = `${debug_log_path}.1`;
    await fs.rm(rotated_path, { force: true });
    await fs.rename(debug_log_path, rotated_path);
  } catch {
  }
}

export async function append_debug_log(event) {
  const entry = {
    timestamp: new Date().toISOString(),
    ...sanitize_value(event),
  };
  try {
    await rotate_if_needed();
    await fs.appendFile(debug_log_path, `${JSON.stringify(entry)}\n`, "utf8");
  } catch {
  }
}

export async function read_debug_log(limit = 80) {
  const safe_limit = Math.max(1, Math.min(Number.parseInt(String(limit), 10) || 80, 500));
  try {
    const text = await fs.readFile(debug_log_path, "utf8");
    return text.split(/\r?\n/).filter(Boolean).slice(-safe_limit).join("\n");
  } catch {
    return "";
  }
}
