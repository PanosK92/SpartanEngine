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
static const int num_view_samples    = 32;                                   // samples along view ray
static const int num_sun_samples     = 256;                                  // samples along sun ray

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
    static float3 blackbody(float temp)
    {
        float3 color = float3(1.0f, 0.0f, 0.0f); // base red to avoid zero
        temp = clamp(temp, 3000.0f, 15000.0f);   // clamp temp to safe range
    
        // green component
        if (temp < 6600.0f)
        {
            color.g = 0.39008157876901960784f * log(temp) - 0.63184144378862745098f;
        }
        else
        {
            color.g = 1.29293618606274509804f * pow(temp / 1000.0f - 6.0f, -0.1332047592f);
        }
    
        // blue component
        if (temp < 6600.0f)
        {
            color.b = 0.54320678911019607843f * log(temp / 1000.0f - 0.6f) - 1.19625408914f;
        }
        else
        {
            color.b = 1.12989086089529411765f * pow(temp / 1000.0f - 6.0f, -0.0755148492f);
        }
    
        return max(saturate(color), float3(0.1f, 0.1f, 0.1f));
    }

    static float2 hash22(float2 p)
    {
        float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
        p3 += dot(p3, p3.yzx + 33.33);
        return frac((p3.xx + p3.yz) * p3.zy);
    }
    
    static float gaussian(float2 uv, float2 center, float sigma)
    {
        float2 d = uv - center;
        return exp(-dot(d, d) / (2.0 * sigma * sigma));
    }
    
    static float3 compute_color(const float2 uv, const float3 sun_direction)
    {
        float sun_elevation = dot(sun_direction, up_direction);
        bool is_night       = sun_elevation < 0.0;
    
        float3 color = 0.0;
        if (is_night)
        {
            float2 star_uv    = uv * 100.0f; // controls density
            float2 cell       = floor(star_uv);
            float star_factor = saturate(-sun_elevation * 10.0f);
        
            // sample 3x3 grid for smoother stars
            for (int y = -1; y <= 1; y++)
            {
                for (int x = -1; x <= 1; x++)
                {
                    float2 offset      = float2(x, y);
                    float2 cell_center = cell + offset + 0.5;
                    float2 hash        = hash22(cell_center);
                
                     // lower threshold for more stars
                    if (hash.x > 0.97f)
                    {
                        float sigma      = 0.02f; // star size
                        float brightness = gaussian(star_uv, cell_center, sigma);
                        
                        // scale brightness to avoid over-brightness
                        brightness *= (hash.x - 0.98f) * 80.0f; // normalize and boost
                        
                        // random temperature for color (4000K to 12000K)
                        float temp         = lerp(4000.0, 12000.0, hash.y);
                        float3 star_color  = blackbody(temp) * brightness * star_factor;
                        color             += star_color;
                    }
                }
            }
        }
    
        return color;
    }
};

// ============================================================================
// VOLUMETRIC CLOUDS - 3D Raymarching with Noise Textures
// Only compiled for non-LUT variant (the main skysphere shader)
// ============================================================================
#ifndef LUT

// Cloud layer base constants
static const float cloud_base_bottom = 1500.0;  // minimum cloud altitude
static const float cloud_base_top    = 4500.0;  // maximum cloud altitude
static const float cloud_scale       = 0.00003; // noise sampling scale
static const float detail_scale      = 0.0003;  // detail noise scale
static const float cloud_absorption  = 0.5;     // light absorption

// Performance constants
static const float cloud_wind_speed = 10.0;
static const int cloud_steps = 200;
static const int light_steps = 30;

struct clouds
{
    // Fast hash functions for seed-based generation
    static float hash11(float p, float seed)
    {
        p = frac(p * 0.1031 + seed * 0.1);
        p *= p + 33.33;
        p *= p + p;
        return frac(p);
    }
    
    static float hash21(float2 p, float seed)
    {
        float3 p3 = frac(float3(p.xyx) * 0.1031 + seed * 0.1);
        p3 += dot(p3, p3.yzx + 33.33);
        return frac((p3.x + p3.y) * p3.z);
    }
    
    static float hash31(float3 p, float seed)
    {
        p = frac(p * 0.1031 + seed * 0.1);
        p += dot(p, p.yzx + 33.33);
        return frac((p.x + p.y) * p.z);
    }
    
