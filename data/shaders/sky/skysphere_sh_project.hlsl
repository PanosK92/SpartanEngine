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
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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
