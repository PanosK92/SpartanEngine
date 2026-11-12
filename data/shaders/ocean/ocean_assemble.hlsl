/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// turns the spatial-domain ifft output into a displacement map plus a slope and foam map

#define OCEAN_WAVE_BOUNDS_WRITE
#include "ocean_common.hlsl"

// tessendorf permutation sign, undoes the fft frequency shift
float ocean_sign(uint2 c)
{
    return ((c.x + c.y) & 1u) ? -1.0 : 1.0;
}

// one cascade cannot fold the sea on its own, each band holds a fraction of the steepness and a crest only
// breaks where the long waves and the shorter ones riding them compress the surface together, so whitecaps
// come from the strain of all the wave bands summed, kept in the grid of the one cascade that carries the foam
uint ocean_foam_carrier(uint cascade_count)
{
    // the second longest band holds the wind sea crests and its texels are fine enough for whitecap shapes
    return min(1u, cascade_count - 1u);
}

// the shortest band is ripples, they sharpen reflections but do not break into lasting whitecaps
bool ocean_foam_contributes(uint cascade, uint cascade_count)
{
    return cascade + 1u < cascade_count || cascade_count < 3u;
}

// symmetric strain of the horizontal displacement at a texel, x = dDx/dx, y = dDz/dz, z = (dDx/dz + dDz/dx) / 2
float3 ocean_strain(int2 texel, uint cascade)
{
    int mask = (int)OCEAN_N - 1;
    texel   &= mask;
    int xp   = (texel.x + 1) & mask;
    int xm   = (texel.x - 1) & mask;
    int yp   = (texel.y + 1) & mask;
    int ym   = (texel.y - 1) & mask;

    // the four neighbours share the opposite permutation sign of the texel between them
    float s    = -ocean_sign(uint2(texel));
    float dx_x = tex_ocean_fft_a_uav[uint3(xp, texel.y, cascade)].z - tex_ocean_fft_a_uav[uint3(xm, texel.y, cascade)].z;
    float dx_z = tex_ocean_fft_a_uav[uint3(texel.x, yp, cascade)].z - tex_ocean_fft_a_uav[uint3(texel.x, ym, cascade)].z;
    float dz_x = tex_ocean_fft_b_uav[uint3(xp, texel.y, cascade)].x - tex_ocean_fft_b_uav[uint3(xm, texel.y, cascade)].x;
    float dz_z = tex_ocean_fft_b_uav[uint3(texel.x, yp, cascade)].x - tex_ocean_fft_b_uav[uint3(texel.x, ym, cascade)].x;

    float scale = s * (float)OCEAN_N / (2.0 * ocean_cascade_length(cascade)) * ocean_choppiness() * ocean_disp_scale();
    return float3(dx_x, dz_z, 0.5 * (dx_z + dz_x)) * scale;
}

