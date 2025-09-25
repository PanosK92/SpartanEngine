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

#include "../common.hlsl"

#ifndef SPARTAN_COMMON_OCEAN
#define SPARTAN_COMMON_OCEAN

static const uint SPECTRUM_TEX_SIZE = 512;

RWTexture2D<float4> initial_spectrum         : register(u9);
RWTexture2D<float4> displacement_spectrum    : register(u10);
RWTexture2D<float4> slope_spectrum           : register(u11);
RWTexture2D<float4> displacement_map         : register(u12);
RWTexture2D<float4> slope_map                : register(u13);
RWTexture2D<float4> synthesised_displacement : register(u14);

float Dispersion(float kMag, float depth)
{
    return sqrt(G * kMag * tanh(min(kMag * depth, 20)));
}

#endif // SPARTAN_COMMON_OCEAN
