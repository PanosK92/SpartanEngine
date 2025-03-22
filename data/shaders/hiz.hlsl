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

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    // get the aabb index and total count
    uint aabb_index = dispatch_thread_id.x;
    uint aabb_count = pass_get_f2_value().x;
    if (aabb_index >= aabb_count)
        return;

    // retrieve the current aabb
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

    // compute the uv coordinates of the corners, the min z, the min and the max
    float4 corners_uv[8];
    float closest_box_z = 0;
    float2 min_uv       = 1;
    float2 max_uv       = 0;
    for (int i = 0; i < 8; ++i)
    {
        corners_uv[i]      = mul(float4(corners_world[i], 1.0), buffer_frame.view_projection);
        corners_uv[i].xyz /= corners_uv[i].w;
        corners_uv[i].z    = saturate(corners_uv[i].z);      // handle corners behind the camera
        corners_uv[i].xy   = clamp(corners_uv[i].xy, -1, 1); // handle corners outside of the screen
        corners_uv[i].xy   = ndc_to_uv(corners_uv[i].xy);
        
        min_uv        = min(corners_uv[i].xy, min_uv);
        max_uv        = max(corners_uv[i].xy, max_uv);
        closest_box_z = max(closest_box_z, corners_uv[i].z); // revese z
    }

    float2 hiz_size;
    tex.GetDimensions(hiz_size.x, hiz_size.y); // get depth buffer dimensions at mip 0
    float4 box_uvs = float4(min_uv, max_uv);   // define the screen-space bounding box

    // calculate initial mip level based on screen-space size
    int2 size           = (max_uv - min_uv) * hiz_size;
    float mip           = ceil(log2(max(size.x, size.y)));
    float max_mip_level = floor(log2(max(hiz_size.x, hiz_size.y)));
    mip                 = clamp(mip, 0, max_mip_level);

    // texel footprint for the lower (finer-grained) level
    float level_lower = max(mip - 1, 0);
    float2 scale      = exp2(-level_lower);
    float2 a          = floor(box_uvs.xy * scale * hiz_size);
    float2 b          = ceil(box_uvs.zw * scale * hiz_size);
    float2 dims       = b - a;

    // use the lower level if we touch <= 2 texels in both dimensions
    if (dims.x <= 2 && dims.y <= 2)
        mip = level_lower;

    float4 depth = float4(
        tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), box_uvs.xy, mip).r, // bottom-left
        tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), box_uvs.zy, mip).r, // bottom-right
        tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), box_uvs.xw, mip).r, // top-left
        tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), box_uvs.zw, mip).r  // top-right
    );

    // find the maximum depth from the four samples - reverse z
    float furthest_z = min(min(min(depth.x, depth.y), depth.z), depth.w);

    // visibility test - reverse z
    bool is_visible = closest_box_z > furthest_z;

    // write visibility flag
    visibility[aabb_index] = is_visible ? 1 : 0;
}
