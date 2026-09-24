/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "../common.hlsl"
#include "../spherical_harmonics.hlsl"
//===================================

// project skysphere radiance into l2 sh, writes 9 float4 coeffs to a 9x1 uav
// runs rarely (warmup or every 4th frame) so a single thread over a 64x32 grid is enough
static const uint sh_samples_x = 64;
static const uint sh_samples_y = 32;

[numthreads(1, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    float mip_count_f;
    tex.GetDimensions(0, resolution.x, resolution.y, mip_count_f);
    // mid mip keeps l2 stable without paying for the full panorama
    float mip = max(mip_count_f - 4.0f, 0.0f);

    float3 sh[9];
    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        sh[i] = 0.0f.xxx;
    }

    for (uint y = 0; y < sh_samples_y; y++)
    {
        for (uint x = 0; x < sh_samples_x; x++)
        {
            float2 uv  = (float2(x, y) + 0.5f) / float2(sh_samples_x, sh_samples_y);
            float3 dir = direction_from_sphere_uv(uv);
            float sin_theta = max(sqrt(max(1.0f - dir.y * dir.y, 0.0f)), 1e-4f);
            float d_omega   = (PI2 / float(sh_samples_x)) * (PI / float(sh_samples_y)) * sin_theta;

            float3 radiance = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, mip).rgb;
            if (any(isnan(radiance)) || any(isinf(radiance)))
            {
                continue;
            }

            float b[9];
            sh_eval_basis_l2(dir, b);

            [unroll]
            for (uint c = 0; c < 9; c++)
            {
                sh[c] += radiance * b[c] * d_omega;
            }
        }
    }

    [unroll]
    for (uint c = 0; c < 9; c++)
    {
        float3 coeff = sh[c];
        if (any(isnan(coeff)) || any(isinf(coeff)))
        {
            coeff = 0.0f.xxx;
        }
        tex_uav[uint2(c, 0)] = float4(coeff, 1.0f);
    }
}
