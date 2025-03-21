/*
Copyright(c) 2016-2025 Panos Karabelas

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

[numthreads(64, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint aabb_index = dispatch_thread_id.x;
    uint aabb_count = pass_get_f2_value().x;
    if (aabb_index >= aabb_count)
        return;

    aabb current_aabb = aabbs[aabb_index];

    // transform world-space aabb to clip space
    float4 min_clip = mul(float4(current_aabb.min, 1.0), buffer_frame.view_projection);
    float4 max_clip = mul(float4(current_aabb.max, 1.0), buffer_frame.view_projection);
    float2 min_ndc  = min_clip.xy / min_clip.w;
    float2 max_ndc  = max_clip.xy / max_clip.w;

    // convert ndc (-1 to 1) to uv (0 to 1)
    float2 min_uv = float2(0.5 * min_ndc.x + 0.5, 0.5 * min_ndc.y + 0.5);
    float2 max_uv = float2(0.5 * max_ndc.x + 0.5, 0.5 * max_ndc.y + 0.5);

    // clamp to valid uv range
    min_uv = saturate(min_uv);
    max_uv = saturate(max_uv);

    // calculate screen-space size of the aabb
    float2 size_uv = abs(max_uv - min_uv);
    float max_size = max(size_uv.x, size_uv.y);

    // determine mip level
    float2 hiz_size;
    tex.GetDimensions(hiz_size.x, hiz_size.y);
    float mip_count = log2(max(hiz_size.x, hiz_size.y)) + 1;
    float mip_level = clamp(log2(max_size * hiz_size.x), 0, mip_count - 1);

    // sample hi-z buffer at the center (reverse z: 1.0 = near, 0.0 = far)
    float2 center_uv = (min_uv + max_uv) * 0.5;
    float hiz_depth  = tex.Load(int3(center_uv.x, center_uv.y, mip_count)).r;

    // compute ndc depth (z/w) for min and max points
    float aabb_min_depth = min_clip.z / min_clip.w; // closest point in ndc
    float aabb_max_depth = max_clip.z / max_clip.w; // farthest point in ndc

    // with reverse z, larger depth = closer. find the farthest point (smallest depth)
    float aabb_farthest_depth = min(aabb_min_depth, aabb_max_depth);

    // visibility test: aabb is visible if its farthest depth is closer than hi-z depth
    // (i.e., aabb_farthest_depth > hiz_depth)
    bool is_visible        = (aabb_farthest_depth > hiz_depth);
    visibility[aabb_index] = is_visible ? 1 : 0;
}
