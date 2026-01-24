/*
Copyright(c) 2015-2025 Panos Karabelas

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

// ============================================================================
// Cloud Shadow Map Generation
// Projects cloud density onto a 2D shadow map from the sun's perspective
// ============================================================================

//= INCLUDES =========
#include "../common.hlsl"
//====================

// Cloud layer base constants - must match skysphere.hlsl
static const float cloud_base_bottom = 1500.0;
static const float cloud_base_top    = 4500.0;
static const float cloud_scale       = 0.00003;
static const float detail_scale      = 0.0003;
static const float cloud_wind_speed  = 10.0;

// Hash function for seed-based generation - must match skysphere.hlsl
float hash21(float2 p, float seed)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031 + seed * 0.1);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float remap(float value, float low1, float high1, float low2, float high2)
{
    return low2 + (value - low1) * (high2 - low2) / max(high1 - low1, 0.0001);
}

float get_height_gradient(float h, float cloud_type)
{
    return smoothstep(0.0, 0.1, h) * smoothstep(0.4 + cloud_type * 0.6, 0.2 + cloud_type * 0.3, h);
}

// Smooth noise for height variation - must match skysphere.hlsl
float smooth_noise(float2 p, float seed)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    
    float a = hash21(i + float2(0, 0), seed);
    float b = hash21(i + float2(1, 0), seed);
    float c = hash21(i + float2(0, 1), seed);
    float d = hash21(i + float2(1, 1), seed);
    
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Get local cloud height bounds - must match skysphere.hlsl
void get_local_cloud_bounds(float3 pos, float seed, out float local_bottom, out float local_top)
{
    float2 coord = pos.xz * 0.00005;
    
    float height_noise1 = smooth_noise(coord, seed);
    float height_noise2 = smooth_noise(coord * 1.7 + 100.0, seed * 2.3);
    
    local_bottom = lerp(1200.0, 2200.0, height_noise1);
    local_top = lerp(3500.0, 4500.0, height_noise2);
    local_top = max(local_top, local_bottom + 1500.0);
}

// Seed-based coordinate transformation - must match skysphere.hlsl
float3 seed_transform(float3 p, float seed)
{
    float3 offset = float3(seed * 50000.0, seed * 30000.0, seed * 70000.0);
    return p + offset;
}

// domain warping - must match skysphere.hlsl
float3 domain_warp(float3 p, float seed)
{
    float sp = seed * 0.31415926;
    
    float3 warp;
    warp.x = sin(p.z * 0.00008 + p.y * 0.00007 + sp) * 2000.0;
    warp.y = sin(p.x * 0.00007 + p.z * 0.00008 + sp) * 500.0;
    warp.z = sin(p.y * 0.00006 + p.x * 0.00009 + sp) * 2000.0;
    
    return p + warp;
}

// Sample cloud density - must match skysphere.hlsl logic exactly
float sample_cloud_density(float3 world_pos, float time, float seed)
{
    // Get local cloud height bounds
    float local_bottom, local_top;
    get_local_cloud_bounds(world_pos, seed, local_bottom, local_top);
    float local_thickness = local_top - local_bottom;
    
    if (world_pos.y < local_bottom || world_pos.y > local_top)
        return 0.0;
    
    float h = (world_pos.y - local_bottom) / local_thickness;
    
    // Wind animation
    float3 wind = buffer_frame.wind;
    float wind_speed = length(wind) * cloud_wind_speed;
    float3 wind_dir = wind_speed > 0.001 ? normalize(wind) : float3(1, 0, 0);
    float3 anim_pos = world_pos + wind_dir * time * wind_speed;
    
    // Apply seed-based transformation
    float3 seed_pos = seed_transform(anim_pos, seed);
    
    // Domain warp
    float3 warped_pos = domain_warp(seed_pos, seed);
    
    // Sample shape noise
    float3 shape_uvw = warped_pos * cloud_scale;
    float4 shape = tex3d_cloud_shape.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), shape_uvw, 0);
    
    float noise = shape.r * 0.625 + shape.g * 0.25 + shape.b * 0.125;
    noise *= get_height_gradient(h, buffer_frame.cloud_type);
    
    float coverage = buffer_frame.cloud_coverage;
    float density = saturate(remap(noise, 1.0 - coverage, 1.0, 0.0, 1.0));
    
    if (density < 0.01)
        return 0.0;
    
    // Detail erosion
    float3 detail_uvw = domain_warp(seed_pos * 1.1, seed) * detail_scale;
    float4 detail = tex3d_cloud_detail.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), detail_uvw, 0);
    float detail_noise = detail.r * 0.625 + detail.g * 0.25 + detail.b * 0.125;
    
    density = saturate(density - detail_noise * (1.0 - density) * 0.35);
    return density;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 shadow_dims;
    tex_uav.GetDimensions(shadow_dims.x, shadow_dims.y);
    
    if (any(thread_id.xy >= shadow_dims))
        return;
    
    // Get directional light direction (first light in the buffer)
    float3 light_dir = -light_parameters[0].direction;
    
    // skip if clouds are disabled or no sun
    if (buffer_frame.cloud_coverage <= 0.0 || buffer_frame.cloud_shadows <= 0.0)
    {
        tex_uav[thread_id.xy] = 1.0;
        return;
    }
    
    // Get seed for consistent cloud generation
    float seed = buffer_frame.cloud_seed;
    
    // Convert shadow map UV to world position
    float shadow_map_size = 10000.0; // meters (10km x 10km area)
    float2 uv = (float2(thread_id.xy) + 0.5) / shadow_dims;
    float2 world_xz = (uv - 0.5) * shadow_map_size + buffer_frame.camera_position.xz;
    
    // March direction: towards sun
    float3 march_dir = normalize(light_dir);
    
    // if sun is below horizon, no shadows
    if (march_dir.y <= 0.0)
    {
        tex_uav[thread_id.xy] = 1.0;
        return;
    }
    
    // Time for wind animation
    float time = (float)buffer_frame.time * 0.001;
    
    // raymarch through cloud layer
    const int shadow_steps = 8;
    
    // use local cloud bounds at sample position for accurate shadow calculation
    // approximate center position for bounds lookup
    float3 approx_center = float3(world_xz.x, (cloud_base_bottom + cloud_base_top) * 0.5, world_xz.y);
    float local_bottom, local_top;
    get_local_cloud_bounds(approx_center, seed, local_bottom, local_top);
    float cloud_thickness = local_top - local_bottom;
    
    // use temporal jitter
    float2 screen_pos = float2(thread_id.xy);
    float jitter = noise_interleaved_gradient(screen_pos, true);
    
    // calculate the slant distance through cloud layer
    float slant_factor = 1.0 / max(march_dir.y, 0.1);
    float ray_length = cloud_thickness * min(slant_factor, 3.0);
    float step_size = ray_length / float(shadow_steps);
    
    // single sample for performance (temporal accumulation handles noise)
    float total_shadow = 0.0;
    const int num_samples = 1;
    const float2 offsets[1] = { float2(0.0, 0.0) };
    
    float sample_spread = 0.0;
    
    [unroll]
    for (int s = 0; s < num_samples; s++)
    {
        float2 sample_xz = world_xz + offsets[s] * sample_spread;
        
        // start position: trace from ground to cloud layer using local bounds
        float3 ground_pos = float3(sample_xz.x, 0.0, sample_xz.y);
        float t_to_bottom = local_bottom / max(march_dir.y, 0.001);
        float3 ray_start = ground_pos + march_dir * t_to_bottom;
        
        float optical_depth = 0.0;
        
        [loop]
        for (int i = 0; i < shadow_steps; i++)
        {
            float sample_jitter = frac(jitter + float(s) * 0.25);
            float t = (float(i) + sample_jitter) * step_size;
            float3 sample_pos = ray_start + march_dir * t;
            
            // Sample with seed
            float density = sample_cloud_density(sample_pos, time, seed);
            optical_depth += density * step_size * 0.001;
        }
        
        // Beer-Lambert attenuation
        float shadow = exp(-optical_depth * buffer_frame.cloud_shadows * 2.5);
        total_shadow += shadow;
    }
    
    // Average samples
    float shadow = total_shadow / float(num_samples);
    
    // soft contrast curve
    shadow = saturate(shadow);
    shadow = shadow * shadow * (3.0 - 2.0 * shadow);
    
    tex_uav[thread_id.xy] = shadow;
}
