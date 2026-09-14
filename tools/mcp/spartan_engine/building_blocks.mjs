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
