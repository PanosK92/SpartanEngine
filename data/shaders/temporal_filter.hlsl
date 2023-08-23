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

/*------------------------------------------------------------------------------
                              HISTORY CLIPPING
------------------------------------------------------------------------------*/

// based on https://github.com/playdeadgames/temporal
float4 clip_aabb(float4 aabb_min, float4 aabb_max, float4 p, float4 q)
{
    float4 r    = q - p;
    float3 rmax = (aabb_max.xyz - p.xyz);
    float3 rmin = (aabb_min.xyz - p.xyz);

    if (r.x > rmax.x + FLT_MIN)
        r *= (rmax.x / r.x);
    if (r.y > rmax.y + FLT_MIN)
        r *= (rmax.y / r.y);
    if (r.z > rmax.z + FLT_MIN)
        r *= (rmax.z / r.z);

    if (r.x < rmin.x - FLT_MIN)
        r *= (rmin.x / r.x);
    if (r.y < rmin.y - FLT_MIN)
        r *= (rmin.y / r.y);
    if (r.z < rmin.z - FLT_MIN)
        r *= (rmin.z / r.z);

    return p + r;
}

static const int2 kOffsets3x3[9] =
{
    int2(-1, -1),
    int2(0, -1),
    int2(1, -1),
    int2(-1, 0),
    int2(0, 0),
    int2(1, 0),
    int2(-1, 1),
    int2(0, 1),
    int2(1, 1),
};

// clip history to the neighbourhood of the current sample
float4 clip_history_3x3(uint2 pos, float4 color_history, float2 velocity_closest)
{
    // sample a 3x3 neighbourhood
    float4 s1 = tex_uav2[pos + kOffsets3x3[0]];
    float4 s2 = tex_uav2[pos + kOffsets3x3[1]];
    float4 s3 = tex_uav2[pos + kOffsets3x3[2]];
    float4 s4 = tex_uav2[pos + kOffsets3x3[3]];
    float4 s5 = tex_uav2[pos + kOffsets3x3[4]];
    float4 s6 = tex_uav2[pos + kOffsets3x3[5]];
    float4 s7 = tex_uav2[pos + kOffsets3x3[6]];
    float4 s8 = tex_uav2[pos + kOffsets3x3[7]];
    float4 s9 = tex_uav2[pos + kOffsets3x3[8]];

    // compute min and max (with an adaptive box size, which greatly reduces ghosting)
    float4 color_avg  = (s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8 + s9) * RPC_9;
    float4 color_avg2 = ((s1 * s1) + (s2 * s2) + (s3 * s3) + (s4 * s4) + (s5 * s5) + (s6 * s6) + (s7 * s7) + (s8 * s8) + (s9 * s9)) * RPC_9;
    float box_size    = lerp(0.0f, 2.5f, smoothstep(0.02f, 0.0f, length(velocity_closest)));
    float4 dev        = sqrt(abs(color_avg2 - (color_avg * color_avg))) * box_size;
    float4 color_min  = color_avg - dev;
    float4 color_max  = color_avg + dev;

    // variance clipping
    float4 color = clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history);

    // clamp to prevent NaNs
    return saturate_16(color);
}

/*------------------------------------------------------------------------------
                               INPUT BLEND FACTOR
------------------------------------------------------------------------------*/

float get_factor_dissoclusion(float2 uv_reprojected, float2 velocity)
{
    float2 velocity_previous = tex_velocity_previous[uv_reprojected * buffer_frame.resolution_render].xy;
    float dissoclusion = length(velocity_previous - velocity);

    return saturate(dissoclusion * 1000.0f);
}

float compute_blend_factor(float2 uv_reprojected, float2 velocity)
{
    float blend_factor        = RPC_32;                                            // accumulate 32 samples
    float factor_screen_edge  = !is_saturated(uv_reprojected);                     // if re-projected UV is out of screen, reject history
    float factor_dissoclusion = get_factor_dissoclusion(uv_reprojected, velocity); // if there is dissoclusion, reject history
    
    return saturate(blend_factor + factor_screen_edge + factor_dissoclusion);
}

/*------------------------------------------------------------------------------
                               ACCUMULATION
------------------------------------------------------------------------------*/

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    // get reprojected uv
    float2 uv             = (thread_id.xy + 0.5f) / pass_get_resolution_out();
    float2 velocity       = tex_velocity[thread_id.xy].xy;
    float2 uv_reprojected = uv - velocity;

    // clip history
    float4 history  = tex_uav[uv_reprojected * buffer_frame.resolution_render];
    history         = clip_history_3x3(thread_id.xy, history, velocity);

    // accumulate
    float4 input_sample   = tex_uav2[thread_id.xy];
    tex_uav[thread_id.xy] = lerp(history, input_sample, compute_blend_factor(uv_reprojected, velocity));
}
