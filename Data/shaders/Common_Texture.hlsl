/*
Copyright(c) 2016-2020 Panos Karabelas

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

// Material
Texture2D tex_material_albedo           : register (t0);
Texture2D tex_material_roughness        : register (t1);
Texture2D tex_material_metallic         : register (t2);
Texture2D tex_material_normal           : register (t3);
Texture2D tex_material_height           : register (t4);
Texture2D tex_material_occlusion        : register (t5);
Texture2D tex_material_emission         : register (t6);
Texture2D tex_material_mask             : register (t7);

// G-buffer
Texture2D tex_albedo                    : register(t8);
Texture2D tex_normal                    : register(t9);
Texture2D tex_material                  : register(t10);
Texture2D tex_velocity                  : register(t11);
Texture2D tex_depth                     : register(t12);

Texture2D tex_light_diffuse                 : register(t13);
Texture2D tex_light_diffuse_transparent     : register(t14);
Texture2D tex_light_specular                : register(t15);
Texture2D tex_light_specular_transparent    : register(t16);
Texture2D tex_light_volumetric              : register(t17);

// Light depth/color maps
Texture2DArray light_directional_depth  : register(t18);
Texture2DArray light_directional_color  : register(t19);
TextureCube light_point_depth           : register(t20);
TextureCube light_point_color           : register(t21);
Texture2D light_spot_depth              : register(t22);
Texture2D light_spot_color              : register(t23);

// Misc
Texture2D tex_lutIbl            : register(t24);
Texture2D tex_environment       : register(t25);
Texture2D tex_normal_noise      : register(t26);
Texture2D tex_hbao              : register(t27);
Texture2D tex_ssr               : register(t28);
Texture2D tex_frame             : register(t29);
Texture2D tex                   : register(t30);
Texture2D tex2                  : register(t31);
Texture2D tex_font_atlas        : register(t32);
Texture2D tex_ssgi              : register(t33);

// Compute
RWTexture2D<float> tex_out_r                : register(u0);
RWTexture2D<float2> tex_out_rg              : register(u1);
RWTexture2D<float3> tex_out_rgb             : register(u2);
RWTexture2D<float4> tex_out_rgba            : register(u3);
RWTexture2D<float3> tex_out_rgb2            : register(u4);
RWTexture2D<float3> tex_out_rgb3            : register(u5);
RWTexture2DArray<float4> uav_array_rgba     : register(u6);
