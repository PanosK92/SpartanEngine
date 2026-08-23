import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { CodebaseIndex } from "./codebase_index.mjs";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const project_root = path.resolve(__dirname, "../../..");

let shared_codebase = null;

export function get_project_root() {
  return project_root;
}

export async function resolve_readable_path(file_path)
{
  const raw = String(file_path ?? "").trim();
  if (!raw)
  {
    return "";
  }
  const candidates = path.isAbsolute(raw)
    ? [raw]
    : [
        path.resolve(raw),
        path.resolve(project_root, raw),
        path.resolve(project_root, "bin", raw),
        path.resolve(project_root, "bin", "Release", raw),
        path.resolve(project_root, "bin", "Debug", raw),
        path.resolve(project_root, "bin", "x64", "Release", raw),
        path.resolve(project_root, "bin", "x64", "Debug", raw),
        path.resolve(process.cwd(), raw),
      ];
  for (const candidate of candidates)
  {
    try
    {
      await fs.access(candidate);
      return candidate;
    }
    catch
    {
    }
  }
  return raw;
}

export function get_shared_codebase() {
  if (!shared_codebase) {
    shared_codebase = new CodebaseIndex(project_root);
    void shared_codebase.ensure().catch((error) => {
      console.error(`spartan codebase indexing failed: ${error.message}`);
    });
  }

  return shared_codebase;
}
