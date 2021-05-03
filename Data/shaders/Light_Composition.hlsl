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

// = INCLUDES ========
#include "Common.hlsl"
#include "Fog.hlsl"
//====================

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;

    Surface surface;
    surface.Build(thread_id.xy);
    
    // If this is a transparent pass, ignore all opaque pixels, and vice versa.
    if ((g_is_transparent_pass && surface.is_opaque()) || (!g_is_transparent_pass && surface.is_transparent() && !surface.is_sky()))
        return;

    // Compute fog
    float3 fog = get_fog_factor(surface.position.y, surface.camera_to_pixel_length);

    // Modulate fog with ambient light
    float ambient_light = saturate(g_directional_light_intensity / 128000.0f);
    fog *= ambient_light * 0.25f;

    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    [branch]
    if (surface.is_sky()) // Sky
    {
        color.rgb   += tex_environment.SampleLevel(sampler_bilinear_clamp, direction_sphere_uv(surface.camera_to_pixel), 0).rgb;
        color.rgb   *= saturate(g_directional_light_intensity / 128000.0f);
        fog         *= luminance(color.rgb);
    }
    else // Everything else
    {
        // Light - Diffuse n Specular
        float3 light_diffuse    = tex_light_diffuse[thread_id.xy].rgb;
        float3 light_specular   = tex_light_specular[thread_id.xy].rgb;

        // Light - Refraction
        float3 light_refraction = 0.0f;
        if (surface.is_transparent())
        {
            // Compute refraction UV offset
            float ior                   = 1.5; // glass
            float scale                 = 0.03f;
            float distance_falloff      = clamp(1.0f / world_to_view(surface.position).z, -3.0f, 3.0f);
            float2 refraction_normal    = world_to_view(surface.normal.xyz, false).xy ;
            float2 refraction_uv_offset = refraction_normal * distance_falloff * scale * max(0.0f, ior - 1.0f);

            // Only refract what's behind the surface
            float depth_surface             = get_linear_depth(surface.depth);
            float depth_surface_refracted   = get_linear_depth(surface.uv + refraction_uv_offset);
            float is_behind                 = step(depth_surface - 0.02f, depth_surface_refracted); // step does a >=, but when the depth is equal, we still want refract, so we use a bias of 0.02

            // Refraction of the poor
            light_refraction = tex_frame.SampleLevel(sampler_bilinear_clamp, surface.uv + refraction_uv_offset * is_behind, 0).rgb;
        }
        
        // Compose everything
        float3 light_ds = light_diffuse * surface.albedo + light_specular;
        color.rgb += lerp(light_ds, light_refraction, 1.0f - surface.alpha);
    }

    // Accumulate fog
    color.rgb += fog; // regular
    color.rgb += tex_light_volumetric[thread_id.xy].rgb; // volumetric

    tex_out_rgba[thread_id.xy] = saturate_16(color);
}
