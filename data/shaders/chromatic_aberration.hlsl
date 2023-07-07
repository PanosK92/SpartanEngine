/*
Copyright(c) 2016-2023 Panos Karabelas

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

static const float g_chromatic_aberration_intensity = 100.0f;

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check
    if (any(int2(thread_id.xy) >= buffer_pass.resolution_rt.xy))
        return;

    const float2 uv    = (thread_id.xy + 0.5f) / buffer_pass.resolution_rt;
    float camera_error = 1.0f / buffer_frame.camera_aperture;
    float intensity    = camera_error * g_chromatic_aberration_intensity;
    float2 shift       = float2(intensity, -intensity);

    // Lens effect
    shift.x *= abs(uv.x * 2.0f - 1.0f);
    shift.y *= abs(uv.y * 2.0f - 1.0f);

    // Sample color
    float3 color = 0.0f; 
    color.r      = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + (get_rt_texel_size() * shift), 0).r;
    color.g      = tex[thread_id.xy].g;
    color.b      = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv - (get_rt_texel_size() * shift), 0).b;

    tex_uav[thread_id.xy] = float4(color, tex[thread_id.xy].a);
}
