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
#include "../common.hlsl"
//====================

// constants - atmosphere
static const float3 up_direction       = float3(0.0, 1.0, 0.0);
static const float earth_radius        = 6360e3;
static const float atmosphere_radius   = 6460e3;
static const float3 earth_center       = float3(0.0, -earth_radius, 0.0);

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
static const float3 sun_illuminance    = float3(1.0, 0.98, 0.95) * 20.0;
static const float sun_angular_radius  = 0.00935;
static const float3 ground_albedo      = float3(0.3, 0.3, 0.3);

// constants - sampling
static const int transmittance_samples = 32;
static const int multiscatter_samples  = 16;
static const int scattering_samples    = 16;

// utility
float safe_sqrt(float x) { return sqrt(max(0.0, x)); }

float2 ray_sphere_intersect(float3 origin, float3 direction, float3 center, float radius)
{
    float3 oc = origin - center;
    float b = dot(direction, oc);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;
    
    if (discriminant < 0.0)
        return float2(-1.0, -1.0);
    
    float d = sqrt(discriminant);
    return float2(-b - d, -b + d);
}

float get_height(float3 position)
{
    return length(position - earth_center) - earth_radius;
}

// density functions
float get_rayleigh_density(float height) { return exp(-height / rayleigh_height); }
float get_mie_density(float height)      { return exp(-height / mie_height); }
float get_ozone_density(float height)    { return max(0.0, 1.0 - abs(height - ozone_center_height) / ozone_width); }

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

float henyey_greenstein_phase(float cos_theta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001), 1.5));
}

float cornette_shanks_phase(float cos_theta, float g)
{
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cos_theta * cos_theta);
    float denom = (8.0 * PI) * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cos_theta, 1.5);
    return num / max(denom, 0.0001);
}

// transmittance lut uv mapping with horizon emphasis
float2 transmittance_lut_params_to_uv(float height, float cos_zenith)
{
    float h = safe_sqrt((height - earth_radius) / (atmosphere_radius - earth_radius));
    float rho = safe_sqrt(max(0.0, height * height - earth_radius * earth_radius));
    float cos_horizon = -rho / height;
    
    float x_mu;
    if (cos_zenith > cos_horizon)
        x_mu = 0.5 + 0.5 * (cos_zenith - cos_horizon) / (1.0 - cos_horizon);
    else
        x_mu = 0.5 * (cos_zenith + 1.0) / (cos_horizon + 1.0);
    
    return float2(saturate(x_mu), h);
}

void transmittance_uv_to_params(float2 uv, out float height, out float cos_zenith)
{
    float h = uv.y * uv.y;
    height = earth_radius + h * (atmosphere_radius - earth_radius);
    
    float rho = safe_sqrt(max(0.0, height * height - earth_radius * earth_radius));
    float cos_horizon = -rho / height;
    
    if (uv.x > 0.5)
    {
        float t = (uv.x - 0.5) * 2.0;
        cos_zenith = cos_horizon + t * (1.0 - cos_horizon);
    }
    else
    {
        float t = uv.x * 2.0;
        cos_zenith = t * (cos_horizon + 1.0) - 1.0;
    }
}

// compute optical depth from position to atmosphere top
float3 compute_transmittance_to_top(float3 position, float3 direction)
{
    float2 intersect = ray_sphere_intersect(position, direction, earth_center, atmosphere_radius);
    float t_max = intersect.y;
    if (t_max < 0.0)
        return float3(1.0, 1.0, 1.0);
    
    float2 ground_hit = ray_sphere_intersect(position, direction, earth_center, earth_radius);
    if (ground_hit.x > 0.0)
        t_max = ground_hit.x;
    
    // trapezoidal integration
    float dt = t_max / transmittance_samples;
    float3 optical_depth = float3(0.0, 0.0, 0.0);
    float3 prev_ext = get_extinction(get_height(position));
    
    for (int i = 1; i <= transmittance_samples; i++)
    {
        float3 sample_pos = position + direction * i * dt;
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
    float3 position = earth_center + float3(0.0, earth_radius + height, 0.0);
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
            
            float dt = t_max / multiscatter_samples;
            float3 trans = float3(1.0, 1.0, 1.0);
            float3 scatter_integral = float3(0.0, 0.0, 0.0);
            
            for (int k = 0; k < multiscatter_samples; k++)
            {
                float3 sample_pos = position + ray_dir * (k + 0.5) * dt;
                float sample_h = get_height(sample_pos);
                
                float3 scatter = get_scattering(sample_h);
                float3 extinct = get_extinction(sample_h);
                
                float cos_sun = dot(normalize(sample_pos - earth_center), sun_dir);
                float2 sun_uv = transmittance_lut_params_to_uv(sample_h + earth_radius, cos_sun);
                float3 trans_sun = transmittance_lut.SampleLevel(samp, sun_uv, 0).rgb;
                
                float3 scatter_no_phase = scatter * trans_sun;
                float3 s_int = (scatter_no_phase - scatter_no_phase * exp(-extinct * dt)) / max(extinct, 0.0001);
                
                scatter_integral += s_int * trans;
                trans *= exp(-extinct * dt);
            }
            
            // ground contribution
            if (hits_ground)
            {
                float3 ground_pos = position + ray_dir * t_max;
                float3 ground_n = normalize(ground_pos - earth_center);
                float ndotl = saturate(dot(ground_n, sun_dir));
                
                float2 sun_uv = transmittance_lut_params_to_uv(earth_radius, dot(ground_n, sun_dir));
                float3 trans_sun = transmittance_lut.SampleLevel(samp, sun_uv, 0).rgb;
                
                luminance_sum += trans * trans_sun * ndotl * ground_albedo / PI;
            }
            
            float phase = 1.0 / (4.0 * PI);
            luminance_sum += scatter_integral * phase;
            f_ms_sum += scatter_integral * phase;
        }
    }
    
    float sphere_samples = sqrt_samples * sqrt_samples;
    luminance_sum *= 4.0 * PI / sphere_samples;
    f_ms_sum *= 4.0 * PI / sphere_samples;
    
    // infinite series approximation
    return luminance_sum / max(float3(1.0, 1.0, 1.0) - f_ms_sum, 0.001);
}

