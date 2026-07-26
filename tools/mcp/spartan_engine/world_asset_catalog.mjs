import fs from "node:fs/promises";
import path from "node:path";

const catalog_version = 1;
const asset_types = new Set([
  "mesh",
  "material",
  "prefab",
]);
const type_folders = {
  mesh: "meshes",
  material: "materials",
  prefab: "prefabs",
};
const type_extensions = {
  mesh: ".mesh",
  material: ".xml",
  prefab: ".prefab",
};

function safe_name(value, fallback = "asset")
{
  return String(value ?? fallback)
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

function normalize_constraints(value)
{
  if (!value || typeof value !== "object" || Array.isArray(value))
  {
    return {};
  }
  return {
    dimensions: value.dimensions ?? {},
    style: unique_strings(value.style),
    materials: unique_strings(value.materials),
    ...value,
  };
}

function normalize_engine_path(value)
{
  const normalized = String(value ?? "")
    .replaceAll("\\", "/")
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

function safe_world_directory(value)
{
  const normalized = normalize_engine_path(value);
  if (
    !/^project\/[a-z0-9_-]+_resources$/i.test(normalized)
  )
  {
    throw new Error(
      "world resource directory must match project/<world>_resources",
    );
  }
  return normalized;
}

function local_path(project_root, engine_path)
{
  const relative = normalize_engine_path(engine_path);
  const root = path.resolve(project_root);
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

function library_paths(project_root, resource_directory)
{
  const directory = safe_world_directory(resource_directory);
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
    `${file_path}.${process.pid}.${Date.now()}.tmp`;
  await fs.writeFile(
    temporary_path,
    `${JSON.stringify(value, null, 2)}\n`,
    "utf8",
  );
  await fs.rename(temporary_path, file_path);
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
        world_resource_directory: paths.directory,
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
  world = null,
)
{
  const native = await send_command(
    "world_resource_directory_get",
    {},
  );
  if (native?.ok)
  {
    const directory =
      native.directory ??
      native.resource_directory ??
      native.path;
    if (directory)
    {
      return safe_world_directory(directory);
    }
  }
  let fallback_world = world;
  if (!fallback_world)
  {
    fallback_world = await send_command("world_summary", {});
  }
  const world_name =
    fallback_world?.name ??
    fallback_world?.file_path ??
    fallback_world?.path ??
    "world";
  return `project/${safe_name(
    path.posix.basename(
      String(world_name).replaceAll("\\", "/"),
    ).replace(/\.[^.]+$/g, ""),
    "world",
  )}_resources`;
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
  const matches = Object.values(catalog.assets)
    .filter((asset) =>
    {
      if (args.type && asset.type !== args.type)
      {
        return false;
      }
      if (
        required_tags.some(
          (tag) => !asset.tags.includes(tag),
        )
      )
      {
        return false;
      }
      const searchable = [
        asset.id,
        asset.name,
        ...asset.aliases,
        ...asset.tags,
        ...asset.constraints.style,
        ...asset.constraints.materials,
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
  return {
    ok: true,
    catalog_path: paths.catalog_path,
    asset,
    version,
  };
}

export async function world_asset_register(
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
    name: String(args.name ?? asset_id),
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
  const number = version_number(asset);
  const extension =
    path.posix.extname(source_path) ||
    type_extensions[type];
  const destination_path = version_file_path(
    paths,
    type,
    asset_id,
    number,
    extension,
  );
  await copy_immutable(
    project_root,
    source_path,
    destination_path,
  );
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
  const version = {
    id: `v${String(number).padStart(4, "0")}`,
    number,
    path: destination_path,
    source_path: immutable_source_path,
    parent_version:
      args.parent_version ??
      asset.active_version,
    created_at: new Date().toISOString(),
    quality: {
      score: Number(args.quality_score ?? 0),
      verified: Boolean(args.verified),
      checks: args.checks ?? {},
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
  if (!asset.active_version && args.promote !== false)
  {
    asset.active_version = version.id;
  }
  asset.updated_at = new Date().toISOString();
  catalog.assets[asset_id] = asset;
  await write_json_atomic(paths.catalog_local_path, catalog);
  return {
    ok: true,
    asset: asset_summary(asset),
    version,
    catalog_path: paths.catalog_path,
  };
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
      version.source_path
        ? JSON.parse(
          await fs.readFile(
            local_path(project_root, version.source_path),
            "utf8",
          ),
        )
        : {},
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

export async function world_asset_promote(
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
  const required_checks = unique_strings(
    args.required_checks ?? [],
  );
  const failed_checks = required_checks.filter(
    (check) => candidate.quality.checks?.[check] !== true,
  );
  const threshold = Number(args.threshold ?? 1);
  const current_score = Number(current?.quality?.score ?? 0);
  if (!candidate.quality.verified)
  {
    return {
      ok: false,
      error: "candidate must be verified before promotion",
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
  asset.updated_at = new Date().toISOString();
  await write_json_atomic(paths.catalog_local_path, catalog);
  return {
    ok: true,
    asset: asset_summary(asset),
    promoted: candidate,
  };
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
  const source =
    args.source ??
    {
      properties: args.properties ?? {},
      textures: args.textures ?? {},
    };
  const created = await send_command(
    "material_create",
    {
      path: material_path,
      name: args.name ?? inspected.asset.name,
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
  if (!type || !resource_path)
  {
    return null;
  }
  if (
    String(resource_path)
      .replaceAll("\\", "/")
      .includes("/mcp_resources/")
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
        promote: true,
        notes: `automatically registered from ${command}`,
      },
    );
  }
  catch
  {
    return null;
  }
}
