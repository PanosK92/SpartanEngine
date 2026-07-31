import fs from "node:fs/promises";
import path from "node:path";
import { createHash } from "node:crypto";
import { AsyncLocalStorage } from "node:async_hooks";
import os from "node:os";

const catalog_version = 2;
const asset_types = new Set([
  "mesh",
  "material",
  "prefab",
  "texture",
]);
const type_folders = {
  mesh: "meshes",
  material: "materials",
  prefab: "prefabs",
  texture: "textures",
};
const type_extensions = {
  mesh: ".mesh",
  material: ".xml",
  prefab: ".prefab",
  texture: ".png",
};
const catalog_queues = new Map();
const catalog_lock_context = new AsyncLocalStorage();
const catalog_lock_timeout_ms = 15000;
const catalog_lock_stale_ms = 300000;
const catalog_lock_unreadable_stale_ms = 3600000;
const catalog_lock_retry_min_ms = 20;
const catalog_lock_retry_max_ms = 250;

function semantic_asset_name(value, fallback = "asset")
{
  const normalized = String(value ?? fallback)
    .trim()
    .replace(
      /^(?:build|create|generate|make)(?=[A-Z_\-\s])/,
      "",
    )
    .replace(/^[_\-\s]+/, "");
  return normalized || fallback;
}

function safe_name(value, fallback = "asset")
{
  return semantic_asset_name(value, fallback)
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/^_+|_+$/g, "") ||
    fallback;
}

function unique_strings(values)
{
  return [...new Set(
    (Array.isArray(values) ? values : [])
      .map((value) => String(value).trim().toLowerCase())
      .filter(Boolean),
  )];
}

function finite_number(value, fallback = 0)
{
  const number = Number(value);
  return Number.isFinite(number)
    ? number
    : fallback;
}

function normalize_constraints(value)
{
  if (!value || typeof value !== "object" || Array.isArray(value))
  {
    return {};
  }
  return {
    ...value,
    dimensions: value.dimensions ?? {},
    style: unique_strings(value.style),
    materials: unique_strings(value.materials),
  };
}

function normalize_engine_path(value)
{
  const normalized = String(value ?? "")
    .replaceAll("\\", "/")
    .replace(/^(?:\.\/)+/g, "")
    .replace(/^\/+/g, "");
  if (
    !normalized ||
    normalized.includes("\0") ||
    normalized.split("/").includes("..") ||
    path.posix.isAbsolute(normalized)
  )
  {
    throw new Error("asset path must be a safe project relative path");
  }
  return normalized;
}

function local_path(project_root, engine_path)
{
  const relative = normalize_engine_path(engine_path);
  const repository_root = path.resolve(project_root);
  const root = path.join(
    repository_root,
    "binaries",
  );
  const resolved = path.resolve(root, ...relative.split("/"));
  if (
    resolved !== root &&
    !resolved.startsWith(`${root}${path.sep}`)
  )
  {
    throw new Error("asset path escapes the project root");
  }
  return resolved;
}

function library_paths(project_root)
{
  const directory = "project";
  const engine_root = `${directory}/mcp_resources`;
  return {
    directory,
    engine_root,
    local_root: local_path(project_root, engine_root),
    catalog_path: `${engine_root}/catalog.json`,
    catalog_local_path: local_path(
      project_root,
      `${engine_root}/catalog.json`,
    ),
  };
}

async function path_exists(value)
{
  try
  {
    await fs.access(value);
    return true;
  }
  catch
  {
    try
    {
      const unreadable_status = await fs.stat(lock_path);
      if (
        Date.now() - unreadable_status.mtimeMs <
        catalog_lock_unreadable_stale_ms
      )
      {
        return false;
      }
      const current_status = await fs.stat(lock_path);
      if (
        current_status.mtimeMs !== unreadable_status.mtimeMs ||
        current_status.size !== unreadable_status.size
      )
      {
        return false;
      }
      await fs.rm(lock_path);
      return true;
    }
    catch
    {
      return false;
    }
  }
}

async function write_json_atomic(file_path, value)
{
  const temporary_path =
    `${file_path}.${process.pid}.${Date.now()}.` +
    `${Math.random().toString(16).slice(2)}.tmp`;
  await fs.writeFile(
    temporary_path,
    `${JSON.stringify(value, null, 2)}\n`,
    "utf8",
  );
  await fs.rename(temporary_path, file_path);
}

function transaction_token()
{
  return (
    `${process.pid}.${Date.now()}.` +
    Math.random().toString(16).slice(2)
  );
}

async function remove_path(value)
{
  await fs.rm(
    value,
    {
      recursive: true,
      force: true,
    },
  );
}

