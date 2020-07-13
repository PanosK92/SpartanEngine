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

//= INCLUDES ===========
#include "Common.hlsl"
#include "Velocity.hlsl"
//======================

float3 Reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 ReinhardInverse(float3 sdr, float k = 1.0)
{
    return k * sdr / (k - sdr);
}

// From "Temporal Reprojection Anti-Aliasing"
// https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
#if USE_OPTIMIZATIONS
    // note: only clips towards aabb center (but fast!)
    float3 p_clip = 0.5f * (aabb_max + aabb_min);
    float3 e_clip = 0.5f * (aabb_max - aabb_min) + EPSILON;

    float3 v_clip = q - p_clip;
    float3 v_unit = v_clip.xyz / e_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return p_clip + v_clip / ma_unit;
    else
        return q;// point inside aabb
#else
    float3 r = q - p;
    float3 rmax = aabb_max - p.xyz;
    float3 rmin = aabb_min - p.xyz;

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
#endif
}

float4 TemporalAntialiasing(uint2 thread_id, Texture2D tex_history, Texture2D tex_current)
{
    // Get history and current colors
    const float2 uv         = (thread_id + 0.5f) / g_resolution;
    float2 velocity         = GetVelocity_DepthMin(uv);
    float2 uv_reprojected   = uv - velocity;
    float3 color_history    = tex_history.SampleLevel(sampler_bilinear_clamp, uv_reprojected, 0).rgb;
    float3 color_current    = tex_current[thread_id].rgb;
      
    //= History clipping ===============================================================================================
    uint2 du = uint2(1, 0);
    uint2 dv = uint2(0, 1);

    float3 ctl = tex_current[thread_id - dv - du].rgb;
    float3 ctc = tex_current[thread_id - dv].rgb;
    float3 ctr = tex_current[thread_id - dv + du].rgb;
    float3 cml = tex_current[thread_id - du].rgb;
    float3 cmc = color_current;
    float3 cmr = tex_current[thread_id + du].rgb;
    float3 cbl = tex_current[thread_id + dv - du].rgb;
    float3 cbc = tex_current[thread_id + dv].rgb;
    float3 cbr = tex_current[thread_id + dv + du].rgb;

    float3 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
    float3 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
    float3 color_avg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0f;

    // Clip history to the neighbourhood of the current sample
    color_history = saturate_16(clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history));
    //==================================================================================================================
    
    // Decrease blend factor when motion gets sub-pixel
    const float threshold   = 0.5f;
    const float base        = 40.0f;
    const float gather      = 0.01f;
    float depth             = get_linear_depth(uv);
    float texel_vel_mag     = length(velocity) * depth;
    float subpixel_motion   = saturate(threshold / (FLT_MIN + texel_vel_mag));
    float factor_subpixel   = texel_vel_mag * base + subpixel_motion * gather;
    
    // Tonemap
    color_history = Reinhard(color_history);
    color_current = Reinhard(color_current);
    
    // Decrease blend factor when contrast is high
    float lum0              = luminance(color_current);
    float lum1              = luminance(color_history);
    float factor_contrast   = 1.0f - abs(lum0 - lum1);
    factor_contrast         = factor_contrast * factor_contrast;

    // Compute blend factor
    float blend_factor = saturate(factor_subpixel * factor_contrast);
    
    // Use max blend if the re-projected uv is out of screen
    blend_factor = is_saturated(uv_reprojected) ? blend_factor : 1.0f;

    // Resolve
    float3 resolved = lerp(color_history, color_current, blend_factor);
    
    // Inverse tonemap
    resolved = ReinhardInverse(resolved);
    
    return float4(resolved, 1.0f);
}

[numthreads(thread_group_count, thread_group_count, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;
    
    tex_out_rgba[thread_id.xy] = TemporalAntialiasing(thread_id.xy, tex, tex2);
}
