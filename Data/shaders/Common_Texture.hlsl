/*
Copyright(c) 2016-2021 Panos Karabelas

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
Texture2DArray tex_light_directional_depth  : register(t18);
Texture2DArray tex_light_directional_color  : register(t19);
TextureCube tex_light_point_depth           : register(t20);
TextureCube tex_light_point_color           : register(t21);
Texture2D tex_light_spot_depth              : register(t22);
Texture2D tex_light_spot_color              : register(t23);

// Noise
Texture2D tex_noise_normal      : register(t24);
Texture2DArray tex_noise_blue   : register(t25);

// Misc
Texture2D tex_lutIbl        : register(t26);
Texture2D tex_environment   : register(t27);
Texture2D tex_ssao          : register(t28);
Texture2D tex_ssr           : register(t29);
Texture2D tex_frame         : register(t30);
Texture2D tex               : register(t31);
Texture2D tex2              : register(t32);
Texture2D tex_font_atlas    : register(t33);

// RWTexture2D
RWTexture2D<float> tex_out_r                                : register(u0);
RWTexture2D<float2> tex_out_rg                              : register(u1);
RWTexture2D<float3> tex_out_rgb                             : register(u2);
RWTexture2D<float3> tex_out_rgb2                            : register(u3);
RWTexture2D<float3> tex_out_rgb3                            : register(u4);
RWTexture2D<float4> tex_out_rgba                            : register(u5);
globallycoherent RWTexture2D<float4> tex_out_rgba_mips[12]  : register(u6);

// Structured buffers
globallycoherent RWStructuredBuffer<uint> g_atomic_counter : register(u18); // u6 + 12 mips = u18

// Misc
static const float2 g_tex_noise_normal_scale    = float2(g_resolution_render.x / 256.0f, g_resolution_render.y / 256.0f);
static const float2 g_tex_noise_blue_scale      = float2(g_resolution_render.x / 470.0f, g_resolution_render.y / 470.0f);
static const float g_envrionement_max_mip       = 11.0f;