async function commit_staged_paths(entries, token)
{
  const committed = [];
  try
  {
    for (const entry of entries)
    {
      const backup = `${entry.target}.${token}.backup`;
      const had_target = await path_exists(entry.target);
      if (had_target)
      {
        await fs.rename(entry.target, backup);
      }
      const state = {
        ...entry,
        backup,
        had_target,
        installed: false,
      };
      committed.push(state);
      if (entry.staged)
      {
        await fs.rename(entry.staged, entry.target);
        state.installed = true;
      }
    }
  }
  catch (error)
  {
    const rollback_errors = [];
    for (const entry of [...committed].reverse())
    {
      try
      {
        if (entry.installed)
        {
          await remove_path(entry.target);
        }
        if (entry.had_target && await path_exists(entry.backup))
        {
          await fs.rename(entry.backup, entry.target);
        }
      }
      catch (rollback_error)
      {
        rollback_errors.push(rollback_error.message);
      }
    }
    if (rollback_errors.length > 0)
    {
      error.message +=
        `, rollback failed: ${rollback_errors.join(", ")}`;
    }
    throw error;
  }

  const cleanup_errors = [];
  for (const entry of committed)
  {
    if (!entry.had_target)
    {
      continue;
    }
    try
    {
      await remove_path(entry.backup);
    }
    catch (error)
    {
      cleanup_errors.push(error.message);
    }
  }
  return cleanup_errors;
}

function wait(milliseconds)
{
  return new Promise((resolve) =>
  {
    setTimeout(resolve, milliseconds);
  });
}

function process_is_alive(pid)
{
  if (!Number.isInteger(pid) || pid <= 0)
  {
    return false;
  }
  try
  {
    process.kill(pid, 0);
    return true;
  }
  catch (error)
  {
    return error.code !== "ESRCH";
  }
}

async function recover_stale_catalog_lock(lock_path)
{
  let text;
  let metadata;
  let status;
  try
  {
    [text, status] = await Promise.all([
      fs.readFile(lock_path, "utf8"),
      fs.stat(lock_path),
    ]);
    metadata = JSON.parse(text);
  }
  catch
  {
    return false;
  }
  const created_at = Number(metadata.created_at_ms);
  if (
    !metadata.owner ||
    metadata.hostname !== os.hostname() ||
    !Number.isFinite(created_at)
  )
  {
    return false;
  }
  const age = Date.now() - Math.max(
    created_at,
    status.mtimeMs,
  );
  if (
    age < catalog_lock_stale_ms ||
    process_is_alive(Number(metadata.pid))
  )
  {
    return false;
  }
  try
  {
    const current = await fs.readFile(lock_path, "utf8");
    if (current !== text)
    {
      return false;
    }
    await fs.rm(lock_path);
    return true;
  }
  catch
  {
    return false;
  }
}

async function acquire_catalog_file_lock(catalog_path)
{
  const lock_path = `${catalog_path}.lock`;
  const owner = transaction_token();
  const started_at = Date.now();
  let retry_delay = catalog_lock_retry_min_ms;
  await fs.mkdir(path.dirname(lock_path), { recursive: true });
  while (true)
  {
    let handle = null;
    try
    {
      handle = await fs.open(lock_path, "wx");
      const metadata = {
        owner,
        pid: process.pid,
        hostname: os.hostname(),
        created_at_ms: Date.now(),
        created_at: new Date().toISOString(),
      };
      await handle.writeFile(
        `${JSON.stringify(metadata, null, 2)}\n`,
        "utf8",
      );
      await handle.sync();
      await handle.close();
      handle = null;
      return async () =>
      {
        try
        {
          const current = JSON.parse(
            await fs.readFile(lock_path, "utf8"),
          );
          if (current.owner === owner)
          {
            await fs.rm(lock_path);
          }
        }
        catch
        {
        }
      };
    }
    catch (error)
    {
      if (handle)
      {
        await handle.close();
        await fs.rm(lock_path, { force: true });
      }
      if (error.code !== "EEXIST")
      {
        throw error;
      }
      if (await recover_stale_catalog_lock(lock_path))
      {
        continue;
      }
      const elapsed = Date.now() - started_at;
      if (elapsed >= catalog_lock_timeout_ms)
      {
        throw new Error(
          "timed out waiting for the world asset catalog lock",
        );
      }
      await wait(
        Math.min(
          retry_delay,
          catalog_lock_timeout_ms - elapsed,
        ),
      );
      retry_delay = Math.min(
        catalog_lock_retry_max_ms,
        Math.ceil(retry_delay * 1.5),
      );
    }
  }
}

async function with_catalog_lock(key, operation)
{
  const held_locks = catalog_lock_context.getStore();
  if (held_locks?.has(key))
  {
    return operation();
  }
  const previous = catalog_queues.get(key) ?? Promise.resolve();
  let release;
  const current = new Promise((resolve) =>
  {
    release = resolve;
  });
  catalog_queues.set(key, current);
  await previous;
  let release_file_lock = null;
  try
  {
    release_file_lock = await acquire_catalog_file_lock(key);
    const next_locks = new Set(held_locks ?? []);
    next_locks.add(key);
    return await catalog_lock_context.run(
      next_locks,
      operation,
    );
  }
  finally
  {
    try
    {
      if (release_file_lock)
      {
        await release_file_lock();
      }
    }
    finally
    {
      release();
      if (catalog_queues.get(key) === current)
      {
        catalog_queues.delete(key);
      }
    }
  }
}

