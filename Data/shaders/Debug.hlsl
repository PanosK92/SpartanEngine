/*
Copyright(c) 2016-2020 Panos Karabelas

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
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;
    
    float4 color = 0.0f;
    
#if NORMAL
    float3 normal   = tex[thread_id.xy].rgb;
    normal          = pack(normal);
    color           = float4(normal, 1.0f);
#endif

#if VELOCITY
    float3 velocity = tex[thread_id.xy].rgb;
    velocity        = abs(velocity) * 20.0f;
    color           = float4(velocity, 1.0f);
#endif

#if R_CHANNEL
    float r = tex[thread_id.xy].r;
    color   = float4(r, r, r, 1.0f);
#endif

#if A_CHANNEL
    float a = tex[thread_id.xy].a;
    color   = float4(a, a, a, 1.0f);
#endif

#if RGB_CHANNEL_GAMMA_CORRECT
    float3 rgb  = tex[thread_id.xy].rgb;
    rgb         = gamma(rgb);
    color       = float4(rgb, 1.0f);
#endif
    
    tex_out_rgba[thread_id.xy] = color;
}
