/*
Copyright(c) 2016-2024 Panos Karabelas

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

#ifndef SPARTAN_COMMON_TEXTURES
#define SPARTAN_COMMON_TEXTURES

//= INCLUDES =================
#include "common_buffers.hlsl"
//============================

// g-buffer
Texture2D tex_albedo       : register(t0);
Texture2D tex_normal       : register(t1);
Texture2D tex_material     : register(t2);
Texture2D tex_velocity     : register(t3);
Texture2D tex_depth        : register(t4);
Texture2D tex_depth_opaque : register(t5);

// lighting
Texture2D tex_light_diffuse              : register(t6);
Texture2D tex_light_diffuse_transparent  : register(t7);
Texture2D tex_light_specular             : register(t8);
Texture2D tex_light_specular_transparent : register(t9);
Texture2D tex_light_volumetric           : register(t10);

// shadow maps (depth and color)
Texture2DArray tex_light_directional_depth : register(t11);
Texture2DArray tex_light_directional_color : register(t12);
TextureCube tex_light_point_depth          : register(t13);
TextureCube tex_light_point_color          : register(t14);
Texture2D tex_light_spot_depth             : register(t15);
Texture2D tex_light_spot_color             : register(t16);

// misc
Texture2D tex_noise_normal       : register(t17);
Texture2DArray tex_noise_blue    : register(t18);
Texture2D tex_lut_ibl            : register(t19);
Texture2D tex_environment        : register(t20);
Texture2D tex_ssgi               : register(t21);
Texture2D tex_ssr                : register(t22);
Texture2D tex_frame              : register(t23);
Texture2D tex                    : register(t24);
Texture2D tex2                   : register(t25);
Texture2D tex_font_atlas         : register(t26);
TextureCube tex_reflection_probe : register(t27);
Texture2DArray tex_sss           : register(t28);

//= MATERIALS ===============================================================================
// texture array containing all material present int the world
static const uint material_albedo    = 0; 
static const uint material_roughness = 4; 
static const uint material_metalness = 8; 
static const uint material_normal    = 12;
static const uint material_occlusion = 16;
static const uint material_emission  = 20;
static const uint material_height    = 24;
static const uint material_mask      = 28;

Texture2D tex_materials[] : register(t29, space1);
#define GET_TEXTURE(index_texture) tex_materials[buffer_frame.material_index + index_texture]

// property buffer containg all materials present in the world
struct Material
{
    float4 color;

    float2 tiling;
    float2 offset;

    float roughness;
    float metallness;
    float normal;
    float height;

    uint flags;
    float world_space_height;
    float ior;
    float subsurface_scattering;
    
    float  sheen;
    float3 sheen_tint;
    
    float anisotropic;
    float anisotropic_rotation;
    float clearcoat;
    float clearcoat_roughness;
};

RWStructuredBuffer<Material> buffer_materials : register(u0);
Material GetMaterial() { return buffer_materials[buffer_frame.material_index]; }
//===========================================================================================

//= LIGHTS =============================================
struct Light_
{
    matrix view_projection[6];
    
    float intensity;
    float range;
    float angle;
    float bias;

    float4 color;
    
    float3 position;
    float normal_bias;
    
    float3 direction;
    uint flags;
};

RWStructuredBuffer<Light_> buffer_lights : register(u1);
//======================================================

// various storage textures/buffers
RWTexture2D<float4> tex_uav                                : register(u2);
RWTexture2D<float4> tex_uav2                               : register(u3);
RWTexture2D<float4> tex_uav3                               : register(u4);
RWTexture2DArray<float4> tex_uav_sss                       : register(u5);
globallycoherent RWStructuredBuffer<uint> g_atomic_counter : register(u6); // used by FidelityFX SPD
globallycoherent RWTexture2D<float4> tex_uav_mips[12]      : register(u7); // used by FidelityFX SPD

#endif // SPARTAN_COMMON_TEXTURES
