/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// shared math for the fft ocean, tessendorf spectrum and complex helpers

#include "../common_resources.hlsl"

static const uint  OCEAN_N          = 512;
static const uint  OCEAN_LOG2N      = 9;
static const float OCEAN_PI         = 3.14159265359;
static const float OCEAN_G          = 9.81;
static const float OCEAN_PHILLIPS_A = 0.002; // base phillips constant, sized so the user amplitude is a plain height multiplier around 1
static const float OCEAN_DIR_SPREAD = 2.0;   // cos power for wind alignment, higher is tighter
static const float OCEAN_CAPILLARY  = 0.003; // sub-capillary cutoff in metres, only the finest ripples below this are damped

float2 ocean_wind_dir()    { return pass_float2(pass_ocean::wind_direction); }
float  ocean_wind_speed()  { return pass_float(pass_ocean::wind_speed); }
float  ocean_amplitude()   { return pass_float(pass_ocean::amplitude); }
float  ocean_choppiness()  { return pass_float(pass_ocean::choppiness); }
float  ocean_disp_scale()  { return pass_float(pass_ocean::displacement_scale); }
float  ocean_normal_str()  { return pass_float(pass_ocean::normal_strength); }

float ocean_cascade_length(uint cascade)
{
    return pass_float4(pass_ocean::cascade_lengths)[min(cascade, 3u)];
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
    if (k2 < 1e-12 || wind_speed <= 0.0001)
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