// main sky color computation
float3 compute_sky_luminance(
    float3 position, float3 view_dir, float3 sun_dir,
    Texture2D transmittance_lut, Texture2D multiscatter_lut,
    SamplerState samp, float jitter = 0.5)
{
    float2 atmo_hit = ray_sphere_intersect(position, view_dir, earth_center, atmosphere_radius);
    if (atmo_hit.y < 0.0) return float3(0.0, 0.0, 0.0);
    
    float t_min = max(0.0, atmo_hit.x);
    float t_max = atmo_hit.y;
    
    float2 ground_hit = ray_sphere_intersect(position, view_dir, earth_center, earth_radius);
    if (ground_hit.x > 0.0) t_max = min(t_max, ground_hit.x);
    
    float cos_theta = dot(view_dir, sun_dir);
    float phase_r = rayleigh_phase(cos_theta);
    float phase_m = cornette_shanks_phase(cos_theta, mie_g);
    
    float3 luminance = float3(0.0, 0.0, 0.0);
    float3 trans = float3(1.0, 1.0, 1.0);
    float dt = (t_max - t_min) / scattering_samples;
    
    for (int i = 0; i < scattering_samples; i++)
    {
        float3 sample_pos = position + view_dir * (t_min + (i + jitter) * dt);
        float height = get_height(sample_pos);
        
        if (height < 0.0 || height > atmosphere_radius - earth_radius)
            continue;
        
        float3 extinction = get_extinction(height);
        float rayleigh_d = get_rayleigh_density(height);
        float mie_d = get_mie_density(height);
        
        float3 up = normalize(sample_pos - earth_center);
        float cos_sun = dot(up, sun_dir);
        
        // allow slightly below-horizon angles for proper twilight colors
        // the transmittance lut uv mapping handles negative cos_zenith values
        float2 sun_uv = transmittance_lut_params_to_uv(height + earth_radius, cos_sun);
        float3 trans_sun = transmittance_lut.SampleLevel(samp, sun_uv, 0).rgb;
        
        // smooth fade for sun visibility near horizon (keeps illumination gradual)
        float sun_visible = smoothstep(-0.1, 0.05, cos_sun);
        trans_sun *= sun_visible;
        
        // single + multi scatter
        float3 scatter_r = rayleigh_scatter * rayleigh_d * phase_r;
        float3 scatter_m = mie_scatter * mie_d * phase_m;
        float3 scattering = (scatter_r + scatter_m) * trans_sun;
        
        float2 ms_uv = float2(max(cos_sun, 0.0) * 0.5 + 0.5, saturate(height / (atmosphere_radius - earth_radius)));
        float3 ms = multiscatter_lut.SampleLevel(samp, ms_uv, 0).rgb * sun_visible;
        float3 ms_scatter = (rayleigh_scatter * rayleigh_d + mie_scatter * mie_d) * ms;
        
        float3 total = scattering + ms_scatter;
        float3 scatter_int = total * (1.0 - exp(-extinction * dt)) / max(extinction, 1e-6);
        
        luminance += scatter_int * trans;
        trans *= exp(-extinction * dt);
        
        if (all(trans < 1e-6)) break;
    }
    
    return luminance * sun_illuminance;
}

