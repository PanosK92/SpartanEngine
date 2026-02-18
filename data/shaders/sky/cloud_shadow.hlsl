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

// cloud shadow map - projects cloud density from sun's perspective
// note: shadows are static (no wind animation) for stability

#include "../common.hlsl"

static const float cloud_scale  = 0.00003;
static const float detail_scale = 0.0003;
static const float seed         = 1.0;

float hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031 + seed * 0.1);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float smooth_noise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash21(i), hash21(i + float2(1, 0)), u.x),
                lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), u.x), u.y);
}

void get_cloud_bounds(float2 xz, out float bottom, out float top)
{
    float2 coord = xz * 0.00005;
    bottom = lerp(1200.0, 2200.0, smooth_noise(coord));
    top = max(lerp(3500.0, 4500.0, smooth_noise(coord * 1.7 + 100.0)), bottom + 1500.0);
}

float3 domain_warp(float3 p)
{
    float sp = seed * 0.31415926;
    return p + float3(sin(p.z * 0.00008 + p.y * 0.00007 + sp) * 2000.0,
                      sin(p.x * 0.00007 + p.z * 0.00008 + sp) * 500.0,
                      sin(p.y * 0.00006 + p.x * 0.00009 + sp) * 2000.0);
}

float sample_density(float3 pos, float coverage, float bottom, float top)
{
    if (pos.y < bottom || pos.y > top)
        return 0.0;

    float h = (pos.y - bottom) / (top - bottom);
    float ctype = saturate(lerp(0.3, 0.85, coverage));

    // static position (no wind animation for shadow stability)
    float3 seed_pos = pos + float3(seed * 50000.0, seed * 30000.0, seed * 70000.0);
    float3 warped = domain_warp(seed_pos);

    // shape noise with height gradient
    float4 shape = tex3d_cloud_shape.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), warped * cloud_scale, 0);
    float noise = dot(shape.rgb, float3(0.625, 0.25, 0.125));
    noise *= smoothstep(0.0, 0.1, h) * smoothstep(0.4 + ctype * 0.6, 0.2 + ctype * 0.3, h);

    float density = saturate((noise - (1.0 - coverage)) / max(coverage, 0.0001));
    if (density < 0.01)
        return 0.0;

    // detail erosion
    float4 detail = tex3d_cloud_detail.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap),
                                                    domain_warp(seed_pos * 1.1) * detail_scale, 0);
    return saturate(density - dot(detail.rgb, float3(0.625, 0.25, 0.125)) * (1.0 - density) * 0.35);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    float2 dims;
    tex_uav.GetDimensions(dims.x, dims.y);
    if (any(tid.xy >= dims))
        return;

    float3 light_dir = -light_parameters[0].direction;
    float coverage = buffer_frame.cloud_coverage;

    // early out
    if (coverage <= 0.0 || buffer_frame.cloud_shadows <= 0.0 || light_dir.y <= 0.0)
    {
        tex_uav[tid.xy] = 1.0;
        return;
    }

    // shadow map covers 10km x 10km area
    float shadow_map_size = 10000.0;
    float texel_size = shadow_map_size / dims.x;
    
    // snap center to texel grid for stability
    float2 snapped_center = floor(buffer_frame.camera_position.xz / texel_size) * texel_size;
    
    // convert uv to world position
    float2 uv = (float2(tid.xy) + 0.5) / dims;
    float2 world_xz = (uv - 0.5) * shadow_map_size + snapped_center;

    // get cloud bounds
    float bottom, top;
    get_cloud_bounds(world_xz, bottom, top);

    // raymarch through cloud layer (no jitter for stability)
    float slant = min(1.0 / max(light_dir.y, 0.1), 3.0);
    float ray_len = (top - bottom) * slant;
    float step_size = ray_len / 8.0;
    
    float3 ray_start = float3(world_xz.x, 0.0, world_xz.y) + light_dir * (bottom / max(light_dir.y, 0.001));
    float optical_depth = 0.0;

    [loop] for (int i = 0; i < 8; i++)
    {
        float3 pos = ray_start + light_dir * (float(i) + 0.5) * step_size;
        optical_depth += sample_density(pos, coverage, bottom, top) * step_size * 0.001;
    }

    // beer-lambert with soft contrast
    float shadow = exp(-optical_depth * buffer_frame.cloud_shadows * 2.5);
    shadow = shadow * shadow * (3.0 - 2.0 * shadow);
    tex_uav[tid.xy] = shadow;
}
