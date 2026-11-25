/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "common.hlsl"
//============================

RWStructuredBuffer<uint>   water_visibility       : register(u89);
StructuredBuffer<float4>   water_visibility_boxes : register(t99);

bool aabb_hiz_visible(float3 bmin, float3 bmax, float3 cam_pos, float2 active_res, uint max_hiz_mip)
{
    if (all(cam_pos >= bmin - 3.0f) && all(cam_pos <= bmax + 3.0f))
    {
        return true;
    }

    matrix vp = get_view_projection();
    float2 uv_min = float2(1e9f, 1e9f);
    float2 uv_max = float2(-1e9f, -1e9f);
    float  max_z  = 0.0f;

    [unroll]
    for (uint c = 0u; c < 8u; ++c)
    {
        float3 corner = float3(
            (c & 1u) ? bmax.x : bmin.x,
            (c & 2u) ? bmax.y : bmin.y,
            (c & 4u) ? bmax.z : bmin.z
        );
        float4 clip = mul(float4(corner, 1.0f), vp);
        if (clip.w <= 1e-4f)
        {
            return true;
        }
        float inv_w = 1.0f / clip.w;
        float2 ndc  = clip.xy * inv_w;
        float2 uv   = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        uv_min      = min(uv_min, uv);
        uv_max      = max(uv_max, uv);
        max_z       = max(max_z, clip.z * inv_w);
    }

    if (uv_max.x < 0.0f || uv_min.x > 1.0f || uv_max.y < 0.0f || uv_min.y > 1.0f)
    {
        return false;
    }

    uv_min = saturate(uv_min);
    uv_max = saturate(uv_max);

    // Sample directly at mip 3 (8x8 pixel tiles) so wide, vertically thin river segments
    // never pull in sky texels from coarse mips (mip 7-9) above the treeline.
    uint   mip      = min(3u, max_hiz_mip);
    float2 full_res = max(buffer_frame.resolution_render, float2(1.0f, 1.0f));
    uint2  mip_dims = max(uint2(1u, 1u), uint2(full_res) >> mip);
    float2 ruv_min  = screen_uv_to_render_uv(uv_min);
    float2 ruv_max  = screen_uv_to_render_uv(uv_max);
    int2   t0       = clamp(int2(floor(ruv_min * float2(mip_dims))), int2(0, 0), int2(mip_dims) - 1);
    int2   t1       = clamp(int2(floor(ruv_max * float2(mip_dims))), int2(0, 0), int2(mip_dims) - 1);
    int2   span     = max(t1 - t0 + 1, int2(1, 1));
    int2   step_t   = max((span + 15) / 16, int2(1, 1));

    for (int ty = t0.y; ty <= t1.y; ty += step_t.y)
    {
        for (int tx = t0.x; tx <= t1.x; tx += step_t.x)
        {
            float d = tex.Load(int3(tx, ty, mip)).r;
            if (max_z >= (d - 1e-4f))
            {
                return true;
            }
        }
    }
    return false;
}

