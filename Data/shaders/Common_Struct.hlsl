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

//= INCLUDES =========
#include "Common.hlsl"
//====================

struct Surface
{
    void Build(float2 _uv, float4 sample_albedo, float4 sample_normal, float4 sample_material, float sample_depth, float sample_ssao)
    {
        id                     = round(sample_normal.a * 65535);
        albedo                 = sample_albedo;
        roughness              = sample_material.r;
        metallic               = sample_material.g;
        emissive               = sample_material.b;
        clearcoat              = mat_clearcoat_clearcoatRough_aniso_anisoRot[id].x;
        clearcoat_roughness    = mat_clearcoat_clearcoatRough_aniso_anisoRot[id].y;
        anisotropic            = mat_clearcoat_clearcoatRough_aniso_anisoRot[id].z;
        anisotropic_rotation   = mat_clearcoat_clearcoatRough_aniso_anisoRot[id].w;
        sheen                  = mat_sheen_sheenTint_pad[id].x;
        sheen_tint             = mat_sheen_sheenTint_pad[id].y;
        occlusion              = min(sample_material.a, sample_ssao);
        F0                     = lerp(0.04f, sample_albedo.rgb, metallic);

        uv                      = _uv;
        depth                   = sample_depth;

        // Reconstruct position from depth
        float x             = uv.x * 2.0f - 1.0f;
        float y             = (1.0f - uv.y) * 2.0f - 1.0f;
        float4 pos_clip     = float4(x, y, depth, 1.0f);
        float4 pos_world    = mul(pos_clip, g_view_projection_inverted);
        position            = pos_world.xyz / pos_world.w;

        normal                  = normalize(sample_normal.xyz);
        camera_to_pixel         = position - g_camera_position.xyz;
        camera_to_pixel_length  = length(camera_to_pixel);
        camera_to_pixel         = normalize(camera_to_pixel);
    }

    bool is_sky()           { return id == 0; }
    bool is_transparent()   { return albedo.a != 1.0f; }

    // Material
    float4  albedo;
    float   roughness;
    float   metallic;
    float   clearcoat;
    float   clearcoat_roughness;
    float   anisotropic;
    float   anisotropic_rotation;
    float   sheen;
    float   sheen_tint;
    float   occlusion;
    float   emissive;
    float3  F0;
    int     id;

    // Positional
    float2  uv;
    float   depth;
    float3  position;
    float3  normal;
    float3  camera_to_pixel;
    float   camera_to_pixel_length;
};

struct Light
{
    float3  color;
    float3  position;
    float3  direction;
    float   distance_to_pixel;
    float   angle;
    float   bias;
    float   normal_bias;
    float   near;
    float   far;
    float   attenuation;
    float   intensity;
    float3  radiance;
    float   n_dot_l;
};
