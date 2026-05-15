/*
Copyright(c) 2015-2026 Panos Karabelas

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

// writes a clustered lighting debug heatmap into debug_output
// mode 1, lights per cluster as a blue->cyan->green->yellow->red ramp normalized to a tunable cap
// mode 2, cluster z slice index as a smooth hue ramp, helpful for spotting slice boundaries

#include "common.hlsl"
#include "light_cluster.hlsl"

// classic blue->cyan->green->yellow->red ramp, t in [0, 1]
float3 heatmap_ramp(float t)
{
    t = saturate(t);
    float3 c0 = float3(0.05f, 0.05f, 0.30f);
    float3 c1 = float3(0.00f, 0.55f, 0.85f);
    float3 c2 = float3(0.00f, 0.85f, 0.20f);
    float3 c3 = float3(1.00f, 0.85f, 0.00f);
    float3 c4 = float3(1.00f, 0.15f, 0.00f);

    if (t < 0.25f) return lerp(c0, c1, t / 0.25f);
    if (t < 0.50f) return lerp(c1, c2, (t - 0.25f) / 0.25f);
    if (t < 0.75f) return lerp(c2, c3, (t - 0.50f) / 0.25f);
    return                lerp(c3, c4, (t - 0.75f) / 0.25f);
}

// turbo-like hue ramp for slice index visualization, low slice = magenta, high slice = red
float3 slice_color(float t)
{
    t = saturate(t);
    float3 c0 = float3(0.40f, 0.00f, 0.60f);
    float3 c1 = float3(0.00f, 0.30f, 1.00f);
    float3 c2 = float3(0.00f, 0.90f, 0.60f);
    float3 c3 = float3(0.95f, 0.95f, 0.00f);
    float3 c4 = float3(0.95f, 0.30f, 0.00f);

    if (t < 0.25f) return lerp(c0, c1, t / 0.25f);
    if (t < 0.50f) return lerp(c1, c2, (t - 0.25f) / 0.25f);
    if (t < 0.75f) return lerp(c2, c3, (t - 0.50f) / 0.25f);
    return                lerp(c3, c4, (t - 0.75f) / 0.25f);
}

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    if (thread_id.x >= (uint)resolution.x || thread_id.y >= (uint)resolution.y)
        return;

    // uv covers [0, resolution_scale] inside the texture, uv_full is the [0, 1] screen space uv
    float2 uv      = (thread_id.xy + 0.5f) / resolution;
    float2 uv_full = uv / buffer_frame.resolution_scale;

    // pixels outside the rendered region read garbage depth, clear them to a neutral value and bail
    if (uv_full.x > 1.0f || uv_full.y > 1.0f)
    {
        tex_uav[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float  depth   = tex_depth.SampleLevel(samplers[sampler_point_clamp], uv_full, 0).r;
    float3 pos_ws  = get_position(depth, uv_full);
    float  view_z  = mul(float4(pos_ws, 1.0f), get_view()).z;

    uint3 cid     = cluster_id_from_screen(uv_full, view_z);
    uint  flat_id = cluster_flat(cid);
    uint2 range   = cluster_light_grid[flat_id];

    // pass_get_f3_value().x carries the visualization mode, .y carries the saturation cap for the count ramp
    uint  mode = (uint)pass_get_f3_value().x;
    float cap  = max(pass_get_f3_value().y, 1.0f);

    float3 color = float3(0.0f, 0.0f, 0.0f);

    if (mode == 1u)
    {
        if (range.y == 0u)
        {
            // empty clusters render as a dim grid so the absence of lights is visible without being noisy
            color = float3(0.02f, 0.02f, 0.02f);
        }
        else
        {
            color = heatmap_ramp((float)range.y / cap);
        }
    }
    else // mode == 2u, slice index visualization
    {
        float t = (float)cid.z / max((float)buffer_frame.cluster_count_z - 1.0f, 1.0f);
        color   = slice_color(t);
    }

    // overlay subtle cluster boundary lines so the grid is legible even with constant content
    float frac_x  = frac(uv_full.x * (float)buffer_frame.cluster_count_x);
    float frac_y  = frac(uv_full.y * (float)buffer_frame.cluster_count_y);
    float edge_x  = step(frac_x, 0.01f) + step(0.99f, frac_x);
    float edge_y  = step(frac_y, 0.01f) + step(0.99f, frac_y);
    color         = lerp(color, float3(0.0f, 0.0f, 0.0f), saturate(edge_x + edge_y) * 0.35f);

    tex_uav[thread_id.xy] = float4(color, 1.0f);
}
