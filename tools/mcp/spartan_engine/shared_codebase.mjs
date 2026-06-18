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

export function get_shared_codebase() {
  if (!shared_codebase) {
    shared_codebase = new CodebaseIndex(project_root);
    void shared_codebase.ensure().catch((error) => {
      console.error(`spartan codebase indexing failed: ${error.message}`);
    });
  }

  return shared_codebase;
}
