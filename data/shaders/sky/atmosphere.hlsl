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

#ifndef SPARTAN_ATMOSPHERE
#define SPARTAN_ATMOSPHERE

//= includes =========
#include "../common.hlsl"
#include "planet.hlsl"
//====================

// constants - atmosphere
static const float3 up_direction       = float3(0.0, 1.0, 0.0);
static const float earth_radius        = planet_earth_radius;
static const float atmosphere_radius   = planet_atmosphere_radius;
static const float3 earth_center       = planet_earth_center;

// constants - scattering coefficients at sea level
static const float3 rayleigh_scatter   = float3(5.802e-6, 13.558e-6, 33.1e-6);
static const float rayleigh_height     = 8000.0;
static const float3 mie_scatter        = float3(3.996e-6, 3.996e-6, 3.996e-6);
static const float3 mie_extinction     = float3(4.4e-6, 4.4e-6, 4.4e-6);
static const float mie_height          = 1200.0;
static const float mie_g               = 0.8;

// constants - ozone
static const float3 ozone_absorption   = float3(0.65e-6, 1.881e-6, 0.085e-6);
static const float ozone_center_height = 25000.0;
static const float ozone_width         = 15000.0;

// constants - sun and ground
// every sun energy term reads get_sun_radiance_toa() from common.hlsl, the neutral white top
// of atmosphere radiance derived from the directional light's intensity, all tinting comes
// from atmospheric transmittance so the sky, sun disc, clouds, ibl and direct surface
// lighting stay locked to the atmosphere as the single ground truth for sun color
// Distance-dependent solar angular radius, supplied by the ephemeris.
static const float3 ground_albedo      = float3(0.3, 0.3, 0.3);

// constants - sampling
static const int transmittance_samples = 64;
static const int multiscatter_samples  = 32;
static const int scattering_samples    = 32;

// utility
float safe_sqrt(float x) { return sqrt(max(0.0, x)); }

float2 ray_sphere_intersect(float3 origin, float3 direction, float3 center, float radius)
{
    float3 oc = origin - center;
    float b = dot(direction, oc);
    float radial_distance = length(oc);
    float c = (radial_distance - radius) * (radial_distance + radius);
    float discriminant = b * b - c;
    
    if (discriminant < 0.0)
        return float2(-1.0, -1.0);
    
    float d = sqrt(discriminant);
    // Avoid cancellation for the root close to the surface.
    float q = -b - (b >= 0.0 ? d : -d);
    if (abs(q) < 1e-6) return float2(0.0, 0.0);
    float other = c / q;
    return float2(min(q, other), max(q, other));
}

float get_height(float3 position)
{
    return length(position - earth_center) - earth_radius;
}

// keep the camera inside the atmosphere shell, shared by the sky view lut and the panorama bake
float3 clamp_camera_to_atmosphere(float3 cam_pos)
{
    float cam_h = get_height(cam_pos);
    if (cam_h < 1.0)
    {
        return earth_center + normalize(cam_pos - earth_center) * (earth_radius + 1.0);
    }
    if (cam_h > atmosphere_radius - earth_radius)
    {
        return earth_center + normalize(cam_pos - earth_center) * (atmosphere_radius - 1.0);
    }
    return cam_pos;
}

// density functions
float get_rayleigh_density(float height) { return exp(-height / rayleigh_height); }
float get_mie_density(float height)      { return exp(-height / mie_height); }
float get_ozone_density(float height)    { return max(0.0, 1.0 - abs(height - ozone_center_height) / ozone_width); }

// Exact constant-medium integral with a cancellation-free vacuum limit.
float3 atmosphere_segment_weight(float3 extinction, float distance)
{
    float3 weight;
    [unroll] for (uint channel = 0u; channel < 3u; ++channel)
    {
        float tau = extinction[channel] * distance;
        weight[channel] = tau < 0.001f
            ? distance * (1.0f - tau * 0.5f + tau * tau / 6.0f)
            : (1.0f - exp(-tau)) / extinction[channel];
    }
    return weight;
}