async function ensure_catalog(
  project_root,
  resource_directory,
)
{
  const paths = library_paths(
    project_root,
    resource_directory,
  );
  await Promise.all(
    [
      "",
      "meshes",
      "materials",
      "prefabs",
      "sources",
      "thumbnails",
    ].map((folder) =>
      fs.mkdir(
        path.join(paths.local_root, folder),
        { recursive: true },
      ),
    ),
  );
  if (!(await path_exists(paths.catalog_local_path)))
  {
    await write_json_atomic(
      paths.catalog_local_path,
      {
        schema_version: catalog_version,
        project_resource_directory: paths.directory,
        assets: {},
      },
    );
  }
  return paths;
}

async function read_catalog_unlocked(
  project_root,
  resource_directory,
)
{
  const paths = await ensure_catalog(
    project_root,
    resource_directory,
  );
  let catalog = JSON.parse(
    await fs.readFile(paths.catalog_local_path, "utf8"),
  );
  const assets_are_empty =
    catalog.assets &&
    typeof catalog.assets === "object" &&
    !Array.isArray(catalog.assets) &&
    Object.keys(catalog.assets).length === 0;
  if (
    catalog.schema_version !== catalog_version &&
    assets_are_empty
  )
  {
    catalog = {
      schema_version: catalog_version,
      project_resource_directory: paths.directory,
      assets: {},
    };
    await write_json_atomic(
      paths.catalog_local_path,
      catalog,
    );
  }
  if (
    catalog.schema_version !== catalog_version ||
    !catalog.assets ||
    typeof catalog.assets !== "object" ||
    Array.isArray(catalog.assets)
  )
  {
    throw new Error("unsupported or invalid world asset catalog");
  }
  return {
    paths,
    catalog,
  };
}

async function read_catalog(
  project_root,
  resource_directory,
)
{
  const paths = library_paths(project_root);
  return with_catalog_lock(
    paths.catalog_local_path,
    () => read_catalog_unlocked(
      project_root,
      resource_directory,
    ),
  );
}

function asset_file_path(
  paths,
  type,
  asset_id,
  extension,
)
{
  const suffix = extension || type_extensions[type];
  return [
    paths.engine_root,
    type_folders[type],
    `${asset_id}${suffix}`,
  ].join("/");
}

function source_file_path(paths, asset_id)
{
  return [
    paths.engine_root,
    "sources",
    `${asset_id}.json`,
  ].join("/");
}

function thumbnail_file_path(
  paths,
  asset_id,
  extension,
)
{
  return [
    paths.engine_root,
    "thumbnails",
    `${asset_id}${extension || ".png"}`,
  ].join("/");
}

function dependency_file_name(reference, dependency_root)
{
  const normalized = normalize_engine_path(reference);
  if (normalized.startsWith(`${dependency_root}/`))
  {
    return path.posix.basename(normalized);
  }
  const basename = path.posix.basename(normalized);
  const extension = path.posix.extname(basename);
  const stem = basename.slice(
    0,
    basename.length - extension.length,
  );
  const hash = createHash("sha256")
    .update(normalized)
    .digest("hex")
    .slice(0, 12);
  return `${safe_name(stem)}_${hash}${extension.toLowerCase()}`;
}

async function internalize_prefab_dependencies(
  project_root,
  paths,
  asset_id,
  prefab_path,
  dependency_local_root,
)
{
  const prefab_local = local_path(project_root, prefab_path);
  const dependency_root =
    `${paths.engine_root}/dependencies/${asset_id}`;
  let text = await fs.readFile(prefab_local, "utf8");
  const copied = [];
  const missing = [];

  for (const attribute of ["mesh_path", "material_path"])
  {
    const references = new Set(
      [...text.matchAll(
        new RegExp(`${attribute}="([^"]+)"`, "g"),
      )].map((match) => match[1]),
    );
    for (const reference of references)
    {
      let normalized = null;
      try
      {
        normalized = normalize_engine_path(reference);
      }
      catch
      {
        missing.push(String(reference));
        continue;
      }
      const source_local = local_path(project_root, normalized);
      if (!(await path_exists(source_local)))
      {
        missing.push(normalized);
        continue;
      }

      const dependency_name = dependency_file_name(
        normalized,
        dependency_root,
      );
      const destination =
        `${dependency_root}/${dependency_name}`;
      const destination_local = path.join(
        dependency_local_root,
        dependency_name,
      );
      await fs.mkdir(
        path.dirname(destination_local),
        { recursive: true },
      );
      await fs.copyFile(source_local, destination_local);
      if (attribute === "material_path")
      {
        let material_text = await fs.readFile(
          destination_local,
          "utf8",
        );
        const texture_references = new Set(
          [...material_text.matchAll(
            /texture_path="([^"]+)"/g,
          )]
            .map((match) => match[1])
            .filter(Boolean),
        );
        for (const texture_reference of texture_references)
        {
          let texture_path = null;
          try
          {
            texture_path = normalize_engine_path(
              texture_reference,
            );
          }
          catch
          {
            missing.push(String(texture_reference));
            continue;
          }
          const texture_local = local_path(
            project_root,
            texture_path,
          );
          if (!(await path_exists(texture_local)))
          {
            missing.push(texture_path);
            continue;
          }

          const texture_name = dependency_file_name(
            texture_path,
            dependency_root,
          );
          const texture_destination =
            `${dependency_root}/${texture_name}`;
          const texture_destination_local = path.join(
            dependency_local_root,
            texture_name,
          );
          await fs.copyFile(
            texture_local,
            texture_destination_local,
          );
          material_text = material_text
            .split(`"${texture_reference}"`)
            .join(`"${texture_destination}"`);
          copied.push(texture_destination);
        }
        await fs.writeFile(
          destination_local,
          material_text,
          "utf8",
        );
      }
      text = text.split(`"${reference}"`).join(`"${destination}"`);
      copied.push(destination);
    }
  }

  if (copied.length > 0)
  {
    await fs.writeFile(prefab_local, text, "utf8");
  }
  return {
    copied: [...new Set(copied)],
    missing: [...new Set(missing)],
  };
}

