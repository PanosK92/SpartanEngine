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

static const float chromatic_amount  = 0.003f; // strength of chromatic aberration
static const float distortion_amount = 0.1f;   // strength of lens distortion (positive for barrel, negative for pincushion)

// compute shader entry point
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // compute uv
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 uv = thread_id.xy / resolution_out;

    // apply lens distortion with zoom to avoid edge artifacts
    float2 center = float2(0.5, 0.5); // center of the texture
    float2 delta  = uv - center;
    float r       = length(delta);

    // calculate zoom to keep distorted uvs within [0,1]
    float max_r          = 0.707; // max distance from center (corner of unit square)
    float max_distortion = 1.0 + distortion_amount * (max_r * max_r); // max distortion at edge
    float zoom           = 1.0 / max_distortion; // scale uvs to counteract max distortion

    // apply distortion with zoom
    float distortion_factor = 1.0 + distortion_amount * (r * r); // quadratic distortion
    float2 distorted_uv     = center + delta * zoom * distortion_factor;

    // chromatic aberration: sample each color channel with a slight offset
    float2 offset = delta * chromatic_amount; // offset proportional to distance from center
    float r_sample = tex.SampleLevel(samplers[sampler_bilinear_clamp], distorted_uv + offset, 0).r;
    float g_sample = tex.SampleLevel(samplers[sampler_bilinear_clamp], distorted_uv, 0).g; // green uses no offset
    float b_sample = tex.SampleLevel(samplers[sampler_bilinear_clamp], distorted_uv - offset, 0).b;

    // combine the samples and write to output
    tex_uav[thread_id.xy] = float4(r_sample, g_sample, b_sample, 1.0);
}