// sun disc with limb darkening
float3 compute_sun_disc(float3 view_dir, float3 sun_dir, float3 transmittance)
{
    float cos_angle = dot(view_dir, sun_dir);
    float edge_softness = sun_angular_radius * 0.5;
    float sun_edge = smoothstep(cos(sun_angular_radius + edge_softness),
                                cos(max(0.0, sun_angular_radius - edge_softness)), cos_angle);
    
    float angle_approx = safe_sqrt(2.0 * max(0.0, 1.0 - cos_angle));
    float r = saturate(angle_approx / sun_angular_radius);
    float mu = safe_sqrt(1.0 - r * r);
    float limb = 0.3 + 0.93 * mu - 0.23 * mu * mu;
    
    return sun_illuminance * transmittance * sun_edge * limb * 1000.0;
}

// stars
struct stars
{
    static float3 blackbody(float temp)
    {
        float3 color = float3(1.0, 1.0, 1.0);
        temp = clamp(temp, 3000.0, 15000.0);
        
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
        float night = saturate(-dot(sun_dir, up_direction) * 5.0);
        if (night < 0.01) return float3(0.0, 0.0, 0.0);
        
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
                    float brightness = exp(-dist * dist / 0.0016) * (hash.x - 0.97) * 30.0;
                    color += blackbody(lerp(4000.0, 12000.0, hash.y)) * brightness * night;
                }
            }
        }
        return color;
    }
};

// moon
float3 compute_moon(float3 view_dir, float3 sun_dir)
{
    float3 moon_dir = -sun_dir;
    float moon_elev = dot(moon_dir, up_direction);
    if (moon_elev < 0.0) return float3(0.0, 0.0, 0.0);
    
    float cos_angle = dot(view_dir, moon_dir);
    float moon_radius = 0.015;
    float edge_softness = moon_radius * 0.1;
    
    float moon_disc = smoothstep(cos(moon_radius + edge_softness),
                                  cos(moon_radius - edge_softness), cos_angle);
    
    float angle_approx = safe_sqrt(2.0 * max(0.0, 1.0 - cos_angle));
    float r = saturate(angle_approx / moon_radius);
    float limb = 0.7 + 0.3 * safe_sqrt(1.0 - r * r);
    
    float night = saturate(-dot(sun_dir, up_direction) * 3.0);
    return float3(0.8, 0.85, 0.95) * 0.02 * moon_disc * limb * night;
}

// volumetric clouds - only for main sky pass
#if !defined(LUT) && !defined(TRANSMITTANCE_LUT) && !defined(MULTISCATTER_LUT)

static const float cloud_base_bottom = 1500.0;
static const float cloud_base_top    = 4500.0;
static const float cloud_scale       = 0.00003;
static const float detail_scale      = 0.0003;
static const float cloud_absorption  = 0.3;
static const float cloud_wind_speed  = 10.0;
static const int cloud_steps         = 64;
static const int light_steps         = 8;   // slightly more samples for better self-shadowing

struct cloud_result
{
    float3 color;
    float  alpha;
    float3 transmittance;
    float3 inscatter;
};

struct clouds
{
    static float hash21(float2 p, float seed)
    {
        float3 p3 = frac(float3(p.xyx) * 0.1031 + seed * 0.1);
        p3 += dot(p3, p3.yzx + 33.33);
        return frac((p3.x + p3.y) * p3.z);
    }
    
    static float remap(float v, float l1, float h1, float l2, float h2)
    {
        return l2 + (v - l1) * (h2 - l2) / max(h1 - l1, 0.0001);
    }
    
    static float height_gradient(float h, float ctype)
    {
        return smoothstep(0.0, 0.1, h) * smoothstep(0.4 + ctype * 0.6, 0.2 + ctype * 0.3, h);
    }
    
    static float smooth_noise(float2 p, float seed)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        float2 u = f * f * (3.0 - 2.0 * f);
        
        float a = hash21(i, seed);
        float b = hash21(i + float2(1, 0), seed);
        float c = hash21(i + float2(0, 1), seed);
        float d = hash21(i + float2(1, 1), seed);
        
