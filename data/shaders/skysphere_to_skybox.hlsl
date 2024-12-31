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

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex.GetDimensions(resolution_out.x, resolution_out.y);
    float2 uv = (float2(thread_id.xy) + 0.5f) / resolution_out;
    
    // compute direction
    float3 direction;
    uint face_index = thread_id.z;
    switch (face_index)
    {
        case 0: direction = float3(1.0f, -(uv.y * 2.0f - 1.0f), -(uv.x * 2.0f - 1.0f)); break;  // +X
        case 1: direction = float3(-1.0f, -(uv.y * 2.0f - 1.0f), uv.x * 2.0f - 1.0f); break;    // -X
        case 2: direction = float3(uv.x * 2.0f - 1.0f, 1.0f, uv.y * 2.0f - 1.0f); break;        // +Y
        case 3: direction = float3(uv.x * 2.0f - 1.0f, -1.0f, -(uv.y * 2.0f - 1.0f)); break;    // -Y
        case 4: direction = float3(uv.x * 2.0f - 1.0f, -(uv.y * 2.0f - 1.0f), 1.0f); break;     // +Z
        case 5: direction = float3(-(uv.x * 2.0f - 1.0f), -(uv.y * 2.0f - 1.0f), -1.0f); break; // -Z
    }
    direction = normalize(direction);

    // compute sphereical uv
    float2 spherical_uv;
    float phi      = atan2(direction.z, direction.x);
    float theta    = acos(direction.y);
    spherical_uv.x = frac((phi / (2.0f * PI)) + 0.5f);
    spherical_uv.y = 1.0f - (theta / PI);
    
    // write
    float4 sampled_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], spherical_uv, 0);
    tex_uav_sss[uint3(thread_id.xy, face_index)] = float4(sampled_color.rgb, 1.0f);
}
