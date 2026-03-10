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

// ntsc yiq color space - this is how composite video actually encodes color,
// and the limited bandwidth of the chroma channels is what gives vhs its look
static const float3x3 rgb_to_yiq_matrix = float3x3(
    0.299f,  0.587f,  0.114f,
    0.596f, -0.274f, -0.322f,
    0.211f, -0.523f,  0.312f
);

static const float3x3 yiq_to_rgb_matrix = float3x3(
    1.0f,  0.956f,  0.621f,
    1.0f, -0.272f, -0.647f,
    1.0f, -1.107f,  1.704f
);

float3 rgb_to_yiq(float3 rgb) { return mul(rgb_to_yiq_matrix, rgb); }
float3 yiq_to_rgb(float3 yiq) { return mul(yiq_to_rgb_matrix, yiq); }

// tape wobble - the slow, wavy horizontal distortion you see on worn tapes
float get_tape_wobble(float y, float time)
{
    float wobble  = sin(y * 1.2f + time * 0.5f) * 0.4f;
    wobble       += sin(y * 4.7f + time * 1.3f) * 0.2f;
    wobble       += noise_perlin(y * 8.0f + time * 0.8f) * 0.3f;
    return wobble;
}

// per-scanline tracking noise - the jittery horizontal displacement
float get_tracking_noise(float y, float time)
{
    float noise  = noise_perlin(y * 40.0f + time * 2.5f) * 0.5f;
    noise       += noise_perlin(y * 120.0f + time * 6.0f) * 0.3f;

    // sporadic tracking glitches - bigger displacement that comes and goes
    float glitch_time = floor(time * 1.5f);
    float glitch_prob = hash(glitch_time);
    float glitch_band = smoothstep(0.0f, 0.05f, abs(y - frac(glitch_time * 0.37f))) *
                        smoothstep(0.15f, 0.0f, abs(y - frac(glitch_time * 0.37f)));
    noise += step(0.85f, glitch_prob) * glitch_band * noise_perlin(y * 200.0f + time * 20.0f) * 6.0f;

    return noise;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    float2 uv    = (thread_id.xy + 0.5f) / resolution;
    float2 texel = 1.0f / resolution;
    float  time  = (float)buffer_frame.time;

    // =========================================================================
    // tape transport distortion
    // =========================================================================

    // slow tape wobble + fast tracking noise, both horizontal only
    float wobble   = get_tape_wobble(uv.y * 6.0f, time) * 0.003f;
    float tracking = get_tracking_noise(uv.y, time) * 0.002f;
    float2 uv_displaced = float2(uv.x + wobble + tracking, uv.y);

    // head switching - the bottom strip of the frame where the drum head switches,
    // causing a visible band of distorted, noisy garbage
    float head_switch = smoothstep(1.0f - 0.04f, 1.0f - 0.005f, uv.y);
    uv_displaced.x += head_switch * sin(uv.y * 600.0f + time * 30.0f) * 0.02f;

    // =========================================================================
    // luma channel - sample with slight horizontal softening
    // vhs resolves about 240 lines of horizontal luma, so it's noticeably soft
    // =========================================================================

    float3 center_rgb = float3(0.0f, 0.0f, 0.0f);
    float luma_weights = 0.0f;
    for (int l = -2; l <= 2; l++)
    {
        float2 tap_uv = uv_displaced + float2(float(l) * texel.x * 0.8f, 0.0f);
        float w = exp(-0.5f * float(l * l) / 1.5f);
        center_rgb += tex.SampleLevel(samplers[sampler_bilinear_clamp], tap_uv, 0).rgb * w;
        luma_weights += w;
    }
    center_rgb /= luma_weights;

    float3 center_yiq = rgb_to_yiq(center_rgb);

    // =========================================================================
    // chroma smearing - the defining vhs artifact
    // the color (I/Q) channels have roughly 1/6th the bandwidth of luma,
    // so color bleeds heavily to the right (asymmetric, like real vhs)
    // =========================================================================

    static const int   chroma_taps   = 15;
    static const float chroma_spread = 24.0f; // pixels - this is the real deal

    float2 chroma_sum   = float2(0.0f, 0.0f);
    float  chroma_total = 0.0f;

    for (int c = 0; c < chroma_taps; c++)
    {
        // asymmetric kernel: biased to the right (trailing smear)
        float t      = float(c) / float(chroma_taps - 1);
        float offset = lerp(-chroma_spread * 0.3f, chroma_spread * 0.7f, t);

        float2 tap_uv  = uv_displaced + float2(offset * texel.x, 0.0f);
        float3 tap_yiq = rgb_to_yiq(tex.SampleLevel(samplers[sampler_bilinear_clamp], tap_uv, 0).rgb);

        // exponential falloff from center, heavier on the left side
        float sigma = (offset < 0.0f) ? chroma_spread * 0.15f : chroma_spread * 0.35f;
        float w = exp(-0.5f * (offset * offset) / (sigma * sigma + 0.001f));

        chroma_sum   += tap_yiq.yz * w;
        chroma_total += w;
    }

    chroma_sum /= chroma_total;

    float3 color = yiq_to_rgb(float3(center_yiq.x, chroma_sum));

    // =========================================================================
    // composite artifact ringing - the luma overshoot/undershoot halos you see
    // around sharp horizontal edges on ntsc composite signals
    // =========================================================================

    float y_left  = rgb_to_yiq(tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_displaced + float2(-texel.x * 3.0f, 0.0f), 0).rgb).x;
    float y_right = rgb_to_yiq(tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_displaced + float2( texel.x * 3.0f, 0.0f), 0).rgb).x;
    float ringing = (center_yiq.x * 2.0f - y_left - y_right) * 0.12f;
    color += ringing;

    // =========================================================================
    // scanlines - ntsc is 480i, so at 1080p each scanline pair is ~4.5 pixels.
    // we darken alternating bands and alternate the phase each frame for flicker
    // =========================================================================

    float ntsc_line_height = resolution.y / 240.0f;
    float scanline_pos     = frac(thread_id.y / ntsc_line_height + float(buffer_frame.frame % 2u) * 0.5f);
    float scanline_dark    = smoothstep(0.3f, 0.5f, scanline_pos) * smoothstep(0.8f, 0.6f, scanline_pos);
    color *= lerp(1.0f, 0.82f, scanline_dark);

    // slight brightness variation per scanline to break up uniformity
    float line_brightness = 1.0f + noise_perlin(float(thread_id.y) * 0.5f + time * 0.3f) * 0.02f;
    color *= line_brightness;

    // =========================================================================
    // tape noise - the static/snow that covers the whole image,
    // plus the bright horizontal streaks that flash across randomly
    // =========================================================================

    // broad-spectrum luma noise (the graininess of vhs)
    float grain_seed = hash(float2(thread_id.xy) + frac(time * 7.13f));
    float grain = (grain_seed - 0.5f) * 0.08f;
    color += grain;

    // horizontal noise lines - bright streaks that appear for single frames
    float streak_seed = hash(float2(floor(uv.y * 480.0f), floor(time * 12.0f)));
    float streak = step(0.975f, streak_seed) * hash(float2(uv.y * 2000.0f, time * 50.0f));
    color += streak * 0.15f;

    // head switching band - heavy noise and brightness in the bottom strip
    float head_noise = head_switch * (hash(float2(uv.x * 200.0f, time * 40.0f)) * 0.5f + 0.2f);
    color = lerp(color, float3(head_noise, head_noise, head_noise), head_switch * 0.7f);

    // =========================================================================
    // color degradation
    // =========================================================================

    // vhs tapes lose color fidelity - desaturate toward a slightly warm tone
    float luma_final = dot(color, float3(0.299f, 0.587f, 0.114f));
    float3 warm_gray = float3(luma_final * 1.02f, luma_final * 1.0f, luma_final * 0.95f);
    color = lerp(color, warm_gray, 0.18f);

    // slight contrast reduction - black level doesn't go fully black on vhs
    color = color * 0.92f + 0.03f;

    tex_uav[thread_id.xy] = float4(saturate(color), 1.0f);
}