        return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
    }
    
    static void get_cloud_bounds(float3 pos, float seed, out float bottom, out float top)
    {
        float2 coord = pos.xz * 0.00005;
        bottom = lerp(1200.0, 2200.0, smooth_noise(coord, seed));
        top = lerp(3500.0, 4500.0, smooth_noise(coord * 1.7 + 100.0, seed * 2.3));
        top = max(top, bottom + 1500.0);
    }
    
    static float3 domain_warp(float3 p, float seed)
    {
        // simplified domain warp - fewer sin() calls for better performance
        float sp = seed * 0.31415926;
        
        float3 warp;
        warp.x = sin(p.z * 0.00008 + p.y * 0.00007 + sp) * 2000.0;
        warp.y = sin(p.x * 0.00007 + p.z * 0.00008 + sp) * 500.0;
        warp.z = sin(p.y * 0.00006 + p.x * 0.00009 + sp) * 2000.0;
        
        return p + warp;
    }
    
    static float sample_density(float3 pos, float coverage, float ctype, float time, float seed)
    {
        float bottom, top;
        get_cloud_bounds(pos, seed, bottom, top);
        
        if (pos.y < bottom || pos.y > top)
            return 0.0;
        
        float h = (pos.y - bottom) / (top - bottom);
        
        // wind animation
        float3 wind = buffer_frame.wind;
        float wspeed = length(wind) * cloud_wind_speed;
        float3 wdir = wspeed > 0.001 ? normalize(wind) : float3(1, 0, 0);
        float3 anim_pos = pos + wdir * time * wspeed;
        
        // transform and warp
        float3 seed_pos = anim_pos + float3(seed * 50000.0, seed * 30000.0, seed * 70000.0);
        float3 warped = domain_warp(seed_pos, seed);
        
        // distance fade and lod factor
        float dist = length(pos.xz);
        float dist_fade = 1.0 - smoothstep(25000.0, 40000.0, dist);
        float detail_lod = 1.0 - smoothstep(12000.0, 25000.0, dist); // preserve detail at medium distance
        
        // shape noise
        float4 shape = tex3d_cloud_shape.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), warped * cloud_scale, 0);
        float noise = shape.r * 0.625 + shape.g * 0.25 + shape.b * 0.125;
        noise *= height_gradient(h, ctype);
        
        float density = saturate(remap(noise, 1.0 - coverage, 1.0, 0.0, 1.0)) * dist_fade;
        if (density < 0.01) return 0.0;
        
        // detail erosion - skip for distant clouds (not visible anyway)
        if (detail_lod > 0.01)
        {
            float3 detail_uvw = domain_warp(seed_pos * 1.1, seed) * detail_scale;
            detail_uvw.y += time * 0.01;
            float4 detail = tex3d_cloud_detail.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), detail_uvw, 0);
            float detail_noise = detail.r * 0.625 + detail.g * 0.25 + detail.b * 0.125;
            density = saturate(density - detail_noise * (1.0 - density) * 0.35 * detail_lod);
        }
        
        return density;
    }
    
    static float hg_phase(float cos_theta, float g)
    {
        float g2 = g * g;
        return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001), 1.5));
    }
    
    struct light_result { float attenuation; float ao; };
    
    static light_result light_march(float3 pos, float3 light_dir, float3 view_dir,
                                     float coverage, float ctype, float time, float seed)
    {
        light_result r;
        r.attenuation = 1.0;
        r.ao = 1.0;
        
        // use local cloud bounds for consistency with sample_density
        float local_bottom, local_top;
        get_cloud_bounds(pos, seed, local_bottom, local_top);
        float local_thickness = local_top - local_bottom;
        
        float dist = min((local_top - pos.y) / max(light_dir.y, 0.01), local_thickness);
        float step = dist / float(light_steps);
        float optical = 0.0;
        
        // upward sampling for ambient occlusion
        float up_dist = local_top - pos.y;
        float up_step = up_dist / float(light_steps / 2);
        float up_optical = 0.0;
        
        // optimized light march - fewer samples with early termination
        [loop]
        for (int i = 0; i < light_steps; i++)
        {
            float3 p = pos + light_dir * step * (float(i) + 0.5);
            if (p.y > local_top) break;
            
            float d = sample_density(p, coverage, ctype, time, seed);
            optical += d * step;
            
            // early out if we've accumulated enough optical depth (light is blocked)
            if (optical > 3.0) break;
            
            // simplified ao - only sample on first iteration
            if (i == 0)
            {
                float3 up_p = pos + float3(0, 1, 0) * up_dist * 0.5;
                if (up_p.y <= local_top)
                    up_optical = sample_density(up_p, coverage, ctype, time, seed) * up_dist;
            }
        }
        
        // beer-powder multi-scatter approximation (tuned for balanced brightness)
        // primary extinction (beer's law) - stronger absorption for darker clouds
        float beer = exp(-optical * cloud_absorption * 2.0);
        
        // powder effect: forward scattering in thin cloud regions when backlit
        float cos_view_light = dot(light_dir, -view_dir);
        float backlit = saturate(cos_view_light * 0.5 + 0.5);
        float depth_powder = 1.0 - exp(-optical * cloud_absorption * 2.5);
        float powder = depth_powder * depth_powder * backlit;
        
        // multi-scatter approximation - reduced contribution for less brightness
        float multi_scatter_contrib = exp(-optical * cloud_absorption * 0.4);
        float multi_forward = multi_scatter_contrib * (0.5 + 0.2 * backlit);
        
        // energy-conserving blend with reduced multi-scatter
        r.attenuation = saturate(beer + powder * multi_forward * 0.25 + multi_forward * 0.08);
        r.ao = exp(-up_optical * cloud_absorption * 0.6);
        return r;
    }
    
    static float3 get_sun_color(float3 sun_dir, float altitude, Texture2D trans_lut, SamplerState samp)
    {
        float2 uv = transmittance_lut_params_to_uv(earth_radius + altitude, sun_dir.y);
        return sun_illuminance * trans_lut.SampleLevel(samp, uv, 0).rgb;
    }
    
    static float3 get_ambient(float3 sun_dir, Texture2D ms_lut, SamplerState samp)
    {
        float2 uv = float2(sun_dir.y * 0.5 + 0.5, 0.02);
        float3 ms = ms_lut.SampleLevel(samp, uv, 0).rgb;
        float3 sky = ms * sun_illuminance * 2.0; // reduced from 4.0 for better balance
        return sky + float3(0.3, 0.25, 0.2) * sky * 0.1;
    }
    
    static float3 get_ground_bounce(float3 sun_dir, float height_fraction, Texture2D trans_lut, SamplerState samp)
    {
        // ground bounce illumination - light reflecting from earth surface into cloud bottoms
        float sun_vis = saturate(sun_dir.y * 2.0 + 0.5);
        float2 ground_uv = transmittance_lut_params_to_uv(earth_radius, max(sun_dir.y, 0.0));
        float3 ground_trans = trans_lut.SampleLevel(samp, ground_uv, 0).rgb;
        
        // ground bounce is strongest at cloud bottom, fades toward top
        float bottom_weight = 1.0 - smoothstep(0.0, 0.5, height_fraction);
        
        return ground_albedo * sun_illuminance * ground_trans * sun_vis * bottom_weight * 0.25;
    }
    
    static void aerial_perspective(float3 view_dir, float dist, float3 sun_dir,
                                    Texture2D trans_lut, SamplerState samp,
                                    out float3 inscatter, out float3 trans)
    {
        // simplified single-sample aerial perspective for performance
        trans = float3(1.0, 1.0, 1.0);
        inscatter = float3(0.0, 0.0, 0.0);
        
        float h = max(100.0, dist * 0.5 * view_dir.y);
        float cos_theta = dot(view_dir, sun_dir);
        
        float3 ext = get_extinction(h);
        float rd = get_rayleigh_density(h);
        float md = get_mie_density(h);
        
        float2 sun_uv = transmittance_lut_params_to_uv(h + earth_radius, max(sun_dir.y, 0.0));
        float3 ts = trans_lut.SampleLevel(samp, sun_uv, 0).rgb;
        
        float phase_r = rayleigh_phase(cos_theta);
        float phase_m = cornette_shanks_phase(cos_theta, mie_g);
        float3 scatter = (rayleigh_scatter * rd * phase_r + mie_scatter * md * phase_m) * ts;
        
        trans = exp(-ext * dist * 0.001);
        inscatter = scatter * (1.0 - trans) / max(ext, 1e-6) * sun_illuminance;
    }
    
    static cloud_result compute(float3 view_dir, float3 sun_dir, float sun_int, float day_night_factor, float time, float2 uv,
                                 Texture2D trans_lut, Texture2D ms_lut, SamplerState samp)
    {
        cloud_result r = (cloud_result)0;
        r.transmittance = float3(1, 1, 1);
        
        float coverage = buffer_frame.cloud_coverage;
        float ctype = buffer_frame.cloud_type;
        float seed = buffer_frame.cloud_seed;
        
        // day_night_factor: 1.0 = full daylight, 0.03 = moonlight
        // this is independent of the preset's intensity setting
        float ambient_scale = day_night_factor;
        
        if (coverage <= 0.0) return r;
        
        float horizon_fade = smoothstep(-0.05, 0.15, view_dir.y);
        if (horizon_fade <= 0.0) return r;
        
        float3 cam = float3(0, 100, 0);
        float dir_y = max(view_dir.y, 0.001);
        float t_enter = (cloud_base_bottom - cam.y) / dir_y;
        float t_exit = (cloud_base_top - cam.y) / dir_y;
        
        if (t_exit <= t_enter || t_enter < 0.0) return r;
        
        float ray_len = t_exit - t_enter;
        float avg_dist = (t_enter + t_exit) * 0.5;
        float avg_alt = (cloud_base_bottom + cloud_base_top) * 0.5;
        
        // atmospheric effects - scale inscatter by ambient_scale for proper night darkening
        aerial_perspective(view_dir, avg_dist, sun_dir, trans_lut, samp, r.inscatter, r.transmittance);
        r.inscatter *= ambient_scale;
        float3 sun_color = get_sun_color(sun_dir, avg_alt, trans_lut, samp);
        float3 ambient = get_ambient(sun_dir, ms_lut, samp);
        
        // adaptive stepping with distance-based lod
        // closer clouds get more samples, distant clouds get fewer
        float dist_lod = 1.0 - smoothstep(5000.0, 25000.0, t_enter);
        int num_steps = int(clamp(float(cloud_steps) * ray_len / 3000.0 * (0.7 + dist_lod * 0.3), 48.0, 96.0));
        float step_size = ray_len / float(num_steps);
        
        // temporal jitter - blue noise style with golden ratio temporal offset
        float base_jitter = noise_interleaved_gradient(uv * buffer_frame.resolution_render, true);
        float temporal_offset = frac(float(buffer_frame.frame) * 0.618033988749);
        float jitter = frac(base_jitter + temporal_offset);
        float t = t_enter + step_size * jitter;
        
        // phase functions
        float cos_ang = dot(view_dir, sun_dir);
        float phase = lerp(hg_phase(cos_ang, -0.3), hg_phase(cos_ang, 0.8), 0.5);
        float silver_phase = hg_phase(cos_ang, 0.95);
        float backlit = saturate(cos_ang);
        
        float trans = 1.0;
        float3 energy = float3(0, 0, 0);
        float prev_d = 0.0;
        
        [loop]
        for (int i = 0; i < num_steps && trans > 0.01; i++)
        {
            float3 pos = cam + view_dir * t;
            float d = sample_density(pos, coverage, ctype, time, seed);
            
            if (d > 0.01)
            {
                light_result lm = light_march(pos, sun_dir, view_dir, coverage, ctype, time, seed);
                
                float bottom, top;
                get_cloud_bounds(pos, seed, bottom, top);
                float h = saturate((pos.y - bottom) / (top - bottom));
                
                // lighting
                float3 direct = sun_color * sun_int;
                float3 light_col = lerp(direct, buffer_frame.cloud_color * sun_int, buffer_frame.cloud_darkness);
                float3 rad = light_col * lm.attenuation * phase;
                
                // silver lining - scaled by ambient_scale so cloud edges don't glow at night
                float edge = saturate(abs(d - prev_d) * 10.0) * (1.0 - saturate(d * 2.0));
                rad += light_col * edge * backlit * silver_phase * lm.attenuation * 0.4 * ambient_scale;
                
                // ambient sky light - scaled by light intensity so moonlit clouds stay dark
                rad += ambient * lerp(0.4, 0.8, h) * lm.ao * 0.2 * ambient_scale;
                
                // ground bounce illumination for cloud undersides - scaled by light intensity
                float3 ground_bounce = get_ground_bounce(sun_dir, h, trans_lut, samp);
                rad += ground_bounce * (0.3 + lm.ao * 0.3) * 0.5 * ambient_scale;
                
                float ext = d * step_size * cloud_absorption;
                float absorbed = 1.0 - exp(-ext);
                
                energy += trans * absorbed * rad;
                trans *= exp(-ext);
            }
            
            prev_d = d;
            t += step_size;
            if (t > t_exit) break;
        }
        
        r.color = energy * horizon_fade;
        r.alpha = (1.0 - trans) * horizon_fade;
        return r;
    }
};

