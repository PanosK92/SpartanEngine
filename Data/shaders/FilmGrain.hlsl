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

static const float g_film_grain_intensity = 0.5f;

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float4 color = tex.Sample(sampler_point_clamp, input.uv);

    // Film grain
    float x = (input.uv.x + 4.0 ) * (input.uv.y + 4.0 ) * (g_time * 10.0);
	float4 grain = (fmod((fmod(x, 13.0) + 1.0) * (fmod(x, 123.0) + 1.0), 0.01)-0.005) * g_film_grain_intensity;

    // Iso noise - It's different from film grain but the assumption is that if the user
    // is running this shader, he is going for a more cinematic look, so he might want iso noise as well.
    float iso_noise = random(frac(input.uv.x * input.uv.y * g_time));
    iso_noise *= g_camera_iso * 0.00001f;
	
    return saturate(color + iso_noise);
}
