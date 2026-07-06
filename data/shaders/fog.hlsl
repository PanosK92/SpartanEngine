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

//= INCLUDES =========
#include "common.hlsl"
//====================

// atmospheric fog using exponential height based falloff
float get_fog_atmospheric(const float camera_to_pixel_length, const float pixel_height_world)
{
    float camera_height = get_camera_position().y;
    float density       = pass_get_f3_value().y * 0.00015f;
    float scale_height  = 50.0f; // lower is denser near ground, higher is more uniform
    float b             = 1.0f / scale_height;
    float delta_height  = pixel_height_world - camera_height;
    float dist          = camera_to_pixel_length;
    
    if (dist < 0.1f)
        return 0.0f;
    
    float rd_y = (abs(dist) > 1e-6f) ? delta_height / dist : 0.0f;
    float tau = 0.0f;
    
    if (abs(rd_y) < 1e-5f)
    {
        // horizontal ray approximation
        float base_density = density * exp(-camera_height * b);
        tau = base_density * dist;
    }
    else
    {
        // analytical optical depth integral, numerically stable
        float base_density = density * exp(-camera_height * b);
        float exponent     = -dist * rd_y * b;
        float exp_term     = 1.0f - exp(exponent);
        tau = base_density * exp_term / (b * rd_y);
    }

    // beer law, fog factor is the inscatter 1 - transmittance
    float transmittance = exp(-tau);
    float fog_factor    = 1.0f - transmittance;
    fog_factor = pow(fog_factor, 0.8f);
    
    return saturate(fog_factor);
}

// returns 1 if the world space position is lit by the light, 0 if occluded
// directional picks a single cascade per sample instead of paying for both
float visible(float3 position, Light light, uint2 pixel_pos)
{
    if (light.is_point())
    {
        // pick the cube face whose axis dominates the light to point vector
        float3 light_to_pixel = position - light.position;
        float3 abs_dir        = abs(light_to_pixel);
        uint face_index = (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z) ? (light_to_pixel.x > 0.0f ? 0u : 1u) :
                          (abs_dir.y >= abs_dir.z)                           ? (light_to_pixel.y > 0.0f ? 2u : 3u) :
                                                                               (light_to_pixel.z > 0.0f ? 4u : 5u);

        float4 clip_pos = mul(float4(position, 1.0f), light.transform[face_index]);
        if (clip_pos.w <= 0.0f)
            return 1.0f;

        float3 ndc          = clip_pos.xyz / clip_pos.w;
        float2 projected_uv = ndc_to_uv(ndc.xy);
        return light.compare_depth(float3(projected_uv, (float)face_index), ndc.z);
    }

    if (light.is_directional())
    {
        // try the near cascade first, fall back to the far cascade if outside its frustum
        const uint near_cascade = 0;
        const uint far_cascade  = 1;

        float3 projected_pos_near = world_to_ndc(position, light.transform[near_cascade]);
        float2 projected_uv_near  = ndc_to_uv(projected_pos_near);
        if (is_valid_uv(projected_uv_near))
        {
            return light.compare_depth(float3(projected_uv_near, (float)near_cascade), projected_pos_near.z);
        }

        float3 projected_pos_far = world_to_ndc(position, light.transform[far_cascade]);
        float2 projected_uv_far  = ndc_to_uv(projected_pos_far);
        if (is_valid_uv(projected_uv_far))
        {
            return light.compare_depth(float3(projected_uv_far, (float)far_cascade), projected_pos_far.z);
        }

        return 1.0f;
    }

    // spot or area light, both render a single perspective slice into the atlas
    float4 clip_pos = mul(float4(position, 1.0f), light.transform[0]);
    if (clip_pos.w <= 0.0f)
        return 1.0f;

    float3 projected_pos = clip_pos.xyz / clip_pos.w;
    float2 projected_uv  = ndc_to_uv(projected_pos.xy);
    if (!is_valid_uv(projected_uv))
        return 1.0f;

    return light.compare_depth(float3(projected_uv, 0.0f), projected_pos.z);
}

// henyey greenstein phase, g 0 isotropic, positive forward scatter, negative back scatter
float henyey_greenstein_phase(float cos_theta, float g)
{
    cos_theta     = clamp(cos_theta, -1.0f, 1.0f);
    float g2      = g * g;
    float denom   = max(1.0f + g2 - 2.0f * g * cos_theta, 1e-4f);
    // d * sqrt(d) instead of pow(d, 1.5)
    float denom32 = denom * sqrt(denom);
    return (1.0f - g2) / (4.0f * PI * denom32);
}

