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

//= TEXTURES ===================================
Texture2D tex_albedo            : register(t0);
Texture2D tex_normal            : register(t1);
Texture2D tex_material          : register(t2);
Texture2D tex_depth             : register(t3);
Texture2D tex_ssao              : register(t4);
Texture2D tex_light_diffuse     : register(t5);
Texture2D tex_light_specular    : register(t6);
Texture2D tex_lightVolumetric   : register(t7);
Texture2D tex_ssr               : register(t8);
Texture2D tex_frame             : register(t9);
Texture2D tex_lutIbl            : register(t10);
Texture2D tex_environment       : register(t11);
//==============================================

// = INCLUDES ======
#include "BRDF.hlsl"
//==================

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float2 uv       = input.uv;
    float3 color    = 0.0f;
    
    // Sample from textures
    float4 sample_material  = tex_material.Sample(sampler_point_clamp, uv);
    float3 light_volumetric = tex_lightVolumetric.Sample(sampler_point_clamp, uv).rgb;
    float3 normal           = tex_normal.Sample(sampler_point_clamp, uv).xyz;
    float depth             = tex_depth.Sample(sampler_point_clamp, uv).r;
    float2 sample_ssr       = tex_ssr.Sample(sampler_point_clamp, uv).xy;
    float sample_ssao       = tex_ssao.Sample(sampler_point_clamp, uv).r;   
    float3 camera_to_pixel  = get_view_direction(depth, uv);
    
    // Volumetric lighting
    color += light_volumetric;
    
    [branch]
    if (sample_material.a != 1.0f)
    {
        color += tex_environment.Sample(sampler_bilinear_clamp, direction_sphere_uv(camera_to_pixel)).rgb;
        color *= clamp(g_directional_light_intensity / 5.0f, 0.01f, 1.0f);
        return float4(color, 1.0f);
    }
    else
    {
        // Sample from textures
        float4 sample_albedo    = tex_albedo.Sample(sampler_point_clamp, uv);
        float4 light_diffuse    = tex_light_diffuse.Sample(sampler_point_clamp, uv);
        float4 light_specular   = tex_light_specular.Sample(sampler_point_clamp, uv);
    
        // Create material
        Material material;
        material.albedo     = sample_albedo.rgb;
        material.roughness  = sample_material.r;
        material.metallic   = sample_material.g;
        material.emissive   = sample_material.b;
        material.F0         = lerp(0.04f, material.albedo, material.metallic);
    
        // Light - Ambient (Hacked together because there is no GI yet and I'm hackerman)
        float light_ambient = clamp(g_directional_light_intensity * 0.12f, 0.0f, 1.0f) * sample_ssao;
        
        // Light - Image based
        float3 F                    = 0.0f;   
        float3 light_ibl_specular   = Brdf_Specular_Ibl(material, normal, camera_to_pixel, tex_environment, tex_lutIbl, F) * light_ambient;
        float3 light_ibl_diffuse    = Brdf_Diffuse_Ibl(material, normal, tex_environment) * energy_conservation(F, material.metallic) * light_ambient;
        
        // Light - SSR
        float3 light_reflection = 0.0f;
        [branch]
        if (g_ssr_enabled && sample_ssr.x != 0.0f && sample_ssr.y != 0.0f)
        {
            light_reflection = tex_frame.Sample(sampler_bilinear_clamp, sample_ssr.xy).rgb * F * light_ambient;
        }
    
        // Light - Emissive
        float3 light_emissive = material.emissive * material.albedo * 50.0f;
    
        // Combine and return
        color += light_diffuse.rgb + light_ibl_diffuse + light_specular.rgb + light_ibl_specular + light_reflection + light_emissive;
        return float4(color, sample_albedo.a);
    }
}