// Unified conservative Hi-Z visibility test for ocean + frustum-visible non-ocean water (rivers, pools, fountains).
// Ocean uses a two-stage mip-3 -> mip-0 exact pixel check (zero horizon/hill/DRS false positives);
// non-ocean water AABBs use a conservative 4-tap Hi-Z box test.
[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint  mip_level   = asuint(buffer_pass.values[0].x);
    uint  mip_width   = asuint(buffer_pass.values[0].y);
    uint  mip_height  = asuint(buffer_pass.values[0].z);
    float sea_level   = buffer_pass.values[0].w;
    uint  box_count   = asuint(buffer_pass.values[1].x);
    uint  max_hiz_mip = asuint(buffer_pass.values[1].y);

    bool   visible    = false;
    float3 cam_pos    = get_camera_position();
    float2 active_res = get_render_resolution_active();

    if (dispatch_thread_id.x < mip_width && dispatch_thread_id.y < mip_height)
    {
        uint flat_id = dispatch_thread_id.x + dispatch_thread_id.y * mip_width;
        if (flat_id < box_count)
        {
            float3 bmin = water_visibility_boxes[flat_id * 2u + 0u].xyz;
            float3 bmax = water_visibility_boxes[flat_id * 2u + 1u].xyz;
            if (aabb_hiz_visible(bmin, bmax, cam_pos, active_res, max_hiz_mip))
            {
                visible = true;
            }
        }

        if (!visible)
        {
            float2 full_res   = max(buffer_frame.resolution_render, float2(1.0f, 1.0f));
            float2 tile_scale = full_res / float2(mip_width, mip_height);
            float2 px_min_f   = float2(dispatch_thread_id.xy) * tile_scale;
            float2 px_max_f   = float2(dispatch_thread_id.xy + 1u) * tile_scale;

            if (px_min_f.x < active_res.x && px_min_f.y < active_res.y)
            {
                if (cam_pos.y <= sea_level + 2.5f)
                {
                    visible = true;
                }
                else
                {
                    float2 clamped_max_f = min(px_max_f, active_res);
                    float2 inv_active    = 1.0f / active_res;
                    float  y_threshold   = sea_level + 2.0f;

                    // Stage 1: conservative coarse reject using Hi-Z min depth (farthest surface in tile).
                    bool needs_exact = (px_max_f.x > active_res.x - 1.0f || px_max_f.y > active_res.y - 1.0f);
                    if (!needs_exact)
                    {
                        float  d_min   = tex.Load(int3(dispatch_thread_id.xy, mip_level)).r;
                        float2 uv_lo_l = float2(px_min_f.x, clamped_max_f.y) * inv_active;
                        float2 uv_lo_r = float2(clamped_max_f.x, clamped_max_f.y) * inv_active;
                        float2 uv_hi_l = float2(px_min_f.x, px_min_f.y) * inv_active;
                        float2 uv_hi_r = float2(clamped_max_f.x, px_min_f.y) * inv_active;

                        if (d_min <= 1e-6f)
                        {
                            float3 r0 = normalize(get_position(0.5f, uv_lo_l) - cam_pos);
                            float3 r1 = normalize(get_position(0.5f, uv_lo_r) - cam_pos);
                            float3 r2 = normalize(get_position(0.5f, uv_hi_l) - cam_pos);
                            float3 r3 = normalize(get_position(0.5f, uv_hi_r) - cam_pos);
                            if (min(min(r0.y, r1.y), min(r2.y, r3.y)) < 0.002f)
                            {
                                needs_exact = true;
                            }
                        }
                        else
                        {
                            float y0 = get_position(d_min, uv_lo_l).y;
                            float y1 = get_position(d_min, uv_lo_r).y;
                            float y2 = get_position(d_min, uv_hi_l).y;
                            float y3 = get_position(d_min, uv_hi_r).y;
                            if (min(min(y0, y1), min(y2, y3)) <= y_threshold)
                            {
                                needs_exact = true;
                            }
                        }
                    }

                    // Stage 2: exact mip-0 verification on candidate tiles.
                    if (needs_exact)
                    {
                        int2 p_min  = int2(px_min_f);
                        int2 p_max  = max(p_min, int2(clamped_max_f) - 1);
                        int2 span   = max(p_max - p_min, int2(1, 1));
                        int2 step_p = max(span / 4, int2(1, 1));

                        for (int py = p_min.y; py <= p_max.y && !visible; py += step_p.y)
                        {
                            for (int px = p_min.x; px <= p_max.x; px += step_p.x)
                            {
                                float  d  = tex.Load(int3(px, py, 0)).r;
                                float2 uv = (float2(px, py) + 0.5f) * inv_active;
                                if (d <= 1e-6f)
                                {
                                    float3 ray_dir = normalize(get_position(0.5f, uv) - cam_pos);
                                    if (ray_dir.y < 0.002f)
                                    {
                                        visible = true;
                                        break;
                                    }
                                }
                                else
                                {
                                    float3 world_pos = get_position(d, uv);
                                    if (world_pos.y <= y_threshold)
                                    {
                                        visible = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

#if defined(API_WEBGPU)
    if (visible)
    {
        water_visibility[0] = 1u;
    }
#else
    if (WaveActiveAnyTrue(visible) && WaveIsFirstLane())
    {
        water_visibility[0] = 1u;
    }
#endif
}
