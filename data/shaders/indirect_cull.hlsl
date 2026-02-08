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

//= INCLUDES =========
#include "common.hlsl"
//====================

// gpu-driven frustum and occlusion culling with draw compaction
// reads per-draw data and aabbs, tests visibility, and compacts
// surviving draws into contiguous indirect argument and draw data buffers

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint draw_index = dispatch_thread_id.x;
    uint draw_count = (uint)pass_get_f4_value().x;
    if (draw_index >= draw_count)
        return;

    // read the aabb for this draw
    uint aabb_index  = indirect_draw_data[draw_index].aabb_index;
    aabb current_aabb = aabbs[aabb_index];

    // compute the world space corners of the aabb
    float3 corners_world[8] =
    {
        current_aabb.min,
        float3(current_aabb.min.x, current_aabb.min.y, current_aabb.max.z),
        float3(current_aabb.min.x, current_aabb.max.y, current_aabb.min.z),
        float3(current_aabb.min.x, current_aabb.max.y, current_aabb.max.z),
        float3(current_aabb.max.x, current_aabb.min.y, current_aabb.min.z),
        float3(current_aabb.max.x, current_aabb.min.y, current_aabb.max.z),
        float3(current_aabb.max.x, current_aabb.max.y, current_aabb.min.z),
        current_aabb.max
    };

    // project all corners to clip space
    float2 min_ndc      = float2(1e30f, 1e30f);
    float2 max_ndc      = float2(-1e30f, -1e30f);
    float closest_box_z = -1e30f;
    bool any_behind     = false;
    bool any_front      = false;
    for (int i = 0; i < 8; ++i)
    {
        float4 clip = mul(float4(corners_world[i], 1.0), buffer_frame.view_projection);
        if (clip.w <= 0.0f)
        {
            any_behind = true;
        }
        else
        {
            float3 ndc    = clip.xyz / clip.w;
            min_ndc       = min(min_ndc, ndc.xy);
            max_ndc       = max(max_ndc, ndc.xy);
            closest_box_z = max(closest_box_z, ndc.z);
            any_front     = true;
        }
    }

    // entirely behind the camera
    if (!any_front)
        return;

    // partially behind - conservatively pass it through
    bool is_visible = true;
    if (!any_behind)
    {
        // frustum cull: check if projected bbox overlaps ndc [-1,1]
        if (max_ndc.x < -1.0f || min_ndc.x > 1.0f || max_ndc.y < -1.0f || min_ndc.y > 1.0f)
        {
            is_visible = false;
        }

        // hi-z occlusion test
        if (is_visible)
        {
            float2 min_uv = saturate(ndc_to_uv(min_ndc));
            float2 max_uv = saturate(ndc_to_uv(max_ndc));

            float2 render_size;
            tex.GetDimensions(render_size.x, render_size.y);
            float4 box_uvs = float4(min_uv, max_uv);

            // select mip level based on screen-space footprint
            int2   size          = (max_uv - min_uv) * render_size;
            float  mip           = ceil(log2(max(size.x, size.y)));
            float  max_mip_level = pass_get_f4_value().y;
            mip                  = clamp(mip, 0, max_mip_level);

            // texel footprint for the finer level
            float  level_lower = max(mip - 1, 0);
            float2 scale_mip   = exp2(-level_lower);
            float2 a           = floor(box_uvs.xy * scale_mip * render_size);
            float2 b           = ceil(box_uvs.zw * scale_mip * render_size);
            float2 dims        = b - a;

            // use finer level if footprint is small enough
            if (dims.x <= 2 && dims.y <= 2)
                mip = level_lower;

            // 5-tap hi-z depth sampling
            float4 scaled_uvs = box_uvs * buffer_frame.resolution_scale;
            float2 center_uv  = (scaled_uvs.xy + scaled_uvs.zw) * 0.5f;
            float4 depth = float4(
                tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.xy, mip).r,
                tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.zy, mip).r,
                tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.xw, mip).r,
                tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.zw, mip).r
            );
            float depth_center = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), center_uv, mip).r;

            // find farthest depth (reverse z)
            float furthest_z = min(min(min(min(depth.x, depth.y), depth.z), depth.w), depth_center);

            // visibility test (reverse z: closer objects have larger z)
            is_visible = closest_box_z > furthest_z;
        }
    }

    if (is_visible)
    {
        // atomically allocate a slot in the output buffers
        uint output_index;
        InterlockedAdd(indirect_draw_count[0], 1, output_index);

        // compact the draw arguments and per-draw data
        indirect_draw_args_out[output_index] = indirect_draw_args[draw_index];
        indirect_draw_data_out[output_index] = indirect_draw_data[draw_index];
    }
}
