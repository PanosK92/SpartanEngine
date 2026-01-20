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

// vulkan fragment shading rate encoding:
// bits 0-1: log2(width)  - 0=1x, 1=2x, 2=4x
// bits 2-3: log2(height) - 0=1x, 1=2x, 2=4x
// common values: 0=1x1, 5=2x2, 10=4x4
static const uint VRS_1X1 = 0;  // full rate
static const uint VRS_2X2 = 5;  // quarter rate
static const uint VRS_4X4 = 10; // sixteenth rate

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution_out;
    tex_uav_uint.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(thread_id.xy >= resolution_out))
        return;

    // sample from previous frame output to determine shading rate
    float2 uv        = (thread_id.xy + 0.5f) / float2(resolution_out);
    float3 color     = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp_border), uv, 0.0f).rgb;
    float luminance_ = luminance(color);

    // determine shading rate based on luminance
    // bright areas get full rate, dark areas can use reduced rate
    uint shading_rate = VRS_1X1;
    if (luminance_ < 0.01f)
    {
        shading_rate = VRS_4X4; // very dark: use lowest rate
    }
    else if (luminance_ < 0.1f)
    {
        shading_rate = VRS_2X2; // dark: use quarter rate
    }

    tex_uav_uint[thread_id.xy] = shading_rate;
}