    static float remap(float v, float l1, float h1, float l2, float h2)
    {
        return l2 + (v - l1) * (h2 - l2) / max(h1 - l1, 0.0001);
    }
    
    static float get_height_gradient(float h, float ctype)
    {
        return smoothstep(0.0, 0.1, h) * smoothstep(0.4 + ctype * 0.6, 0.2 + ctype * 0.3, h);
    }
    
    // Smooth noise function for height variation (no hard cell boundaries)
    static float smooth_noise(float2 p, float seed)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        
        // Smooth interpolation curve
        float2 u = f * f * (3.0 - 2.0 * f);
        
        // Four corners
        float a = hash21(i + float2(0, 0), seed);
        float b = hash21(i + float2(1, 0), seed);
        float c = hash21(i + float2(0, 1), seed);
        float d = hash21(i + float2(1, 1), seed);
        
        // Bilinear interpolation
        return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
    }
    
    // Get local cloud height bounds - uses smooth noise for seamless transitions
    static void get_local_cloud_bounds(float3 pos, float seed, out float local_bottom, out float local_top)
    {
        // Use smooth noise instead of cell-based hashing (no visible seams)
        float2 coord = pos.xz * 0.00005; // ~20km wavelength
        
        float height_noise1 = smooth_noise(coord, seed);
        float height_noise2 = smooth_noise(coord * 1.7 + 100.0, seed * 2.3);
        
        // Vary bottom between 1200-2200m, top between 3500-4500m
        local_bottom = lerp(1200.0, 2200.0, height_noise1);
        local_top = lerp(3500.0, 4500.0, height_noise2);
        
        // Ensure minimum thickness
        local_top = max(local_top, local_bottom + 1500.0);
    }
    
    // Seed-based coordinate transformation - pure offset and rotation
    static float3 seed_transform(float3 p, float seed)
    {
        // Large offset based on seed to sample completely different noise region
        float3 offset = float3(seed * 50000.0, seed * 30000.0, seed * 70000.0);
        return p + offset;
    }
    
    // Domain warping - smooth sinusoidal distortion (no discontinuities)
    static float3 domain_warp(float3 p, float seed)
    {
        // Use irrational frequency ratios for non-repeating patterns
        const float phi = 1.61803398875;    // golden ratio
        const float sqrt2 = 1.41421356237;
        const float sqrt3 = 1.73205080757;
        const float e = 2.71828182846;
        
        float seed_phase = seed * 0.31415926; // seed * π/10
        
        // Multi-octave smooth warping - all continuous functions
        float3 warp;
        warp.x = sin(p.z * 0.00005 * phi + p.y * 0.00007 + seed_phase) * 1500.0
               + sin(p.z * 0.00013 * sqrt2 + p.y * 0.00011 + seed_phase * 2.0) * 750.0
               + sin(p.x * 0.00017 * sqrt3 + p.z * 0.00019 + seed_phase * 3.0) * 375.0;
               
        warp.y = sin(p.x * 0.00006 * e + p.z * 0.00008 + seed_phase) * 400.0
               + sin(p.x * 0.00014 + p.z * 0.00012 * phi + seed_phase * 2.0) * 200.0;
               
        warp.z = sin(p.y * 0.00005 + p.x * 0.00009 * sqrt2 + seed_phase) * 1500.0
               + sin(p.y * 0.00011 * phi + p.x * 0.00015 + seed_phase * 2.0) * 750.0
               + sin(p.z * 0.00019 * sqrt3 + p.y * 0.00021 + seed_phase * 3.0) * 375.0;
        
        return p + warp;
    }
    
    static float sample_density(float3 pos, float coverage, float ctype, float time, float seed)
    {
        // Get local cloud height bounds (varying per location)
        float local_bottom, local_top;
        get_local_cloud_bounds(pos, seed, local_bottom, local_top);
        float local_thickness = local_top - local_bottom;
        
        // Check bounds with local heights
        if (pos.y < local_bottom || pos.y > local_top)
            return 0.0;
        
        float h = (pos.y - local_bottom) / local_thickness;
        
        // Wind animation
        float3 wind = buffer_frame.wind;
        float wind_speed = length(wind) * cloud_wind_speed;
        float3 wind_dir = wind_speed > 0.001 ? normalize(wind) : float3(1, 0, 0);
        float3 anim_pos = pos + wind_dir * time * wind_speed;
        
        // Apply seed-based transformation first
        float3 seed_pos = seed_transform(anim_pos, seed);
        
        // Domain warp with seed
        float3 warped_pos = domain_warp(seed_pos, seed);
        
        // Distance fade
        float dist_from_cam = length(pos.xz);
        float distance_fade = 1.0 - smoothstep(25000.0, 40000.0, dist_from_cam);
        
        // Sample shape noise
        float3 shape_uvw = warped_pos * cloud_scale;
        float4 shape = tex3d_cloud_shape.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), shape_uvw, 0);
        
        float noise = shape.r * 0.625 + shape.g * 0.25 + shape.b * 0.125;
        noise *= get_height_gradient(h, ctype);
        
        // Coverage threshold
        float density = saturate(remap(noise, 1.0 - coverage, 1.0, 0.0, 1.0)) * distance_fade;
        if (density < 0.01)
            return 0.0;
        
        // Detail erosion
        float3 detail_uvw = domain_warp(seed_pos * 1.1, seed) * detail_scale;
        detail_uvw.y += time * 0.01;
        
        float4 detail = tex3d_cloud_detail.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), detail_uvw, 0);
        float detail_noise = detail.r * 0.625 + detail.g * 0.25 + detail.b * 0.125;
        
        density = saturate(density - detail_noise * (1.0 - density) * 0.35);
        return density;
    }
    
    static float hg_phase(float cos_theta, float g)
    {
        float g2 = g * g;
        return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001), 1.5));
    }
    
    static float light_march(float3 pos, float3 light_dir, float coverage, float ctype, float time, float seed)
    {
        float dist = min((cloud_base_top - pos.y) / max(light_dir.y, 0.01), cloud_base_top - cloud_base_bottom);
        float step_size = dist / float(light_steps);
        float optical_depth = 0.0;
        
        [unroll]
        for (int i = 0; i < light_steps; i++)
        {
            float3 p = pos + light_dir * step_size * (float(i) + 0.5);
            if (p.y > cloud_base_top) break;
            optical_depth += sample_density(p, coverage, ctype, time, seed) * step_size;
        }
        
        // Multi-scattering approximation (based on Horizon Zero Dawn technique)
        // Beer-Lambert for primary attenuation
        float beer = exp(-optical_depth * cloud_absorption * 2.0);
        
        // Powder effect - darkening at cloud edges when backlit
        float powder = 1.0 - exp(-optical_depth * cloud_absorption * 4.0);
        
        // Multi-scatter contribution - light that bounces multiple times
        // appears as soft diffuse glow, especially in thick clouds
        float multi_scatter = exp(-optical_depth * cloud_absorption * 0.5) * 0.6;
        
        // Combine: primary transmission + multi-scatter contribution
        // The powder effect modulates multi-scatter for realistic edge darkening
        return beer + multi_scatter * powder;
    }
    
    static float4 compute(float3 view_dir, float3 sun_dir, float3 sun_color, float sun_intensity, float time, float2 pixel_uv)
    {
        float coverage = buffer_frame.cloud_coverage;
        float cloud_type = buffer_frame.cloud_type;
        float seed = buffer_frame.cloud_seed;
        
        if (coverage <= 0.0)
            return float4(0, 0, 0, 0);
        
        // Smooth horizon falloff
        float horizon_fade = smoothstep(-0.05, 0.15, view_dir.y);
        if (horizon_fade <= 0.0)
            return float4(0, 0, 0, 0);
        
        // Camera at origin
        float3 cam_pos = float3(0, 100, 0);
        
        // Ray-layer intersection using base bounds (covers all possible cloud heights)
        float dir_y = max(view_dir.y, 0.001);
        float t_enter = (cloud_base_bottom - cam_pos.y) / dir_y;
        float t_exit = (cloud_base_top - cam_pos.y) / dir_y;
        
        if (t_exit <= t_enter || t_enter < 0.0)
            return float4(0, 0, 0, 0);
        
        float ray_length = t_exit - t_enter;
        
        // Adaptive step count based on:
        // 1. View angle (horizon = longer path, needs more steps)
        // 2. Ray length (longer rays need proportionally more steps)
        // 3. Distance to clouds (farther = more steps to avoid banding)
        float base_steps = float(cloud_steps);
        
        // Scale by ray length - longer rays through cloud layer need more steps
        // Reference: vertical ray through 3000m layer = base_steps
        float ray_length_factor = ray_length / 3000.0;
        
        // Scale by distance - clouds at horizon need more steps
        float distance_factor = 1.0 + smoothstep(5000.0, 30000.0, t_enter) * 1.5;
        
        // Combine factors: more steps for shallow angles and distant clouds
        int num_steps = int(clamp(base_steps * ray_length_factor * distance_factor, 64.0, 600.0));
        float step_size = ray_length / float(num_steps);
        
        // Temporal jitter for TAA
        float jitter = noise_interleaved_gradient(pixel_uv * buffer_frame.resolution_render, true);
        float t = t_enter + step_size * jitter;
        
        // Phase function
        float cos_angle = dot(view_dir, sun_dir);
        float phase = lerp(hg_phase(cos_angle, -0.3), hg_phase(cos_angle, 0.8), 0.5);
        
        // Integration
        float transmittance = 1.0;
        float3 light_energy = 0.0;
        
        // Ambient lighting
        float3 sky_ambient = sun_color * 0.35;
        float3 ground_bounce = sun_color * float3(0.4, 0.35, 0.3) * 0.25;
        
        [loop]
        for (int i = 0; i < num_steps && transmittance > 0.01; i++)
        {
            float3 pos = cam_pos + view_dir * t;
            float density = sample_density(pos, coverage, cloud_type, time, seed);
            
            if (density > 0.01)
            {
                float light_atten = light_march(pos, sun_dir, coverage, cloud_type, time, seed);
                
                // Get local height for ambient calculation
                float local_bottom, local_top;
                get_local_cloud_bounds(pos, seed, local_bottom, local_top);
                float h = saturate((pos.y - local_bottom) / (local_top - local_bottom));
                
                // Direct sunlight contribution
                float3 radiance = lerp(sun_color * sun_intensity, buffer_frame.cloud_color, buffer_frame.cloud_darkness) * light_atten * phase;
                
                // Ambient contribution
                float3 ambient = lerp(ground_bounce, sky_ambient, h);
                radiance += lerp(ambient * 0.5, ambient, h) * sun_intensity * 0.6;
                
                float extinction = density * step_size * cloud_absorption;
                float absorbed = 1.0 - exp(-extinction);
                
                light_energy += transmittance * absorbed * radiance;
                transmittance *= exp(-extinction);
            }
            
            t += step_size;
            if (t > t_exit) break;
        }
        
        float alpha = (1.0 - transmittance) * horizon_fade;
        return float4(light_energy * horizon_fade, alpha);
    }
};

