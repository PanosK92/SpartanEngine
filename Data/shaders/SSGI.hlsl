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

static const uint g_ssgi_directions         = 2;
static const uint g_ssgi_steps              = 6;
static const float g_ssgi_radius            = 5.0f;
static const float g_ssgi_bounce_intensity  = 10.0f;
static const float g_ssgi_samples           = (float)(g_ssgi_directions * g_ssgi_steps);
static const float g_ssgi_radius2           = g_ssgi_radius * g_ssgi_radius;

float falloff(float distance_squared)
{
    return saturate(1.0f - distance_squared / g_ssgi_radius2);
}

float compute_occlusion(float3 center_normal, float3 center_to_sample, float distance_squared, float attunate)
{
    return saturate(dot(center_normal, center_to_sample) / sqrt(distance_squared)) * attunate;
}

float3 compute_light(float3 center_normal, float3 center_to_sample, float distance_squared, float attunate, float2 sample_uv, inout uint indirect_light_samples)
{
    float3 indirect = 0.0f;
    
    // Compute falloff
    attunate = attunate * screen_fade(sample_uv);
    
    [branch]
    if (attunate > 0.0f)
	{
	    // Reproject light
	    float2 velocity         = GetVelocity_DepthMin(sample_uv);
	    float2 uv_reprojected   = sample_uv - velocity;
	    float3 light            = tex_light_diffuse.SampleLevel(sampler_bilinear_clamp, uv_reprojected, 0).rgb * attunate;
	    
	    // Transport
	    [branch]
	    if (luminance(light) > 0.0f)
	    {
	    	float distance      = clamp(sqrt(distance_squared), 0.1, 50);
	    	float attunation    = clamp(1.0 / (distance), 0, 50);
	    	float occlusion     = saturate(dot(center_normal, center_to_sample)) * attunation;
	    
	    	[branch]
	    	if (occlusion > 0.0f)
	    	{
	    		float3 sample_normal    = get_normal_view_space(sample_uv);
	    		float visibility        = saturate(dot(sample_normal, -center_to_sample));
	    	
	    		indirect = light * visibility * occlusion;
	    		indirect_light_samples++;
	    	}
	    }
    }   

    return indirect;
}

float3 ssgi(float2 uv, float3 position, float3 normal)
{
    float3 light        = 0.0f;
    uint light_samples  = 0;
    
    float radius_pixels = max((g_ssgi_radius * g_resolution.x * 0.5f) / position.z, (float)g_ssgi_steps);
    radius_pixels       = radius_pixels / (g_ssgi_steps + 1); // divide by ao_steps + 1 so that the farthest samples are not fully attenuated
    float rotation_step = PI2 / (float)g_ssgi_directions;

    // Offsets (noise over space and time)
    float noise_gradient_temporal   = interleaved_gradient_noise(uv * g_resolution);
    float offset_spatial            = noise_spatial_offset(uv * g_resolution);
    float offset_temporal           = noise_temporal_offset();
    float offset_rotation_temporal  = noise_temporal_direction();
    float ray_offset                = frac(offset_spatial + offset_temporal) + (random(uv) * 2.0 - 1.0) * 0.25;
    
    [unroll]
    for (uint direction_index = 0; direction_index < g_ssgi_directions; direction_index++)
    {
        float rotation_angle        = (direction_index + noise_gradient_temporal + offset_rotation_temporal) * rotation_step;
        float2 rotation_direction   = float2(cos(rotation_angle), sin(rotation_angle)) * g_texel_size;

        [unroll]
        for (uint step_index = 0; step_index < g_ssgi_steps; ++step_index)
        {
            float2 uv_offset        = max(radius_pixels * (step_index + ray_offset), 1 + step_index) * rotation_direction;
            float2 sample_uv        = uv + uv_offset;
            float3 sample_position  = get_position_view_space(sample_uv);
			float3 center_to_sample = sample_position - position;
			float distance_squared  = dot(center_to_sample, center_to_sample);
            center_to_sample        = normalize(center_to_sample);
			float attunation 		= falloff(distance_squared);
			
			[branch]
            if (attunation != 0.0f)
			{
				light += compute_light(normal, center_to_sample, distance_squared, attunation, sample_uv, light_samples);
            }
        }
    }

    return saturate(light * g_ssgi_bounce_intensity / (float(light_samples) + FLT_MIN));
}

[numthreads(thread_group_count, thread_group_count, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;
    
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;
    float3 position = get_position_view_space(uv);
    float3 normal   = get_normal_view_space(uv);
    
    // Diffuse
    float3 light = ssgi(uv, position, normal);

    // Specular
    [branch]
    if (g_ssr_enabled)
    {
        float2 sample_ssr = tex_ssr.Load(int3(thread_id.xy, 0)).xy;
        
        [branch]
        if (all(sample_ssr))
        {
            float roughness = tex_material.Load(int3(thread_id.xy, 0)).r;
            float fade = 1.0f - roughness; // fade with roughness as we don't have blurry screen space reflections yet
            light += tex_light_specular.SampleLevel(sampler_point_clamp, sample_ssr, 0).rgb * fade;
        }
    }

    tex_out_rgb[thread_id.xy] = light;
}

