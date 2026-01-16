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

//= includes =========
#include "common.hlsl"
//====================

// ============================================================================
// physical constants
// ============================================================================

static const float3 up_direction       = float3(0.0, 1.0, 0.0);
static const float earth_radius        = 6360e3;                               // meters
static const float atmosphere_radius   = 6460e3;                               // meters (earth_radius + 100km)
static const float3 earth_center       = float3(0.0, -earth_radius, 0.0);      // world origin at sea level

// scattering coefficients at sea level (per meter)
static const float3 rayleigh_scatter   = float3(5.802e-6, 13.558e-6, 33.1e-6); // blue scatters most
static const float rayleigh_height     = 8000.0;                               // scale height in meters

static const float3 mie_scatter        = float3(3.996e-6, 3.996e-6, 3.996e-6); // wavelength independent
static const float3 mie_extinction     = float3(4.4e-6, 4.4e-6, 4.4e-6);       // scatter + absorption
static const float mie_height          = 1200.0;                               // scale height in meters
static const float mie_g               = 0.8;                                  // asymmetry factor (forward scattering)

// ozone absorption (peaks around 25km altitude)
static const float3 ozone_absorption   = float3(0.65e-6, 1.881e-6, 0.085e-6);
static const float ozone_center_height = 25000.0;
static const float ozone_width         = 15000.0;

// sun properties
static const float3 sun_illuminance    = float3(1.0, 0.98, 0.95) * 20.0;       // warm white, normalized
static const float sun_angular_radius  = 0.00935;                              // ~0.536 degrees in radians

// ground
static const float3 ground_albedo      = float3(0.3, 0.3, 0.3);

// sampling quality
static const int transmittance_samples = 40;
static const int multiscatter_samples  = 20;
static const int scattering_samples    = 48;

// ============================================================================
// utility functions
// ============================================================================

float safe_sqrt(float x)
{
    return sqrt(max(0.0, x));
}

// ray-sphere intersection, returns distance to intersection or -1
float2 ray_sphere_intersect(float3 origin, float3 direction, float3 center, float radius)
{
    float3 oc = origin - center;
    float b = dot(direction, oc);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;
    
    if (discriminant < 0.0)
        return float2(-1.0, -1.0);
    
    float sqrt_disc = sqrt(discriminant);
    return float2(-b - sqrt_disc, -b + sqrt_disc);
}

// get height above sea level
float get_height(float3 position)
{
    return length(position - earth_center) - earth_radius;
}

// get normalized height (0 = sea level, 1 = top of atmosphere)
float get_normalized_height(float3 position)
{
    float height = get_height(position);
    return saturate(height / (atmosphere_radius - earth_radius));
}

// ============================================================================
// density functions
// ============================================================================

float get_rayleigh_density(float height)
{
    return exp(-height / rayleigh_height);
}

float get_mie_density(float height)
{
    return exp(-height / mie_height);
}

float get_ozone_density(float height)
{
    // ozone layer gaussian distribution centered at 25km
    float h = height - ozone_center_height;
    return max(0.0, 1.0 - abs(h) / ozone_width);
}

// combined extinction coefficient at given height
float3 get_extinction(float height)
{
    float rayleigh_d = get_rayleigh_density(height);
    float mie_d      = get_mie_density(height);
    float ozone_d    = get_ozone_density(height);
    
    return rayleigh_scatter * rayleigh_d + mie_extinction * mie_d + ozone_absorption * ozone_d;
}

// combined scattering coefficient at given height
float3 get_scattering(float height)
{
    float rayleigh_d = get_rayleigh_density(height);
    float mie_d      = get_mie_density(height);
    
    return rayleigh_scatter * rayleigh_d + mie_scatter * mie_d;
}

// ============================================================================
// phase functions
// ============================================================================

float rayleigh_phase(float cos_theta)
{
    // classical rayleigh phase function
    return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

float henyey_greenstein_phase(float cos_theta, float g)
{
    // mie scattering phase function
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, 0.0001), 1.5));
}