#endif // !LUT

float intersect_sphere(float3 origin, float3 direction, float3 center, float radius)
{
    float3 oc          = origin - center;
    float b            = dot(direction, oc);
    float c            = dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;
    
    if (discriminant < 0.0f)
        return -1.0f;
    
    float sqrt_disc = sqrt(discriminant);
    float t1        = -b - sqrt_disc;
    float t2        = -b + sqrt_disc;
    float t_near    = min(t1, t2);
    float t_far     = max(t1, t2);
    
    if (t_near >= 1e-3f)
        return t_near;
    
    if (t_far >= 1e-3f)
        return t_far;
    
    return -1.0f;
}

float compute_density_ozone(float height)
{
    float h_km = height * 0.001f;
    return exp(-((h_km - 22.0f) * (h_km - 22.0f)) / (2.0f * 8.0f * 8.0f));  // gaussian, sigma=8km
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
    float2 uv = (float2(thread_id.xy)) / resolution;
    
    // map UV to spherical coordinates (lat-long)
    float phi   = uv.x * PI2 + PI;
    float theta = (0.5f - uv.y) * PI;

    // convert spherical coords to direction vector
    float cos_theta       = cos(theta);
    float sin_theta       = sin(theta);
    float3 view_direction = normalize(float3(
        cos(phi) * cos_theta,
        sin_theta,
        sin(phi) * cos_theta
    ));

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
    float t_max             = intersect_sphere(buffer_frame.camera_position, view_direction, earth_center, earth_radius + atmosphere_height);
    if (t_max < 0)
    {
        tex_uav[thread_id.xy] = float4(atmosphere_color, 1.0f);
        return;
    }    
    float ds             = t_max / num_view_samples;
    float sun_zenith     = dot(sun_direction, up_direction);
    float w              = saturate((sun_zenith + 1.0f) * 0.5f);
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
        atmosphere_color *= 0.3f;
        sun_color         = 0.0f;
        star_color        = 0.0f;
        moon_color        = 0.0f;
    }

    // volumetric clouds (only above horizon)
    float4 cloud_result = float4(0.0, 0.0, 0.0, 0.0);
    if (!is_below_horizon)
    {
        float sun_elevation = sun_direction.y;
        float moon_elevation = moon_direction.y;
        
        // Determine if we should render clouds (either sun or moon provides light)
        bool sun_provides_light = sun_elevation > -0.33;
        bool moon_provides_light = moon_elevation > 0.0 && sun_elevation < 0.1;
        
        if (sun_provides_light || moon_provides_light)
        {
            float time = (float)buffer_frame.time * 0.001;
            
            // === SUNLIGHT CONTRIBUTION ===
            float4 sun_cloud_result = float4(0, 0, 0, 0);
            if (sun_provides_light)
            {
                float sun_intensity = light.intensity;
                float twilight_factor = saturate(sun_elevation * 5.0 + 1.0);
                sun_intensity *= max(twilight_factor, 0.05);
                
                float3 cloud_light_color = light_color;
                
                sun_cloud_result = clouds::compute(original_view_direction, sun_direction, cloud_light_color, sun_intensity, time, uv);
                
                // Fade sun contribution as it goes below horizon
                float sun_fade = saturate(sun_elevation * 3.0 + 1.0);
                sun_cloud_result.rgb *= sun_fade;
                sun_cloud_result.a *= sun_fade;
            }
            
            // === MOONLIGHT CONTRIBUTION ===
            float4 moon_cloud_result = float4(0, 0, 0, 0);
            if (moon_provides_light)
            {
                // Moonlight is much dimmer than sunlight (~0.001 of sun)
                // But we boost it for visibility while keeping it realistic
                float moon_intensity = light.intensity * 0.03;
                
                // Moonlight has a cool blue-silver tint
                float3 moon_light_color = float3(0.6, 0.7, 1.0);
                
                moon_cloud_result = clouds::compute(original_view_direction, moon_direction, moon_light_color, moon_intensity, time, uv);
                
                // Fade moon clouds as sun rises
                float moon_fade = saturate((0.1 - sun_elevation) * 5.0) * saturate(moon_elevation * 3.0);
                moon_cloud_result.rgb *= moon_fade;
                moon_cloud_result.a *= moon_fade;
            }
            
            // Combine sun and moon cloud lighting
            cloud_result.rgb = sun_cloud_result.rgb + moon_cloud_result.rgb;
            cloud_result.a = max(sun_cloud_result.a, moon_cloud_result.a);
        }
    }

    // combine atmosphere, celestial bodies, and clouds
    float cloud_alpha = cloud_result.a;
    
    // Celestial body occlusion based on cloud density
    // With higher coverage, clouds are denser and block more light
    // Scale thresholds by coverage: high coverage = lower threshold to start blocking
    float coverage = buffer_frame.cloud_coverage;
    float occlusion_start = lerp(0.5, 0.15, coverage);  // thin clouds start blocking earlier with high coverage
    float occlusion_end = lerp(0.9, 0.5, coverage);     // full occlusion earlier with high coverage
    float celestial_occlusion = smoothstep(occlusion_start, occlusion_end, cloud_alpha);
    
    // Atmosphere blends normally with all clouds
    float3 final_color = lerp(atmosphere_color, cloud_result.rgb, cloud_alpha);
    
    // Celestial bodies shine through thin clouds only
    final_color += sun_color * (1.0 - celestial_occlusion);
    final_color += star_color * (1.0 - celestial_occlusion);
    final_color += moon_color * (1.0 - celestial_occlusion);

    // out
    tex_uav[thread_id.xy] = float4(final_color, 1.0f);
}
#endif
