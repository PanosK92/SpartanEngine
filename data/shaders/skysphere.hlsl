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

//= includes =========
#include "common.hlsl"
//====================

// constants
static const float3 up_direction     = float3(0, 1, 0);                      // up direction
static const float3 earth_center     = float3(0, -6371e3, 0);                // earth center at -radius (meters), y-up
static const float earth_radius      = 6371e3;                               // earth radius in meters
static const float atmosphere_height = 100e3;                                // atmosphere thickness in meters
static const float h_rayleigh        = 7994.0;                               // rayleigh scale height in meters
static const float h_mie             = 1200.0;                               // mie scale height in meters
static const float3 beta_rayleigh    = float3(5.802e-6, 13.558e-6, 33.1e-6); // m^-1, rayleigh scattering coefficients
static const float beta_mie_scatter  = 3.996e-6;                             // m^-1, mie scattering
static const float beta_mie_abs      = 4.40e-6;                              // m^-1, mie absorption
static const float3 beta_ozone_abs   = float3(0.650e-6, 1.881e-6, 0.085e-6); // m^-1, ozone absorption
static const float g_mie             = 0.80;                                 // mie phase asymmetry factor
static const int num_view_samples    = 16;                                   // samples along view ray
static const int num_sun_samples     = 1024;                                 // samples along sun ray

struct sun
{
    static float3 compute_mie_scatter_color(float3 view_direction, float3 sun_direction, float mie, float mie_g, float3 light_color)
    {
        float eye_cos  = -dot(view_direction, sun_direction);
        float mie_g2   = mie_g * mie_g;
        float temp     = mad(mie_g, -2.0 * eye_cos, 1.0 + mie_g2);
        temp           = smoothstep(0.0, 0.01f, temp) * temp;   
        float eye_cos2 = eye_cos * eye_cos;
        
        return mie * (1.5 * ((1.0 - mie_g2) / (2.0 + mie_g2)) * (1.0 + eye_cos2) / temp) * light_color;
    }

    static float3 compute_color(float3 view_dir, float3 sun_dir, float3 light_color)
    {
        float sun_elevation      = saturate(dot(sun_dir, up_direction) + 1.0);
        float mie                = lerp(0.01f, 0.04f, sun_elevation);
        float mie_g              = lerp(-0.9f, -0.6f, sun_elevation);
        float3 directional_light = compute_mie_scatter_color(view_dir, sun_dir, mie, mie_g, light_color) * 0.3f;
        float3 sun_disc          = compute_mie_scatter_color(view_dir, sun_dir, 0.001f, -0.998f, light_color);
        float fade_out_factor    = smoothstep(0.0f, 0.15f, sun_elevation);
        
        return (directional_light + sun_disc) * fade_out_factor;
    }
};

struct stars
{
    static float2 hash22(float2 p)
    {
        float3 p3  = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
        p3        += dot(p3, p3.yzx + 33.33);
        return frac((p3.xx + p3.yz) * p3.zy);
    }

    static float3 compute_color(const float2 uv, const float3 sun_direction)
    {
        float sun_elevation = dot(sun_direction, up_direction);
        bool is_night       = sun_elevation < 0.0;
    
        float brightness = 0.0;
        if (is_night)
        {
            float2 star_uv     = uv * 100.0f;
            float2 hash        = hash22(star_uv);
            brightness         = step(0.999f, hash.x);
            float star_factor  = saturate(-sun_elevation * 10.0f);
            brightness        *= star_factor;
        }
        
        return float3(brightness, brightness, brightness);
    }
};

float intersect_sphere(float3 origin, float3 direction, float3 center, float radius)
{
    float3 oc          = origin - center;
    float b            = dot(direction, oc);
    float c            = dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;
    
    if (discriminant < 0)
        return -1.0;
        
    float sqrt_disc = sqrt(discriminant);
    float t1        = -b - sqrt_disc;
    float t2        = -b + sqrt_disc;
    
    if (t2 > 0)
        return t2;
    else if (t1 > 0)
        return t1;
    else
        return -1.0;
}

float compute_density_ozone(float height)
{
    height *= 0.001; // to km
    if (height < 10.0)
        return 0.0;
    else if (height < 25.0)
        return (height - 10.0) / 15.0;
    else if (height < 40.0)
        return 1.0 - (height - 25.0) / 15.0;
    else
        return 0.0;
}

float3 compute_optical_depth(float3 position, float3 direction, float t_max)
{
    float ds             = t_max / num_sun_samples;
    float3 optical_depth = 0.0;

    for (int i = 0; i < num_sun_samples; i++)
    {
        float t           = (i + 0.5) * ds;
        float3 sample_pos = position + t * direction;
        float height      = length(sample_pos - earth_center) - earth_radius;
        if (height < 0)
            break;

        float density_rayleigh = exp(-height / h_rayleigh);
        float density_mie      = exp(-height / h_mie);
        float density_ozone    = compute_density_ozone(height);
        float3 extinction      = density_rayleigh * beta_rayleigh + density_mie * (beta_mie_scatter + beta_mie_abs) + density_ozone * beta_ozone_abs;
        optical_depth         += extinction * ds;
    }
    return optical_depth;
}

