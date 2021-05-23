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

//= INCLUDES ===========
#include "Common.hlsl"
#include "Velocity.hlsl"
//======================

static const float g_ssgi_blend_min = 0.1f;
static const float g_ssgi_blend_max = 1.0f;

// From "Temporal Reprojection Anti-Aliasing"
// https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
    float3 r = q - p;
    float3 rmax = aabb_max - p.xyz;
    float3 rmin = aabb_min - p.xyz;

    if (r.x > rmax.x + FLT_MIN)
        r *= (rmax.x / r.x);
    if (r.y > rmax.y + FLT_MIN)
        r *= (rmax.y / r.y);
    if (r.z > rmax.z + FLT_MIN)
        r *= (rmax.z / r.z);

    if (r.x < rmin.x - FLT_MIN)
        r *= (rmin.x / r.x);
    if (r.y < rmin.y - FLT_MIN)
        r *= (rmin.y / r.y);
    if (r.z < rmin.z - FLT_MIN)
        r *= (rmin.z / r.z);

    return p + r;
}

// Clip history to the neighbourhood of the current sample
float3 clip_history(uint2 thread_id,  Texture2D tex_input, float3 color_history)
{
    float3 ctl = tex_input[thread_id + uint2(-1, -1)].rgb;
    float3 ctc = tex_input[thread_id + uint2(0, -1)].rgb;
    float3 ctr = tex_input[thread_id + uint2(1, -1)].rgb;
    float3 cml = tex_input[thread_id + uint2(-1, 0)].rgb;
    float3 cmc = tex_input[thread_id].rgb;
    float3 cmr = tex_input[thread_id + uint2(1, 0)].rgb;
    float3 cbl = tex_input[thread_id + uint2(-1, 1)].rgb;
    float3 cbc = tex_input[thread_id + uint2(0, 1)].rgb;
    float3 cbr = tex_input[thread_id + uint2(1, 1)].rgb;

    float3 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
    float3 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
    float3 color_avg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0f;

    return saturate_16(clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history));
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution_rt;
    float depth     = get_linear_depth(thread_id.xy);

    // Get input color
    float3 color_input = tex[thread_id.xy].rgb;
    
    // Get history color
    float2 velocity         = get_velocity_closest_3x3(uv);
    float2 uv_reprojected   = uv - velocity;
    float3 color_history    = tex2.SampleLevel(sampler_bilinear_clamp, uv_reprojected, 0).rgb;

    // Clip history to the neighbourhood of the current sample
    color_history = clip_history(thread_id.xy, tex, color_history);

    // Clamp
    float blend_factor = g_ssgi_blend_min;
    blend_factor = clamp(blend_factor, g_ssgi_blend_min, g_ssgi_blend_max);

    // Override blend factor if the re-projected uv is out of screen
    blend_factor = is_saturated(uv_reprojected) ? blend_factor : 1.0f;

    // Blend
    tex_out_rgb[thread_id.xy] = lerp(color_history, color_input, blend_factor);
}