#endif

// compute shaders

#if defined(TRANSMITTANCE_LUT)
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    float2 res;
    tex_uav.GetDimensions(res.x, res.y);
    if (any(tid.xy >= uint2(res))) return;
    
    float2 uv = (tid.xy + 0.5) / res;
    float height, cos_zenith;
    transmittance_uv_to_params(uv, height, cos_zenith);
    
    float3 pos = earth_center + float3(0.0, height, 0.0);
    float sin_z = safe_sqrt(1.0 - cos_zenith * cos_zenith);
    float3 dir = float3(sin_z, cos_zenith, 0.0);
    
    tex_uav[tid.xy] = float4(compute_transmittance_to_top(pos, dir), 1.0);
}

#elif defined(MULTISCATTER_LUT)
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    float2 res;
    tex_uav.GetDimensions(res.x, res.y);
    if (any(tid.xy >= uint2(res))) return;
    
    float2 uv = (tid.xy + 0.5) / res;
    float cos_sun = uv.x * 2.0 - 1.0;
    float height = uv.y * (atmosphere_radius - earth_radius);
    
    tex_uav[tid.xy] = float4(compute_multiscatter(height, cos_sun, tex, GET_SAMPLER(sampler_bilinear_clamp)), 1.0);
}

#elif defined(LUT)
[numthreads(8, 8, 8)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint3 dims;
    tex3d_uav.GetDimensions(dims.x, dims.y, dims.z);
    if (any(tid >= dims)) return;
    
    float height = float(tid.y) / (dims.y - 1) * (atmosphere_radius - earth_radius);
    float cos_zenith = float(tid.x) / (dims.x - 1) * 2.0 - 1.0;
    
    float3 pos = earth_center + float3(0.0, earth_radius + height, 0.0);
    float sin_z = safe_sqrt(1.0 - cos_zenith * cos_zenith);
    
    tex3d_uav[tid] = float4(compute_transmittance_to_top(pos, float3(sin_z, cos_zenith, 0.0)), 1.0);
}

