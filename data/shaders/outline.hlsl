/*
Copyright(c) 2016-2023 Panos Karabelas

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

// = INCLUDES ========
#include "common.hlsl"
//====================

Pixel_Pos mainVS(Vertex_PosUv input)
{
    Pixel_Pos output;

    input.position.w = 1.0f;
    output.position  = mul(input.position, buffer_pass.transform);
    output.position  = mul(output.position, buffer_frame.view_projection_unjittered);

    return output;
}
 
float4 mainPS(Pixel_Pos input) : SV_Target
{
    // just a color
    return pass_get_f4_value();
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    float4 silhouette = tex[thread_id.xy];
    float3 color      = silhouette.rgb;
    float alpha       = silhouette.a;

    tex_uav[thread_id.xy] += float4(color * (1.0f - alpha), 0.0f);
}
