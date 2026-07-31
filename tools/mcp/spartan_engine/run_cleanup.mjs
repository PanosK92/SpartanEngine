import fs from "node:fs/promises";
import path from "node:path";

const reference_keys = new Set([
  "prefab_path",
  "mesh_path",
  "material_path",
  "texture_path",
]);

function normalized_path(value)
{
  return String(value ?? "")
    .replaceAll("\\", "/")
    .replace(/^\/+/, "");
}

function resource_root_path(
  project_root,
  resource_directory,
)
{
  const normalized = normalized_path(resource_directory);
  const binaries_root = path.resolve(
    project_root,
    "binaries",
  );
  if (path.isAbsolute(resource_directory))
  {
    return path.resolve(resource_directory);
  }
  return path.resolve(binaries_root, normalized);
}

async function list_files(root)
{
  const files = [];
  const pending = [root];
  while (pending.length > 0)
  {
    const directory = pending.pop();
    const entries = await fs.readdir(
      directory,
      {
        withFileTypes: true,
      },
    );
    for (const entry of entries)
    {
      const local_path = path.join(directory, entry.name);
      if (entry.isSymbolicLink())
      {
        continue;
      }
      if (entry.isDirectory())
      {
        pending.push(local_path);
      }
      else if (entry.isFile())
      {
        files.push(local_path);
      }
    }
  }
  return files;
}

async function file_fingerprint(local_path)
{
  const status = await fs.stat(local_path);
  return {
    size: status.size,
    mtime_ms: status.mtimeMs,
  };
}

async function scan_resources(root)
{
  const result = new Map();
  for (const local_path of await list_files(root))
  {
    const relative_path = normalized_path(
      path.relative(root, local_path),
    );
    result.set(
      relative_path,
      await file_fingerprint(local_path),
    );
  }
  return result;
}

export async function snapshot_run_resources({
  project_root,
  resource_directory,
})
{
  const requested_root = resource_root_path(
    project_root,
    resource_directory,
  );
  await fs.mkdir(requested_root, { recursive: true });
  const root = await fs.realpath(requested_root);
  return {
    root,
    real_root: root,
    files: await scan_resources(root),
    captured_at: new Date().toISOString(),
  };
}

