/*
Copyright(c) 2016-2024 Panos Karabelas

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
                           TONEMAPPING
------------------------------------------------------------------------------*/
float3 reinhard(const float3 color)
{
    return color / (color + 1.0f);
}

float3 reinhard_inverse(const float3 color)
{
    return color / (1.0f - color);
}

/*------------------------------------------------------------------------------
                           NEIGHBOURHOOD OFFSETS
------------------------------------------------------------------------------*/
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

/*------------------------------------------------------------------------------
                                VELOCITY
------------------------------------------------------------------------------*/
void depth_test_min(uint2 screen_position, inout float min_depth, inout uint2 min_pos)
{
    float depth = tex_depth[screen_position].r;

    if (depth < min_depth)
    {
        min_depth = depth;
        min_pos   = screen_position;
    }
}

// returns velocity with closest depth
float2 get_closest_pixel_velocity_3x3(uint2 screen_position)
{
    float min_depth = 1.0f;
    uint2 min_pos   = 0;

    depth_test_min(screen_position + kOffsets3x3[0], min_depth, min_pos);
    depth_test_min(screen_position + kOffsets3x3[1], min_depth, min_pos);
    depth_test_min(screen_position + kOffsets3x3[2], min_depth, min_pos);
    depth_test_min(screen_position + kOffsets3x3[3], min_depth, min_pos);
    depth_test_min(screen_position + kOffsets3x3[4], min_depth, min_pos);
    depth_test_min(screen_position + kOffsets3x3[5], min_depth, min_pos);
    depth_test_min(screen_position + kOffsets3x3[6], min_depth, min_pos);
    depth_test_min(screen_position + kOffsets3x3[7], min_depth, min_pos);
    depth_test_min(screen_position + kOffsets3x3[8], min_depth, min_pos);

    // velocity out
    return tex_velocity[screen_position + min_pos].xy;
}

/*------------------------------------------------------------------------------
                             HISTORY PROCESSING
------------------------------------------------------------------------------*/
float4 sample_catmull_rom(Texture2D tex_history, float2 uv, float2 texture_size)
{
    // based on: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
    
    // scale UV coordinates by the texture size to work in texel space
    float2 sample_position = uv * texture_size;

    // calculate the center of the starting texel in a 4x4 grid. The grid's [1, 1] position.
    float2 texPos1 = floor(sample_position - 0.5f) + 0.5f;

    // calculate the fractional part of the original sample position relative to the starting texel.
    float2 fractional_offset = sample_position - texPos1;

    // calculate the Catmull-Rom weights for each axis using the fractional offset.
    float2 w0 = fractional_offset * (-0.5f + fractional_offset * (1.0f - 0.5f * fractional_offset));
    float2 w1 = 1.0f + fractional_offset * fractional_offset * (-2.5f + 1.5f * fractional_offset);
    float2 w2 = fractional_offset * (0.5f + fractional_offset * (2.0f - 1.5f * fractional_offset));
    float2 w3 = fractional_offset * fractional_offset * (-0.5f + 0.5f * fractional_offset);

    // combine weights for the middle two samples and calculate their combined offset.
    float2 combinedWeightMiddleSamples = w1 + w2;
    float2 combinedOffsetMiddleSamples = w2 / combinedWeightMiddleSamples;

    // calculate final UV coordinates for texture sampling.
    float2 texPos0      = (texPos1 - 1) / texture_size;
    float2 texPos3      = (texPos1 + 2) / texture_size;
    float2 texPosMiddle = (texPos1 + combinedOffsetMiddleSamples) / texture_size;

    // sample the texture at the calculated UV coordinates and accumulate the results.
    float4 result = 0.0f;
    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPosMiddle.x, texPos0.y), 0.0f) * combinedWeightMiddleSamples.x * w0.y;
    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPos0.x, texPosMiddle.y), 0.0f) * w0.x * combinedWeightMiddleSamples.y;
    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPosMiddle.x, texPosMiddle.y), 0.0f) * combinedWeightMiddleSamples.x * combinedWeightMiddleSamples.y;
    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPos3.x, texPosMiddle.y), 0.0f) * w3.x * combinedWeightMiddleSamples.y;

    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPosMiddle.x, texPos3.y), 0.0f) * combinedWeightMiddleSamples.x * w3.y;
    result += tex_history.SampleLevel(samplers[sampler_bilinear_clamp], float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

    return result;
}

