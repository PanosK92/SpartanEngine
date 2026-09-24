/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// Shared by the public MCP server and the editor assistant bridge.
export const parametric_shapes = [
  "box",
  "cube",
  "plane",
  "quad",
  "sphere",
  "ellipsoid",
  "hemisphere",
  "cylinder",
  "cone",
  "frustum",
  "arc",
  "sector",
  "disk",
  "ring",
  "tube",
  "beveled_box",
  "rounded_box",
  "wedge",
  "wall_opening",
  "wall_openings",
  "extruded_profile",
  "revolved_profile",
  "torus",
  "capsule",
  "rounded_cylinder",
  "pipe",
  "curved_profile",
  "loft",
  "arch",
  "inset_panel",
  "tapered_extrusion",
  "grid",
  "grass_blade",
  "flower",
];

export const textured_material_scalar_keys = new Set([
  "flake_strength",
  "flake_scale",
  "pearl_strength",
  "pearl_color_r",
  "pearl_color_g",
  "pearl_color_b",
  "coat_tint_r",
  "coat_tint_g",
  "coat_tint_b",
  "coat_tint_strength",
  "world_space_uv",
  "texture_rotation",
  "texture_invert_x",
  "texture_invert_y",
  "cull_mode",
  "color_r",
  "color_g",
  "color_b",
  "color_a",
  "normal",
  "height",
  "clearcoat",
  "clearcoat_roughness",
  "anisotropic",
  "anisotropic_rotation",
  "sheen",
  "subsurface_scattering",
  "ior",
  "absorption",
  "thickness",
  "emissive_from_albedo",
  "texture_tiling_x",
  "texture_tiling_y",
  "texture_offset_x",
  "texture_offset_y",
]);

export function textured_material_scalars(args) {
  const scalars = {};
  for (const [key, value] of Object.entries(args))
  {
    if (
      key !== "height" &&
      textured_material_scalar_keys.has(key) &&
      Number.isFinite(value)
    )
    {
      scalars[key] = value;
    }
  }
  if (Number.isFinite(args.displacement_height))
    scalars.height = args.displacement_height;
  const color = args.color ?? args.base_color;
  if (Array.isArray(color))
  {
    for (const [index, key] of ["color_r", "color_g", "color_b", "color_a"].entries())
    {
      if (Number.isFinite(color[index]) && scalars[key] === undefined)
        scalars[key] = color[index];
    }
  }
  const tiling = Number(args.tiling ?? 0);
  if (tiling > 0)
  {
    scalars.texture_tiling_x = tiling;
    scalars.texture_tiling_y = tiling;
  }
  return scalars;
}
