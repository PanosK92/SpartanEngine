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

//= includes =========
#include "common.hlsl"
//====================

[numthreads(1, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // 1. compute average luminance (in watts for now)
    float avg_watts = 0.0f;
    {
        uint w, h, mip_count;
        tex.GetDimensions(0, w, h, mip_count);

        // we use the lowest mip (1x1 or 2x2) for performance
        uint mip = max(0, mip_count - 1); // safety check
        tex.GetDimensions(mip, w, h, mip_count);

        float sum_watts = 0.0f;
        for (uint y = 0; y < h; y++)
        {
            for (uint x = 0; x < w; x++)
            {
                float3 col = tex.Load(int3(x, y, mip)).rgb;
                
                // get luminance (watts)
                float lum = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
                
                // [sun protection]
                // if we look at the sun, the average skyrockets, causing the rest of 
                // the world to become pitch black. we clamp the max sample value 
                // to prevent the sun from dominating the average.
                // 100.0f watts ~ 68,000 nits (very bright highlight, but not the sun)
                lum = min(lum, 100.0f); 

                sum_watts += lum;
            }
        }
        avg_watts = sum_watts / float(w * h);
    }

    // 2. physics conversion (watts -> nits)
    const float luminous_efficacy = 683.0f;
    float avg_nits = avg_watts * luminous_efficacy;

    // 3. apply current camera exposure
    // we need to know how bright the image is *currently* with the c++ camera settings.
    float current_brightness = avg_nits * buffer_frame.camera_exposure;

    // 4. target luminance (middle gray)
    // we want the average brightness to hit middle gray.
    // in gt7 setup, white = 250 nits.
    // middle gray is usually 18% of reference white.
    // 250 * 0.18 = 45 nits.
    const float target_nits = 45.0f;

    // 5. compute exposure compensation factor
    float desired_exposure = target_nits / max(current_brightness, 0.001f);

    // 6. clamp exposure (ev range)
    // we allow the ae to adjust the camera by +/- 4 stops (ev).
    // your previous range (-0.2 to 1.5) was too small to fix anything.
    const float min_ev = -4.0f; 
    const float max_ev =  4.0f;

    float min_exposure = exp2(min_ev);
    float max_exposure = exp2(max_ev);

    desired_exposure = clamp(desired_exposure, min_exposure, max_exposure);

    // 7. temporal adaptation (smooth transition)
    float prev_exposure = tex2.Load(int3(0, 0, 0)).r;
    float adaptation_speed = pass_get_f3_value().x;
    
    // safety for first frame
    if (isnan(prev_exposure) || prev_exposure <= 0.0f) prev_exposure = 1.0f;

    float tau = 1.0f / max(adaptation_speed, 0.001f);
    float exposure = prev_exposure + (desired_exposure - prev_exposure) * (1.0f - exp(-tau * buffer_frame.delta_time));

    // write output
    tex_uav[uint2(0, 0)] = float4(exposure, exposure, exposure, 1.0f);
}