float3 get_extinction(float height)
{
    return rayleigh_scatter * get_rayleigh_density(height) +
           mie_extinction * get_mie_density(height) +
           ozone_absorption * get_ozone_density(height);
}

float3 get_scattering(float height)
{
    return rayleigh_scatter * get_rayleigh_density(height) + mie_scatter * get_mie_density(height);
}

// phase functions
float rayleigh_phase(float cos_theta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

float cornette_shanks_phase(float cos_theta, float g)
{
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cos_theta * cos_theta);
    float denom = (8.0 * PI) * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cos_theta, 1.5);
    return num / max(denom, 0.0001);
}

// Inverse of planet_transmittance_uv, used when baking the lookup.
void transmittance_uv_to_params(float2 uv, out float height, out float cos_zenith)
{
    float h = uv.y * uv.y;
    height = earth_radius + h * (atmosphere_radius - earth_radius);
    
    float rho = safe_sqrt(max(0.0, (height - earth_radius) * (height + earth_radius)));
    float cos_horizon = -rho / height;
    
    if (uv.x > 0.5)
    {
        float t = (uv.x - 0.5) * 2.0;
        cos_zenith = cos_horizon + t * t * (1.0 - cos_horizon);
    }
    else
    {
        float t = 1.0 - uv.x * 2.0;
        cos_zenith = cos_horizon - t * t * (1.0 + cos_horizon);
    }
}

// compute optical depth from position to atmosphere top
float3 compute_transmittance_to_top(float3 position, float3 direction)
{
    float2 intersect = ray_sphere_intersect(position, direction, earth_center, atmosphere_radius);
    float t_max = intersect.y;
    if (t_max < 0.0)
        return float3(1.0, 1.0, 1.0);
    
    // rays that hit the planet are fully occluded, zero gives every consumer the earth shadow
    float2 ground_hit = ray_sphere_intersect(position, direction, earth_center, earth_radius);
    if (ground_hit.x >= 0.0 && dot(position - earth_center, direction) < 0.0)
        return float3(0.0, 0.0, 0.0);
    
    // Quadratic spacing resolves the dense aerosol layer close to the observer.
    float previous_t = 0.0;
    float3 optical_depth = float3(0.0, 0.0, 0.0);
    float3 prev_ext = get_extinction(get_height(position));
    
    for (int i = 1; i <= transmittance_samples; i++)
    {
        float u = float(i) / transmittance_samples;
        float t = t_max * u * u;
        float dt = t - previous_t;
        previous_t = t;
        float3 sample_pos = position + direction * t;
        float height = get_height(sample_pos);
        if (height < 0.0) break;
        
        float3 curr_ext = get_extinction(height);
        optical_depth += (prev_ext + curr_ext) * 0.5 * dt;
        prev_ext = curr_ext;
    }
    
    return exp(-optical_depth);
}