#ifdef LUT
[numthreads(8, 8, 8)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint3 lut_dimensions;
    tex3d_uav.GetDimensions(lut_dimensions.x, lut_dimensions.y, lut_dimensions.z);

    if (any(thread_id >= lut_dimensions))
        return;

    float height     = (float)thread_id.y / (lut_dimensions.y - 1) * atmosphere_height;
    float cos_theta  = (float)thread_id.x / (lut_dimensions.x - 1) * 2.0 - 1.0;
    float sun_zenith = (float)thread_id.z / (lut_dimensions.z - 1) * 2.0 - 1.0;
    float theta_sun  = acos(clamp(sun_zenith, -1.0, 1.0));
    float phi_sun    = PI * 0.5;

    float3 sun_dir = float3(
        sin(theta_sun) * cos(phi_sun),
        cos(theta_sun),
        sin(theta_sun) * sin(phi_sun)
    );

    float3 position = earth_center + float3(0, earth_radius + height, 0);

    float s_earth = intersect_sphere(position, sun_dir, earth_center, earth_radius);
    float3 optical_depth_sun;
    if (s_earth > 0)
        optical_depth_sun = 1e6;
    else
    {
        float s_max = intersect_sphere(position, sun_dir, earth_center, earth_radius + atmosphere_height);
        optical_depth_sun = s_max < 0 ? 1e6 : compute_optical_depth(position, sun_dir, s_max);
    }

    float3 t_sun = exp(-optical_depth_sun);

    tex3d_uav[thread_id.xyz] = float4(t_sun, 1.0);
}
#else
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);

    // common values
    float2 uv             = (float2(thread_id.xy)) / resolution;
    float phi             = uv.x * PI2;
    float theta           = -uv.y * PI;
    float sin_theta       = sin(theta);
    float cos_theta       = cos(theta);
    float3 view_direction = float3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));

    float3 original_view_direction = view_direction;
    bool is_below_horizon = (view_direction.y < 0.0f);
    if (is_below_horizon)
    {
        view_direction.y = -view_direction.y;
    }

    // light
    Light light;
    Surface surface;
    light.Build(0, surface);
    float3 sun_direction = -light.forward;
    float3 light_color   = light.color;

    // phase functions
    float cos_theta_phase = dot(view_direction, sun_direction);
    float phase_rayleigh  = (3.0 / (16.0 * PI)) * (1.0 + cos_theta_phase * cos_theta_phase);
    float phase_mie       = (1.0 - g_mie * g_mie) / (4.0 * PI * pow(1.0 + g_mie * g_mie - 2.0 * g_mie * cos_theta_phase, 1.5));

    // integration
    float3 atmosphere_color = 0.0f;
    float t_max = intersect_sphere(buffer_frame.camera_position, view_direction, earth_center, earth_radius + atmosphere_height);
    if (t_max < 0)
    {
        tex_uav[thread_id.xy] = float4(atmosphere_color, 1.0f);
        return;
    }    
    float ds         = t_max / num_view_samples;
    float sun_zenith = dot(sun_direction, up_direction);
    float w          = saturate((sun_zenith + 1.0f) * 0.5f);
    float3 transmittance = 1.0f;
    [unroll]
    for (int i = 0; i < num_view_samples; i++)
    {
        float t         = (i + 0.5f) * ds;
        float3 position = buffer_frame.camera_position + t * view_direction;
        float height    = max(0.0f, length(position - earth_center) - earth_radius);
        if (height > atmosphere_height)
            break;

        float density_rayleigh = exp(-height / h_rayleigh);
        float density_mie      = exp(-height / h_mie);
        float density_ozone    = compute_density_ozone(height);

        float3 extinction = density_rayleigh * beta_rayleigh + density_mie * (beta_mie_scatter + beta_mie_abs) + density_ozone * beta_ozone_abs;
        float3 tau_step   = extinction * ds;
        float3 trans_step = exp(-tau_step);

        float v           = saturate(height / atmosphere_height);
        float3 lut_coords = float3(0.5f, v, w);
        float3 t_sun      = tex3d.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), lut_coords, 0).rgb;

        float3 scattering_rayleigh = beta_rayleigh * density_rayleigh * phase_rayleigh * t_sun * ds;
        float3 scattering_mie      = beta_mie_scatter.xxx * density_mie * phase_mie * t_sun * ds;

        atmosphere_color += (scattering_rayleigh + scattering_mie) * transmittance * light.intensity * light_color;
        transmittance    *= trans_step;
    }

    // artistic touches (stars, moon, sun)
    float3 sun_color      = sun::compute_color(original_view_direction, sun_direction, light_color);
    float3 star_color     = stars::compute_color(uv, sun_direction);
    float3 moon_color     = 0.0f;
    float3 moon_direction = -sun_direction;
    if (dot(moon_direction, up_direction) > 0.0f)
    {
        float3 moon_disc = sun::compute_mie_scatter_color(original_view_direction, moon_direction, 0.001f, -0.997f, light_color);
        moon_color       = moon_disc * float3(0.5f, 0.65f, 1.0f);
    }

    if (is_below_horizon)
    {
        atmosphere_color *= 0.5f;
        sun_color         = 0.0f;
        star_color        = 0.0f;
        moon_color        = 0.0f;
    }

    // out
    tex_uav[thread_id.xy] = float4(atmosphere_color + sun_color + star_color + moon_color, 1.0f);
}
#endif