// based on "temporal reprojection anti-Aliasing" - https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
    float3 r    = q - p;
    float3 rmax = (aabb_max - p.xyz);
    float3 rmin = (aabb_min - p.xyz);

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

float3 clip_history_to_neighbourhood_of_current_sample(float3 color_history, uint2 screen_position)
{
    // sample a 3x3 neighbourhood
    float3 s1 = tex2[screen_position + kOffsets3x3[0]].rgb;
    float3 s2 = tex2[screen_position + kOffsets3x3[1]].rgb;
    float3 s3 = tex2[screen_position + kOffsets3x3[2]].rgb;
    float3 s4 = tex2[screen_position + kOffsets3x3[3]].rgb;
    float3 s5 = tex2[screen_position + kOffsets3x3[4]].rgb;
    float3 s6 = tex2[screen_position + kOffsets3x3[5]].rgb;
    float3 s7 = tex2[screen_position + kOffsets3x3[6]].rgb;
    float3 s8 = tex2[screen_position + kOffsets3x3[7]].rgb;
    float3 s9 = tex2[screen_position + kOffsets3x3[8]].rgb;

    // compute min and max (with an adaptive box size, which greatly reduces ghosting)
    float3 color_avg        = (s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8 + s9) * RPC_9;
    float3 color_avg2       = ((s1 * s1) + (s2 * s2) + (s3 * s3) + (s4 * s4) + (s5 * s5) + (s6 * s6) + (s7 * s7) + (s8 * s8) + (s9 * s9)) * RPC_9;
    float2 velocity_closest = get_closest_pixel_velocity_3x3(screen_position);
    float box_size          = lerp(0.0f, 2.5f, smoothstep(0.02f, 0.0f, length(velocity_closest)));
    float3 dev              = sqrt(abs(color_avg2 - (color_avg * color_avg))) * box_size;
    float3 color_min        = color_avg - dev;
    float3 color_max        = color_avg + dev;

    // variance clipping
    float3 color = clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history);

    // clamp to prevent NaNs
    color = saturate_16(color);

    return color;
}

/*------------------------------------------------------------------------------
                                    TAA
------------------------------------------------------------------------------*/
float get_factor_dissoclusion(float2 uv_reprojected, float2 velocity)
{
    return 0.0f;
    
   //float2 velocity_previous = tex_velocity_previous[uv_reprojected * pass_get_resolution_out()].xy;
   //float dissoclusion       = length(velocity_previous - velocity) - 0.0001f;
   //
   //return saturate(dissoclusion * 5000.0f);
}

float3 temporal_antialiasing(uint2 screen_position)
{
    // reproject uv
    float2 velocity       = get_velocity_uv(screen_position);
    float2 uv             = (screen_position + 0.5f) / pass_get_resolution_out();
    float2 uv_reprojected = uv - velocity;

    // get input color
    float3 color_input   = tex2[screen_position].rgb;

    // get history color - not a raw sample though
    float3 color_history = sample_catmull_rom(tex, uv, pass_get_resolution_out()).rgb;                      // ghosting reduction: low
    color_history        = clip_history_to_neighbourhood_of_current_sample(color_history, screen_position); // ghosting reduction: high

    // compute blend factor
    float blend_factor = RPC_32; // accumulate as many jitter samples as generated, that is, 32
    {
        float factor_screen       = !is_valid_uv(uv_reprojected);                                 // if the re-projected UV is out of screen, converge to current color immediately
        float factor_dissoclusion = get_factor_dissoclusion(uv_reprojected, velocity);            // ghosting reduction: medium
        blend_factor              = saturate(blend_factor + factor_screen + factor_dissoclusion); // add to the blend factor
    }

    // resolve
    float3 color_resolved = 0.0f;
    {
        // tonemap
        color_history = reinhard(color_history);
        color_input   = reinhard(color_input);

        // lerp/blend
        color_resolved = lerp(color_history, color_input, blend_factor);

        // inverse tonemap
        color_resolved = reinhard_inverse(color_resolved);
    }

    return color_resolved;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;
    
    tex_uav[thread_id.xy].rgb = temporal_antialiasing(thread_id.xy);
}
