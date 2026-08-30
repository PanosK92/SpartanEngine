/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "common.hlsl"

// relative depth match for the same surface
static const float reuse_depth_tol = 0.02f;
static const float still_px         = 0.5f;
static const float edge_px_start    = 2.0f;
static const float edge_px_full     = 10.0f;

float history_reuse(int2 px, float2 res, int2 px_max, float2 velocity_ndc)
{
    float  depth_raw = tex_depth[px].r;
    float2 uv        = (float2(px) + 0.5f) / res;
    float3 position   = get_position(depth_raw, uv);
    float4 prev_clip = mul(float4(position, 1.0f), get_view_projection_previous());
    if (prev_clip.w < 1e-6f)
    {
        return 0.0f;
    }

    float2 prev_uv = ndc_to_uv(prev_clip.xy / prev_clip.w);
    if (any(prev_uv < 0.0f) || any(prev_uv > 1.0f))
    {
        return 0.0f;
    }

    int2  prev_px        = clamp(int2(prev_uv * res), int2(0, 0), px_max);
    float prev_depth_raw = tex3[prev_px].r;
    float expected       = linearize_depth(prev_clip.z / prev_clip.w);
    float actual         = linearize_depth(prev_depth_raw);
    float abs_delta      = abs(actual - expected);
    if (abs_delta <= reuse_depth_tol * max(expected, 1e-3f))
    {
        return 1.0f;
    }

    static const int2 n4[4] =
    {
        int2(1, 0), int2(-1, 0),
        int2(0, 1), int2(0, -1)
    };
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        int2 tap = px + n4[i];
        if (any(tap < 0) || any(tap > px_max))
        {
            continue;
        }

        float2 vel_n  = tex_velocity[tap].xy;
        float  rel_px = length((vel_n - velocity_ndc) * float2(0.5f, -0.5f) * res);
        if (rel_px > still_px)
        {
            continue;
        }

        float z_n = linearize_depth(tex_depth[tap].r);
        if (abs(actual - z_n) <= reuse_depth_tol * max(z_n, 1e-3f))
        {
            return 1.0f;
        }
    }

    return 0.0f;
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
