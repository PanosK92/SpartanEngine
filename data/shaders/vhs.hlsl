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

/*
copyright(c) 2015-2025 panos karabelas
permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "software"), to deal
in the software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the software, and to permit persons to whom the software is furnished
to do so, subject to the following conditions:

the above copyright notice and this permission notice shall be included in
all copies or substantial portions of the software.

the software is provided "as is", without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability, fitness
for a particular purpose and noninfringement. in no event shall the authors or
copyright holders be liable for any claim, damages or other liability, whether
in an action of contract, tort or otherwise, arising from, out of or in
connection with the software or the use or other dealings in the software.
*/

//= includes =========
#include "common.hlsl"
//====================

// static constants
static const float2 vhs_resolution           = float2(320.0, 240.0);
static const int samples                     = 2;
static const float crease_noise              = 1.0;
static const float crease_opacity            = 0.5;
static const float filter_intensity          = 0.0001;
static const float tape_crease_smear         = 0.1;
static const float tape_crease_intensity     = 0.2;
static const float tape_crease_jitter        = 0.10;
static const float tape_crease_speed         = 0.3;
static const float tape_crease_discoloration = 0.1;
static const float bottom_border_thickness   = 6.0;
static const float bottom_border_jitter      = 3.0;
static const float noise_intensity           = 0.04;
static const float tape_wave_speed           = 0.5;
static const float switching_noise_speed     = 0.5;
static const float image_jiggle_intensity    = 0.01;
static const float image_jiggle_speed        = 1.0;
static const float noise_scale               = 200.0;

// helper functions
float v2random(float2 uv)
{
    return noise_perlin(uv * noise_scale);
}

float2x2 rotate2D(float t)
{
    float c = cos(t), s = sin(t);
    return float2x2(c, s, -s, c);
}

float3 rgb2yiq(float3 rgb)
{
    float3x3 mat = float3x3(
        0.299, 0.587, 0.114, // Y
        0.596, -0.274, -0.322, // I
        0.211, -0.523, 0.312  // Q
    );
    return mul(rgb, mat);
}

float3 yiq2rgb(float3 yiq)
{
    float3x3 mat = float3x3(
        1.0, 0.956, 0.621,   // R
        1.0, -0.272, -0.647, // G
        1.0, -1.106, 1.703   // B
    );
    return mul(yiq, mat);
}

float3 vhx_tex_2D(float2 uv, float rot)
{
    float3 yiq = 0.0;
    float2 resolution;
    tex.GetDimensions(resolution.x, resolution.y);
    for (int i = 0; i < samples; i++)
    {
        float2 offset = float2(float(i), 0.0) / vhs_resolution;
        float2 texel = uv * resolution - offset * resolution;
        yiq += rgb2yiq(tex.Load(int3(int2(texel), 0)).xyz) *
               float3(float(samples - 1 - i), float(i), float(i)) / float(samples - 1) / float(samples) * 2.0;
    }
    if (rot != 0.0)
    {
        yiq.yz = mul(rotate2D(rot * tape_crease_discoloration), yiq.yz);
    }
    return yiq2rgb(yiq);
}

// compute shader entry point
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // compute uv and resolution
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 uvn = thread_id.xy / resolution_out;

    // initialize color
    float3 col = 0.0;

    float time = (float)buffer_frame.time;

    // image jiggle
    uvn += (v2random(float2(time * image_jiggle_speed, uvn.y)) - 0.5) * image_jiggle_intensity / vhs_resolution;

    // tape wave
    uvn.x += (v2random(float2(uvn.y / 10.0, time / 10.0 * tape_wave_speed)) - 0.5) / vhs_resolution.x;
    uvn.x += (v2random(float2(uvn.y, time * 10.0 * tape_wave_speed)) - 0.5) / vhs_resolution.x;

    // tape crease
    float tc_phase = smoothstep(0.9, 0.96, sin(uvn.y * 8.0 - (time * tape_crease_speed + tape_crease_jitter * v2random(time * float2(0.67, 0.59))) * 3.14159 * 1.2));
    float tc_noise = smoothstep(0.3, 1.0, v2random(float2(uvn.y * 4.77, time)));
    float tc = tc_phase * tc_noise;
    uvn.x -= tc / vhs_resolution.x * 8.0 * tape_crease_smear;

    // switching noise
    float sn_phase = smoothstep(1.0 - bottom_border_thickness / vhs_resolution.y, 1.0, uvn.y);
    uvn.x += sn_phase * (v2random(float2(uvn.y * 100.0, time * 10.0 * switching_noise_speed)) - 0.5) / vhs_resolution.x * bottom_border_jitter;

    // fetch color
    col = vhx_tex_2D(uvn, tc_phase * 0.2 + sn_phase * 2.0);

    // crease noise
    float cn = tc_noise * crease_noise * (0.7 * tc_phase * tape_crease_intensity + 0.3);
    if (cn > 0.29)
    {
        float2 V = float2(0.0, crease_opacity);
        float2 uvt = (uvn + V.yx * v2random(float2(uvn.y, time))) * float2(0.1, 1.0);
        float n0 = v2random(uvt);
        float n1 = v2random(uvt + V.yx / vhs_resolution.x);
        if (n1 < n0)
        {
            col = lerp(col, 2.0 * V.yyy, pow(n0, 10.0));
        }
    }

    // ac beat
    col *= 1.0 + 0.1 * smoothstep(0.4, 0.6, v2random(float2(0.0, 0.1 * (uvn.y + time * 0.2)) / 10.0));

    // color noise
    float noise = v2random(fmod(uvn * float2(1.0, 1.0) + time * float2(5.97, 4.45), 1.0));
    col *= 1.0 - noise_intensity * 0.5 + noise_intensity * noise;
    col = clamp(col, 0.0, 1.0);

    // yiq color adjustment
    col = rgb2yiq(col);
   col = float3(0.8, 1.1, 1.2) * col  + float3(0.1, -0.1, 0.2) * filter_intensity; // bluish tint
    col = yiq2rgb(col);

    // write to output
    tex_uav[thread_id.xy] = float4(col, 1.0);
}