// multi-scatter lut - infinite bounce approximation
float3 compute_multiscatter(float height, float cos_sun_zenith, Texture2D transmittance_lut, SamplerState samp)
{
    float3 position = earth_center + float3(0.0, earth_radius + max(height, 1.0), 0.0);
    float3 sun_dir = float3(safe_sqrt(1.0 - cos_sun_zenith * cos_sun_zenith), cos_sun_zenith, 0.0);
    
    float3 luminance_sum = float3(0.0, 0.0, 0.0);
    float3 f_ms_sum = float3(0.0, 0.0, 0.0);
    
    const int sqrt_samples = 8;
    for (int i = 0; i < sqrt_samples; i++)
    {
        for (int j = 0; j < sqrt_samples; j++)
        {
            float u = (i + 0.5) / sqrt_samples;
            float v = (j + 0.5) / sqrt_samples;
            
            float cos_theta = u * 2.0 - 1.0;
            float sin_theta = safe_sqrt(1.0 - cos_theta * cos_theta);
            float phi = v * PI2;
            float3 ray_dir = float3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
            
            float2 atmo_hit = ray_sphere_intersect(position, ray_dir, earth_center, atmosphere_radius);
            float t_max = atmo_hit.y;
            
            float2 ground_hit = ray_sphere_intersect(position, ray_dir, earth_center, earth_radius);
            bool hits_ground = ground_hit.x > 0.0;
            if (hits_ground) t_max = ground_hit.x;
            
            // Cluster samples at both endpoints, including the dense ground layer.
            float previous_t = 0.0;
            float3 trans = float3(1.0, 1.0, 1.0);
            float3 scatter_integral = float3(0.0, 0.0, 0.0);
            float3 transfer_integral = float3(0.0, 0.0, 0.0);
            
            for (int k = 0; k < multiscatter_samples; k++)
            {
                float u = float(k + 1) / multiscatter_samples;
                float t = t_max * (0.5 - 0.5 * cos(PI * u));
                float dt = t - previous_t;
                float3 sample_pos = position + ray_dir * ((previous_t + t) * 0.5);
                previous_t = t;
                float sample_h = get_height(sample_pos);
                
                float3 scatter = get_scattering(sample_h);
                float3 extinct = get_extinction(sample_h);
                
                float cos_sun = dot(normalize(sample_pos - earth_center), sun_dir);
                float3 trans_sun = planet_transmittance(transmittance_lut, samp, sample_h + earth_radius, cos_sun);
                
                float3 scatter_no_phase = scatter * trans_sun;
                float3 weight = atmosphere_segment_weight(extinct, dt);
                float3 s_int = scatter_no_phase * weight;
                transfer_integral += scatter * weight * trans;
                
                scatter_integral += s_int * trans;
                trans *= exp(-extinct * dt);
            }
            
            // ground contribution
            if (hits_ground)
            {
                float3 ground_pos = position + ray_dir * t_max;
                float3 ground_n = normalize(ground_pos - earth_center);
                float ndotl = saturate(dot(ground_n, sun_dir));
                
                float3 trans_sun = planet_transmittance(transmittance_lut, samp, earth_radius, dot(ground_n, sun_dir));
                
                luminance_sum += trans * trans_sun * ndotl * ground_albedo / PI;
            }
            
            float phase = 1.0 / (4.0 * PI);
            luminance_sum += scatter_integral * phase;
            // The bounce transfer contains no solar transmittance or phase term.
            f_ms_sum += transfer_integral;
        }
    }
    
    float sphere_samples = sqrt_samples * sqrt_samples;
    // Isotropic scattering uses the spherical MEAN radiance, not its integral.
    luminance_sum /= sphere_samples;
    f_ms_sum /= sphere_samples;
    
    // infinite series approximation
    return luminance_sum / max(float3(1.0, 1.0, 1.0) - f_ms_sum, 0.001);
}

// Source per metre and unit top-of-atmosphere irradiance. Shared by the
// infinite sky ray and finite camera-to-surface transport.
float3 atmosphere_source(float3 position, float3 view_dir, float3 light_dir,
    Texture2D transmittance_lut, Texture2D multiscatter_lut, SamplerState samp)
{
    float height = max(get_height(position), 0.0f);
    float cos_sun = dot(normalize(position - earth_center), light_dir);
    float3 trans_sun = planet_transmittance(transmittance_lut, samp, height + earth_radius, cos_sun);
    float3 rayleigh = rayleigh_scatter * get_rayleigh_density(height);
    float3 mie = mie_scatter * get_mie_density(height);
    float mu = dot(view_dir, light_dir);
    float3 single_scatter = (rayleigh * rayleigh_phase(mu)
        + mie * cornette_shanks_phase(mu, mie_g)) * trans_sun;
    float2 ms_uv = float2(cos_sun * 0.5f + 0.5f,
        sqrt(saturate(height / (atmosphere_radius - earth_radius))));
    float2 ms_size;
    multiscatter_lut.GetDimensions(ms_size.x, ms_size.y);
    ms_uv = (ms_uv * (ms_size - 1.0) + 0.5) / ms_size;
    return single_scatter + (rayleigh + mie) * multiscatter_lut.SampleLevel(samp, ms_uv, 0).rgb;
}
#endif
