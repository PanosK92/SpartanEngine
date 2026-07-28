import path from "node:path";

const shared_library_directory = "project/mcp_resources";

function safe_name(value)
{
  return String(value ?? "world")
    .replaceAll("\\", "/")
    .split("/")
    .pop()
    .replace(/\.[^.]+$/g, "")
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/^_+|_+$/g, "") ||
    "world";
}

function file_name(value, fallback, extension)
{
  const requested = String(value ?? "")
    .replaceAll("\\", "/");
  let name = path.posix.basename(requested);
  if (!name)
  {
    name = `${safe_name(fallback)}${extension}`;
  }
  if (!path.posix.extname(name))
  {
    name += extension;
  }
  return name;
}

// where generated resources live
//
// this used to derive a directory per world and refuse to answer without one. the library is shared across
// worlds now, every path built below goes to project/mcp_resources regardless of what is passed in, so
// demanding a saved world only blocked work that never needed one
export function world_resource_directory()
{
  return shared_library_directory;
}

// a material name is not a file name. the model passes a bare word, a file name, or the full path it read
// back from an earlier reply, and appending an extension to the last of those produced office_chair_nylon
// .xml.xml, which then became a second material the rest of the run could not find
export function material_file_name(name)
{
  const bare = String(name ?? "material")
    .replace(/\\/g, "/")
    .split("/")
    .pop()
    .replace(/\.xml$/i, "");
  const safe = bare
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return `${safe || "material"}.xml`;
}

export function generated_resource_command(command)
{
  return (
    command === "material_create" ||
    command === "material_semantic_create" ||
    command === "mesh_generate" ||
    command === "mesh_generate_batch" ||
    command === "texture_generate" ||
    command === "prefab_save" ||
    command === "material_palette_create" ||
    command === "compound_create" ||
    command === "construction_grammar_create" ||
    command === "detail_pattern_create"
  );
}

export function constrain_generated_resources(
  command,
  args,
  directory,
)
{
  const constrained = {
    ...args,
  };
  if (
    command === "material_create" ||
    command === "material_semantic_create"
  )
  {
    const requested_path = String(args.path ?? "")
      .replaceAll("\\", "/");
    const library_root =
      `${shared_library_directory}/materials/`;
    const destination_directory =
      `${shared_library_directory}/materials`;
    constrained.path =
      requested_path.startsWith(library_root) &&
      !requested_path.split("/").includes("..")
        ? requested_path
        : [
            destination_directory,
            file_name(
              requested_path,
              args.name ?? args.semantic ?? "material",
              ".xml",
            ),
          ].join("/");
  }
  else if (command === "material_palette_create")
  {
    constrained.directory =
      `${shared_library_directory}/materials`;
    constrained.materials = (
      Array.isArray(args.materials)
        ? args.materials
        : []
    ).map((material) => ({
      ...material,
      path: [
        `${shared_library_directory}/materials`,
        file_name(
          material.path,
          material.name ?? "material",
          ".xml",
        ),
      ].join("/"),
    }));
  }
  else if (command === "mesh_generate")
  {
    const requested_path = String(
      args.path ??
      args.mesh_path ??
      "",
    ).replaceAll("\\", "/");
    const library_root =
      `${shared_library_directory}/meshes/`;
    const destination_directory =
      `${shared_library_directory}/meshes`;
    constrained.path =
      requested_path.startsWith(library_root) &&
      !requested_path.split("/").includes("..")
        ? requested_path
        : [
            destination_directory,
            file_name(
              requested_path,
              args.name ??
                args.shape ??
                args.generator ??
                "mesh",
              ".mesh",
            ),
          ].join("/");
    delete constrained.mesh_path;
  }
  else if (command === "texture_generate")
  {
    const requested_path = String(
      args.path ??
      args.texture_path ??
      "",
    ).replaceAll("\\", "/");
    const library_root =
      `${shared_library_directory}/textures/`;
    constrained.library = true;
    constrained.library_asset = true;
    constrained.path =
      requested_path.startsWith(library_root) &&
      !requested_path.split("/").includes("..")
        ? requested_path
        : [
            `${shared_library_directory}/textures`,
            file_name(
              requested_path,
              args.name ?? "texture",
              ".png",
            ),
          ].join("/");
    delete constrained.texture_path;
  }
  else if (command === "mesh_generate_batch")
  {
    if (Array.isArray(args.items))
    {
      constrained.items = args.items.map((item) =>
        constrain_generated_resources(
          "mesh_generate",
          item,
          directory,
        ),
      );
    }
    const count = Number(args.count ?? 0);
    for (let index = 0; index < count; index++)
    {
      const prefix = `item_${index}_`;
      const destination_directory =
        `${shared_library_directory}/meshes`;
      constrained[`${prefix}path`] = [
        destination_directory,
        file_name(
          args[`${prefix}path`] ??
            args[`${prefix}mesh_path`],
          args[`${prefix}name`] ??
            args[`${prefix}shape`] ??
            "mesh",
          ".mesh",
        ),
      ].join("/");
      delete constrained[`${prefix}mesh_path`];
    }
  }
  else if (
    command === "compound_create" ||
    command === "construction_grammar_create" ||
    command === "detail_pattern_create"
  )
  {
    constrained.asset_directory =
      `${shared_library_directory}/meshes`;
    if (args.prefab_path)
    {
      constrained.prefab_path = [
        `${shared_library_directory}/prefabs`,
        file_name(
          args.prefab_path,
          args.name ?? "prefab",
          ".prefab",
        ),
      ].join("/");
    }
    if (Array.isArray(args.parts))
    {
      constrained.parts = args.parts.map((part) => {
        if (!part.shape)
        {
          return part;
        }
        return {
          ...part,
          mesh_path: [
            `${shared_library_directory}/meshes`,
            file_name(
              part.mesh_path,
              part.name ?? part.shape,
              ".mesh",
            ),
          ].join("/"),
        };
      });
    }
  }
  else if (command === "prefab_save")
  {
    const requested_path = String(args.path ?? "")
      .replaceAll("\\", "/");
    const prefab_root =
      `${shared_library_directory}/prefabs/`;
    constrained.path =
      requested_path.startsWith(prefab_root) &&
      !requested_path.split("/").includes("..")
        ? requested_path
        : [
            `${shared_library_directory}/prefabs`,
            file_name(
              requested_path,
              args.name ?? "prefab",
              ".prefab",
            ),
          ].join("/");
  }
  return constrained;
}
