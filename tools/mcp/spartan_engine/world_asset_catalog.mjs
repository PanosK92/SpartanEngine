import fs from "node:fs/promises";
import path from "node:path";

const catalog_version = 1;
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
    return false;
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

async function with_catalog_lock(key, operation)
{
  const previous = catalog_queues.get(key) ?? Promise.resolve();
  let release;
  const current = new Promise((resolve) =>
  {
    release = resolve;
  });
  catalog_queues.set(key, current);
  await previous;
  try
  {
    return await operation();
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

async function read_catalog(
  project_root,
  resource_directory,
)
{
  const paths = await ensure_catalog(
    project_root,
    resource_directory,
  );
  const catalog = JSON.parse(
    await fs.readFile(paths.catalog_local_path, "utf8"),
  );
  if (
    catalog.schema_version !== catalog_version ||
    !catalog.assets ||
    typeof catalog.assets !== "object"
  )
  {
    throw new Error("unsupported or invalid world asset catalog");
  }
  return {
    paths,
    catalog,
  };
}

function version_number(asset)
{
  return (
    Math.max(
      0,
      ...(asset?.versions ?? []).map(
        (version) => Number(version.number) || 0,
      ),
    ) + 1
  );
}

function find_version(asset, requested)
{
  const version_id =
    requested ??
    asset.active_version ??
    asset.versions.at(-1)?.id;
  return asset.versions.find(
    (version) =>
      version.id === version_id ||
      version.number === Number(version_id),
  );
}

function version_file_path(
  paths,
  type,
  asset_id,
  number,
  extension,
)
{
  const version = `v${String(number).padStart(4, "0")}`;
  const suffix = extension || type_extensions[type];
  return [
    paths.engine_root,
    type_folders[type],
    asset_id,
    `${version}${suffix}`,
  ].join("/");
}

function source_file_path(paths, asset_id, number)
{
  const version = `v${String(number).padStart(4, "0")}`;
  return [
    paths.engine_root,
    "sources",
    asset_id,
    `${version}.json`,
  ].join("/");
}

function thumbnail_file_path(
  paths,
  asset_id,
  number,
  extension,
)
{
  const version = `v${String(number).padStart(4, "0")}`;
  return [
    paths.engine_root,
    "thumbnails",
    asset_id,
    `${version}${extension || ".png"}`,
  ].join("/");
}

// once a version is promoted the earlier attempts are dead weight, they are only reachable through
// compare and fork which both run before promotion, so their files are deleted and their catalog
// entries dropped rather than kept forever
async function prune_superseded_versions(
  project_root,
  paths,
  asset,
)
{
  const kept = asset.versions.find(
    (version) => version.id === asset.active_version,
  );
  if (!kept)
  {
    return {
      removed_versions: [],
      removed_files: [],
    };
  }

  // a legacy entry can carry a path this rejects, a bad record must not block a promotion
  const safe_engine_path = (value) =>
  {
    try
    {
      return normalize_engine_path(value);
    }
    catch
    {
      return null;
    }
  };

  // a kept prefab can still point at a dependency snapshot taken for an earlier version, so the
  // paths a kept version actually needs are collected before anything is deleted
  const protected_paths = new Set(
    [
      kept.path,
      kept.source_path,
      kept.thumbnail_path,
      ...(Array.isArray(kept.dependencies) ? kept.dependencies : []),
    ]
      .filter(Boolean)
      .map(safe_engine_path)
      .filter(Boolean),
  );

  const removed_versions = [];
  const removed_files = [];
  for (const version of asset.versions)
  {
    if (version.id === kept.id)
    {
      continue;
    }

    const candidates = [
      version.path,
      version.source_path,
      version.thumbnail_path,
      ...(
        Array.isArray(version.dependencies)
          ? version.dependencies
          : []
      ),
    ].filter(Boolean);

    for (const candidate of candidates)
    {
      const engine_path = safe_engine_path(candidate);
      if (
        !engine_path ||
        protected_paths.has(engine_path) ||
        !engine_path.startsWith(`${paths.engine_root}/`)
      )
      {
        continue;
      }

      await fs.rm(
        local_path(project_root, engine_path),
        { force: true },
      );
      removed_files.push(engine_path);
    }

    // the dependency snapshot directory is per version, dropping it whole also catches copies the
    // catalog never listed
    const snapshot =
      `${paths.engine_root}/dependencies/${asset.id}/${version.id}`;
    await fs.rm(
      local_path(project_root, snapshot),
      {
        recursive: true,
        force: true,
      },
    );
    removed_versions.push(version.id);
  }

  if (removed_versions.length > 0)
  {
    asset.versions = asset.versions.filter(
      (version) => version.id === kept.id,
    );
    // the parent chain now dangles, the kept version is the whole history
    kept.parent_version = null;
  }

  return {
    removed_versions,
    removed_files,
  };
}

async function copy_immutable(
  project_root,
  source_path,
  destination_path,
)
{
  const source = local_path(project_root, source_path);
  const destination = local_path(project_root, destination_path);
  if (source === destination)
  {
    if (!(await path_exists(source)))
    {
      throw new Error(`asset source file does not exist: ${source_path}`);
    }
    return;
  }
  if (await path_exists(destination))
  {
    throw new Error("asset version file already exists");
  }
  if (!(await path_exists(source)))
  {
    throw new Error(`asset source file does not exist: ${source_path}`);
  }
  await fs.mkdir(path.dirname(destination), { recursive: true });
  await fs.copyFile(source, destination);
}

// snapshot prefab dependencies so every version remains immutable
async function internalize_prefab_dependencies(
  project_root,
  paths,
  asset_id,
  number,
  prefab_path,
)
{
  const prefab_local = local_path(project_root, prefab_path);
  const version = `v${String(number).padStart(4, "0")}`;
  const dependency_root =
    `${paths.engine_root}/dependencies/${asset_id}/${version}`;
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
        continue;
      }
      const source_local = local_path(project_root, normalized);
      if (!(await path_exists(source_local)))
      {
        missing.push(normalized);
        continue;
      }

      const destination =
        `${dependency_root}/${path.posix.basename(normalized)}`;
      const destination_local = local_path(project_root, destination);
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
            continue;
          }
          if (texture_path.startsWith(`${dependency_root}/`))
          {
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

          const texture_destination =
            `${dependency_root}/${path.posix.basename(texture_path)}`;
          const texture_destination_local = local_path(
            project_root,
            texture_destination,
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
  const active = find_version(asset, asset.active_version);
  return {
    id: asset.id,
    name: asset.name,
    type: asset.type,
    aliases: asset.aliases,
    tags: asset.tags,
    constraints: asset.constraints,
    active_version: asset.active_version,
    active_path: active?.path,
    version_count: asset.versions.length,
  };
}

export async function resolve_world_resource_directory(
  send_command,
  _world = null,
)
{
  const native = await send_command(
    "world_resource_directory_get",
    {},
  );
  if (native?.ok)
  {
    const directory = normalize_engine_path(
      native.mcp_resources?.root ??
      "project/mcp_resources",
    ).replace(/\/+$/g, "");
    if (directory === "project/mcp_resources")
    {
      return directory;
    }
  }
  return "project/mcp_resources";
}

// every promoted entry as a flat list, for callers that need to rank the whole library themselves
// rather than filter it, world_asset_search answers does this match, this answers what is in there
export async function world_asset_catalog_entries(
  project_root,
  resource_directory,
)
{
  const { catalog } = await read_catalog(
    project_root,
    resource_directory,
  );
  return Object.values(catalog.assets)
    .filter((asset) => Boolean(asset?.active_version))
    .map(asset_summary);
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
  const version = find_version(asset, args.version);
  if (!version)
  {
    return {
      ok: false,
      error: "asset version not found",
    };
  }
  return {
    ok: true,
    catalog_path: paths.catalog_path,
    asset,
    version,
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
    throw new Error("asset type must be mesh, material, or prefab");
  }
  const source_path = normalize_engine_path(
    args.path ?? args.resource_path,
  );
  const { paths, catalog } = await read_catalog(
    project_root,
    resource_directory,
  );
  if (
    args.parent_version &&
    !args.asset_id &&
    !args.id
  )
  {
    throw new Error(
      "parent_version requires an explicit asset_id",
    );
  }
  const asset_id = safe_name(
    args.asset_id ?? args.id ?? args.name,
  );
  const existing = catalog.assets[asset_id];
  if (existing && existing.type !== type)
  {
    throw new Error("asset id is already registered with another type");
  }
  const asset = existing ?? {
    id: asset_id,
    name: semantic_asset_name(
      args.name,
      asset_id,
    ),
    type,
    aliases: [],
    tags: [],
    constraints: {
      dimensions: {},
      style: [],
      materials: [],
    },
    active_version: null,
    versions: [],
    created_at: new Date().toISOString(),
  };
  const parent_version =
    args.parent_version ??
    asset.active_version;
  const replaceable_candidate =
    args.replace_candidate === true
      ? [...asset.versions]
        .reverse()
        .find((version) =>
          version.id !== asset.active_version &&
          version.parent_version === parent_version
        )
      : null;
  const number =
    replaceable_candidate?.number ??
    version_number(asset);
  const extension =
    path.posix.extname(source_path) ||
    type_extensions[type];
  if (extension.toLowerCase() !== type_extensions[type])
  {
    throw new Error(
      `${type} assets must use ${type_extensions[type]} files`,
    );
  }
  const destination_path = version_file_path(
    paths,
    type,
    asset_id,
    number,
    extension,
  );
  if (!(await path_exists(local_path(project_root, source_path))))
  {
    throw new Error(
      `asset source file does not exist: ${source_path}`,
    );
  }
  if (
    args.thumbnail_path &&
    !(
      await path_exists(
        local_path(project_root, args.thumbnail_path),
      )
    )
  )
  {
    throw new Error(
      `asset thumbnail does not exist: ${args.thumbnail_path}`,
    );
  }
  if (replaceable_candidate)
  {
    const candidate_files = [
      replaceable_candidate.path,
      replaceable_candidate.source_path,
      replaceable_candidate.thumbnail_path,
      ...(
        Array.isArray(replaceable_candidate.dependencies)
          ? replaceable_candidate.dependencies
          : []
      ),
    ].filter(Boolean);
    for (const candidate of candidate_files)
    {
      const engine_path = normalize_engine_path(candidate);
      if (engine_path.startsWith(`${paths.engine_root}/`))
      {
        await fs.rm(
          local_path(project_root, engine_path),
          { force: true },
        );
      }
    }
    await fs.rm(
      local_path(
        project_root,
        `${paths.engine_root}/dependencies/${
          asset.id
        }/${replaceable_candidate.id}`,
      ),
      {
        recursive: true,
        force: true,
      },
    );
    asset.versions = asset.versions.filter(
      (version) => version.id !== replaceable_candidate.id,
    );
  }
  await copy_immutable(
    project_root,
    source_path,
    destination_path,
  );
  let dependencies = null;
  if (type === "prefab")
  {
    dependencies = await internalize_prefab_dependencies(
      project_root,
      paths,
      asset_id,
      number,
      destination_path,
    );
    if (
      dependencies.missing.length > 0 ||
      dependencies.copied.length === 0
    )
    {
      await fs.rm(
        local_path(project_root, destination_path),
        { force: true },
      );
      await fs.rm(
        local_path(
          project_root,
          `${paths.engine_root}/dependencies/${asset_id}/v${
            String(number).padStart(4, "0")
          }`,
        ),
        {
          recursive: true,
          force: true,
        },
      );
      if (dependencies.missing.length > 0)
      {
        throw new Error(
          `prefab has missing dependencies: ${
            dependencies.missing.join(", ")
          }`,
        );
      }
      throw new Error(
        "prefab has no versioned mesh or material dependencies",
      );
    }
  }
  let immutable_source_path = null;
  if (args.source !== undefined)
  {
    immutable_source_path = source_file_path(
      paths,
      asset_id,
      number,
    );
    const source_local = local_path(
      project_root,
      immutable_source_path,
    );
    await fs.mkdir(path.dirname(source_local), { recursive: true });
    await write_json_atomic(source_local, args.source);
  }
  let immutable_thumbnail_path = null;
  if (args.thumbnail_path)
  {
    const thumbnail_source = normalize_engine_path(
      args.thumbnail_path,
    );
    immutable_thumbnail_path = thumbnail_file_path(
      paths,
      asset_id,
      number,
      path.posix.extname(thumbnail_source),
    );
    await copy_immutable(
      project_root,
      thumbnail_source,
      immutable_thumbnail_path,
    );
  }
  const version = {
    id: `v${String(number).padStart(4, "0")}`,
    number,
    path: destination_path,
    source_path: immutable_source_path,
    thumbnail_path: immutable_thumbnail_path,
    dependencies: dependencies?.copied ?? [],
    missing_dependencies: dependencies?.missing ?? [],
    parent_version,
    created_at: new Date().toISOString(),
    quality: {
      score: Math.min(
        100,
        Math.max(
          0,
          finite_number(args.quality_score),
        ),
      ),
      verified: Boolean(args.verified),
      checks: args.checks ?? {},
      required_checks:
        unique_strings(args.required_checks).length > 0
          ? unique_strings(args.required_checks)
          : type === "material"
            ? [
                "material_valid",
                "visual_review",
              ]
            : [
                "geometry_valid",
                "collision_coverage",
                "material_coverage",
                "visual_review",
              ],
    },
    notes: String(args.notes ?? ""),
  };
  asset.name = String(args.name ?? asset.name);
  asset.aliases = unique_strings([
    ...asset.aliases,
    ...(args.aliases ?? []),
  ]);
  asset.tags = unique_strings([
    ...asset.tags,
    ...(args.tags ?? []),
  ]);
  asset.constraints = {
    ...asset.constraints,
    ...normalize_constraints(args.constraints),
  };
  asset.versions.push(version);
  const initial_promotion_checks =
    version.quality.required_checks;
  let pruned = {
    removed_versions: [],
    removed_files: [],
  };
  if (
    !asset.active_version &&
    args.promote === true &&
    version.quality.verified &&
    initial_promotion_checks.every(
      (check) => version.quality.checks[check] === true,
    )
  )
  {
    asset.active_version = version.id;
    if (args.keep_history !== true)
    {
      pruned = await prune_superseded_versions(
        project_root,
        paths,
        asset,
      );
    }
  }
  asset.updated_at = new Date().toISOString();
  catalog.assets[asset_id] = asset;
  await write_json_atomic(paths.catalog_local_path, catalog);
  return {
    ok: true,
    asset: asset_summary(asset),
    version,
    catalog_path: paths.catalog_path,
    replaced_candidate:
      replaceable_candidate?.id ??
      null,
    pruned_versions: pruned.removed_versions,
    pruned_files: pruned.removed_files,
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

export async function world_asset_fork(
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
  const version = inspected.version;
  if (!version)
  {
    return {
      ok: false,
      error: "source asset version not found",
    };
  }
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
    parent_version: version.id,
    resource_path: version.path,
    source:
      args.draft_source ??
      (
        version.source_path
        ? JSON.parse(
          await fs.readFile(
            local_path(project_root, version.source_path),
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
    parent_version: version,
    draft_path,
    draft,
  };
}

export async function world_asset_compare(
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
  const asset = inspected.asset;
  const left = find_version(
    asset,
    args.left_version ?? asset.active_version,
  );
  const right = find_version(
    asset,
    args.right_version ?? args.candidate_version,
  );
  if (!left || !right)
  {
    return {
      ok: false,
      error: "both asset versions must exist",
    };
  }
  return {
    ok: true,
    asset: asset_summary(asset),
    left,
    right,
    quality_score_delta:
      right.quality.score - left.quality.score,
    changed: {
      path: left.path !== right.path,
      source: left.source_path !== right.source_path,
      checks:
        JSON.stringify(left.quality.checks) !==
        JSON.stringify(right.quality.checks),
    },
  };
}

async function world_asset_promote_unlocked(
  project_root,
  resource_directory,
  args,
)
{
  const { paths, catalog } = await read_catalog(
    project_root,
    resource_directory,
  );
  const asset_id = safe_name(args.asset_id ?? args.id);
  const asset = catalog.assets[asset_id];
  if (!asset)
  {
    return {
      ok: false,
      error: "asset not found",
    };
  }
  const candidate = find_version(
    asset,
    args.version ?? args.candidate_version,
  );
  const current = find_version(asset, asset.active_version);
  if (!candidate)
  {
    return {
      ok: false,
      error: "candidate version not found",
    };
  }
  const requested_checks = unique_strings(args.required_checks);
  const candidate_checks = unique_strings(
    candidate.quality.required_checks,
  );
  const required_checks = unique_strings([
    ...(
      candidate_checks.length > 0
        ? candidate_checks
        : asset.type === "material"
          ? [
              "material_valid",
              "visual_review",
            ]
          : [
              "geometry_valid",
              "collision_coverage",
              "material_coverage",
              "visual_review",
            ]
    ),
    ...requested_checks,
  ]);
  const failed_checks = required_checks.filter(
    (check) => candidate.quality.checks?.[check] !== true,
  );
  const threshold = Math.max(
    0,
    finite_number(args.threshold, 1),
  );
  const current_score = finite_number(
    current?.quality?.score,
  );
  if (!candidate.quality.verified)
  {
    return {
      ok: false,
      error: "candidate must be verified before promotion",
    };
  }
  if (
    asset.type === "prefab" &&
    (
      !Array.isArray(candidate.dependencies) ||
      candidate.dependencies.length === 0 ||
      candidate.missing_dependencies?.length > 0
    )
  )
  {
    return {
      ok: false,
      error:
        "candidate prefab must have a complete dependency snapshot",
      dependencies: candidate.dependencies ?? [],
      missing_dependencies:
        candidate.missing_dependencies ?? [],
    };
  }
  if (
    required_checks.includes("visual_review") &&
    (
      !candidate.thumbnail_path ||
      !await path_exists(
        local_path(
          project_root,
          candidate.thumbnail_path,
        ),
      )
    )
  )
  {
    return {
      ok: false,
      error:
        "candidate visual review thumbnail is missing",
    };
  }
  if (failed_checks.length > 0)
  {
    return {
      ok: false,
      error: "candidate failed required checks",
      failed_checks,
    };
  }
  if (
    current &&
    candidate.id !== current.id &&
    candidate.quality.score < current_score + threshold
  )
  {
    return {
      ok: false,
      error: "candidate quality score does not exceed threshold",
      current_score,
      candidate_score: candidate.quality.score,
      threshold,
    };
  }
  asset.active_version = candidate.id;

  // the promoted version is the answer, the attempts that led to it are not worth the disk, pass
  // keep_history to opt out
  const pruned = args.keep_history === true
    ? {
        removed_versions: [],
        removed_files: [],
      }
    : await prune_superseded_versions(
        project_root,
        paths,
        asset,
      );

  asset.updated_at = new Date().toISOString();
  await write_json_atomic(paths.catalog_local_path, catalog);
  return {
    ok: true,
    asset: asset_summary(asset),
    promoted: candidate,
    pruned_versions: pruned.removed_versions,
    pruned_files: pruned.removed_files,
  };
}

export async function world_asset_promote(
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
    () => world_asset_promote_unlocked(
      project_root,
      resource_directory,
      args,
    ),
  );
}

export async function world_asset_load(
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
  const version = inspected.version;
  if (!args.version && !asset.active_version)
  {
    return {
      ok: false,
      error: "asset has no active promoted version",
    };
  }
  if (!version)
  {
    return {
      ok: false,
      error: "asset version not found",
    };
  }
  const loaded = asset.type === "prefab"
    ? await send_command(
      "prefab_load",
      {
        path: version.path,
        parent_id: args.parent_id,
        name: args.name,
      },
    )
    : await send_command(
      "resource_load",
      {
        type: asset.type,
        path: version.path,
      },
    );
  return {
    ...loaded,
    asset: asset_summary(asset),
    version,
  };
}

export async function world_material_inspect(
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
    { path: inspected.version.path },
  );
  return {
    ...inspected,
    material,
  };
}

export async function world_material_fork(
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
    { path: inspected.version.path },
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
        material.material ??
        material,
    },
  );
}

export async function world_material_publish(
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
  const number = version_number(inspected.asset);
  const material_path = version_file_path(
    paths,
    "material",
    inspected.asset.id,
    number,
    ".xml",
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
  const created = await send_command(
    "material_create",
    {
      path: material_path,
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
        path: material_path,
        property,
        value,
      },
    );
    if (!updated.ok)
    {
      return updated;
    }
  }
  for (const [texture_type, texture] of Object.entries(
    source.textures ?? {},
  ))
  {
    const texture_value =
      typeof texture === "string"
        ? { texture_path: texture }
        : texture;
    const updated = await send_command(
      "material_set_texture",
      {
        path: material_path,
        texture_type,
        ...texture_value,
      },
    );
    if (!updated.ok)
    {
      return updated;
    }
  }
  return world_asset_register(
    project_root,
    resource_directory,
    {
      ...args,
      type: "material",
      asset_id: inspected.asset.id,
      name: inspected.asset.name,
      path: material_path,
      source,
      parent_version:
        args.parent_version ??
        inspected.asset.active_version,
      promote: false,
    },
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
    /\/mcp_resources\/(?:meshes|materials|prefabs|textures)\/[^/]+\/v\d+\.[^/]+$/i.test(
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
        promote: false,
        notes: `automatically registered from ${command}`,
      },
    );
  }
  catch
  {
    return null;
  }
}
