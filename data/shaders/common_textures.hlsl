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
Texture2D tex_velocity          : register(t13);
Texture2D tex_velocity_previous : register(t14);
Texture2D tex_depth             : register(t15);

// lighting
Texture2D tex_light_diffuse              : register(t16);
Texture2D tex_light_diffuse_transparent  : register(t17);
Texture2D tex_light_specular             : register(t18);
Texture2D tex_light_specular_transparent : register(t19);
Texture2D tex_light_volumetric           : register(t20);

// shadow maps (depth and color)
Texture2DArray tex_light_directional_depth : register(t21);
Texture2DArray tex_light_directional_color : register(t22);
TextureCube tex_light_point_depth          : register(t23);
TextureCube tex_light_point_color          : register(t24);
Texture2D tex_light_spot_depth             : register(t25);
Texture2D tex_light_spot_color             : register(t26);

// noise
Texture2D tex_noise_normal    : register(t27);
Texture2DArray tex_noise_blue : register(t28);

// misc
Texture2D tex_lut_ibl            : register(t29);
Texture2D tex_environment        : register(t30);
Texture2D tex_ssgi               : register(t31);
Texture2D tex_ssr                : register(t32);
Texture2D tex_frame              : register(t33);
Texture2D tex                    : register(t34);
Texture2D tex2                   : register(t35);
Texture2D tex_font_atlas         : register(t36);
TextureCube tex_reflection_probe : register(t37);
Texture2DArray tex_sss			 : register(t38);

struct MaterialProperties
{
    float sheen;
    float3 sheen_tint;
    
    float anisotropic;
    float anisotropic_rotation;
    float clearcoat;
    float clearcoat_roughness;
    
    float subsurface_scattering;
    float ior;
    float2 padding;
};

// storage
RWTexture2D<float4> tex_uav                                : register(u0);
RWTexture2D<float4> tex_uav2                               : register(u1);
RWTexture2D<float4> tex_uav3                               : register(u2);
RWTexture2DArray<float4> tex_uav_sss                       : register(u3);
RWStructuredBuffer<MaterialProperties> material_properties : register(u4); // matches ID/index to material properties, used for lighting
globallycoherent RWStructuredBuffer<uint> g_atomic_counter : register(u5); // used by FidelityFX SPD
globallycoherent RWTexture2D<float4> tex_uav_mips[12]      : register(u6); // used by FidelityFX SPD
