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

// once-per-frame procedural wind field sampled by all wind-driven geometry
// rg = flow vector (signed, [-1,1]), b = gust pressure (0..1), a = micro turbulence (0..1)
// the texture tiles seamlessly in world space so a single sampler_bilinear_wrap covers any range

#include "common.hlsl"

static const int   FREQ_CURL_BASE = 4;    // base octave cycles across one tile
static const int   FREQ_GUST      = 2;    // gust front cycles across one tile
static const int   FREQ_MICRO     = 32;   // micro turbulence cycles across one tile
static const int   CURL_OCTAVES   = 4;    // 4 -> top freq 32 cycles per tile
static const float CURL_DRIFT     = 0.03; // per-octave tile fraction drifted per second
static const float GUST_SPEED     = 0.07; // tile fractions per second the gust front travels
static const float MICRO_SPEED    = 0.9;  // tile fractions per second the micro pattern slides
static const float LIFE_RATE      = 0.55; // octave breathing rate, controls birth/death of features

uint hash_u32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float2 hash22(int2 p)
{
    uint h0 = hash_u32(uint(p.x) * 374761393u + uint(p.y) * 668265263u);
    uint h1 = hash_u32(h0 ^ 0x9e3779b9u);
    return float2(h0, h1) * (2.0 / 4294967295.0) - 1.0;
}

int2 wrap_cell(int2 p, int period)
{
    return ((p % period) + period) % period;
}

// tileable 2d gradient noise, period = freq cells across the unit uv range
float gnoise_tiled(float2 p, int period)
{
    int2 ic   = int2(floor(p));
    float2 f  = p - float2(ic);
    float2 u  = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float2 ga = hash22(wrap_cell(ic + int2(0, 0), period));
    float2 gb = hash22(wrap_cell(ic + int2(1, 0), period));
    float2 gc = hash22(wrap_cell(ic + int2(0, 1), period));
    float2 gd = hash22(wrap_cell(ic + int2(1, 1), period));

    float a = dot(ga, f - float2(0, 0));
    float b = dot(gb, f - float2(1, 0));
    float c = dot(gc, f - float2(0, 1));
    float d = dot(gd, f - float2(1, 1));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// fbm with per-octave drift in different directions and time-breathing amplitude
// the per-octave amplitude breathing is what kills the scrolling-texture look,
// patterns are born and decay in place rather than translating rigidly
float fbm_evolving(float2 uv, float t, float phase)
{
    static const float2 dirs[4] = {
        float2( 0.80,  0.60),
        float2(-0.60,  0.80),
        float2(-0.80, -0.60),
        float2( 0.60, -0.80)
    };

    float n         = 0.0;
    float amp       = 1.0;
    float total_amp = 0.0;
    int   freq      = FREQ_CURL_BASE;

    [unroll] for (int i = 0; i < CURL_OCTAVES; i++)
    {
        float2 drift = dirs[i] * t * CURL_DRIFT * float(freq);
        float life   = 0.6 + 0.4 * sin(t * LIFE_RATE + float(i) * 1.7 + phase);
        n           += amp * life * gnoise_tiled(uv * float(freq) + drift, freq);
        total_amp   += amp;
        amp         *= 0.55;
        freq        *= 2;
    }
    return n / total_amp;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint2 dims;
    tex_uav.GetDimensions(dims.x, dims.y);
    if (any(tid.xy >= dims))
        return;

    float2 uv = (float2(tid.xy) + 0.5) / float2(dims);
    float  t  = (float)buffer_frame.time;

    float3 wind     = buffer_frame.wind;
    float  wind_mag = length(float2(wind.x, wind.z));
    float2 wind_dir = wind_mag > 1e-4 ? float2(wind.x, wind.z) / wind_mag : float2(0.0, 1.0);

    // flow channel - two independent fbms that evolve in time so it never just scrolls
    // domain-warp with a low-frequency 2d noise so the field swirls instead of drifting in one direction
    // the warp is computed from gnoise_tiled directly so it stays seamlessly tileable
    float2 warp_drift_a = float2( 0.06, -0.09) * t;
    float2 warp_drift_b = float2(-0.07,  0.05) * t;
    float2 warp = float2(
        gnoise_tiled(uv * 2.0 + warp_drift_a,                   2),
        gnoise_tiled(uv * 2.0 + warp_drift_b + float2(5.1, 3.7), 2)
    ) * 0.18;
    float  flow_x = fbm_evolving(uv + warp,        t,        0.0);
    float  flow_y = fbm_evolving(uv - warp.yx,     t * 1.07, 2.71);
    float2 flow   = clamp(float2(flow_x, flow_y) * 1.6, -1.0, 1.0);

    // gust pressure - travels with the wind direction, sharpened into visible fronts
    float2 gust_advect = -wind_dir * t * GUST_SPEED * float(FREQ_GUST);
    float  gust_low    = gnoise_tiled(uv * float(FREQ_GUST)       + gust_advect,        FREQ_GUST) * 0.5 + 0.5;
    float  gust_hi     = gnoise_tiled(uv * float(FREQ_GUST * 2)   + gust_advect * 2.0,  FREQ_GUST * 2) * 0.5 + 0.5;
    float  gust        = saturate(lerp(gust_low, gust_hi, 0.35));
    gust               = smoothstep(0.42, 0.78, gust);

    // micro turbulence - high frequency, fast, no preferred direction
    float2 micro_advect = float2(0.31, -0.27) * t * MICRO_SPEED * float(FREQ_MICRO);
    float  micro        = gnoise_tiled(uv * float(FREQ_MICRO) + micro_advect, FREQ_MICRO) * 0.5 + 0.5;

    tex_uav[tid.xy] = float4(flow.x, flow.y, gust, micro);
}
