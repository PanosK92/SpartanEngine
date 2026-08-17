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

//= INCLUDES ============
#include "fog.hlsl"
#include "sky/clouds.hlsl"
//=======================

#if defined(FOG_INJECT)

float3 fog_sky_ambient(float3 ray_direction)
{
    const uint sky_mip = 7;
    float3 light_dir = float3(0.0f, 1.0f, 0.0f);
    if (buffer_frame.cluster_light_count > 0u)
    {
        light_dir = normalize(-light_parameters[0].direction);
    }

    float3 sun_sample_dir = normalize(float3(light_dir.x, max(light_dir.y, 0.0f), light_dir.z));
    float3 sky_sun = tex.SampleLevel(
        GET_SAMPLER(sampler_trilinear_clamp),
        direction_sphere_uv(sun_sample_dir),
        sky_mip
    ).rgb;
    float3 sky_zenith = tex.SampleLevel(
        GET_SAMPLER(sampler_trilinear_clamp),
        direction_sphere_uv(float3(0.0f, 1.0f, 0.0f)),
        sky_mip
    ).rgb;

    const float sky_color_max = 50.0f;
    sky_sun    = min(sky_sun, sky_color_max);
    sky_zenith = min(sky_zenith, sky_color_max);

    float sun_lobe = pow(saturate(dot(ray_direction, light_dir)), 4.0f);
    return lerp(sky_zenith, sky_sun, sun_lobe);
}

float3 fog_evaluate_light(
    uint light_index,
    float3 sample_pos,
    float3 ray_direction,
    uint3 thread_id,
    float2 uv,
    bool in_water,
    float2 caustic_xz,
    float sigma_s
)
{
    uint2 pixel = thread_id.xy;
    Surface surface = fog_build_surface(sample_pos, ray_direction, pixel, uv);
    Light light;
    light.Build(light_index, surface);
    if (!light.is_volumetric())
    {
        return 0.0f;
    }

    float3 light_dir;
    float local_atten;
    compute_volumetric_light_sample(light, sample_pos, light_dir, local_atten);
    if (local_atten <= 0.0f)
    {
        return 0.0f;
    }

    float phase_g = light.is_directional() ? 0.4f : 0.6f;
    float phase = henyey_greenstein_phase(dot(ray_direction, light_dir), phase_g);
    float visibility = 1.0f;
    if (light.has_shadows())
    {
    #ifdef RAY_TRACING_ENABLED
        if (is_ray_traced_shadows_enabled())
        {
            // far froxels cover hundreds of meters, a shadow ray per voxel is wasted
            if (thread_id.z < (fog_depth * 3u) / 4u)
            {
                visibility = fog_trace_shadow(light, sample_pos);
            }
        }
        else
    #endif
        {
            visibility = visible(sample_pos, light, pixel);
        }
    }

    float3 tint = 1.0f;
    if (light.is_directional() && visibility > 0.0f)
    {
        visibility *= cloud_shadow_sample(
            tex5,
            GET_SAMPLER(sampler_bilinear_clamp),
            sample_pos,
            light_dir,
            get_camera_position()
        );

        if (in_water)
        {
            float water_y = get_ocean_height(caustic_xz);
            float sun_path = (water_y - sample_pos.y) / max(light_dir.y, 0.05f);
            float2 entry_xz = caustic_xz + light_dir.xz * sun_path;
            tint = exp(-ocean_extinction * sun_path) * get_ocean_caustic(entry_xz, sun_path);
        }
    }

    return light.color * light.intensity * local_atten * visibility * phase * tint * sigma_s;
}