// strain of another cascade at a world position, texel centres sit at half texel offsets like the bilinear sampler sees them
float3 ocean_strain_at(float2 world_xz, uint cascade, bool filtered)
{
    float2 f = world_xz / ocean_cascade_length(cascade) * (float)OCEAN_N;
    if (!filtered)
    {
        return ocean_strain(int2(floor(f)), cascade);
    }

    f       -= 0.5;
    int2 i   = int2(floor(f));
    float2 t = f - float2(i);
    float3 s00 = ocean_strain(i, cascade);
    float3 s10 = ocean_strain(i + int2(1, 0), cascade);
    float3 s01 = ocean_strain(i + int2(0, 1), cascade);
    float3 s11 = ocean_strain(i + int2(1, 1), cascade);
    return lerp(lerp(s00, s10, t.x), lerp(s01, s11, t.x), t.y);
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

    float s    = ocean_sign(id.xy);
    float4 a   = tex_ocean_fft_a_uav[id];
    float4 b   = tex_ocean_fft_b_uav[id];

    float height = a.x * s;
    float dx     = a.z * s;
    float dz     = b.x * s;

    float3 displacement            = float3(dx * chop, height, dz * chop) * disp_scale;
    tex_ocean_displacement_uav[id] = float4(displacement, 0.0);
    // Bound the actual fp16 texture values, including rounding. Bilinear
    // sampling and the shore multiplier (0.4..1) cannot exceed this magnitude.
    float stored_height = abs(f16tof32(f32tof16(displacement.y)));
    float wave_maximum = WaveActiveMax(stored_height);
    if (WaveIsFirstLane())
        InterlockedMax(ocean_wave_bounds[cascade], asuint(wave_maximum));

    // mirror displacement for cpu buoyancy
    if ((id.x & 3u) == 0u && (id.y & 3u) == 0u)
    {
        const uint hn = OCEAN_N / 4u;
        ocean_heights[
            cascade * hn * hn +
            (id.y >> 2u) * hn +
            (id.x >> 2u)
        ] = float4(displacement, 0.0f);
    }

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
    float4 b_xp = tex_ocean_fft_b_uav[uint3(xp, id.y, cascade)];
    float4 b_xm = tex_ocean_fft_b_uav[uint3(xm, id.y, cascade)];
    float4 b_yp = tex_ocean_fft_b_uav[uint3(id.x, yp, cascade)];
    float4 b_ym = tex_ocean_fft_b_uav[uint3(id.x, ym, cascade)];

    float cell = length_m / (float)OCEAN_N; // world distance between neighbouring texels
    float inv2 = 1.0 / (2.0 * cell);

    // horizontal displacement gradients of the rendered surface, the displacement map applies chop and disp_scale so both belong here
    float dDx_dx = (((a_xp.z * s_xp) - (a_xm.z * s_xm)) * inv2) * chop * disp_scale;
    float dDz_dz = (((b_yp.x * s_yp) - (b_ym.x * s_ym)) * inv2) * chop * disp_scale;
    float dDx_dz = (((a_yp.z * s_yp) - (a_ym.z * s_ym)) * inv2) * chop * disp_scale;
    float dDz_dx = (((b_xp.x * s_xp) - (b_xm.x * s_xm)) * inv2) * chop * disp_scale;

    // jacobian of the horizontal mapping, 1 is undeformed, below 1 is a compressed crest, 0 is a fold
    float jacobian = (1.0 + dDx_dx) * (1.0 + dDz_dz) - dDx_dz * dDz_dx;

    // inverse-transpose of the full horizontal mapping. Ignoring the cross terms
    // gave incorrect normals for diagonal waves. Bound the slope at folded crests.
    float2 height_gradient = float2(slope_x, slope_z) * disp_scale;
    slope_x = ((1.0 + dDz_dz) * height_gradient.x - dDz_dx * height_gradient.y) / max(jacobian, 0.1);
    slope_z = ((1.0 + dDx_dx) * height_gradient.y - dDx_dz * height_gradient.x) / max(jacobian, 0.1);

    bool reset     = pass_bool(pass_ocean::reset_history);
    float2 history = reset ? float2(0.0, 0.0) : tex_ocean_normal_uav[id].zw;
    float dt       = max(buffer_frame.delta_time, 0.0);

    // whitecaps, only the carrier cascade keeps foam, the shading takes the largest value over the cascades
    float foam         = 0.0;
    uint cascade_count = clamp(buffer_frame.ocean_cascade_count, 1u, 4u);
    if (cascade == ocean_foam_carrier(cascade_count))
    {
        // strain of every contributing band where this texel sits in the world, coarser bands are filtered
        // across their texels, finer ones are read at the nearest texel
        float3 strain   = float3(dDx_dx, dDz_dz, 0.5 * (dDx_dz + dDz_dx));
        float2 world_xz = (float2(id.xy) + 0.5) * cell;
        for (uint other = 0; other < cascade_count; ++other)
        {
            if (other != cascade && ocean_foam_contributes(other, cascade_count))
            {
                strain += ocean_strain_at(world_xz, other, other < cascade);
            }
        }

        // breaking follows the most compressed direction, the determinant alone can miss a crest compressed
        // in one axis and stretched in the other
        float stretch_min = 1.0 + 0.5 * (strain.x + strain.y) - sqrt(0.25 * (strain.x - strain.y) * (strain.x - strain.y) + strain.z * strain.z);
        float inject      = 1.0 - smoothstep(0.64, 0.74, stretch_min);
        // whitecaps need wind, coverage grows with the cube of the wind speed (monahan 1980) and is next to nothing
        // below a gentle breeze, a calm sea that folds a crest here and there does not turn white over it
        inject *= smoothstep(3.0, 8.0, ocean_wind_speed() * sqrt(max(ocean_amplitude(), 0.0)));

        // exact integration of production and dissipation avoids frame rate dependent coverage, a crest whitens
        // within a fraction of a second and the foam it leaves rides the surface for seconds after it has passed
        float prev        = saturate(history.x);
        float production  = inject * 6.0;
        float rate        = production + 0.55;
        float equilibrium = production / rate;
        foam              = saturate(equilibrium + (prev - equilibrium) * exp(-rate * dt));
    }

    // A slow wave-energy envelope lets contact foam survive mean-level crossings,
    // while genuinely still water stays clean. w stores mean squared height.
    float h = height * disp_scale;
    float energy = lerp(h * h, max(history.y, 0.0), exp(-dt));
    tex_ocean_normal_uav[id] = float4(slope_x, slope_z, foam, energy);
}
