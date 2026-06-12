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
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ===================
#include "common.hlsl"
//==============================

// sample duplication map, lin 2026 5
// counts reservoirs in the surrounding 17x17 window that carry the same replay seed as this
// pixel, i.e. shifted copies of the same initial candidate, the normalized score feeds the
// adaptive confidence cap in the temporal pass to decorrelate firefly blobs
// input on tex is reservoir texture 2 whose x channel stores the replay seed

static const int DUPLICATION_WINDOW_RADIUS = 8;

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint resolution_x, resolution_y;
    tex_uav.GetDimensions(resolution_x, resolution_y);

    if (pixel.x >= resolution_x || pixel.y >= resolution_y)
        return;

    uint own_seed = asuint(tex[pixel].x);

    // empty reservoirs and nee samples store no replay seed, nothing to correlate on
    if (own_seed == 0u)
    {
        tex_uav[pixel] = float4(0, 0, 0, 0);
        return;
    }

    uint duplicates = 0u;
    for (int dy = -DUPLICATION_WINDOW_RADIUS; dy <= DUPLICATION_WINDOW_RADIUS; dy++)
    {
        for (int dx = -DUPLICATION_WINDOW_RADIUS; dx <= DUPLICATION_WINDOW_RADIUS; dx++)
        {
            if (dx == 0 && dy == 0)
                continue;

            int2 p = int2(pixel) + int2(dx, dy);
            if (p.x < 0 || p.y < 0 || p.x >= (int)resolution_x || p.y >= (int)resolution_y)
                continue;

            if (asuint(tex[p].x) == own_seed)
            {
                duplicates++;
            }
        }
    }

    tex_uav[pixel] = float4(float(duplicates) / 288.0f, 0, 0, 0);
}
