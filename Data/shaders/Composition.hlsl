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

// = INCLUDES ======
#include "BRDF.hlsl"
#include "Fog.hlsl"
//==================

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    const float2 uv         = input.uv;
    const float2 screen_pos = uv * g_resolution;
    float4 color            = float4(0.0f, 0.0f, 0.0f, 1.0f);
    
    // Sample from textures
    float4 sample_normal    = tex_normal.Load(int3(screen_pos, 0));
    float4 sample_material  = tex_material.Load(int3(screen_pos, 0));
    float3 light_volumetric = tex_light_volumetric.Load(int3(screen_pos, 0)).rgb;
    float depth             = tex_depth.Load(int3(screen_pos, 0)).r;
    #if TRANSPARENT
    float2 sample_ssr       = 0.0f; // we don't do ssr for transparents
    float sample_hbao       = 1.0f; // we don't do ao for transparents
    float3 sample_ssgi      = 0.0f; // we don't do ssgi for transparents
    #else
    float2 sample_ssr       = tex_ssr.Load(int3(screen_pos, 0)).xy;
    float sample_hbao       = tex_hbao.SampleLevel(sampler_point_clamp, uv, 0).r; // if hbao is disabled, the texture will be 1x1 white pixel, so we use a sampler
    float3 sample_ssgi      = tex_ssgi.Load(int3(screen_pos, 0)).rgb;
    #endif
    
    float3 camera_to_pixel  = get_view_direction(depth, uv);

    // Post-process samples
    int mat_id = round(sample_normal.a * 65535);

    // Fog
    float3 position                 = get_position(uv);
    float camera_to_pixel_length    = length(position - g_camera_position.xyz);
    float3 fog                      = get_fog_factor(position.y, camera_to_pixel_length);
    
    [branch]
    if (mat_id == 0)
    {
        color.rgb   += tex_environment.Sample(sampler_bilinear_clamp, direction_sphere_uv(camera_to_pixel)).rgb;
        color.rgb   *= saturate(g_directional_light_intensity / 128000.0f);
        fog         *= luminance(color.rgb);
    }
    else
    {
        // Sample from textures
        float4 sample_albedo    = tex_albedo.Load(int3(screen_pos, 0));
        float3 light_diffuse    = tex_light_diffuse.Load(int3(screen_pos, 0)).rgb;
        float3 light_specular   = tex_light_specular.Load(int3(screen_pos, 0)).rgb;
        
        // Create material
        Material material;
        material.albedo     = sample_albedo;
        material.roughness  = sample_material.r;
        material.metallic   = sample_material.g;
        material.emissive   = sample_material.b;
        material.F0         = lerp(0.04f, material.albedo.rgb, material.metallic);

        // Light - Image based
        float3 diffuse_energy       = 1.0f;
        float3 reflective_energy    = 1.0f;
        float3 light_ibl_specular   = Brdf_Specular_Ibl(material, sample_normal.xyz, camera_to_pixel, tex_environment, tex_lutIbl, diffuse_energy, reflective_energy);
        float3 light_ibl_diffuse    = Brdf_Diffuse_Ibl(material, sample_normal.xyz, tex_environment) * diffuse_energy; // Tone down diffuse such as that only non metals have it

        // Light - SSR
        float3 light_reflection = 0.0f;
        [branch]
        if (g_ssr_enabled && all(sample_ssr))
        {
            float fade = 1.0f - material.roughness; // fade with roughness as we don't have blurry screen space reflections yet
            
            // Reflection
            light_reflection = saturate(tex_frame.Sample(sampler_bilinear_clamp, sample_ssr).rgb);
            light_reflection *= fade * light_ibl_specular;
        }

        // Light - SSGI
        float3 light_ssgi = sample_ssgi * material.albedo.rgb;
        
        // Light - Ambient
        float3 light_ambient = saturate(g_directional_light_intensity / 128000.0f);
        
        // Modulate fog with ambient light
        fog *= light_ambient * 0.25f;
                
        // Apply ambient occlusion to ambient light
        #if SSGI
        light_ambient *= sample_hbao;
        #else
        light_ambient *= MultiBounceAO(sample_hbao, sample_albedo.rgb);
        #endif

        // Modulate with ambient light
        light_reflection    *= light_ambient;
        light_ibl_diffuse   *= light_ambient;
        light_ibl_specular  *= light_ambient;

        // Combine all light
        color.rgb += light_diffuse + light_ibl_diffuse + light_specular + light_ibl_specular + light_reflection + light_ssgi;
    }

    // Volumetric lighting and fog
    color.rgb += light_volumetric;
    color.rgb += fog;
    
    return saturate_16(color);
}