float cornette_shanks_phase(float cos_theta, float g)
{
    // improved mie phase function (cornette-shanks)
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cos_theta * cos_theta);
    float denom = (8.0 * PI) * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cos_theta, 1.5);
    return num / max(denom, 0.0001);
}

// ============================================================================
// transmittance lut
// parameterization: x = cos(zenith), y = normalized height
// ============================================================================

float2 transmittance_lut_params_to_uv(float height, float cos_zenith)
{
    // remap height to 0-1 with better distribution near ground
    float h = safe_sqrt((height - earth_radius) / (atmosphere_radius - earth_radius));
    
    // improved cos_zenith mapping with horizon emphasis (bruneton 2017 style)
    // more texels allocated near horizon where transmittance changes rapidly
    float rho = safe_sqrt(max(0.0, height * height - earth_radius * earth_radius));
    float H = safe_sqrt(atmosphere_radius * atmosphere_radius - earth_radius * earth_radius);
    
    // horizon angle at this height
    float cos_horizon = -rho / height;
    
    // discriminant for atmospheric intersection
    float discriminant = height * height * (cos_zenith * cos_zenith - 1.0) + atmosphere_radius * atmosphere_radius;
    
    float x_mu;
    if (cos_zenith > cos_horizon)
    {
        // above horizon: map to [0.5, 1]
        x_mu = 0.5 + 0.5 * (cos_zenith - cos_horizon) / (1.0 - cos_horizon);
    }
    else
    {
        // below horizon: map to [0, 0.5]
        x_mu = 0.5 * (cos_zenith + 1.0) / (cos_horizon + 1.0);
    }
    
    return float2(saturate(x_mu), h);
}

void transmittance_uv_to_params(float2 uv, out float height, out float cos_zenith)
{
    float h = uv.y * uv.y;
    height = earth_radius + h * (atmosphere_radius - earth_radius);
    
    // inverse of the improved mapping
    float rho = safe_sqrt(max(0.0, height * height - earth_radius * earth_radius));
    float cos_horizon = -rho / height;
    
    if (uv.x > 0.5)
    {
        // above horizon
        float t = (uv.x - 0.5) * 2.0;
        cos_zenith = cos_horizon + t * (1.0 - cos_horizon);
    }
    else
    {
        // below horizon
        float t = uv.x * 2.0;
        cos_zenith = t * (cos_horizon + 1.0) - 1.0;
    }
}

float3 compute_transmittance_to_top(float3 position, float3 direction)
{
    float2 intersect = ray_sphere_intersect(position, direction, earth_center, atmosphere_radius);
    float t_max = intersect.y;
    
    if (t_max < 0.0)
        return float3(1.0, 1.0, 1.0);
    
    // check for ground intersection
    float2 ground_intersect = ray_sphere_intersect(position, direction, earth_center, earth_radius);
    if (ground_intersect.x > 0.0)
        t_max = ground_intersect.x;
    
    float dt = t_max / transmittance_samples;
    float3 optical_depth = float3(0.0, 0.0, 0.0);
    
    // trapezoidal integration for better accuracy at same sample count
    float3 prev_extinction = get_extinction(get_height(position));
    
    for (int i = 1; i <= transmittance_samples; i++)
    {
        float t = i * dt;
        float3 sample_pos = position + direction * t;
        float height = get_height(sample_pos);
        
        if (height < 0.0)
            break;
        
        float3 curr_extinction = get_extinction(height);
        
        // trapezoidal rule: (f(a) + f(b)) / 2 * dt
        optical_depth += (prev_extinction + curr_extinction) * 0.5 * dt;
        prev_extinction = curr_extinction;
    }
    
    return exp(-optical_depth);
}

// ============================================================================
// multi-scattering lut (sebh/epic approach)
// accounts for infinite bounces of scattered light
// ============================================================================

