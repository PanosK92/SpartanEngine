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

    if (current_aabb.alpha_tested == 1 || current_aabb.transparent == 1)
    {
        visibility[aabb_index] = 1;
        return;
    }

    // define all eight corners of the aabb in world space
    float3 corners[8] =
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

    // transform all corners to clip space using the view-projection matrix
    float4 clip_corners[8];
    bool behind_camera = false;
    for (int i = 0; i < 8; ++i)
    {
        clip_corners[i] = mul(float4(corners[i], 1.0), buffer_frame.view_projection);
        if (clip_corners[i].w < 0.0)  // check if any corner is behind the camera
            behind_camera = true;
    }

    // compute ndc coordinates and track min/max values
    float2 ndc_min       = float2(1e9, 1e9); 
    float2 ndc_max       = float2(-1e9, -1e9);
    float aabb_min_depth = 1e9;
    float aabb_max_depth = -1e9;

    for (int i = 0; i < 8; ++i)
    {
        float4 clip = clip_corners[i];
        float w = max(clip.w, 0.0001);               // avoid division by zero or negative w
        float3 ndc = clip.xyz / w;                   // convert to ndc by dividing by w
        ndc_min = min(ndc_min, ndc.xy);              // update min xy in ndc
        ndc_max = max(ndc_max, ndc.xy);              // update max xy in ndc
        aabb_min_depth = min(aabb_min_depth, ndc.z); // update min depth (farthest)
        aabb_max_depth = max(aabb_max_depth, ndc.z); // update max depth (closest)
    }

    // clamp ndc coordinates to [-1, 1] to handle objects outside the frustum
    ndc_min = clamp(ndc_min, -1.0, 1.0);
    ndc_max = clamp(ndc_max, -1.0, 1.0);

    // convert ndc xy to uv coordinates (0 to 1 range)
    float2 min_uv = float2(0.5 * ndc_min.x + 0.5, 0.5 * ndc_min.y + 0.5);
    float2 max_uv = float2(0.5 * ndc_max.x + 0.5, 0.5 * ndc_max.y + 0.5);

    // calculate the screen-space size of the aabb in uv space and convert to pixels
    float2 size_uv = abs(max_uv - min_uv);
    float2 hiz_size;
    tex.GetDimensions(hiz_size.x, hiz_size.y);
    float2 size_pixels    = size_uv * hiz_size;
    float max_size_pixels = max(size_pixels.x, size_pixels.y);

    // calculate mip level with conservative adjustment
    float mip_count = log2(max(hiz_size.x, hiz_size.y)) + 1;  // total mip levels
    float mip_level = clamp(log2(max_size_pixels) + 1.0, 0, mip_count - 1);  // conservative mip level

    // calculate the center uv and convert to texel coordinates for the selected mip level
    float2 center_uv = (min_uv + max_uv) * 0.5;
    float2 mip_size  = hiz_size / pow(2.0, mip_level);  // size of the texture at the selected mip level
    int2 texel       = int2(center_uv * mip_size);  // texel coordinates at the mip level

    // sample the hi-z buffer at the correct texel and mip level
    float hiz_depth = tex.Load(int3(texel, mip_level)).r;

    // perform visibility test: visible if behind camera or closest depth is closer than hi-z depth
    bool is_visible = behind_camera || (aabb_max_depth > hiz_depth);  // reverse z: larger ndc z is closer

    // write visibility flag to the buffer
    visibility[aabb_index] = is_visible ? 1 : 0;
}