function asset_summary(asset)
{
  return {
    id: asset.id,
    name: asset.name,
    type: asset.type,
    aliases: asset.aliases,
    tags: asset.tags,
    constraints: asset.constraints,
    path: asset.path,
    source_path: asset.source_path,
    thumbnail_path: asset.thumbnail_path,
    dependencies: asset.dependencies,
    quality: asset.quality,
  };
}

function owned_sidecar_path(
  paths,
  asset_id,
  folder,
  candidate,
)
{
  if (!candidate)
  {
    return null;
  }
  let normalized;
  try
  {
    normalized = normalize_engine_path(candidate);
  }
  catch
  {
    return null;
  }
  const expected_directory =
    `${paths.engine_root}/${folder}`;
  if (
    path.posix.dirname(normalized) !== expected_directory ||
    !path.posix.basename(normalized).startsWith(`${asset_id}.`)
  )
  {
    return null;
  }
  return normalized;
}

export async function resolve_world_resource_directory(
  _send_command,
  world = null,
)
{
  const provided_directory =
    world?.mcp_resources?.root ??
    world?.mcp_resource_directory ??
    world?.resource_directory;
  if (provided_directory)
  {
    return normalize_engine_path(
      provided_directory,
    ).replace(/\/+$/g, "");
  }

  return "project/mcp_resources";
}

export async function world_asset_catalog_entries(
  project_root,
  resource_directory,
)
{
  const { catalog } = await read_catalog(
    project_root,
    resource_directory,
  );
  return Object.values(catalog.assets).map(asset_summary);
}

export async function world_asset_search(
  project_root,
  resource_directory,
  args = {},
)
{
  const { catalog } = await read_catalog(
    project_root,
    resource_directory,
  );
  const terms = String(args.query ?? "")
    .toLowerCase()
    .split(/[^a-z0-9_-]+/g)
    .filter(Boolean);
  const required_tags = unique_strings(args.tags);
  const required_styles = unique_strings(args.style);
  const required_materials = unique_strings(args.materials);
  const required_dimensions =
    args.dimensions &&
    typeof args.dimensions === "object"
      ? args.dimensions
      : {};
  const matches = Object.values(catalog.assets)
    .filter((asset) =>
    {
      if (args.type && asset.type !== args.type)
      {
        return false;
      }
      if (
        required_tags.some(
          (tag) => !(asset.tags ?? []).includes(tag),
        )
      )
      {
        return false;
      }
      if (
        required_styles.some(
          (style) =>
            !(asset.constraints?.style ?? []).includes(style),
        ) ||
        required_materials.some(
          (material) =>
            !(asset.constraints?.materials ?? []).includes(material),
        )
      )
      {
        return false;
      }
      for (
        const [dimension, requested] of
        Object.entries(required_dimensions)
      )
      {
        const available =
          asset.constraints?.dimensions?.[dimension];
        if (available === undefined)
        {
          return false;
        }
        if (
          typeof requested === "number" &&
          Number(available) !== requested
        )
        {
          return false;
        }
        if (
          requested &&
          typeof requested === "object" &&
          (
            (
              requested.min !== undefined &&
              Number(available) < Number(requested.min)
            ) ||
            (
              requested.max !== undefined &&
              Number(available) > Number(requested.max)
            )
          )
        )
        {
          return false;
        }
      }
      const searchable = [
        asset.id,
        asset.name,
        ...(asset.aliases ?? []),
        ...(asset.tags ?? []),
        ...(asset.constraints?.style ?? []),
        ...(asset.constraints?.materials ?? []),
      ].join(" ").toLowerCase();
      return terms.every((term) => searchable.includes(term));
    })
    .map(asset_summary)
    .slice(0, Math.min(100, args.limit ?? 25));
  return {
    ok: true,
    matches,
    count: matches.length,
  };
}

export async function world_asset_inspect(
  project_root,
  resource_directory,
  args,
)
{
  const { paths, catalog } = await read_catalog(
    project_root,
    resource_directory,
  );
  const requested = String(args.asset_id ?? args.id ?? "")
    .toLowerCase();
  const asset =
    catalog.assets[requested] ??
    Object.values(catalog.assets).find((entry) =>
      entry.name.toLowerCase() === requested ||
      entry.aliases.includes(requested),
    );
  if (!asset)
  {
    return {
      ok: false,
      error: "asset not found",
    };
  }
  return {
    ok: true,
    catalog_path: paths.catalog_path,
    asset,
  };
}