float3 compute_multiscatter(float height, float cos_sun_zenith, Texture2D transmittance_lut, SamplerState samp)
{
    float3 position = earth_center + float3(0.0, earth_radius + height, 0.0);
    float3 sun_dir = float3(safe_sqrt(1.0 - cos_sun_zenith * cos_sun_zenith), cos_sun_zenith, 0.0);
    
    float3 luminance_sum = float3(0.0, 0.0, 0.0);
    float3 f_ms_sum = float3(0.0, 0.0, 0.0);
    
    // integrate over sphere of directions
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
            
            // sample atmosphere along this direction
            float2 atmo_intersect = ray_sphere_intersect(position, ray_dir, earth_center, atmosphere_radius);
            float t_max = atmo_intersect.y;
            
            float2 ground_intersect = ray_sphere_intersect(position, ray_dir, earth_center, earth_radius);
            bool hits_ground = ground_intersect.x > 0.0;
            if (hits_ground)
                t_max = ground_intersect.x;
            
            float dt = t_max / multiscatter_samples;
            float3 transmittance = float3(1.0, 1.0, 1.0);
            float3 scattering_integral = float3(0.0, 0.0, 0.0);
            
            for (int k = 0; k < multiscatter_samples; k++)
            {
                float t = (k + 0.5) * dt;
                float3 sample_pos = position + ray_dir * t;
                float sample_height = get_height(sample_pos);
                
                float3 scatter = get_scattering(sample_height);
                float3 extinct = get_extinction(sample_height);
                
                // transmittance to sun
                float cos_sun = dot(normalize(sample_pos - earth_center), sun_dir);
                float2 sun_uv = transmittance_lut_params_to_uv(sample_height + earth_radius, cos_sun);
                float3 trans_sun = transmittance_lut.SampleLevel(samp, sun_uv, 0).rgb;
                
                float3 scattering_no_phase = scatter * trans_sun;
                float3 scatter_int = (scattering_no_phase - scattering_no_phase * exp(-extinct * dt)) / max(extinct, 0.0001);
                
                scattering_integral += scatter_int * transmittance;
                transmittance *= exp(-extinct * dt);
            }
            
            // ground contribution
            if (hits_ground)
            {
                float3 ground_pos = position + ray_dir * t_max;
                float3 ground_normal = normalize(ground_pos - earth_center);
                float ground_ndotl = saturate(dot(ground_normal, sun_dir));
                
                float cos_sun = dot(ground_normal, sun_dir);
                float2 sun_uv = transmittance_lut_params_to_uv(earth_radius, cos_sun);
                float3 trans_sun = transmittance_lut.SampleLevel(samp, sun_uv, 0).rgb;
                
                luminance_sum += transmittance * trans_sun * ground_ndotl * ground_albedo / PI;
            }
            
            // isotropic phase for multi-scattering
            float phase = 1.0 / (4.0 * PI);
            luminance_sum += scattering_integral * phase;
            f_ms_sum += scattering_integral * phase;
        }
    }
    
    float sphere_samples = sqrt_samples * sqrt_samples;
    luminance_sum *= 4.0 * PI / sphere_samples;
    f_ms_sum *= 4.0 * PI / sphere_samples;
    
    // infinite series: L = L0 / (1 - f_ms)
    float3 f_ms = f_ms_sum;
    float3 ms_factor = float3(1.0, 1.0, 1.0) / max(float3(1.0, 1.0, 1.0) - f_ms, 0.001);
    
    return luminance_sum * ms_factor;
}

// ============================================================================
// sky-view lut computation
// stores pre-integrated sky luminance for fast runtime lookup
// ============================================================================

float2 sky_view_lut_params_to_uv(float3 view_dir, float3 sun_dir)
{
    float cos_view_zenith = view_dir.y;
    float cos_light_view = dot(view_dir, sun_dir);
    
    // non-linear mapping to give more resolution near horizon
    float v_horizon = sqrt(1.0 - saturate(cos_view_zenith));
    if (cos_view_zenith < 0.0)
        v_horizon = 1.0 - v_horizon * 0.5;
    else
        v_horizon = 0.5 + v_horizon * 0.5;
    
    // u based on azimuth relative to sun
    float u = (cos_light_view + 1.0) * 0.5;
    
    return float2(u, v_horizon);
}

