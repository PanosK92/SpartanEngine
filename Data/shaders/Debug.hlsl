/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Common.hlsl"
//====================

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;
    
    float4 color = has_uav() ? tex_out_rgba2[thread_id.xy] : tex[thread_id.xy];

    if (needs_packing())
    {
        color.rgb = pack(color.rgb);
    }

    if (needs_gamma_correction())
    {
        color.rgb = gamma(color.rgb);
    }
  
    if (needs_channel_r())
    {
        color = float4(color.rrr, 1.0f);
    }

    if (needs_channel_a())
    {
        color = float4(color.aaa, 1.0f);
    }

    if (needs_channel_rg())
    {
        color = float4(color.r, color.g, 0.0f, 1.0f);
    }

    if (needs_channel_rgb())
    {
        color = float4(color.rgb, 1.0f);
    }

    if (needs_abs())
    {
        color = abs(color);
    }
    
    if (needs_boost())
    {
        color.rgb *= 10.0f;
    }
    
    tex_out_rgba[thread_id.xy] = color;
}
