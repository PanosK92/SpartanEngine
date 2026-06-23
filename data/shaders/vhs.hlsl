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

static const float3x3 rgb_to_yiq_matrix = float3x3(0.299f, 0.587f, 0.114f, 0.596f, -0.274f, -0.322f, 0.211f, -0.523f, 0.312f);
static const float3x3 yiq_to_rgb_matrix = float3x3(1.0f, 0.956f, 0.621f, 1.0f, -0.272f, -0.647f, 1.0f, -1.107f, 1.704f);

float3 rgb_to_yiq(float3 rgb)
{
    return mul(rgb_to_yiq_matrix, rgb);
}

float3 yiq_to_rgb(float3 yiq)
{
    return mul(yiq_to_rgb_matrix, yiq);
}

float3 sample_rgb(float2 uv)
{
    return tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
}

float get_band(float y, float center, float width)
{
    return saturate(1.0f - abs(y - center) / width);
}

float get_tape_offset(float2 uv, float texel_x, float time)
{
    float y = uv.y;
    float weave = sin(y * 17.0f + time * 0.85f) * 1.8f + sin(y * 73.0f - time * 1.70f) * 0.8f;
    float jitter = noise_perlin(float2(y * 42.0f, time * 2.7f)) * 2.2f;
    float event_time = floor(time * 1.6f);
    float tear_center = frac(hash(event_time) * 1.618f);
    float tear = get_band(y, tear_center, 0.028f) * step(0.74f, hash(event_time + 31.0f));
    float tear_noise = noise_perlin(float2(y * 220.0f, time * 8.0f)) * tear * 5.0f;
    return (weave + jitter + tear_noise) * texel_x;
}

float sample_luma(float2 uv)
{
    return rgb_to_yiq(sample_rgb(uv)).x;
}

float sample_soft_luma(float2 uv, float2 texel)
{
    float luma = sample_luma(uv) * 0.38f;
    luma += sample_luma(uv + float2(-texel.x * 1.0f, 0.0f)) * 0.20f;
    luma += sample_luma(uv + float2( texel.x * 1.0f, 0.0f)) * 0.20f;
    luma += sample_luma(uv + float2(-texel.x * 3.0f, 0.0f)) * 0.11f;
    luma += sample_luma(uv + float2( texel.x * 3.0f, 0.0f)) * 0.11f;
    return luma;
}

float2 sample_vhs_chroma(float2 uv, float2 texel)
{
    float spread = 30.0f;
    float2 chroma = float2(0.0f, 0.0f);
    float weight_sum = 0.0f;

    [unroll]
    for (int i = 0; i < 11; i++)
    {
        float t = float(i) * 0.1f;
        float offset = lerp(-0.25f, 1.0f, t) * spread;
        float falloff = offset < 0.0f ? 0.18f : 0.55f;
        float weight = exp(-0.5f * offset * offset / max(spread * spread * falloff * falloff, 0.001f));
        chroma += rgb_to_yiq(sample_rgb(uv + float2(offset * texel.x, 0.0f))).yz * weight;
        weight_sum += weight;
    }

    return chroma / weight_sum;
}

float3 apply_composite_signal(float2 uv, float2 texel)
{
    float luma = sample_soft_luma(uv, texel);
    float2 chroma = sample_vhs_chroma(uv + float2(texel.x * 5.5f, 0.0f), texel);
    float y_left = sample_luma(uv + float2(-texel.x * 3.0f, 0.0f));
    float y_right = sample_luma(uv + float2(texel.x * 3.0f, 0.0f));
    float ringing = (luma * 2.0f - y_left - y_right) * 0.18f;
    return yiq_to_rgb(float3(luma + ringing, chroma.x, chroma.y));
}

float3 apply_rgb_drift(float3 color, float2 uv, float2 texel, float time)
{
    float drift = (sin(uv.y * 61.0f + time * 2.0f) * 0.5f + noise_perlin(float2(uv.y * 90.0f, time * 1.5f)) * 0.5f) * texel.x;
    float red = sample_rgb(uv + float2(4.5f * texel.x + drift * 2.0f, 0.0f)).r;
    float blue = sample_rgb(uv - float2(3.5f * texel.x - drift * 1.5f, 0.0f)).b;
    return lerp(color, float3(red, color.g, blue), 0.42f);
}

float get_scanline(uint2 pixel, float2 resolution)
{
    float ntsc_line_height = max(resolution.y / 480.0f, 1.0f);
    float phase = float(buffer_frame.frame & 1u) * 0.5f;
    float scanline_phase = frac(float(pixel.y) / ntsc_line_height + phase);
    float mask = smoothstep(0.12f, 0.38f, scanline_phase) * smoothstep(0.96f, 0.56f, scanline_phase);
    return lerp(1.02f, 0.86f, mask);
}

float3 apply_tape_damage(float3 color, float2 uv, uint2 pixel, float2 resolution, float time)
{
    float scanline_id = floor(uv.y * 486.0f);
    float grain = hash(float2(float(pixel.x), float(pixel.y)) + float2(float(buffer_frame.frame) * 17.0f, time * 29.0f)) - 0.5f;
    float snow = step(0.995f, hash(float2(float(pixel.x) * 3.7f + time * 71.0f, float(pixel.y) * 2.3f + float(buffer_frame.frame))));
    float scanline_noise = noise_perlin(float2(scanline_id * 0.45f, time * 0.75f)) * 0.018f;
    color += grain * 0.075f + snow * 0.35f + scanline_noise;

    float head_switch = smoothstep(0.945f, 0.992f, uv.y);
    float head_wave = sin(uv.y * 820.0f + time * 34.0f) * 0.5f + 0.5f;
    float head_noise = hash(float2(uv.x * resolution.x * 0.25f, time * 45.0f)) * 0.50f + head_wave * 0.18f;
    color = lerp(color, float3(head_noise, head_noise, head_noise), head_switch * 0.46f);

    return color;
}

float3 apply_tape_grade(float3 color, float2 uv)
{
    float luma = luminance(color);
    float3 warm_gray = float3(luma * 1.05f, luma * 1.00f, luma * 0.91f);
    color = lerp(color, warm_gray, 0.28f);
    color = color * 0.84f + 0.045f;

    float2 centered = uv * 2.0f - 1.0f;
    float vignette = saturate(1.0f - dot(centered, centered) * 0.24f);
    return color * vignette;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    float2 texel = 1.0f / resolution;
    float2 uv = (thread_id.xy + 0.5f) * texel;
    float time = (float)buffer_frame.time;

    float2 tape_uv = uv;
    tape_uv.x += get_tape_offset(uv, texel.x, time);
    tape_uv.x += smoothstep(0.94f, 0.992f, uv.y) * sin(uv.y * 720.0f + time * 31.0f) * texel.x * 30.0f;

    float3 color = apply_composite_signal(tape_uv, texel);
    color = apply_rgb_drift(color, tape_uv, texel, time);
    color *= get_scanline(thread_id.xy, resolution);
    color *= 1.0f + noise_perlin(float(thread_id.y) * 0.45f + time * 0.35f) * 0.045f;
    color = apply_tape_damage(color, uv, thread_id.xy, resolution, time);
    color = apply_tape_grade(color, uv);

    tex_uav[thread_id.xy] = float4(saturate(color), 1.0f);
}