// local light direction and volumetric attenuation at a sample inside the medium
// area lights use the closest point on the rectangle, point and spot use the centroid
void compute_volumetric_light_sample(Light light, float3 sample_pos, out float3 light_dir, out float local_atten)
{
    if (light.is_directional())
    {
        light_dir   = normalize(-light.forward);
        local_atten = 1.0f;
        return;
    }

    // soft minimum distance so the inverse square does not blow up at the surface
    const float soft_radius = 0.05f;

    if (light.is_area())
    {
        float3 closest    = light.compute_closest_point_on_area(sample_pos);
        float3 to_light   = closest - sample_pos;
        float  dist       = length(to_light);
        light_dir         = (dist > 1e-4f) ? to_light / dist : light.forward;

        float dist_eff    = max(dist, soft_radius);
        float range_atten = light.compute_attenuation_range(dist);
        float emitter_area = 0.5f * max(light.area_width * light.area_height, 0.0001f);
        float emission_cos = saturate(dot(light.forward, -light_dir));
        local_atten        = min((range_atten / (dist_eff * dist_eff)) * emission_cos * emitter_area, PI);
        return;
    }

    // point or spot
    float3 to_light = light.position - sample_pos;
    float  dist     = length(to_light);
    light_dir       = (dist > 1e-4f) ? to_light / dist : float3(0.0f, 1.0f, 0.0f);

    float dist_eff    = max(dist, soft_radius);
    float range_atten = light.compute_attenuation_range(dist);
    local_atten       = range_atten / (dist_eff * dist_eff);

    if (light.is_spot())
    {
        // cos_outer and angle_scale are precomputed in Light::Build to keep this path trig free
        float cd          = dot(-light_dir, light.forward);
        float angle_atten = saturate((cd - light.cos_outer) * light.angle_scale);
        local_atten      *= angle_atten * angle_atten;
    }
}

