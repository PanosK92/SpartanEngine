/*
Copyright(c) 2016-2024 Panos Karabelas

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

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex.GetDimensions(resolution_out.x, resolution_out.y);

    // Convert thread_id.xy to UV coordinates
    float2 uv = (float2(thread_id.xy) + 0.5f) / resolution_out;

    // Convert UVs from cubemap to direction
    float3 direction;
    float3 abs_dir;
    uint face_index = thread_id.z;  // cubemap face index

    switch (face_index)
    {
        case 0: direction = float3(1.0f, uv.y * 2.0f - 1.0f, uv.x * 2.0f - 1.0f); break;  // +X
        case 1: direction = float3(-1.0f, uv.y * 2.0f - 1.0f, -(uv.x * 2.0f - 1.0f)); break;  // -X
        case 2: direction = float3(uv.x * 2.0f - 1.0f, 1.0f, uv.y * 2.0f - 1.0f); break;  // +Y
        case 3: direction = float3(uv.x * 2.0f - 1.0f, -1.0f, -(uv.y * 2.0f - 1.0f)); break;  // -Y
        case 4: direction = float3(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, 1.0f); break;  // +Z
        case 5: direction = float3(-(uv.x * 2.0f - 1.0f), uv.y * 2.0f - 1.0f, -1.0f); break;  // -Z
    }

    // Normalize the direction
    direction = normalize(direction);

    // Convert the cubemap direction back to spherical coordinates
    float phi   = atan2(direction.z, direction.x);
    float theta = acos(direction.y);

    // Map spherical coordinates back to UV space of the input texture
    float2 spherical_uv;
    spherical_uv.x = (phi / (2.0f * PI)) + 0.5f;
    spherical_uv.y = 1.0f - (theta / PI);

    // Sample from the spherical map
    float4 spherical_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], spherical_uv, 0);

    // Store the sampled color in the cubemap
    tex_uav_sss[uint3(thread_id.xy, face_index)] = spherical_color;
}
