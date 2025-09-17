/*
Copyright(c) 2025 George Bolba

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

// Inspired by Acerola's Implementation:
// https://github.com/GarrettGunnell/Water/blob/main/Assets/Shaders/FFTWater.compute

#include "common_ocean.hlsl"

float4 Permute(float4 data, float3 id)
{
    return data * (1.0f - 2.0f * ((id.x + id.y) % 2));
}

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    const float2 Lambda = float2(1.0f, 1.0f);

    float4 htildeDisplacement = Permute(displacement_spectrum[thread_id.xy], thread_id);
    float4 htildeSlope = Permute(slope_spectrum[thread_id.xy], thread_id);

    float2 dxdz = htildeDisplacement.rg;
    float2 dydxz = htildeDisplacement.ba;
    float2 dyxdyz = htildeSlope.rg;
    float2 dxxdzz = htildeSlope.ba;

    float3 displacement = float3(Lambda.x * dxdz.x, dydxz.x, Lambda.y * dxdz.y);
    float2 slopes = dyxdyz.xy / (1 + abs(dxxdzz * Lambda));

    float jacobian = (1.0f + Lambda.x * dxxdzz.x) * (1.0f + Lambda.y * dxxdzz.y) - Lambda.x * Lambda.y * dydxz.y * dydxz.y;
    float covariance = slopes.x * slopes.y;

    // create surface
    float2 resolution_out;
    slope_map.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, false, false);

    OceanParameters params = surface.ocean_parameters;
    
    float foam = htildeDisplacement.a;
    foam *= exp(-params.foamDecayRate);
    foam = saturate(foam);
    
    float biasedJacobian = max(0.0f, -(jacobian - params.foamBias));
    
    if (biasedJacobian > params.foamThreshold)
        foam += params.foamAdd * biasedJacobian;

    displacement_map[thread_id.xy] = float4(displacement, 1.0f);
    slope_map[thread_id.xy] = float4(slopes, 0.0f, foam);
}
