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

//= INCLUDES =========
#include "common.hlsl"
//====================

void mainVS(uint vertex_id : SV_VertexID, out float4 position : SV_POSITION)
{
    float3 box_min = pass_get_f3_value();
    float3 box_max = pass_get_f3_value2();

    // define the 8 corners of the bounding box
    float3 corners[8];
    corners[0] = float3(box_min.x, box_min.y, box_min.z);
    corners[1] = float3(box_max.x, box_min.y, box_min.z);
    corners[2] = float3(box_max.x, box_max.y, box_min.z);
    corners[3] = float3(box_min.x, box_max.y, box_min.z);
    corners[4] = float3(box_min.x, box_min.y, box_max.z);
    corners[5] = float3(box_max.x, box_min.y, box_max.z);
    corners[6] = float3(box_max.x, box_max.y, box_max.z);
    corners[7] = float3(box_min.x, box_max.y, box_max.z);

    // define the edges of the bounding box using the corners
    uint edges[24] =
    {
        0, 1, 1, 2, 2, 3, 3, 0, // bottom face edges
        4, 5, 5, 6, 6, 7, 7, 4, // top face edges
        0, 4, 1, 5, 2, 6, 3, 7  // side edges
    };

    // use the vertex id to pick the correct edge vertex
    uint index   = edges[vertex_id];
    position.xyz = corners[index];
    position.w   = 1.0f;

    // to clip space
    position = mul(position, buffer_frame.view_projection);
}
