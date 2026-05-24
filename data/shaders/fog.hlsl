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

// Atmospheric fog using exponential height-based falloff model
float get_fog_atmospheric(const float camera_to_pixel_length, const float pixel_height_world)
{
    float camera_height = get_camera_position().y;
    float density       = pass_get_f3_value().y * 0.00015f;
    float scale_height  = 50.0f; // Lower = denser near ground, higher = more uniform
    float b             = 1.0f / scale_height;
    float delta_height  = pixel_height_world - camera_height;
    float dist          = camera_to_pixel_length;
    
    if (dist < 0.1f)
        return 0.0f;
    
    float rd_y = (abs(dist) > 1e-6f) ? delta_height / dist : 0.0f;
    float tau = 0.0f;
    
    if (abs(rd_y) < 1e-5f)
    {
        // Horizontal ray approximation
        float base_density = density * exp(-camera_height * b);
        tau = base_density * dist;
    }
    else
    {
        // Analytical integral for optical depth (numerically stable)
        float base_density = density * exp(-camera_height * b);
        float exponent     = -dist * rd_y * b;
        float exp_term     = 1.0f - exp(exponent);
        tau = base_density * exp_term / (b * rd_y);
    }

    // Beer's law: fog factor = in-scatter (1 - transmittance)
    float transmittance = exp(-tau);
    float fog_factor    = 1.0f - transmittance;
    fog_factor = pow(fog_factor, 0.8f); // Smooth falloff curve
    
    return saturate(fog_factor);
}

// returns 1.0 if the world space position is lit by the given light, 0.0 if it is occluded
// uses the hardware comparison sampler so the depth compare happens in the texture unit
// for directional we pick a single cascade per sample instead of paying for both
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
        // try the near cascade first, fall back to the far cascade only if outside its frustum
        // single cascade per sample keeps shadow lookups halved across the raymarch
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

// henyey greenstein phase function, g = 0 isotropic, g positive forward scatter, g negative back scatter
// returned in 1/sr, the scattering coefficient sigma_s controls the overall brightness
float henyey_greenstein_phase(float cos_theta, float g)
{
    cos_theta     = clamp(cos_theta, -1.0f, 1.0f);
    float g2      = g * g;
    float denom   = max(1.0f + g2 - 2.0f * g * cos_theta, 1e-4f);
    // pow(d, 1.5) replaced with d * sqrt(d), one rsq + a mul instead of exp2/log2
    float denom32 = denom * sqrt(denom);
    return (1.0f - g2) / (4.0f * PI * denom32);
}

// computes the local light direction and the volumetric attenuation at a sample inside the medium
// area lights use the closest point on the rectangle so tube emitters illuminate the volume along their full length
// instead of only near the centroid, point and spot use the centroid with a soft minimum distance to avoid singularities
void compute_volumetric_light_sample(Light light, float3 sample_pos, out float3 light_dir, out float local_atten)
{
    if (light.is_directional())
    {
        light_dir   = normalize(-light.forward);
        local_atten = 1.0f;
        return;
    }

    // soft minimum distance, treats analytical lights as small spheres so the inverse square does not blow up at the surface
    const float soft_radius = 0.05f;

    if (light.is_area())
    {
        float3 closest    = light.compute_closest_point_on_area(sample_pos);
        float3 to_light   = closest - sample_pos;
        float  dist       = length(to_light);
        light_dir         = (dist > 1e-4f) ? to_light / dist : light.forward;

        float dist_eff    = max(dist, soft_radius);
        float range_atten = light.compute_attenuation_range(dist);
        // emitter only radiates into its front hemisphere, dot uses light forward and the direction from emitter to sample
        float emission_cos = saturate(dot(light.forward, -light_dir));
        local_atten        = (range_atten / (dist_eff * dist_eff)) * emission_cos;
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
        // cos_outer / angle_scale precomputed in Light::Build, hot raymarch path stays trig free
        float cd          = dot(-light_dir, light.forward);
        float angle_atten = saturate((cd - light.cos_outer) * light.angle_scale);
        local_atten      *= angle_atten * angle_atten;
    }
}

// volumetric fog raymarch, single scattering with beer lambert transmittance
// works for any light type, the per type differences are isolated in compute_volumetric_light_sample and visible
float3 compute_volumetric_fog(Surface surface, Light light, uint2 pixel_pos)
{
    // sigma_s is the medium scattering coefficient in 1/m, sigma_t is extinction
    // the engine packs the user facing fog density into pass_get_f3_value().y so r.fog scales it linearly
    const float sigma_s        = pass_get_f3_value().y * 0.0012f;
    const float sigma_t        = sigma_s; // pure scattering, no absorption
    const float total_distance = surface.camera_to_pixel_length;

    if (total_distance < 0.1f || sigma_s <= 0.0f)
        return 0.0f;

    const float3 ray_origin    = get_camera_position();
    const float3 ray_direction = normalize(surface.camera_to_pixel);

    // restrict the march to the slab of the ray where the light can actually contribute
    // for punctual emitters this is the intersection with their range sphere, clamped to a volumetric horizon
    // because 1/r^2 makes contribution invisible past a few tens of meters anyway
    // without this clip, sky pixels (depth at the far plane) would spread the same sample budget over kilometers
    // and step density would collapse to where small beams disappear into noise
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

    const float march_length = march_end - march_start;
    if (march_length < 0.1f)
        return 0.0f;

    // step count proportional to march length, sub meter sampling for finer beam capture and
    // less per pixel jitter noise, hard capped on both ends, denser steps shrink the inscatter
    // each sample contributes which directly lowers the spatial variance from the temporal jitter
    const uint  min_steps        = 24;
    const uint  max_steps        = 96;
    const float target_step      = 0.65f;
    const float step_count_float = clamp(march_length / target_step, (float)min_steps, (float)max_steps);
    const uint  step_count       = (uint)step_count_float;
    const float step_length      = march_length / step_count_float;
    const float3 ray_step        = ray_direction * step_length;

    // temporal jitter, taa resolves the noise into smooth scattering
    const float temporal_noise = noise_interleaved_gradient(pixel_pos, true);
    float3 ray_pos = ray_origin + ray_direction * (march_start + temporal_noise * step_length);

    // moderate forward scattering, dust beams readable for punctual lights without producing a bright sun halo
    const float phase_g           = 0.6f;
    const float min_transmittance = 0.005f;

    // hoisted invariants, step transmittance is constant for the whole march
    const float step_transmittance = exp(-sigma_t * step_length);
    const float sigma_s_dt         = sigma_s * step_length;

    // for directional lights, light direction, cos_theta and the phase function are all constant
    // for the whole ray, evaluate them once instead of per step
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
            float visibility = visible(ray_pos, light, pixel_pos);

            // single scattering integrand, sigma_s * phase * incident_radiance * transmittance * dt
            inscatter += phase * visibility * local_atten * transmittance * sigma_s_dt;
        }

        transmittance *= step_transmittance;
        ray_pos       += ray_step;
    }

    // light radiance scaling, color and intensity are constants along the ray so they pull out of the integral
    float3 result = inscatter * light.intensity * light.color;

    // fade the contribution by surface distance for non sky pixels
    // the inscatter is largest near the camera and becomes a constant additive haze on every pixel
    // whose ray passes through the light volume, without this fade it lights up the entire ground
    // plane out to the horizon as a uniform glow that does not match the falling off surface lighting
    // sky pixels keep the full inscatter so the beams stay visible against the horizon
    if (!surface.is_sky())
    {
        result *= exp(-total_distance * 0.005f);
    }

    return result;
}
