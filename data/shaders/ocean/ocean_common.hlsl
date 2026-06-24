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

// shared math for the fft ocean, tessendorf spectrum and complex helpers

#include "../common_resources.hlsl"

static const uint  OCEAN_N          = 256;
static const uint  OCEAN_LOG2N      = 8;
static const float OCEAN_PI         = 3.14159265359;
static const float OCEAN_G          = 9.81;
static const float OCEAN_FOAM_DECAY = 0.6;   // foam fade rate per second, lower lingers longer
static const float OCEAN_DIR_SPREAD = 2.0;   // cos power for wind alignment, higher is tighter
static const float OCEAN_CAPILLARY  = 0.003; // sub-capillary cutoff in metres, only the finest ripples below this are damped

// push constant unpack, the cascade lengths are packed across the value slots by Pass_Ocean
float2 ocean_wind_dir()    { return pass_get_f3_value().xy; }
float  ocean_wind_speed()  { return pass_get_f3_value().z; }
float  ocean_amplitude()   { return pass_get_f4_value().x; }
float  ocean_choppiness()  { return pass_get_f4_value().y; }
float  ocean_foam()        { return pass_get_f4_value().z; }
float  ocean_disp_scale()  { return pass_get_f4_value().w; }
float  ocean_normal_str()  { return pass_get_f2_value().y; }

float ocean_cascade_length(uint cascade)
{
    float3 l012 = pass_get_f3_value2();
    float  l3   = pass_get_f2_value().x;
    if (cascade == 0) { return l012.x; }
    if (cascade == 1) { return l012.y; }
    if (cascade == 2) { return l012.z; }
    return l3;
}

float2 ocean_cmul(float2 a, float2 b)
{
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// multiply by -i, rotates a complex number clockwise by 90 degrees
float2 ocean_mul_neg_i(float2 a)
{
    return float2(a.y, -a.x);
}

uint ocean_hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float ocean_rand01(inout uint state)
{
    state = ocean_hash(state);
    return (state & 0x00ffffffu) / 16777216.0;
}

// two independent gaussian samples via box-muller
float2 ocean_gauss(uint seed)
{
    uint state = ocean_hash(seed);
    float u1   = max(ocean_rand01(state), 1e-6);
    float u2   = ocean_rand01(state);
    float r    = sqrt(-2.0 * log(u1));
    float a    = 2.0 * OCEAN_PI * u2;
    return float2(r * cos(a), r * sin(a));
}

// phillips spectrum, energy distribution of wind waves over wave vector k
float ocean_phillips(float2 k, float2 wind_dir, float wind_speed, float amplitude)
{
    float k2 = dot(k, k);
    if (k2 < 1e-12)
    {
        return 0.0;
    }

    float largest = wind_speed * wind_speed / OCEAN_G; // longest wave the wind can raise
    float k4      = k2 * k2;

    // directional spreading, the cos power aligns waves with the wind, higher is tighter
    float k_dot_w     = dot(normalize(k), wind_dir);
    float directional = pow(abs(k_dot_w), OCEAN_DIR_SPREAD);
    float spectrum    = amplitude * exp(-1.0 / (k2 * largest * largest)) / k4 * directional;

    // damp waves travelling against the wind
    if (k_dot_w < 0.0)
    {
        spectrum *= 0.07;
    }

    // damp only the sub-capillary ripples to curb aliasing, fine detail above the cutoff survives
    spectrum *= exp(-k2 * OCEAN_CAPILLARY * OCEAN_CAPILLARY);

    return spectrum;
}