function relative_resource_path(root, value)
{
  const raw = String(value ?? "");
  if (!raw || raw.includes("\0"))
  {
    return "";
  }
  if (path.isAbsolute(raw))
  {
    const relative = normalized_path(
      path.relative(root, path.resolve(raw)),
    );
    return (
      relative &&
      relative !== ".." &&
      !relative.startsWith("../")
    )
      ? relative
      : "";
  }
  const normalized = normalized_path(raw);
  const marker = "mcp_resources/";
  const marker_index = normalized
    .toLowerCase()
    .indexOf(marker);
  if (marker_index >= 0)
  {
    return safe_relative_path(
      normalized.slice(marker_index + marker.length),
    );
  }
  return safe_relative_path(
    normalized.replace(/^project\//i, ""),
  );
}

function safe_relative_path(value)
{
  const normalized = path.posix.normalize(
    normalized_path(value),
  );
  if (
    !normalized ||
    normalized === "." ||
    normalized === ".." ||
    normalized.startsWith("../") ||
    path.posix.isAbsolute(normalized)
  )
  {
    return "";
  }
  return normalized;
}

function collect_json_references(value, references)
{
  if (Array.isArray(value))
  {
    for (const item of value)
    {
      collect_json_references(item, references);
    }
    return;
  }
  if (!value || typeof value !== "object")
  {
    return;
  }
  for (const [key, item] of Object.entries(value))
  {
    if (
      reference_keys.has(key.toLowerCase()) &&
      typeof item === "string"
    )
    {
      references.add(item);
    }
    collect_json_references(item, references);
  }
}

function references_from_text(text)
{
  const references = new Set();
  try
  {
    collect_json_references(
      JSON.parse(text),
      references,
    );
  }
  catch
  {
  }
  const key_pattern = new RegExp(
    "(?:prefab_path|mesh_path|" +
      "material_path|texture_path)" +
      "\\s*(?:=|:)\\s*[\"']([^\"']+)[\"']",
    "gi",
  );
  for (const match of text.matchAll(key_pattern))
  {
    references.add(match[1]);
  }
  const resource_pattern = new RegExp(
    "(?:project/)?mcp_resources/" +
      "[a-z0-9_./-]+\\." +
      "(?:prefab|mesh|material|xml|" +
      "png|jpg|jpeg|dds|tga)",
    "gi",
  );
  for (const match of text.matchAll(resource_pattern))
  {
    references.add(match[0]);
  }
  const quoted_path_pattern = new RegExp(
    "[\"']([^\"'\\r\\n]+\\.[a-z0-9]{1,16})[\"']",
    "gi",
  );
  for (const match of text.matchAll(quoted_path_pattern))
  {
    references.add(match[1]);
  }
  return [...references];
}

function texture_family_name(file_name)
{
  const extension = path.extname(file_name);
  return path
    .basename(file_name, extension)
    .replace(
      new RegExp(
        "_(?:albedo|base_color|color|diffuse|" +
          "normal|roughness|metallic|metalness|" +
          "ao|ambient_occlusion|packed|orm|" +
          "opacity|alpha|emissive)$",
        "i",
      ),
      "",
    );
}

function add_texture_siblings(
  relative_path,
  available,
  reachable,
)
{
  const extension = path.extname(relative_path)
    .toLowerCase();
  if (
    ![
      ".png",
      ".jpg",
      ".jpeg",
      ".dds",
      ".tga",
    ].includes(extension)
  )
  {
    return;
  }
  const directory = normalized_path(
    path.posix.dirname(relative_path),
  );
  const family = texture_family_name(
    path.posix.basename(relative_path),
  );
  for (const entry of available)
  {
    if (
      normalized_path(path.posix.dirname(entry)) ===
        directory &&
      texture_family_name(path.posix.basename(entry)) ===
        family
    )
    {
      reachable.add(entry);
    }
  }
}

async function reachable_resources(
  root,
  available,
  starting_paths,
)
{
  const reachable = new Set();
  const resolve_available = (
    value,
    referring_path = "",
  ) =>
  {
    const relative = relative_resource_path(
      root,
      value,
    );
    if (available.has(relative))
    {
      return relative;
    }
    const raw = normalized_path(value);
    const is_anchored =
      path.isAbsolute(String(value ?? "")) ||
      raw.toLowerCase().includes("mcp_resources/") ||
      raw.toLowerCase().startsWith("project/");
    if (!is_anchored && referring_path)
    {
      const local_relative = safe_relative_path(
        path.posix.join(
          path.posix.dirname(referring_path),
          raw,
        ),
      );
      if (available.has(local_relative))
      {
        return local_relative;
      }
    }
    const file_name = path.posix.basename(
      relative || raw,
    );
    const suffix = `/${file_name}`;
    const matches = [...available].filter(
      (entry) =>
        entry === file_name ||
        entry.endsWith(suffix),
    );
    return matches.length === 1
      ? matches[0]
      : "";
  };
  const pending = starting_paths
    .map(resolve_available)
    .filter(Boolean);
  while (pending.length > 0)
  {
    const relative_path = pending.pop();
    if (reachable.has(relative_path))
    {
      continue;
    }
    reachable.add(relative_path);
    add_texture_siblings(
      relative_path,
      available,
      reachable,
    );
    let text;
    try
    {
      text = await fs.readFile(
        path.join(root, relative_path),
        "utf8",
      );
    }
    catch (error)
    {
      throw new Error(
        `failed to read resource dependency ${relative_path}: ${error.message}`,
      );
    }
    for (const reference of references_from_text(text))
    {
      const dependency = resolve_available(
        reference,
        relative_path,
      );
      if (
        available.has(dependency) &&
        !reachable.has(dependency)
      )
      {
        pending.push(dependency);
      }
    }
  }
  return reachable;
}

function resolve_protected_paths(
  root,
  available,
  protected_paths,
)
{
  const protected_resources = new Set();
  for (const value of protected_paths)
  {
    const relative = relative_resource_path(
      root,
      value,
    );
    if (available.has(relative))
    {
      protected_resources.add(relative);
      continue;
    }
    const file_name = path.posix.basename(relative);
    const matches = [...available].filter(
      (entry) =>
        path.posix.basename(entry) === file_name,
    );
    if (matches.length === 1)
    {
      protected_resources.add(matches[0]);
    }
  }
  return protected_resources;
}

function resolve_exact_paths(
  root,
  available,
  values,
)
{
  const resolved = new Set();
  for (const value of values)
  {
    const relative = relative_resource_path(
      root,
      value,
    );
    if (available.has(relative))
    {
      resolved.add(relative);
    }
  }
  return resolved;
}

async function contained_file_path(
  root,
  real_root,
  relative_path,
)
{
  const safe_relative = safe_relative_path(relative_path);
  if (!safe_relative)
  {
    throw new Error("resource path is not contained");
  }
  const local_path = path.resolve(
    root,
    ...safe_relative.split("/"),
  );
  if (
    local_path === root ||
    !local_path.startsWith(`${root}${path.sep}`)
  )
  {
    throw new Error("resource path escapes the root");
  }
  if (await fs.realpath(root) !== real_root)
  {
    throw new Error("resource root identity changed");
  }
  let current = root;
  for (const part of safe_relative.split("/"))
  {
    current = path.join(current, part);
    const status = await fs.lstat(current);
    if (status.isSymbolicLink())
    {
      throw new Error("resource path crosses a link");
    }
  }
  const status = await fs.lstat(local_path);
  if (!status.isFile())
  {
    throw new Error("resource deletion target is not a file");
  }
  return local_path;
}

export async function cleanup_run_resources({
  snapshot,
  final_prefab_path = "",
  latest_prefab_path = "",
  protected_paths = [],
  owned_paths = [],
  outcome = "unknown",
})
{
  if (!snapshot?.root || !(snapshot.files instanceof Map))
  {
    return {
      ok: false,
      outcome,
      error: "resource snapshot is unavailable",
      removed: [],
      failed: [],
    };
  }
  let real_root;
  let current;
  try
  {
    real_root = await fs.realpath(snapshot.root);
    if (
      snapshot.real_root &&
      real_root !== snapshot.real_root
    )
    {
      throw new Error("resource root identity changed");
    }
    current = await scan_resources(real_root);
  }
  catch (error)
  {
    return {
      ok: false,
      outcome,
      error: `resource scan failed: ${error.message}`,
      removed: [],
      failed: [],
    };
  }
  const created = [];
  const overwritten = [];
  const unchanged_pre_existing = [];
  for (const [relative_path, fingerprint] of current)
  {
    const previous = snapshot.files.get(relative_path);
    if (!previous)
    {
      created.push(relative_path);
    }
    else if (
      previous.mtime_ms !== fingerprint.mtime_ms ||
      previous.size !== fingerprint.size
    )
    {
      overwritten.push(relative_path);
    }
    else
    {
      unchanged_pre_existing.push(relative_path);
    }
  }
  const available = new Set(current.keys());
  let reachable;
  let protected_resources;
  try
  {
    reachable = await reachable_resources(
      real_root,
      available,
      [
        final_prefab_path,
        latest_prefab_path,
        ...protected_paths,
      ].filter(Boolean),
    );
    protected_resources = resolve_protected_paths(
      real_root,
      available,
      protected_paths,
    );
  }
  catch (error)
  {
    return {
      ok: false,
      outcome,
      captured_at: snapshot.captured_at,
      created,
      overwritten,
      error:
        `resource dependency scan failed: ${error.message}`,
      removed: [],
      failed: [],
    };
  }
  const owned_resources = resolve_exact_paths(
    real_root,
    available,
    owned_paths,
  );
  const owned_created = [...owned_resources].filter(
    (relative_path) =>
      !snapshot.files.has(relative_path),
  );
  const removed = [];
  const failed = [];
  for (const relative_path of owned_created)
  {
    if (
      reachable.has(relative_path) ||
      protected_resources.has(relative_path)
    )
    {
      continue;
    }
    try
    {
      const local_path = await contained_file_path(
        real_root,
        snapshot.real_root ?? real_root,
        relative_path,
      );
      await fs.unlink(local_path);
      removed.push(relative_path);
    }
    catch (error)
    {
      failed.push({
        path: relative_path,
        error: error.message,
      });
    }
  }
  return {
    ok: failed.length === 0,
    outcome,
    captured_at: snapshot.captured_at,
    created,
    overwritten,
    owned_created,
    reachable: [...reachable],
    protected_run_outputs:
      [...protected_resources],
    protected_pre_existing:
      unchanged_pre_existing.length,
    removed,
    failed,
  };
}
