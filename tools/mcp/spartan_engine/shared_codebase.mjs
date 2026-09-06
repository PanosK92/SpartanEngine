/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

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
