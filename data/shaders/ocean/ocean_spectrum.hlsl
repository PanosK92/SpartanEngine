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

// fft ocean spectrum, INIT seeds h0(k), UPDATE evolves it to the current time and packs the ifft inputs

#include "ocean_common.hlsl"

// wave vector for a texel, centered so k spans [-pi*N/L, pi*N/L]
float2 ocean_wave_vector(uint2 id, float length_m)
{
    int n = (int)id.x - (int)(OCEAN_N / 2);
    int m = (int)id.y - (int)(OCEAN_N / 2);
    return 2.0 * OCEAN_PI * float2(n, m) / length_m;
}

#if defined(INIT)
[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OCEAN_N || id.y >= OCEAN_N)
    {
        return;
    }

    uint cascade        = id.z;
    float length_m      = ocean_cascade_length(cascade);
    float2 wind_dir     = ocean_wind_dir();
    float wind_speed    = ocean_wind_speed();
    float amplitude     = ocean_amplitude();

    float2 k    = ocean_wave_vector(id.xy, length_m);
    float k_len = length(k);

    // band-limit, each cascade keeps its own wave-number range so detail spreads across scales instead of piling onto one
    // the boundary between a cascade and the next finer one sits at ~1/10 of the coarser patch length, which keeps every band well populated
    uint cascade_count = buffer_frame.ocean_cascade_count;
    float k_low        = (cascade > 0)                 ? (2.0 * OCEAN_PI * 10.0 / ocean_cascade_length(cascade - 1)) : 0.0;
    float k_high       = (cascade + 1 < cascade_count) ? (2.0 * OCEAN_PI * 10.0 / length_m)                          : 1e9;
    if (k_len < k_low || k_len >= k_high)
    {
        tex_ocean_spectrum_uav[id] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // h0(k) from the spectrum modulated by complex gaussian noise
    uint seed     = id.x + id.y * OCEAN_N + cascade * OCEAN_N * OCEAN_N + 0x9e3779b9u;
    float2 xi     = ocean_gauss(seed);
    float2 h0     = (1.0 / sqrt(2.0)) * xi * sqrt(ocean_phillips(k, wind_dir, wind_speed, amplitude));

    // conjugate of h0(-k), uses the mirrored texel so neighbouring cells stay consistent
    uint2 mirror   = uint2((OCEAN_N - id.x) % OCEAN_N, (OCEAN_N - id.y) % OCEAN_N);
    uint seed_m    = mirror.x + mirror.y * OCEAN_N + cascade * OCEAN_N * OCEAN_N + 0x9e3779b9u;
    float2 xi_m    = ocean_gauss(seed_m);
    float2 h0_minus = (1.0 / sqrt(2.0)) * xi_m * sqrt(ocean_phillips(-k, wind_dir, wind_speed, amplitude));
    float2 h0_minus_conj = float2(h0_minus.x, -h0_minus.y);

    tex_ocean_spectrum_uav[id] = float4(h0, h0_minus_conj);
}
#endif

#if defined(UPDATE)
[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OCEAN_N || id.y >= OCEAN_N)
    {
        return;
    }

    uint cascade   = id.z;
    float length_m = ocean_cascade_length(cascade);

    float2 k    = ocean_wave_vector(id.xy, length_m);
    float k_len = length(k);

    // deep water dispersion relation
    float w = sqrt(OCEAN_G * k_len);
    float t = (float)buffer_frame.time;

    float4 seed       = tex_ocean_spectrum_uav[id];
    float2 h0         = seed.xy;
    float2 h0_minus_c = seed.zw;

    float2 e      = float2(cos(w * t), sin(w * t));   // exp(+i w t)
    float2 e_conj = float2(e.x, -e.y);                // exp(-i w t)

    // time-evolved height spectrum, conjugate-symmetric so the ifft is real
    float2 h_tilde = ocean_cmul(h0, e) + ocean_cmul(h0_minus_c, e_conj);

    // horizontal displacement spectra, dx = -i * (kx/|k|) * h, dz = -i * (kz/|k|) * h
    float2 k_dir       = k_len > 1e-6 ? k / k_len : float2(0.0, 0.0);
    float2 disp_x      = ocean_mul_neg_i(h_tilde) * k_dir.x;
    float2 disp_z      = ocean_mul_neg_i(h_tilde) * k_dir.y;

    // pack two complex fields per texture so a single ifft transforms them together
    tex_ocean_fft_a_uav[id] = float4(h_tilde, disp_x);
    tex_ocean_fft_b_uav[id] = float4(disp_z, 0.0, 0.0);
}
#endif