async function world_asset_register_unlocked(
  project_root,
  resource_directory,
  args,
)
{
  const type = String(args.type ?? "").toLowerCase();
  if (!asset_types.has(type))
  {
    throw new Error(
      "asset type must be mesh, material, prefab, or texture",
    );
  }
  const source_path = normalize_engine_path(
    args.path ?? args.resource_path,
  );
  const { paths, catalog } = await read_catalog(
    project_root,
    resource_directory,
  );
  const asset_id = safe_name(
    args.asset_id ?? args.id ?? args.name,
  );
  const existing = catalog.assets[asset_id];
  if (existing && existing.type !== type)
  {
    throw new Error("asset id is already registered with another type");
  }
  const extension =
    path.posix.extname(source_path) ||
    type_extensions[type];
  if (extension.toLowerCase() !== type_extensions[type])
  {
    throw new Error(
      `${type} assets must use ${type_extensions[type]} files`,
    );
  }
  const destination_path = asset_file_path(
    paths,
    type,
    asset_id,
    extension,
  );
  if (!(await path_exists(local_path(project_root, source_path))))
  {
    throw new Error(
      `asset source file does not exist: ${source_path}`,
    );
  }
  const source_supplied = args.source !== undefined;
  const source_cleared =
    source_supplied &&
    args.source === null;
  const thumbnail_supplied =
    args.thumbnail_path !== undefined;
  const thumbnail_cleared =
    thumbnail_supplied &&
    args.thumbnail_path === null;
  let thumbnail_source = null;
  if (thumbnail_supplied && !thumbnail_cleared)
  {
    thumbnail_source = normalize_engine_path(
      args.thumbnail_path,
    );
    if (
      !await path_exists(
        local_path(project_root, thumbnail_source),
      )
    )
    {
      throw new Error(
        `asset thumbnail does not exist: ${thumbnail_source}`,
      );
    }
  }
  const token = transaction_token();
  const destination_local = local_path(
    project_root,
    destination_path,
  );
  const staged_asset_path =
    `${destination_path}.${token}.stage`;
  const staged_asset_local = local_path(
    project_root,
    staged_asset_path,
  );
  const dependency_path =
    `${paths.engine_root}/dependencies/${asset_id}`;
  const dependency_local = local_path(
    project_root,
    dependency_path,
  );
  const staged_dependency_local =
    `${dependency_local}.${token}.stage`;
  const staged_paths = [
    staged_asset_local,
    staged_dependency_local,
  ];
  let dependencies = null;
  let immutable_source_path =
    source_supplied
      ? null
      : existing?.source_path ?? null;
  let staged_source_local = null;
  if (source_supplied && !source_cleared)
  {
    immutable_source_path = source_file_path(
      paths,
      asset_id,
    );
    const source_local = local_path(
      project_root,
      immutable_source_path,
    );
    await fs.mkdir(path.dirname(source_local), { recursive: true });
    staged_source_local = `${source_local}.${token}.stage`;
    staged_paths.push(staged_source_local);
  }
  let immutable_thumbnail_path =
    thumbnail_supplied
      ? null
      : existing?.thumbnail_path ?? null;
  let staged_thumbnail_local = null;
  if (thumbnail_source)
  {
    immutable_thumbnail_path = thumbnail_file_path(
      paths,
      asset_id,
      path.posix.extname(thumbnail_source),
    );
    const thumbnail_local = local_path(
      project_root,
      immutable_thumbnail_path,
    );
    staged_thumbnail_local =
      `${thumbnail_local}.${token}.stage`;
    staged_paths.push(staged_thumbnail_local);
  }
  const default_required_checks =
    type === "material"
      ? [
          "material_valid",
          "visual_review",
        ]
      : [
          "geometry_valid",
          "collision_coverage",
          "material_coverage",
          "visual_review",
        ];
  const required_checks =
    args.required_checks !== undefined
      ? unique_strings(args.required_checks)
      : existing?.quality?.required_checks ??
        default_required_checks;
  const now = new Date().toISOString();
  const asset = {
    id: asset_id,
    name: String(
      args.name ??
      existing?.name ??
      semantic_asset_name(args.name, asset_id),
    ),
    type,
    aliases: unique_strings([
      ...(existing?.aliases ?? []),
      ...(args.aliases ?? []),
    ]),
    tags: unique_strings([
      ...(existing?.tags ?? []),
      ...(args.tags ?? []),
    ]),
    constraints: {
      ...(
        existing?.constraints ??
        {
          dimensions: {},
          style: [],
          materials: [],
        }
      ),
      ...normalize_constraints(args.constraints),
    },
    path: destination_path,
    source_path: immutable_source_path,
    thumbnail_path: immutable_thumbnail_path,
    dependencies: dependencies?.copied ?? [],
    quality: {
      score:
        args.quality_score !== undefined
          ? Math.min(
              100,
              Math.max(
                0,
                finite_number(args.quality_score),
              ),
            )
          : existing?.quality?.score ?? 0,
      verified:
        args.verified !== undefined
          ? Boolean(args.verified)
          : existing?.quality?.verified ?? false,
      checks:
        args.checks !== undefined
          ? args.checks
          : existing?.quality?.checks ?? {},
      required_checks,
    },
    notes:
      args.notes !== undefined
        ? String(args.notes)
        : existing?.notes ?? "",
    created_at: existing?.created_at ?? now,
    updated_at: now,
  };
  catalog.assets[asset_id] = asset;
  const staged_catalog_local =
    `${paths.catalog_local_path}.${token}.stage`;
  staged_paths.push(staged_catalog_local);
  let cleanup_warnings = [];
  try
  {
    await fs.mkdir(
      path.dirname(staged_asset_local),
      { recursive: true },
    );
    await fs.copyFile(
      local_path(project_root, source_path),
      staged_asset_local,
    );
    if (type === "prefab")
    {
      dependencies = await internalize_prefab_dependencies(
        project_root,
        paths,
        asset_id,
        staged_asset_path,
        staged_dependency_local,
      );
      if (
        dependencies.missing.length > 0 ||
        dependencies.copied.length === 0
      )
      {
        if (dependencies.missing.length > 0)
        {
          throw new Error(
            `prefab has missing dependencies: ${
              dependencies.missing.join(", ")
            }`,
          );
        }
        throw new Error(
          "prefab has no mesh or material dependencies",
        );
      }
      asset.dependencies = dependencies.copied;
    }
    if (staged_source_local)
    {
      await fs.writeFile(
        staged_source_local,
        `${JSON.stringify(args.source, null, 2)}\n`,
        "utf8",
      );
    }
    if (staged_thumbnail_local)
    {
      await fs.copyFile(
        local_path(project_root, thumbnail_source),
        staged_thumbnail_local,
      );
    }
    await fs.writeFile(
      staged_catalog_local,
      `${JSON.stringify(catalog, null, 2)}\n`,
      "utf8",
    );

    const entries = [
      {
        target: destination_local,
        staged: staged_asset_local,
      },
      {
        target: dependency_local,
        staged:
          type === "prefab"
            ? staged_dependency_local
            : null,
      },
    ];
    if (source_supplied)
    {
      const source_local = local_path(
        project_root,
        source_file_path(paths, asset_id),
      );
      entries.push({
        target: source_local,
        staged: staged_source_local,
      });
      const old_source_path = owned_sidecar_path(
        paths,
        asset_id,
        "sources",
        existing?.source_path,
      );
      const next_source_path =
        source_cleared
          ? null
          : immutable_source_path;
      if (
        old_source_path &&
        old_source_path !== next_source_path &&
        old_source_path !== source_file_path(paths, asset_id)
      )
      {
        entries.push({
          target: local_path(
            project_root,
            old_source_path,
          ),
          staged: null,
        });
      }
    }
    if (thumbnail_supplied && immutable_thumbnail_path)
    {
      entries.push({
        target: local_path(
          project_root,
          immutable_thumbnail_path,
        ),
        staged: staged_thumbnail_local,
      });
    }
    if (thumbnail_supplied)
    {
      const old_thumbnail_path = owned_sidecar_path(
        paths,
        asset_id,
        "thumbnails",
        existing?.thumbnail_path,
      );
      if (
        old_thumbnail_path &&
        old_thumbnail_path !== immutable_thumbnail_path
      )
      {
        entries.push({
          target: local_path(
            project_root,
            old_thumbnail_path,
          ),
          staged: null,
        });
      }
    }
    entries.push({
      target: paths.catalog_local_path,
      staged: staged_catalog_local,
    });
    cleanup_warnings = await commit_staged_paths(
      entries,
      token,
    );
  }
  finally
  {
    await Promise.all(
      staged_paths.map(async (staged_path) =>
      {
        await remove_path(staged_path);
      }),
    );
  }
  return {
    ok: true,
    asset: asset_summary(asset),
    catalog_path: paths.catalog_path,
    cleanup_warnings,
  };
}