void sky_view_uv_to_params(float2 uv, float3 sun_dir, out float3 view_dir)
{
    float cos_light_view = uv.x * 2.0 - 1.0;
    
    // decode zenith from v
    float v = uv.y;
    float cos_view_zenith;
    if (v < 0.5)
    {
        float t = 1.0 - v * 2.0;
        cos_view_zenith = -(t * t);
    }
    else
    {
        float t = (v - 0.5) * 2.0;
        cos_view_zenith = t * t;
    }
    
    float sin_view_zenith = safe_sqrt(1.0 - cos_view_zenith * cos_view_zenith);
    
    // construct view direction
    float3 sun_right = normalize(cross(sun_dir, up_direction));
    float3 sun_forward = normalize(cross(up_direction, sun_right));
    
    // compute azimuth from cos_light_view
    float sin_light_view = safe_sqrt(1.0 - cos_light_view * cos_light_view);
    
    view_dir = normalize(
        up_direction * cos_view_zenith +
        sun_forward * sin_view_zenith * cos_light_view +
        sun_right * sin_view_zenith * sin_light_view
    );
}

// ============================================================================
// main sky color computation
// ============================================================================

float3 compute_sky_luminance(
    float3 position,
    float3 view_dir,
    float3 sun_dir,
    Texture2D transmittance_lut,
    Texture2D multiscatter_lut,
    SamplerState samp,
    float jitter = 0.5)
{
    // ray march through atmosphere
    float2 atmo_intersect = ray_sphere_intersect(position, view_dir, earth_center, atmosphere_radius);
    if (atmo_intersect.y < 0.0)
        return float3(0.0, 0.0, 0.0);
    
    float t_min = max(0.0, atmo_intersect.x);
    float t_max = atmo_intersect.y;
    
    // check for ground intersection
    float2 ground_intersect = ray_sphere_intersect(position, view_dir, earth_center, earth_radius);
    if (ground_intersect.x > 0.0)
        t_max = min(t_max, ground_intersect.x);
    
    // phase functions
    float cos_theta = dot(view_dir, sun_dir);
    float phase_r = rayleigh_phase(cos_theta);
    float phase_m = cornette_shanks_phase(cos_theta, mie_g);
    
    float3 luminance = float3(0.0, 0.0, 0.0);
    float3 transmittance = float3(1.0, 1.0, 1.0);
    
    // uniform sampling with analytical integration for clean results
    float dt = (t_max - t_min) / scattering_samples;
    
    for (int i = 0; i < scattering_samples; i++)
    {
        float t = t_min + (i + jitter) * dt;
        float3 sample_pos = position + view_dir * t;
        float height = get_height(sample_pos);
        
        if (height < 0.0 || height > atmosphere_radius - earth_radius)
            continue;
        
        float3 extinction = get_extinction(height);
        float rayleigh_d = get_rayleigh_density(height);
        float mie_d = get_mie_density(height);
        
        // transmittance to sun
        float3 up_at_sample = normalize(sample_pos - earth_center);
        float cos_sun = dot(up_at_sample, sun_dir);
        
        // when sun is below local horizon, no direct light reaches this point
        // aggressive fade to prevent brightness spikes near horizon
        float sun_visible = smoothstep(-0.05, 0.1, cos_sun);
        
        float2 sun_uv = transmittance_lut_params_to_uv(height + earth_radius, max(cos_sun, 0.0));
        float3 trans_sun = transmittance_lut.SampleLevel(samp, sun_uv, 0).rgb * sun_visible;
        
        // single scattering
        float3 scatter_r = rayleigh_scatter * rayleigh_d * phase_r;
        float3 scatter_m = mie_scatter * mie_d * phase_m;
        float3 scattering = (scatter_r + scatter_m) * trans_sun;
        
        // multi-scattering contribution (also attenuated when sun below local horizon)
        float2 ms_uv = float2(max(cos_sun, 0.0) * 0.5 + 0.5, saturate(height / (atmosphere_radius - earth_radius)));
        float3 ms = multiscatter_lut.SampleLevel(samp, ms_uv, 0).rgb * sun_visible;
        float3 ms_scatter = (rayleigh_scatter * rayleigh_d + mie_scatter * mie_d) * ms;
        
        // analytical integration of exponential transmittance (more accurate than discrete)
        float3 total_scatter = scattering + ms_scatter;
        float3 scatter_int = total_scatter * (1.0 - exp(-extinction * dt)) / max(extinction, 1e-6);
        
        luminance += scatter_int * transmittance;
        transmittance *= exp(-extinction * dt);
        
        // early out if transmittance is negligible
        if (all(transmittance < 1e-6))
            break;
    }
    
    return luminance * sun_illuminance;
}

