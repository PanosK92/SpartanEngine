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

struct VSOUT
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VSOUT main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    VSOUT vs_out;
    
    float3 displaced_pos = input.position.xyz + tex.SampleLevel(samplers[sampler_point_clamp], input.uv, 0).xyz * GetMaterial().ocean_parameters.displacementScale;
    float3 wpos = mul(float4(displaced_pos, 1.0f), buffer_pass.transform).xyz;
    vs_out.pos = mul(float4(wpos, 1.0f), buffer_frame.view_projection);
    vs_out.uv = input.uv;

    return vs_out;
}

float4 main_ps(VSOUT vertex) : SV_Target0
{
    const float4 foam_color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float foam = tex2.Sample(samplers[sampler_trilinear_clamp], vertex.uv).a * 1.0f;

    return foam_color * foam;
}
