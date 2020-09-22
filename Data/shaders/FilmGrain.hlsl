/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Common.hlsl"
//====================

static const float g_film_grain_intensity   = 0.002f;
static const float g_film_grain_speed       = 3.0f;
static const float g_film_grain_mean        = 0.0f; // What gray level noise should tend to.
static const float g_film_grain_variance    = 0.5f; // Controls the contrast/variance of noise.

float gaussian(float z, float u, float o) {
    return (1.0 / (o * sqrt(2.0 * 3.1415))) * exp(-(((z - u) * (z - u)) / (2.0 * (o * o))));
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;
    
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;
    float4 color    = tex[thread_id.xy];

    // Film grain
	float t             = g_time * float(g_film_grain_speed);
    float seed          = dot(uv, float2(12.9898, 78.233));
    float noise         = frac(sin(seed) * 43758.5453 + t);
    noise               = gaussian(noise, float(g_film_grain_mean), float(g_film_grain_variance) * float(g_film_grain_variance));
    float3 film_grain   =  noise * (1.0f - color.rgb) * g_film_grain_intensity;

    // Iso noise
    noise               = random(frac(uv.x * uv.y * g_time));
    noise               *= g_camera_iso * 0.00001f;
    float3 iso_noise    = noise * (1.0f - color.rgb);
    
    // Additive blending
    color.rgb += (film_grain + iso_noise) * 0.5f;
	
    tex_out_rgba[thread_id.xy] = saturate(color);
}