// ============================================================================
// sun disc rendering
// ============================================================================

float3 compute_sun_disc(float3 view_dir, float3 sun_dir, float3 transmittance)
{
    float cos_angle = dot(view_dir, sun_dir);
    
    // wider edge softness for smooth anti-aliased sun disc
    float edge_softness = sun_angular_radius * 0.5;
    float cos_inner = cos(max(0.0, sun_angular_radius - edge_softness));
    float cos_outer = cos(sun_angular_radius + edge_softness);
    
    // smoothstep for smooth falloff
    float sun_edge = smoothstep(cos_outer, cos_inner, cos_angle);
    
    // limb darkening
    float angle_approx = safe_sqrt(2.0 * max(0.0, 1.0 - cos_angle));
    float r = saturate(angle_approx / sun_angular_radius);
    float mu = safe_sqrt(1.0 - r * r);
    float limb_darkening = 0.3 + 0.93 * mu - 0.23 * mu * mu;
    
    return sun_illuminance * transmittance * sun_edge * limb_darkening * 1000.0;
}

// ============================================================================
// stars and moon (artistic)
// ============================================================================

struct stars
{
    static float3 blackbody(float temp)
    {
        float3 color = float3(1.0, 1.0, 1.0);
        temp = clamp(temp, 3000.0, 15000.0);
        
        // approximate blackbody rgb
        if (temp < 6600.0)
        {
            color.g = 0.39008 * log(temp * 0.01) - 0.63184;
            color.b = 0.54321 * log(temp * 0.001 - 0.6) - 1.19625;
        }
        else
        {
            color.r = 1.29294 * pow(temp * 0.001 - 6.0, -0.1332);
            color.g = 1.29294 * pow(temp * 0.001 - 6.0, -0.1332);
            color.b = 1.12989 * pow(temp * 0.001 - 6.0, -0.0755);
        }
        
        return saturate(color);
    }
    
