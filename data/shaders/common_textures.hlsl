/*
Copyright(c) 2016-2023 Panos Karabelas

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

// material
Texture2D tex_material_albedo     : register (t0);
Texture2D tex_material_albedo_2   : register (t1);
Texture2D tex_material_roughness  : register (t2);
Texture2D tex_material_metallness : register (t3);
Texture2D tex_material_normal     : register (t4);
Texture2D tex_material_normal2    : register (t5);
Texture2D tex_material_height     : register (t6);
Texture2D tex_material_occlusion  : register (t7);
Texture2D tex_material_emission   : register (t8);
Texture2D tex_material_mask       : register (t9);

// g-buffer
Texture2D tex_albedo            : register(t10);
Texture2D tex_normal            : register(t11);
Texture2D tex_material          : register(t12);
Texture2D tex_material_2        : register(t13);
Texture2D tex_velocity          : register(t14);
Texture2D tex_velocity_previous : register(t15);
Texture2D tex_depth             : register(t16);

Texture2D tex_light_diffuse              : register(t17);
Texture2D tex_light_diffuse_transparent  : register(t18);
Texture2D tex_light_specular             : register(t19);
Texture2D tex_light_specular_transparent : register(t20);
Texture2D tex_light_volumetric           : register(t21);

// light depth/color maps
Texture2DArray tex_light_directional_depth : register(t22);
Texture2DArray tex_light_directional_color : register(t23);
TextureCube tex_light_point_depth          : register(t24);
TextureCube tex_light_point_color          : register(t25);
Texture2D tex_light_spot_depth             : register(t26);
Texture2D tex_light_spot_color             : register(t27);

// noise
Texture2D tex_noise_normal    : register(t28);
Texture2DArray tex_noise_blue : register(t29);

// misc
Texture2D tex_lut_ibl            : register(t30);
Texture2D tex_environment        : register(t31);
Texture2D tex_ssgi               : register(t32);
Texture2D tex_ssr                : register(t33);
Texture2D tex_frame              : register(t34);
Texture2D tex                    : register(t35);
Texture2D tex2                   : register(t36);
Texture2D tex_font_atlas         : register(t37);
TextureCube tex_reflection_probe : register(t38);
Texture2DArray tex_sss			 : register(t39);

// storage
RWTexture2D<float4> tex_uav                                : register(u0);
RWTexture2D<float4> tex_uav2                               : register(u1);
RWTexture2D<float4> tex_uav3                               : register(u2);
globallycoherent RWStructuredBuffer<uint> g_atomic_counter : register(u3);
globallycoherent RWTexture2D<float4> tex_uav_mips[12]      : register(u4);
RWTexture2DArray<float4> tex_uav4                          : register(u5);