[numthreads(8, 8, 4)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= fog_width || thread_id.y >= fog_height || thread_id.z >= fog_depth)
    {
        return;
    }

    float3 sample_pos = fog_froxel_world(float3(thread_id));
    float3 ray_direction = normalize(sample_pos - get_camera_position());
    float2 uv = (float2(thread_id.xy) + 0.5f) / float2((float)fog_width, (float)fog_height);

    bool camera_underwater = fog_camera_underwater();
    float water_y = 0.0f;
    bool in_water = false;
    if (camera_underwater)
    {
        water_y = get_ocean_height(sample_pos.xz);
        in_water = sample_pos.y < water_y;
    }

    float ground_y = buffer_frame.ocean_enabled > 0.5f ? buffer_frame.ocean_sea_level : 0.0f;
    float height_world = max(sample_pos.y - ground_y, 0.0f);
    float height_sigma = pass_get_f3_value().y * fog_density_scale
        * exp(-height_world / fog_scale_height);
    float water_sigma = 0.0f;
    if (in_water)
    {
        water_sigma = dot(ocean_extinction, float3(0.2126f, 0.7152f, 0.0722f))
            * buffer_frame.ocean_turbidity;
    }
    float sigma_s = max(height_sigma + water_sigma, 0.0f);
    float sigma_t = sigma_s;

    float3 scatter_rate = 0.0f;
    if (sigma_s > 0.0f)
    {
        if (in_water)
        {
            float3 sky_down = tex.SampleLevel(
                GET_SAMPLER(sampler_trilinear_clamp),
                direction_sphere_uv(float3(0.0f, 1.0f, 0.0f)),
                7.0f
            ).rgb;
            float3 downwelling = get_sun_radiance()
                * saturate(-light_parameters[0].direction.y)
                * (1.0f / PI) + sky_down;
            scatter_rate += ocean_scatter_albedo * downwelling * water_sigma;
        }
        else
        {
            scatter_rate += fog_sky_ambient(ray_direction) * height_sigma;
        }

        if (buffer_frame.cluster_light_count > 0u)
        {
            scatter_rate += fog_evaluate_light(
                0u,
                sample_pos,
                ray_direction,
                thread_id,
                uv,
                in_water,
                sample_pos.xz,
                sigma_s
            );
        }

        uint volumetric_count = buffer_frame.volumetric_light_count;
        [loop]
        for (uint k = 0u; k < volumetric_count; k++)
        {
            uint light_index = volumetric_light_indices[k];
            scatter_rate += fog_evaluate_light(
                light_index,
                sample_pos,
                ray_direction,
                thread_id,
                uv,
                in_water,
                sample_pos.xz,
                sigma_s
            );
        }
    }

    float4 current = float4(scatter_rate, sigma_t);

    bool reset_history = pass_get_f3_value().x > 0.5f;
    float4 result = current;
    if (!reset_history)
    {
        float4 prev_clip = mul(
            float4(sample_pos, 1.0f),
            get_view_projection_previous_unjittered()
        );
        if (prev_clip.w > 0.0f)
        {
            float3 prev_ndc = prev_clip.xyz / prev_clip.w;
            float2 prev_uv = ndc_to_uv(prev_ndc.xy);
            float prev_dist = length(sample_pos - buffer_frame.camera_position_previous);
            float prev_u = fog_distance_to_slice(prev_dist);
            float prev_w = (prev_u * ((float)fog_depth - 1.0f) + 0.5f) / (float)fog_depth;
            if (is_valid_uv(prev_uv) && prev_u > 0.0f && prev_u < 1.0f)
            {
                float4 history = tex3d.SampleLevel(
                    GET_SAMPLER(sampler_trilinear_clamp),
                    float3(prev_uv, prev_w),
                    0.0f
                );
                float2 uv_delta = prev_uv - uv;
                float froxel_motion = length(
                    uv_delta * float2((float)fog_width, (float)fog_height)
                );
                float keep = lerp(0.97f, 0.55f, saturate(froxel_motion * 0.5f));
                result = lerp(current, history, keep);
            }
        }
    }

    tex3d_uav[thread_id] = result;
}

#elif defined(FOG_INTEGRATE)

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= fog_width || thread_id.y >= fog_height)
    {
        return;
    }

    float transmittance = 1.0f;
    float3 inscatter = 0.0f;

    [loop]
    for (uint z = 0u; z < fog_depth; z++)
    {
        // inject already temporally filters, a 3x3 here is 9x the bandwidth
        float4 scatter = tex3d[uint3(thread_id.xy, z)];
        float u0 = (float)z / (float)fog_depth;
        float u1 = (float)(z + 1u) / (float)fog_depth;
        float d0 = fog_slice_to_distance(u0);
        float d1 = fog_slice_to_distance(u1);
        float dt = max(d1 - d0, 0.001f);

        float sigma_t = scatter.a;
        float3 scatter_rate = scatter.rgb;
        float step_t = exp(-sigma_t * dt);
        inscatter += transmittance * scatter_rate * dt;
        transmittance *= step_t;
        tex3d_uav[uint3(thread_id.xy, z)] = float4(inscatter, transmittance);
    }
}

#endif
