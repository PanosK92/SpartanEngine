/*
Copyright(c) 2015-2025 Panos Karabelas

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

[numthreads(1, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // compute luminance (16x16 mip)
    float lum_sum = 0.0;
    for (uint y = 0; y < 16; y++)
    {
        for (uint x = 0; x < 16; x++)
        {
            float3 col  = tex.Load(int3(x, y, 0)).rgb;
            lum_sum    += dot(col, float3(0.2126, 0.7152, 0.0722));
        }
    }
    float lum = lum_sum / 256.0;

    // read previous exposure
    float prev = tex2.Load(int3(0, 0, 0)).r;

    // target luminance (middle gray)
    const float target_luminance = 0.5;

    // compute desired exposure
    float desired_exposure = target_luminance / max(lum, 0.0001);

    // min/max exposure in EV
    const float min_ev = -6.0; // dark
    const float max_ev = 2.0;  // bright

    // convert to linear
    float min_exposure = exp2(min_ev);
    float max_exposure = exp2(max_ev);

    // clamp exposure
    desired_exposure = clamp(desired_exposure, min_exposure, max_exposure);

    // exponential adaptation
    float adaptation_speed = pass_get_f3_value().x;
    float tau              = 1.0 / max(adaptation_speed, 0.001);
    float exposure         = prev + (desired_exposure - prev) * (1.0 - exp(-tau * buffer_frame.delta_time));

    // write output
    tex_uav[uint2(0, 0)] = float4(exposure, exposure, exposure, 1.0);
}