#else
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    float2 res;
    tex_uav.GetDimensions(res.x, res.y);
    if (any(tid.xy >= uint2(res))) return;
    
    float2 uv = (float2(tid.xy) + 0.5) / res;
    
    // equirectangular to direction
    float phi = uv.x * PI2 + PI;
    float theta = (0.5 - uv.y) * PI;
    float cos_t = cos(theta);
    float3 view_dir = normalize(float3(cos(phi) * cos_t, sin(theta), sin(phi) * cos_t));
    float3 orig_view = view_dir;
    
    // mirror below horizon for ibl
    bool below_horizon = view_dir.y < 0.0;
    if (below_horizon) view_dir.y = -view_dir.y;
    
    // sun direction from light
    Light light;
    Surface surface;
    light.Build(0, surface);
    float3 sun_dir = normalize(-light.forward);
    float sun_elev = dot(sun_dir, up_direction);
    
    // day/night factor
    float day_factor = 1.0 / (1.0 + exp(-sun_elev * 20.0));
    
    // camera in atmosphere
    float3 cam_pos = buffer_frame.camera_position;
    float cam_h = get_height(cam_pos);
    if (cam_h < 0.0)
        cam_pos = earth_center + normalize(cam_pos - earth_center) * (earth_radius + 1.0);
    else if (cam_h > atmosphere_radius - earth_radius)
        cam_pos = earth_center + normalize(cam_pos - earth_center) * (atmosphere_radius - 1.0);
    
    // sky luminance
    float3 luminance = compute_sky_luminance(cam_pos, view_dir, sun_dir, tex, tex2,
                                              GET_SAMPLER(sampler_bilinear_clamp), 0.5);
    
    // celestial bodies
    float3 sun_col = float3(0, 0, 0);
    float3 star_col = float3(0, 0, 0);
    float3 moon_col = float3(0, 0, 0);
    
    if (below_horizon)
    {
        luminance *= 0.3;
    }
    else
    {
        if (sun_elev > -0.02)
        {
            float3 cam_up = normalize(cam_pos - earth_center);
            float2 sun_uv = transmittance_lut_params_to_uv(length(cam_pos - earth_center), max(dot(cam_up, sun_dir), 0.0));
            float3 sun_trans = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), sun_uv, 0).rgb;
            sun_col = compute_sun_disc(orig_view, sun_dir, sun_trans) * smoothstep(-0.02, 0.02, sun_elev);
        }
        star_col = stars::compute_color(uv, sun_dir);
        moon_col = compute_moon(orig_view, sun_dir);
    }
    
    // intensity scaling
    float intensity = lerp(0.5, 1.5, saturate(light.intensity / 100000.0));
    luminance *= day_factor;
    
    float night_factor = 1.0 - day_factor;
    float3 night_ambient = float3(0.001, 0.002, 0.004) * night_factor;
    
    // clouds with checkerboard temporal distribution
    // only compute clouds for half the pixels per frame, interpolate the rest
    cloud_result clouds_sun = (cloud_result)0;
    clouds_sun.transmittance = float3(1, 1, 1);
    cloud_result clouds_moon = (cloud_result)0;
    clouds_moon.transmittance = float3(1, 1, 1);
    
    // checkerboard pattern: compute clouds on alternating pixels each frame
    uint2 pixel = tid.xy;
    uint frame_parity = buffer_frame.frame & 1;
    bool compute_clouds = ((pixel.x + pixel.y + frame_parity) & 1) == 0;
    
    if (!below_horizon && buffer_frame.cloud_coverage > 0.0 && compute_clouds)
    {
        float3 moon_dir = -sun_dir;
        float moon_elev = dot(moon_dir, up_direction);
        float time_val = (float)buffer_frame.time * 0.001f;
        
        if (sun_elev > -0.15)
        {
            // sun intensity drops to zero when sun is below horizon
            // day_night_factor is based on sun elevation, independent of preset intensity
            float day_night_factor = saturate(sun_elev * 5.0 + 1.0);
            float sun_int = light.intensity * day_night_factor;
            clouds_sun = clouds::compute(orig_view, sun_dir, sun_int, day_night_factor, time_val, uv,
                                          tex, tex2, GET_SAMPLER(sampler_bilinear_clamp));
            clouds_sun.color *= day_night_factor;
            clouds_sun.alpha *= day_night_factor;
        }
        
        if (moon_elev > 0.0 && sun_elev < 0.1)
        {
            // moonlight: 3% of sun intensity, day_night_factor = 0.03
            float day_night_factor = 0.03;
            clouds_moon = clouds::compute(orig_view, moon_dir, light.intensity * day_night_factor, day_night_factor, time_val, uv,
                                           tex, tex2, GET_SAMPLER(sampler_bilinear_clamp));
            float fade = saturate((0.1 - sun_elev) * 5.0) * saturate(moon_elev * 3.0);
            clouds_moon.color *= fade;
            clouds_moon.alpha *= fade;
        }
    }
    
    // combine clouds
    float cloud_alpha = max(clouds_sun.alpha, clouds_moon.alpha);
    float3 cloud_color = clouds_sun.color + clouds_moon.color;
    float3 cloud_trans = clouds_sun.transmittance * clouds_moon.transmittance;
    float3 cloud_inscatter = clouds_sun.inscatter + clouds_moon.inscatter;
    
    // final composition
    float3 final_color;
    
    // for checkerboard-skipped pixels: don't add sun/moon - let temporal blend handle it
    // this prevents sun from appearing in front of clouds on skipped pixels
    bool has_clouds_coverage = buffer_frame.cloud_coverage > 0.0 && !below_horizon;
    bool should_add_celestials = compute_clouds || !has_clouds_coverage;
    
    if (cloud_alpha > 0.0)
    {
        // sun/moon/stars should be BEHIND clouds - occlude based on cloud density
        float celestial_occ = smoothstep(0.02, 0.15, cloud_alpha);
        
        float3 sky_behind = (luminance + night_ambient) * intensity;
        float3 sky_through = sky_behind * cloud_trans;
        
        // clouds in front of sky
        final_color = sky_through * (1.0 - cloud_alpha) + cloud_color +
                      cloud_inscatter * (1.0 - cloud_alpha * 0.5);
        
        // celestial bodies behind clouds - strongly attenuated
        float3 celestials = (sun_col + star_col + moon_col) * cloud_trans * cloud_trans;
        final_color += celestials * (1.0 - celestial_occ) * intensity;
    }
    else if (should_add_celestials)
    {
        // no clouds computed AND (we computed this frame OR no cloud coverage at all)
        final_color = (luminance + night_ambient + sun_col + star_col + moon_col) * intensity;
    }
    else
    {
        // checkerboard-skipped pixel with cloud coverage - DON'T add sun, use sky only
        // temporal blend will bring in the properly occluded result from neighboring frames
        final_color = (luminance + night_ambient) * intensity;
    }
    
    final_color = min(final_color, 100.0);
    
    // temporal accumulation with checkerboard reconstruction
    float4 prev = tex_uav[tid.xy];
    float3 blended = final_color;
    
    if (!compute_clouds && has_clouds_coverage && prev.a > 0.5)
    {
        // checkerboard reconstruction: blend neighboring computed pixels with temporal history
        // read 4 diagonal neighbors (these were computed this frame due to checkerboard pattern)
        float3 n0 = tex_uav[tid.xy + int2(-1, -1)].rgb;
        float3 n1 = tex_uav[tid.xy + int2( 1, -1)].rgb;
        float3 n2 = tex_uav[tid.xy + int2(-1,  1)].rgb;
        float3 n3 = tex_uav[tid.xy + int2( 1,  1)].rgb;
        
        // use box filter of neighbors for spatial reconstruction
        float3 spatial = (n0 + n1 + n2 + n3) * 0.25;
        
        // blend spatial reconstruction with temporal history for stability
        // higher spatial weight = sharper but potentially more flickery
        // higher temporal weight = smoother but potentially more ghosting
        blended = lerp(prev.rgb, spatial, 0.4);
    }
    else if (cloud_alpha > 0.0 && prev.a > 0.5)
    {
        // computed pixel with clouds - temporal blend with variance-aware weight
        float blend = 0.25;
        
        // increase blend when colors differ significantly (reduces ghosting during movement)
        float color_diff = dot(abs(final_color - prev.rgb), float3(0.299, 0.587, 0.114));
        blend = lerp(blend, 0.5, saturate(color_diff * 3.0));
        
        blended = lerp(prev.rgb, final_color, blend);
    }
    else if (prev.a > 0.5)
    {
        // no clouds this frame but have history - blend slowly for smooth transitions
        blended = lerp(prev.rgb, final_color, 0.3);
    }
    
    tex_uav[tid.xy] = float4(blended, 1.0);
}
#endif