export async function world_asset_register(
  project_root,
  resource_directory,
  args,
)
{
  const paths = library_paths(
    project_root,
    resource_directory,
  );
  return with_catalog_lock(
    paths.catalog_local_path,
    () => world_asset_register_unlocked(
      project_root,
      resource_directory,
      args,
    ),
  );
}

async function world_asset_fork_unlocked(
  project_root,
  resource_directory,
  args,
)
{
  const inspected = await world_asset_inspect(
    project_root,
    resource_directory,
    args,
  );
  if (!inspected.ok)
  {
    return inspected;
  }
  const { paths } = await read_catalog(
    project_root,
    resource_directory,
  );
  const asset = inspected.asset;
  const token =
    `${Date.now()}_${Math.random().toString(16).slice(2, 10)}`;
  const draft_path = [
    paths.engine_root,
    "sources",
    asset.id,
    `draft_${token}.json`,
  ].join("/");
  const draft = {
    asset_id: asset.id,
    type: asset.type,
    resource_path: asset.path,
    source:
      args.draft_source ??
      (
        asset.source_path
        ? JSON.parse(
          await fs.readFile(
            local_path(project_root, asset.source_path),
            "utf8",
          ),
        )
        : {}
      ),
    created_at: new Date().toISOString(),
  };
  const draft_local = local_path(project_root, draft_path);
  await fs.mkdir(path.dirname(draft_local), { recursive: true });
  await write_json_atomic(draft_local, draft);
  return {
    ok: true,
    asset: asset_summary(asset),
    draft_path,
    draft,
  };
}

