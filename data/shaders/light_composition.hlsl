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

// = INCLUDES ========
#include "common.hlsl"
//====================

float compute_fade_factor(float2 uv)
{
    float edge_threshold = 0.05f; // how close to the edge to start fading
    float2 edge_distance = min(uv, 1.0f - uv);
    return saturate(min(edge_distance.x, edge_distance.y) / edge_threshold);
}

float3 refract_vector(float3 i, float3 n, float eta)
{
    float cosi = dot(-i, n);
    float cost2 = 1.0f - eta * eta * (1.0f - cosi * cosi);
    return eta * i + (eta * cosi - sqrt(abs(cost2))) * n;
}

float3 refraction(Surface surface, float ior, float scale, float depth_bias)
{
    const float chromatic_aberration_strength = 0.0012f;
    
    float3 view_pos    = world_to_view(surface.position);
    float3 view_normal = world_to_view(surface.normal, false);
    float3 view_dir    = normalize(view_pos);
    
    // compute refracted uv
    float3 refracted_dir        = refract_vector(view_dir, view_normal, 1.0f / ior);
    float2 refraction_uv_offset = refracted_dir.xy * scale;
    float2 refracted_uv         = surface.uv + refraction_uv_offset;

    // get mip level
    float frame_mip_count = pass_get_f3_value().x;
    float mip_level       = lerp(0, frame_mip_count, surface.roughness_alpha);
    
    // chromatic aberration
    float3 color_refracted = float3(0, 0, 0);
    {
        float2 ca_offsets[3];
        ca_offsets[0] = float2(chromatic_aberration_strength, 0.0f);
        ca_offsets[1] = float2(0.0f, 0.0f);
        ca_offsets[2] = float2(-chromatic_aberration_strength, 0.0f); 

        [unroll]
        for (int i = 0; i < 3; ++i)
        {
            float4 sampled_color = tex_frame.SampleLevel(samplers[sampler_trilinear_clamp], refracted_uv + ca_offsets[i], mip_level);
            color_refracted[i] = sampled_color[i];
        }
    }

    // screen fade
    float fade_factor     = compute_fade_factor(refracted_uv);
    float3 color_original = tex_frame.SampleLevel(samplers[sampler_trilinear_clamp], surface.uv, mip_level).rgb;
    
    return lerp(color_original, color_refracted, fade_factor);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    Surface surface;
    surface.Build(thread_id.xy, true, true, false);

    bool early_exit_1 = pass_is_opaque() && surface.is_transparent(); // if this is an opaque pass, ignore all transparent pixels.
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque(); // if this is an transparent pass, ignore all opaque pixels.
    bool early_exit_3 = pass_is_transparent() && surface.is_sky();    // if this is a transparent pass, ignore sky pixels (they only render in the opaque)
    if (early_exit_1 || early_exit_2 || early_exit_3)
        return;

    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    // sky
    if (surface.is_sky()) 
    {
        color.rgb += tex_environment.SampleLevel(samplers[sampler_bilinear_clamp], direction_sphere_uv(surface.camera_to_pixel), 0).rgb;
        color.rgb *= saturate(buffer_light.intensity); // modulate it's intensity in order to fake day/night.
    }
    else // everything else
    {
        // diffuse and specular
        float3 light_diffuse  = tex_light_diffuse[thread_id.xy].rgb;
        float3 light_specular = tex_light_specular[thread_id.xy].rgb;

        // refraction
        float3 light_refraction = 0.0f;
        if (surface.is_transparent())
        {
            float ior        = 1.33; // water
            float scale      = 0.05f;
            float depth_bias = 0.02f;
            light_refraction = refraction(surface, ior, scale, depth_bias); 
        }
        
        // compose
        float3 light_ds = (light_diffuse + surface.gi) * surface.albedo + light_specular;
        color.rgb       += lerp(light_ds, light_refraction, 1.0f - surface.alpha);
    }

    // volumetric fog
    color.rgb += tex_light_volumetric[thread_id.xy].rgb;

    tex_uav[thread_id.xy] = saturate_16(color);
}

