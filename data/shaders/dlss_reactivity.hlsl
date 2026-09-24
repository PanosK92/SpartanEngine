/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"

// relative depth match for the same surface
static const float reuse_depth_tol = 0.02f;
static const float edge_px_start    = 0.5f;
static const float edge_px_full     = 3.0f;

float history_reuse(int2 px, float2 res, int2 px_max, float2 velocity_ndc)
{
    float depth_raw = tex_depth[px].r;
    float2 uv = (float2(px) + 0.5f) / res;
    // Geometry motion includes the object's previous transform. Camera-only reprojection
    // follows the road behind a moving car instead of the car and its thin mirrors.
    float2 prev_uv = uv - velocity_ndc * float2(0.5f, -0.5f)
        + (buffer_frame.taa_jitter_previous - buffer_frame.taa_jitter_current) * float2(0.5f, -0.5f);
    if (any(prev_uv < 0.0f) || any(prev_uv > 1.0f))
        return 0.0f;

    float expected = abs(tex_velocity[px].w);
    if (expected <= 0.0f || expected >= FLT_MAX_16U || isnan(expected) || isinf(expected))
    {
        float3 position = get_position(depth_raw, uv);
        float4 prev_clip = mul(float4(position, 1.0f), get_view_projection_previous());
        if (prev_clip.w <= 1e-6f)
            return 0.0f;
        expected = linearize_depth(prev_clip.z / prev_clip.w);
    }

    // Weight depth support at the reprojected footprint; a single unrelated neighbor
    // must not validate the entire pixel along a moving silhouette.
    float2 pos = prev_uv * res - 0.5f;
    int2 base = int2(floor(pos));
    float2 f = frac(pos);
    float reuse = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            int2 tap = base + int2(x, y);
            if (any(tap < 0) || any(tap > px_max))
                continue;
            float z = tex3[tap].r;
            float match = 0.0f;
            if (depth_raw <= 1e-8f)
                match = z <= 1e-8f ? 1.0f : 0.0f;
            else if (z > 1e-8f)
            {
                float error = abs(linearize_depth(z) - expected) / max(expected, 1e-3f);
                match = 1.0f - smoothstep(reuse_depth_tol, 2.0f * reuse_depth_tol, error);
            }
            float2 weight = float2(x == 0 ? 1.0f - f.x : f.x, y == 0 ? 1.0f - f.y : f.y);
            reuse += weight.x * weight.y * match;
        }
    }
    return saturate(reuse);
}

float velocity_edge(int2 px, float2 res, int2 px_max, float2 velocity_ndc)
{
    float max_rel = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            int2 tap = clamp(px + int2(x, y), int2(0, 0), px_max);
            float2 vel_n = tex_velocity[tap].xy;
            float rel_px = length((vel_n - velocity_ndc) * float2(0.5f, -0.5f) * res);
            max_rel = max(max_rel, rel_px);
        }
    }

    return saturate((max_rel - edge_px_start) / (edge_px_full - edge_px_start));
}

[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    float2 res = get_render_resolution_active();
    int2 px_max = max(int2(res) - 1, int2(0, 0));
    int2 px = int2(tid.xy);
    if (any(px > px_max))
    {
        return;
    }

    float scale = saturate(pass_get_f3_value().x);
    float2 velocity_ndc = tex_velocity[px].xy;
    float disocclusion = 1.0f - history_reuse(px, res, px_max, velocity_ndc);
    float edge = velocity_edge(px, res, px_max, velocity_ndc);
    float reactive = saturate(max(disocclusion, edge)) * scale;
    tex_uav[px] = float4(reactive, 0.0f, 0.0f, 0.0f);
}
