/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= includes =========
#include "common.hlsl"
//====================

// constants
static const float refraction_strength = 0.3f;

// emulates the fact that deeper water is less transparent and shifts the color towards blue
float3 apply_water_absorption(float3 color, float depth)
{
    float3 absorption = float3(0.1f, 0.05f, 0.02f); // absorption coefficients for rgb (approximate, in 1/meters)
    return color * exp(-absorption * depth);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get resolution
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);

    // create surface
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // skip sky
    if (surface.is_sky())
        return;

    // compute common values
    float3 view_dir         = -surface.camera_to_pixel;
    float2 uv               = (thread_id.xy + 0.5f) / resolution_out;
    float depth_transparent = linearize_depth(surface.depth);
    
    // background/transparency/refraction
    float3 background = tex2[thread_id.xy].rgb;
    float3 refraction = background;
    if (surface.is_transparent())
    {
        // refraction
        float inv_dist     = saturate(1.0f / (surface.camera_to_pixel_length + 0.0001f));
        float2 uv_offset   = world_to_view(surface.normal, false).xy * refraction_strength * inv_dist;
        float2 refract_uv  = uv + uv_offset;
        float3 refracted   = tex2.SampleLevel(samplers[sampler_bilinear_clamp], refract_uv, 0.0f).rgb;
        float depth_opaque = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], refract_uv, 0.0f).r);
        float water_depth  = max(depth_opaque - depth_transparent, 0.0f);
        refraction         = lerp(background, refracted, saturate(water_depth * 100.0f));

        // absorption
        refraction = apply_water_absorption(refraction, water_depth);
    }

    // add reflections and refraction
    float n_dot_v        = saturate(dot(surface.normal, view_dir));
    float3 reflection    = tex[thread_id.xy].rgb;
    float2 brdf          = tex3.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).rg;
    float3 surface_color = reflection * (surface.F0 * brdf.x + brdf.y) + refraction;

    tex_uav[thread_id.xy] += float4(surface_color, 0.0f);
}
