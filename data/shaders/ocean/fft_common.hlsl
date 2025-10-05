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

#ifndef SPARTAN_FFT_COMMON
#define SPARTAN_FFT_COMMON

#include "common_ocean.hlsl"

static const uint LOG_SIZE = log(SPECTRUM_TEX_SIZE) / log(2); // result of Log base 2 of SPECTRUM_TEX_SIZE

groupshared float4 fftGroupBuffer[2][SPECTRUM_TEX_SIZE];

void ButterflyValues(uint step, uint index, out uint2 indices, out float2 twiddle)
{
    const float twoPi = 6.28318530718;
    uint b = SPECTRUM_TEX_SIZE >> (step + 1);
    uint w = b * (index / b);
    uint i = (w + index) % SPECTRUM_TEX_SIZE;
    sincos(-twoPi / SPECTRUM_TEX_SIZE * w, twiddle.y, twiddle.x);

    // This is what makes it the inverse FFT
    twiddle.y = -twiddle.y;
    indices = uint2(i, i + b);
}

float2 ComplexMult(float2 a, float2 b)
{
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float4 FFT(uint threadIndex, float4 input)
{
    fftGroupBuffer[0][threadIndex] = input;
    GroupMemoryBarrierWithGroupSync();
    
    bool flag = false;

    /*[unroll]*/
    for (uint step = 0; step < LOG_SIZE; ++step)
    {
        uint2 inputsIndices;
        float2 twiddle;
        ButterflyValues(step, threadIndex, inputsIndices, twiddle);

        float4 v = fftGroupBuffer[flag][inputsIndices.y];
        fftGroupBuffer[!flag][threadIndex] =
            fftGroupBuffer[flag][inputsIndices.x] + float4(ComplexMult(twiddle, v.xy), ComplexMult(twiddle, v.zw));

        flag = !flag;
        GroupMemoryBarrierWithGroupSync();
    }

    return fftGroupBuffer[flag][threadIndex];
}

#endif // SPARTAN_FFT_COMMON
