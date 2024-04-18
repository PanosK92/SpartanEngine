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

// empirically chosen to match how much the lens effect is visible in real world images
static const float g_chromatic_aberration_intensity = 5.0f;

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    const float2 uv       = (thread_id.xy + 0.5f) / resolution_out;
    float camera_aperture = pass_get_f3_value().x;
    float camera_error    = sqrt(1.0f / camera_aperture);
    float intensity       = camera_error * g_chromatic_aberration_intensity;
    float2 shift          = float2(intensity, -intensity);

    // lens effect
    shift.x *= abs(uv.x * 2.0f - 1.0f);
    shift.y *= abs(uv.y * 2.0f - 1.0f);

    // sample color
    float3 color      = 0.0f;
    float2 texel_size = 1.0f / resolution_out;
    color.r           = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + (texel_size * shift), 0).r;
    color.g           = tex[thread_id.xy].g;
    color.b           = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv - (texel_size * shift), 0).b;

    tex_uav[thread_id.xy] = float4(color, tex[thread_id.xy].a);
}