    static float2 hash22(float2 p)
    {
        float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.103, 0.0973));
        p3 += dot(p3, p3.yzx + 33.33);
        return frac((p3.xx + p3.yz) * p3.zy);
    }
    
    static float3 compute_color(float2 uv, float3 sun_dir)
    {
        float sun_elevation = dot(sun_dir, up_direction);
        float night_factor = saturate(-sun_elevation * 5.0);
        
        if (night_factor < 0.01)
            return float3(0.0, 0.0, 0.0);
        
        float3 color = float3(0.0, 0.0, 0.0);
        float2 star_uv = uv * 400.0;
        float2 cell = floor(star_uv);
        
        for (int y = -1; y <= 1; y++)
        {
            for (int x = -1; x <= 1; x++)
            {
                float2 cell_center = cell + float2(x, y) + 0.5;
                float2 hash = hash22(cell_center);
                
                if (hash.x > 0.97)
                {
                    float2 star_pos = cell_center + (hash - 0.5) * 0.5;
                    float dist = length(star_uv - star_pos);
                    
                    // small, sharp star points
                    float kernel_width = 0.04; // much smaller kernel
                    float brightness = exp(-dist * dist / (kernel_width * kernel_width));
                    brightness *= (hash.x - 0.97) * 30.0;
                    
                    float temp = lerp(4000.0, 12000.0, hash.y);
                    color += blackbody(temp) * brightness * night_factor;
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
float3 compute_moon(float3 view_dir, float3 sun_dir)
{
    float3 moon_dir = -sun_dir;
    float moon_elevation = dot(moon_dir, up_direction);
    
    if (moon_elevation < 0.0)
        return float3(0.0, 0.0, 0.0);
    
    float cos_angle = dot(view_dir, moon_dir);
    float moon_radius = 0.015;
    
    // work in cosine space to avoid acos instability
    float cos_moon_radius = cos(moon_radius);
    float edge_softness = moon_radius * 0.1;
    float cos_inner = cos(moon_radius - edge_softness);
    float cos_outer = cos(moon_radius + edge_softness);
    float moon_disc = smoothstep(cos_outer, cos_inner, cos_angle);
    
    // stable angle approximation for limb darkening
    float angle_approx = safe_sqrt(2.0 * max(0.0, 1.0 - cos_angle));
    float r = saturate(angle_approx / moon_radius);
    float limb = safe_sqrt(1.0 - r * r);
    float moon_limb = 0.7 + 0.3 * limb;
    
    // moon is full when opposite sun (always the case since moon_dir = -sun_dir)
    float3 moon_color_val = float3(0.8, 0.85, 0.95) * 0.02;
    
    float night_factor = saturate(-dot(sun_dir, up_direction) * 3.0);
    
    return moon_color_val * moon_disc * moon_limb * night_factor;
}

// ============================================================================
// compute shaders
// ============================================================================

#if defined(TRANSMITTANCE_LUT)
// transmittance lut pass - precomputes optical depth to atmosphere top
[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    
    if (any(thread_id.xy >= uint2(resolution)))
        return;
    
    float2 uv = (thread_id.xy + 0.5) / resolution;
    
    float height, cos_zenith;
    transmittance_uv_to_params(uv, height, cos_zenith);
    
    float3 position = earth_center + float3(0.0, height, 0.0);
    float sin_zenith = safe_sqrt(1.0 - cos_zenith * cos_zenith);
    float3 direction = float3(sin_zenith, cos_zenith, 0.0);
    
    float3 transmittance = compute_transmittance_to_top(position, direction);
    
    tex_uav[thread_id.xy] = float4(transmittance, 1.0);
}

#elif defined(MULTISCATTER_LUT)
// multi-scatter lut pass - approximates infinite bounce scattering
[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    
    if (any(thread_id.xy >= uint2(resolution)))
        return;
    
    float2 uv = (thread_id.xy + 0.5) / resolution;
    
    float cos_sun_zenith = uv.x * 2.0 - 1.0;
    float height = uv.y * (atmosphere_radius - earth_radius);
    
    float3 ms = compute_multiscatter(height, cos_sun_zenith, tex, GET_SAMPLER(sampler_bilinear_clamp));
    
    tex_uav[thread_id.xy] = float4(ms, 1.0);
}

#elif defined(LUT)
// legacy 3d lut for backward compatibility
[numthreads(8, 8, 8)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint3 lut_dimensions;
    tex3d_uav.GetDimensions(lut_dimensions.x, lut_dimensions.y, lut_dimensions.z);
    
    if (any(thread_id >= lut_dimensions))
        return;
    
    float height = (float)thread_id.y / (lut_dimensions.y - 1) * (atmosphere_radius - earth_radius);
    float cos_zenith = (float)thread_id.x / (lut_dimensions.x - 1) * 2.0 - 1.0;
    float sun_zenith = (float)thread_id.z / (lut_dimensions.z - 1) * 2.0 - 1.0;
    
    float3 position = earth_center + float3(0.0, earth_radius + height, 0.0);
    float sin_zenith = safe_sqrt(1.0 - cos_zenith * cos_zenith);
    float3 direction = float3(sin_zenith, cos_zenith, 0.0);
    
    float3 transmittance = compute_transmittance_to_top(position, direction);
    
    tex3d_uav[thread_id.xyz] = float4(transmittance, 1.0);
}

#else
// main sky rendering pass
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    
    if (any(thread_id.xy >= uint2(resolution)))
        return;
    
    float2 uv = (float2(thread_id.xy) + 0.5) / resolution;
    
    // map uv to spherical coordinates (equirectangular)
    float phi = uv.x * PI2 + PI;
    float theta = (0.5 - uv.y) * PI;
    
    float cos_theta = cos(theta);
    float3 view_dir = normalize(float3(
        cos(phi) * cos_theta,
        sin(theta),
        sin(phi) * cos_theta
    ));
    
    // store original direction for sun/moon/stars
    float3 original_view_dir = view_dir;
    
    // below horizon: mirror the view direction to sample sky above
    // this fakes diffuse ground bounce for better ibl
    bool is_below_horizon = view_dir.y < 0.0;
    if (is_below_horizon)
    {
        view_dir.y = -view_dir.y;
    }
    
    // get sun direction from light (normalize to avoid precision issues)
    Light light;
    Surface surface;
    light.Build(0, surface);
    float3 sun_dir = normalize(-light.forward);
    
    // sun elevation for day/night transition
    // elevation: 1 = overhead, 0 = horizon, negative = below horizon
    float sun_elevation = dot(sun_dir, up_direction);
    
    // day/night factor based on sun elevation
    // use exponential falloff that's continuous and drops quickly below horizon
    // at elevation 0: day_factor = 0.5
    // at elevation 0.1: day_factor ≈ 0.82
    // at elevation -0.1: day_factor ≈ 0.18
    // at elevation -0.2: day_factor ≈ 0.05
    float day_factor = 1.0 / (1.0 + exp(-sun_elevation * 20.0)); // sigmoid centered at horizon
    
    // camera position in atmosphere
    float3 camera_pos = buffer_frame.camera_position;
    float camera_height = get_height(camera_pos);
    
    // clamp camera to atmosphere bounds
    if (camera_height < 0.0)
        camera_pos = earth_center + normalize(camera_pos - earth_center) * (earth_radius + 1.0);
    else if (camera_height > atmosphere_radius - earth_radius)
        camera_pos = earth_center + normalize(camera_pos - earth_center) * (atmosphere_radius - 1.0);
    
    // center sampling for clean, stable results
    float jitter = 0.5;
    
    // compute sky luminance (with mirrored direction if below horizon)
    float3 luminance = compute_sky_luminance(
        camera_pos,
        view_dir,
        sun_dir,
        tex,
        tex2,
        GET_SAMPLER(sampler_bilinear_clamp),
        jitter
    );
    
    // artistic touches (sun, stars, moon)
    float3 sun_color  = float3(0.0, 0.0, 0.0);
    float3 star_color = float3(0.0, 0.0, 0.0);
    float3 moon_color = float3(0.0, 0.0, 0.0);
    
    if (is_below_horizon)
    {
        // darken to simulate diffuse ground bounce when doing IBL
        luminance *= 0.3f;
    }
    else
    {
        // sun disc visible until it sets below horizon
        if (sun_elevation > -0.02)
        {
            float3 cam_up = normalize(camera_pos - earth_center);
            float cos_sun = dot(cam_up, sun_dir);
            float2 sun_uv = transmittance_lut_params_to_uv(length(camera_pos - earth_center), max(cos_sun, 0.0));
            float3 sun_transmittance = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), sun_uv, 0).rgb;
            
            // fade sun disc as it crosses the horizon (eclipsed by horizon)
            float sun_horizon_fade = smoothstep(-0.02, 0.02, sun_elevation);
            sun_color = compute_sun_disc(original_view_dir, sun_dir, sun_transmittance) * sun_horizon_fade;
        }
        
        // stars (only at night)
        star_color = stars::compute_color(uv, sun_dir);
        
        // moon
        moon_color = compute_moon(original_view_dir, sun_dir);
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
    
    // artistic intensity from light (optional exposure control)
    float intensity_scale = saturate(light.intensity / 100000.0);
    intensity_scale = lerp(0.5, 1.5, intensity_scale);
    
    // apply day/night darkening to atmospheric scattering
    luminance *= day_factor;
    
    // subtle night sky ambient (deep blue) when sun is below horizon
    float night_factor = 1.0 - day_factor;
    float3 night_ambient = float3(0.001, 0.002, 0.004) * night_factor; // very subtle deep blue
    
    float3 final_color = (luminance + night_ambient + sun_color + star_color + moon_color) * intensity_scale;
    
    // safety: prevent any unexpected brightness spikes
    final_color = min(final_color, 100.0);
    
    tex_uav[thread_id.xy] = float4(final_color, 1.0);
}

#endif
