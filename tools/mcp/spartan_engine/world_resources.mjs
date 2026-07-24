import path from "node:path";

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

export function world_resource_directory(world)
{
  const world_name =
    world?.name ??
    world?.file_path ??
    world?.path ??
    "world";
  return `project/${safe_name(world_name)}_resources`;
}

export function generated_resource_command(command)
{
  return (
    command === "material_create" ||
    command === "material_semantic_create" ||
    command === "mesh_generate" ||
    command === "mesh_generate_batch"
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
    constrained.path = [
      directory,
      file_name(
        args.path,
        args.name ?? args.semantic ?? "material",
        ".material",
      ),
    ].join("/");
  }
  else if (command === "material_palette_create")
  {
    constrained.directory = directory;
    constrained.materials = (
      Array.isArray(args.materials)
        ? args.materials
        : []
    ).map((material) => ({
      ...material,
      path: [
        directory,
        file_name(
          material.path,
          material.name ?? "material",
          ".material",
        ),
      ].join("/"),
    }));
  }
  else if (command === "mesh_generate")
  {
    constrained.path = [
      directory,
      file_name(
        args.path ?? args.mesh_path,
        args.name ??
          args.shape ??
          args.generator ??
          "mesh",
        ".mesh",
      ),
    ].join("/");
    delete constrained.mesh_path;
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
      constrained[`${prefix}path`] = [
        directory,
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
    constrained.asset_directory = directory;
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
            directory,
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
  return constrained;
}