export async function world_asset_fork(
  project_root,
  resource_directory,
  args,
)
{
  const paths = library_paths(project_root);
  return with_catalog_lock(
    paths.catalog_local_path,
    () => world_asset_fork_unlocked(
      project_root,
      resource_directory,
      args,
    ),
  );
}

async function world_asset_load_unlocked(
  project_root,
  resource_directory,
  args,
  send_command,
)
{
  const inspected = await world_asset_inspect(
    project_root,
    resource_directory,
    args,
  );
  if (!inspected.ok)
  {
    return inspected;
  }
  const asset = inspected.asset;
  const loaded = asset.type === "prefab"
    ? await send_command(
      "prefab_load",
      {
        path: asset.path,
        parent_id: args.parent_id,
        name: args.name,
      },
    )
    : await send_command(
      "resource_load",
      {
        type: asset.type,
        path: asset.path,
      },
    );
  return {
    ...loaded,
    asset: asset_summary(asset),
  };
}

export async function world_asset_load(
  project_root,
  resource_directory,
  args,
  send_command,
)
{
  const paths = library_paths(project_root);
  return with_catalog_lock(
    paths.catalog_local_path,
    () => world_asset_load_unlocked(
      project_root,
      resource_directory,
      args,
      send_command,
    ),
  );
}

async function world_material_inspect_unlocked(
  project_root,
  resource_directory,
  args,
  send_command,
)
{
  const inspected = await world_asset_inspect(
    project_root,
    resource_directory,
    args,
  );
  if (!inspected.ok)
  {
    return inspected;
  }
  if (inspected.asset.type !== "material")
  {
    return {
      ok: false,
      error: "asset is not a material",
    };
  }
  const material = await send_command(
    "material_get",
    { path: inspected.asset.path },
  );
  return {
    ...inspected,
    material,
  };
}

export async function world_material_inspect(
  project_root,
  resource_directory,
  args,
  send_command,
)
{
  const paths = library_paths(project_root);
  return with_catalog_lock(
    paths.catalog_local_path,
    () => world_material_inspect_unlocked(
      project_root,
      resource_directory,
      args,
      send_command,
    ),
  );
}

function material_draft_source(material)
{
  const textures = {};
  for (const [texture_type, slots] of Object.entries(
    material?.textures ?? {},
  ))
  {
    const assigned = [];
    if (Array.isArray(slots))
    {
      slots.forEach((texture_path, slot) =>
      {
        if (typeof texture_path === "string" && texture_path)
        {
          assigned.push({
            texture_path,
            slot,
          });
        }
      });
    }
    else if (typeof slots === "string" && slots)
    {
      assigned.push({
        texture_path: slots,
        slot: 0,
      });
    }
    if (assigned.length > 0)
    {
      textures[texture_type] = assigned;
    }
  }
  return {
    properties: {
      ...(material?.properties ?? {}),
    },
    textures,
  };
}

function material_texture_assignments(textures)
{
  const assignments = [];
  const append = (
    texture_type,
    value,
    fallback_slot,
  ) =>
  {
    const texture_path =
      typeof value === "string"
        ? value
        : value?.texture_path;
    if (
      typeof texture_path !== "string" ||
      texture_path.length === 0
    )
    {
      return;
    }
    const requested_slot =
      typeof value === "object"
        ? value.slot
        : fallback_slot;
    const slot = Number.isInteger(requested_slot)
      ? requested_slot
      : fallback_slot;
    assignments.push({
      texture_type,
      texture_path,
      slot,
    });
  };
  for (const [texture_type, value] of Object.entries(
    textures ?? {},
  ))
  {
    if (Array.isArray(value))
    {
      value.forEach((entry, slot) =>
      {
        append(texture_type, entry, slot);
      });
    }
    else
    {
      append(texture_type, value, 0);
    }
  }
  return assignments;
}

async function world_material_fork_unlocked(
  project_root,
  resource_directory,
  args,
  send_command,
)
{
  const inspected = await world_asset_inspect(
    project_root,
    resource_directory,
    args,
  );
  if (!inspected.ok)
  {
    return inspected;
  }
  if (inspected.asset.type !== "material")
  {
    return {
      ok: false,
      error: "asset is not a material",
    };
  }
  const material = await send_command(
    "material_get",
    { path: inspected.asset.path },
  );
  if (!material.ok)
  {
    return material;
  }
  return world_asset_fork(
    project_root,
    resource_directory,
    {
      ...args,
      draft_source:
        material_draft_source(
          material.material ??
          material,
        ),
    },
  );
}

export async function world_material_fork(
  project_root,
  resource_directory,
  args,
  send_command,
)
{
  const paths = library_paths(project_root);
  return with_catalog_lock(
    paths.catalog_local_path,
    () => world_material_fork_unlocked(
      project_root,
      resource_directory,
      args,
      send_command,
    ),
  );
}

