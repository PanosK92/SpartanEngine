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

// turns the spatial-domain ifft output into a displacement map plus a slope and foam map

#include "ocean_common.hlsl"

// tessendorf permutation sign, undoes the fft frequency shift
float ocean_sign(uint2 c)
{
    return ((c.x + c.y) & 1u) ? -1.0 : 1.0;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OCEAN_N || id.y >= OCEAN_N)
    {
        return;
    }

    uint cascade     = id.z;
    float length_m   = ocean_cascade_length(cascade);
    float chop       = ocean_choppiness();
    float disp_scale = ocean_disp_scale();
    float normal_str = ocean_normal_str();

    float s    = ocean_sign(id.xy);
    float4 a   = tex_ocean_fft_a_uav[id];
    float4 b   = tex_ocean_fft_b_uav[id];

    float height = a.x * s;
    float dx     = a.z * s;
    float dz     = b.x * s;

    float3 displacement            = float3(dx * chop, height, dz * chop) * disp_scale;
    tex_ocean_displacement_uav[id] = float4(displacement, 0.0);

    // analytic surface slope carried through the ifft, full spectral detail unlike a finite difference of the height
    float slope_x = b.z * s;
    float slope_z = b.w * s;

    // wrapped neighbours for the horizontal-displacement finite differences, used for the choppiness stretch and foam jacobian
    uint xp = (id.x + 1) % OCEAN_N;
    uint xm = (id.x + OCEAN_N - 1) % OCEAN_N;
    uint yp = (id.y + 1) % OCEAN_N;
    uint ym = (id.y + OCEAN_N - 1) % OCEAN_N;

    float s_xp = ocean_sign(uint2(xp, id.y));
    float s_xm = ocean_sign(uint2(xm, id.y));
    float s_yp = ocean_sign(uint2(id.x, yp));
    float s_ym = ocean_sign(uint2(id.x, ym));

    float4 a_xp = tex_ocean_fft_a_uav[uint3(xp, id.y, cascade)];
    float4 a_xm = tex_ocean_fft_a_uav[uint3(xm, id.y, cascade)];
    float4 a_yp = tex_ocean_fft_a_uav[uint3(id.x, yp, cascade)];
    float4 a_ym = tex_ocean_fft_a_uav[uint3(id.x, ym, cascade)];
    float4 b_yp = tex_ocean_fft_b_uav[uint3(id.x, yp, cascade)];
    float4 b_ym = tex_ocean_fft_b_uav[uint3(id.x, ym, cascade)];

    float cell = length_m / (float)OCEAN_N; // world distance between neighbouring texels
    float inv2 = 1.0 / (2.0 * cell);

    // horizontal displacement gradients from the choppiness term, used for normal stretch below
    float dDx_dx = (((a_xp.z * s_xp) - (a_xm.z * s_xm)) * inv2) * chop;
    float dDz_dz = (((b_yp.x * s_yp) - (b_ym.x * s_ym)) * inv2) * chop;
    float dDx_dz = (((a_yp.z * s_yp) - (a_ym.z * s_ym)) * inv2) * chop;

    // jacobian of the horizontal displacement, foam forms where the surface folds onto itself
    // the displacement map also scales the horizontal offset by disp_scale, so the folding metric must include it
    float jx       = dDx_dx * disp_scale;
    float jz       = dDz_dz * disp_scale;
    float jxz      = dDx_dz * disp_scale;
    float jacobian = (1.0 + jx) * (1.0 + jz) - jxz * jxz;

    // choppiness folds the surface horizontally, dividing the height gradient by the horizontal stretch
    // sharpens normals on compressed crests and flattens stretched troughs, this is what makes choppiness read in the lighting
    float stretch_x = max(1.0 + dDx_dx, 0.1);
    float stretch_z = max(1.0 + dDz_dz, 0.1);
    slope_x        /= stretch_x;
    slope_z        /= stretch_z;

    // whitewater is a pure consequence of the surface folding onto itself, nothing artistic gates it
    // the jacobian of the horizontal displacement passes through zero as a crest pinches and goes negative where it overhangs,
    // so the fold depth itself is the foam intensity, steeper or choppier waves fold harder and foam more while calm water never folds and stays clear
    // the result is accumulated over time so it trails behind the crests as fading streaks instead of single-frame sparkle,
    // the previous value is read back from the persistent normal target whose slope channels are overwritten below
    float inject    = saturate(-jacobian);
    float prev_foam = saturate(tex_ocean_normal_uav[id].z);
    float decay     = exp(-buffer_frame.delta_time * OCEAN_FOAM_DECAY);
    float foam      = max(inject, prev_foam * decay);

    tex_ocean_normal_uav[id] = float4(slope_x * normal_str, slope_z * normal_str, foam, 0.0);
}