// volumetric fog raymarch, single scattering with beer lambert transmittance
// works for any light type, the per type differences are isolated in compute_volumetric_light_sample and visible
float3 compute_volumetric_fog(Surface surface, Light light, uint2 pixel_pos)
{
    // sigma_s is the scattering coefficient, the sun uses a smaller one since its intensity
    // is orders of magnitude larger and would otherwise wash close geometry
    const float sigma_s_base   = pass_get_f3_value().y * 0.0012f;
    const float sigma_s        = light.is_directional() ? (sigma_s_base * 0.25f) : sigma_s_base;
    const float sigma_t        = sigma_s; // pure scattering, no absorption
    const float total_distance = surface.camera_to_pixel_length;

    // the sun marches through the water body even when atmospheric fog is off, the shafts are
    // only visible from inside the water so a dry camera never pays for them
    const bool ocean_march = light.is_directional() && buffer_frame.ocean_enabled > 0.5f && buffer_frame.ocean_turbidity > 0.0f && get_camera_position().y < buffer_frame.ocean_sea_level;

    if (total_distance < 0.1f || (sigma_s <= 0.0f && !ocean_march))
        return 0.0f;

    const float3 ray_origin    = get_camera_position();
    const float3 ray_direction = normalize(surface.camera_to_pixel);

    // restrict the march to where the light can contribute, punctual lights clip to their range sphere
    float march_start = 0.0f;
    float march_end   = total_distance;

    if (!light.is_directional())
    {
        const float volumetric_horizon = 60.0f;
        float effective_range          = min(light.far, volumetric_horizon);

        // ray sphere intersection around the light position
        float3 oc = ray_origin - light.position;
        float  b  = dot(oc, ray_direction);
        float  c  = dot(oc, oc) - effective_range * effective_range;
        float  h  = b * b - c;
        if (h < 0.0f)
            return 0.0f;

        h = sqrt(h);
        march_start = max(0.0f, -b - h);
        march_end   = min(total_distance, -b + h);
        if (march_end <= march_start)
            return 0.0f;
    }

    // underwater the extinction makes anything past this range invisible, keep the march short
    if (ocean_march)
    {
        march_end = min(march_end, 40.0f);
    }

    const float march_length = march_end - march_start;
    if (march_length < 0.1f)
        return 0.0f;

    // step count proportional to march length, capped on both ends
    // the water body is a smooth medium so the underwater march affords far coarser steps, the temporal jitter below resolves the difference
    const uint  min_steps        = 24;
    const uint  max_steps        = 96;
    const float target_step      = ocean_march ? 2.0f : 0.65f;
    const float step_count_float = clamp(march_length / target_step, (float)min_steps, (float)max_steps);
    const uint  step_count       = (uint)step_count_float;
    const float step_length      = march_length / step_count_float;
    const float3 ray_step        = ray_direction * step_length;

    // temporal jitter, taa resolves the noise into smooth scattering
    const float temporal_noise = noise_interleaved_gradient(pixel_pos, true);
    float3 ray_pos = ray_origin + ray_direction * (march_start + temporal_noise * step_length);

    // forward scattering, the sun uses a weaker bias to avoid a bright halo around its direction
    const float phase_g           = light.is_directional() ? 0.25f : 0.6f;
    const float min_transmittance = 0.005f;

    // hoisted invariants, step transmittance is constant for the whole march
    const float step_transmittance = exp(-sigma_t * step_length);
    const float sigma_s_dt         = sigma_s * step_length;

    // for directional lights the direction, cos_theta and phase are constant, evaluate once
    const bool is_dir = light.is_directional();
    float3 dir_light_dir = 0.0f;
    float  dir_phase     = 0.0f;
    if (is_dir)
    {
        dir_light_dir       = normalize(-light.forward);
        float dir_cos_theta = dot(ray_direction, dir_light_dir);
        dir_phase           = henyey_greenstein_phase(dir_cos_theta, phase_g);
    }

    // start with the extinction accumulated over the unmarched segment from camera to march_start
    // the water absorption along the view path is applied by the underwater post pass, not here
    float3 inscatter     = 0.0f;
    float  transmittance = exp(-sigma_t * march_start);

    [loop]
    for (uint i = 0; i < step_count; i++)
    {
        if (transmittance < min_transmittance)
            break;

        float3 light_dir;
        float  local_atten;
        float  phase;
        if (is_dir)
        {
            light_dir   = dir_light_dir;
            local_atten = 1.0f;
            phase       = dir_phase;
        }
        else
        {
            compute_volumetric_light_sample(light, ray_pos, light_dir, local_atten);
            float cos_theta = dot(ray_direction, light_dir);
            phase           = henyey_greenstein_phase(cos_theta, phase_g);
        }

        if (local_atten > 0.0f)
        {
            // underwater samples scatter through the suspended particles, the flat sea level is a good enough waterline since the waves average out over the march
            bool  in_water  = ocean_march && ray_pos.y < buffer_frame.ocean_sea_level;
            float sigma_dt  = in_water ? 0.05f * buffer_frame.ocean_turbidity * step_length : sigma_s_dt;

            // steps that scatter nothing skip the shadow fetch, fully shadowed steps skip the caustic samples
            if (sigma_dt > 0.0f)
            {
                float visibility = visible(ray_pos, light, pixel_pos);
                if (visibility > 0.0f)
                {
                    // single scattering integrand, sigma_s * phase * incident_radiance * transmittance * dt
                    float3 tint = 1.0f;
                    if (in_water)
                    {
                        float sun_path = (buffer_frame.ocean_sea_level - ray_pos.y) / max(dir_light_dir.y, 0.05f);
                        tint           = exp(-ocean_extinction * sun_path) * (0.2f + 0.8f * get_ocean_caustic(ray_pos.xz + dir_light_dir.xz * sun_path, sun_path));
                    }

                    inscatter += phase * visibility * local_atten * transmittance * sigma_dt * tint;
                }
            }
        }

        transmittance *= step_transmittance;
        ray_pos       += ray_step;
    }

    // color and intensity are constant along the ray so they pull out of the integral
    float3 result = inscatter * light.intensity * light.color;

    // fade by surface distance for non sky pixels so the haze does not flood close geometry,
    // sky pixels keep the full inscatter so the beams stay visible against the horizon,
    // a submerged camera skips it since the underwater post pass already absorbs with distance
    bool camera_underwater = ocean_march && ray_origin.y < buffer_frame.ocean_sea_level;
    if (!surface.is_sky() && !camera_underwater)
    {
        const float fade_rate = light.is_directional() ? 0.04f : 0.02f;
        result *= exp(-total_distance * fade_rate);
    }

    return result;
}
