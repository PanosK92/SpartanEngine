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

float2 ComplexMult(float2 a, float2 b)
{
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float2 EulerFormula(float x)
{
    return float2(cos(x), sin(x));
}

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float4 initialSignal = initial_spectrum[thread_id.xy];
    float2 h0 = initialSignal.rg;
    float2 h0conj = initialSignal.ba;
    
    float halfN = SPECTRUM_TEX_SIZE / 2.0f;
    float2 K = (thread_id.xy - halfN) * 2.0f * PI / LENGTH_SCALE;
    float kMag = length(K);
    float kMagRcp = rcp(kMag);

    if (kMag < 0.0001f)
    {
        kMagRcp = 1.0f;
    }

    float2 resolution_out;
    initial_spectrum.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);
    OceanParameters params = surface.ocean_parameters;
    
    float repeatTime = params.repeatTime;
    float w_0 = 2.0f * PI / repeatTime;
    float dispersion = floor(sqrt(G * kMag) / w_0) * w_0 * buffer_frame.time;

    float2 exponent = EulerFormula(dispersion);

    float2 htilde = ComplexMult(h0, exponent) + ComplexMult(h0conj, float2(exponent.x, -exponent.y));
    float2 ih = float2(-htilde.y, htilde.x);

    float2 displacementX = ih * K.x * kMagRcp;
    float2 displacementY = htilde;
    float2 displacementZ = ih * K.y * kMagRcp;

    float2 displacementX_dx = -htilde * K.x * K.x * kMagRcp;
    float2 displacementY_dx = ih * K.x;
    float2 displacementZ_dx = -htilde * K.x * K.y * kMagRcp;

    float2 displacementY_dz = ih * K.y;
    float2 displacementZ_dz = -htilde * K.y * K.y * kMagRcp;

    float2 htildeDisplacementX = float2(displacementX.x - displacementZ.y, displacementX.y + displacementZ.x);
    float2 htildeDisplacementZ = float2(displacementY.x - displacementZ_dx.y, displacementY.y + displacementZ_dx.x);
        
    float2 htildeSlopeX = float2(displacementY_dx.x - displacementY_dz.y, displacementY_dx.y + displacementY_dz.x);
    float2 htildeSlopeZ = float2(displacementX_dx.x - displacementZ_dz.y, displacementX_dx.y + displacementZ_dz.x);

    displacement_spectrum[thread_id.xy] = float4(htildeDisplacementX, htildeDisplacementZ);
    slope_spectrum[thread_id.xy] = float4(htildeSlopeX, htildeSlopeZ);
}