async function world_material_publish_unlocked(
  project_root,
  resource_directory,
  args,
  send_command,
)
{
  const inspected = await world_asset_inspect(
    project_root,
    resource_directory,
    args,
  );
  if (!inspected.ok)
  {
    return inspected;
  }
  if (inspected.asset.type !== "material")
  {
    return {
      ok: false,
      error: "asset is not a material",
    };
  }
  const { paths } = await read_catalog(
    project_root,
    resource_directory,
  );
  const material_path = asset_file_path(
    paths,
    "material",
    inspected.asset.id,
    ".xml",
  );
  const token = transaction_token()
    .replaceAll(".", "_");
  const staged_material_path = [
    paths.engine_root,
    "materials",
    `${inspected.asset.id}_stage_${token}.xml`,
  ].join("/");
  const staged_material_local = local_path(
    project_root,
    staged_material_path,
  );
  let source = args.source;
  if (!source && args.draft_path)
  {
    const draft_path = normalize_engine_path(args.draft_path);
    if (!draft_path.startsWith(`${paths.engine_root}/sources/`))
    {
      throw new Error("material draft must be under mcp_resources/sources");
    }
    const draft = JSON.parse(
      await fs.readFile(
        local_path(project_root, draft_path),
        "utf8",
      ),
    );
    source = draft.source;
  }
  source ??=
    {
      properties: args.properties ?? {},
      textures: args.textures ?? {},
    };
  try
  {
    const created = await send_command(
      "material_create",
      {
        path: staged_material_path,
        name: args.name ?? inspected.asset.name,
        library_asset: true,
        skip_catalog_registration: true,
      },
    );
    if (!created.ok)
    {
      return created;
    }
    for (const [property, value] of Object.entries(
      source.properties ?? {},
    ))
    {
      const updated = await send_command(
        "material_set_property",
        {
          path: staged_material_path,
          property,
          value,
        },
      );
      if (!updated.ok)
      {
        return updated;
      }
    }
    for (const texture of material_texture_assignments(
      source.textures,
    ))
    {
      const updated = await send_command(
        "material_set_texture",
        {
          path: staged_material_path,
          ...texture,
        },
      );
      if (!updated.ok)
      {
        return updated;
      }
    }
    const registered = await world_asset_register(
      project_root,
      resource_directory,
      {
        ...args,
        type: "material",
        asset_id: inspected.asset.id,
        name: inspected.asset.name,
        path: staged_material_path,
        source,
      },
    );
    if (!registered.ok)
    {
      return registered;
    }
    const reloaded = await send_command(
      "resource_reload",
      {
        path: material_path,
        type: "material",
      },
    );
    return {
      ...registered,
      resource_reload: reloaded,
    };
  }
  finally
  {
    try
    {
      await send_command(
        "resource_remove",
        {
          path: staged_material_path,
          type: "material",
        },
      );
    }
    catch
    {
    }
    await remove_path(staged_material_local);
  }
}

export async function world_material_publish(
  project_root,
  resource_directory,
  args,
  send_command,
)
{
  const paths = library_paths(project_root);
  return with_catalog_lock(
    paths.catalog_local_path,
    () => world_material_publish_unlocked(
      project_root,
      resource_directory,
      args,
      send_command,
    ),
  );
}

export async function auto_register_world_asset(
  project_root,
  resource_directory,
  command,
  args,
  result,
)
{
  if (!result?.ok)
  {
    return null;
  }
  if (args.skip_catalog_registration === true)
  {
    return null;
  }
  if (
    command === "texture_generate" &&
    args.material_path
  )
  {
    return null;
  }
  if (
    command !== "mesh_raw_create" &&
    args.library_asset !== true &&
    args.catalog_register !== true
  )
  {
    return null;
  }
  let type = null;
  let resource_path = null;
  let name = args.name;
  if (
    command === "mesh_generate" ||
    command === "mesh_raw_create"
  )
  {
    type = "mesh";
    resource_path =
      result.resource?.path ??
      args.path ??
      args.mesh_path;
  }
  else if (
    command === "material_create" ||
    command === "material_semantic_create"
  )
  {
    type = "material";
    resource_path =
      result.material?.path ??
      result.path ??
      args.path;
    name ??= args.semantic;
  }
  else if (command === "prefab_save")
  {
    type = "prefab";
    resource_path = result.path ?? args.path;
  }
  else if (command === "texture_generate")
  {
    type = "texture";
    resource_path = result.path ?? args.path;
  }
  if (!type || !resource_path)
  {
    return null;
  }
  const normalized_resource_path = String(resource_path)
    .replaceAll("\\", "/");
  if (
    /\/mcp_resources\/(?:meshes|materials|prefabs|textures)\/[^/]+\.[^/]+$/i.test(
      normalized_resource_path,
    )
  )
  {
    return null;
  }
  try
  {
    return await world_asset_register(
      project_root,
      resource_directory,
      {
        type,
        name:
          name ??
          path.posix.basename(
            String(resource_path).replaceAll("\\", "/"),
            path.posix.extname(resource_path),
          ),
        path: resource_path,
        aliases: args.aliases,
        tags: args.tags,
        constraints: args.constraints,
        source: args.source ?? args,
        notes: `automatically registered from ${command}`,
      },
    );
  }
  catch
  {
    return null;
  }
}
