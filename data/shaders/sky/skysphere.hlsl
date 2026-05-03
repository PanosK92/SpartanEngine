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

// =====================================================================
// night sky
// =====================================================================

// blackbody color via tanner helland approximation, biased to plausible stellar temperatures
float3 night_blackbody(float temp_k)
{
    temp_k = clamp(temp_k, 1000.0, 40000.0);
    float t = temp_k * 0.01;
    float3 c;
    if (t <= 66.0)
    {
        c.r = 1.0;
        c.g = saturate(0.39008157 * log(t) - 0.63184144);
        c.b = (t <= 19.0) ? 0.0 : saturate(0.54320678 * log(t - 10.0) - 1.19625408);
    }
    else
    {
        c.r = saturate(1.29293619 * pow(t - 60.0, -0.1332047));
        c.g = saturate(1.12989086 * pow(t - 60.0, -0.0755148));
        c.b = 1.0;
    }
    return c;
}

float night_hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

float3 night_hash33(float3 p)
{
    p = frac(p * float3(0.1031, 0.103, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.xxy + p.yxx) * p.zyx);
}

// smooth 3d value noise on a hashed lattice
float night_value_noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);
    
    float n000 = night_hash13(i);
    float n100 = night_hash13(i + float3(1, 0, 0));
    float n010 = night_hash13(i + float3(0, 1, 0));
    float n110 = night_hash13(i + float3(1, 1, 0));
    float n001 = night_hash13(i + float3(0, 0, 1));
    float n101 = night_hash13(i + float3(1, 0, 1));
    float n011 = night_hash13(i + float3(0, 1, 1));
    float n111 = night_hash13(i + float3(1, 1, 1));
    
    return lerp(
        lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
        lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y),
        u.z);
}

float night_fbm(float3 p, int octaves)
{
    float v = 0.0;
    float a = 0.5;
    [loop] for (int i = 0; i < octaves; i++)
    {
        v += a * night_value_noise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

// orthonormal tangent frame around n
void night_make_basis(float3 n, out float3 t, out float3 b)
{
    float3 a = abs(n.y) < 0.95 ? float3(0, 1, 0) : float3(1, 0, 0);
    t = normalize(cross(a, n));
    b = cross(n, t);
}

// stars and milky way are dimmed by airmass toward the horizon, blue is attenuated more than red
float3 night_atmospheric_extinction(float3 view_dir)
{
    float airmass = 1.0 / max(view_dir.y + 0.05, 0.05);
    return exp(-airmass * float3(0.07, 0.10, 0.16));
}

// one star layer over a 3x3x3 neighborhood of a uniform 3d cell grid sampled along view_dir
float3 night_compute_star_layer(float3 view_dir, float time,
    float scale, float threshold, float radius_cells,
    float mag_pow, float brightness, float twinkle, float halo)
{
    float3 sample_pos = view_dir * scale;
    float3 cell       = floor(sample_pos);
    float3 frac_pos   = frac(sample_pos);
    float r2_max      = radius_cells * radius_cells;
    
    float3 result = float3(0, 0, 0);
    
    [unroll]
    for (int z = -1; z <= 1; z++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            [unroll]
            for (int x = -1; x <= 1; x++)
            {
                float3 ofs = float3(x, y, z);
                float3 c   = cell + ofs;
                float3 h   = night_hash33(c);
                
                // probability gate, most cells produce no star
                if (h.x > threshold) continue;
                
                // star center inside the cell, distance in cell units
                float3 d     = ofs + h - frac_pos;
                float dist2  = dot(d, d);
                float gauss  = exp(-dist2 / r2_max);
                if (gauss < 1e-3) continue;
                
                // power law magnitude, most stars dim and a few bright
                float mag = pow(h.x / threshold, mag_pow);
                
                // blackbody color across plausible stellar temperatures
                float temp = lerp(3500.0, 11000.0, h.y);
                float3 col = night_blackbody(temp);
                
                // per-star twinkle, slow sinusoid offset by hash
                float seed = h.z * PI2;
                float tw   = 1.0 + twinkle * sin(time * 3.5 + seed);
                
                // soft additive halo only on the rare brightest layer
                float halo_glow = 0.0;
                if (halo > 0.0)
                    halo_glow = halo * mag / (1.0 + dist2 * 60.0);
                
                result += col * mag * brightness * tw * (gauss + halo_glow);
            }
        }
    }
    return result;
}

// three layers of stars blended together for a believable magnitude distribution
float3 night_compute_stars(float3 view_dir, float time)
{
    float3 result = float3(0, 0, 0);
    
    // dim background field, very dense, no twinkle
    result += night_compute_star_layer(view_dir, time,
        140.0, 0.45, 0.10, 4.0, 0.30, 0.0, 0.0);
    
    // medium magnitude stars, moderate density, gentle twinkle
    result += night_compute_star_layer(view_dir, time,
        55.0, 0.18, 0.09, 5.0, 0.85, 0.15, 0.0);
    
    // rare bright stars, sharp pinpoint core with a tiny halo and stronger twinkle
    result += night_compute_star_layer(view_dir, time,
        22.0, 0.08, 0.045, 6.0, 1.05, 0.30, 0.05);
    
    return result;
}

// procedural galactic plane, fbm density modulated along a tilted band with dust rifts and a bulge
float3 night_compute_milky_way(float3 view_dir)
{
    // precomputed normalized orientations to avoid intrinsics in const initializers
    const float3 galactic_up = float3(0.42064, 0.55179, 0.72260);
    const float3 bulge_dir   = float3(-0.79602, -0.09950, 0.59702);
    
    // angular distance from the galactic plane, gaussian envelope
    float lat   = abs(dot(view_dir, galactic_up));
    float band  = exp(-lat * lat / 0.040);
    if (band < 0.001) return float3(0, 0, 0);
    
    // density along the band, broken up by an fbm and eaten away by a second fbm acting as dust
    float3 fp    = view_dir * 4.0;
    float density = night_fbm(fp, 4);
    density = saturate(density * 1.6 - 0.35);
    
    float dust = night_fbm(fp * 2.5 + 11.7, 4);
    dust       = saturate(dust * 1.3 - 0.40);
    density    = saturate(density - dust * 0.7);
    
    // brighter galactic bulge concentrated in one direction along the band
    float bulge = pow(saturate(dot(view_dir, bulge_dir)), 6.0);
    float intensity = density * (0.5 + bulge * 1.5);
    
    // cool blue at the edges, warm cream toward the bulge
    float3 cool = float3(0.55, 0.70, 1.00);
    float3 warm = float3(1.00, 0.85, 0.65);
    float3 col  = lerp(cool, warm, bulge);
    
    return col * intensity * band * 0.030;
}

// moon split into disc and halo so they can be composited differently against clouds
struct moon_result
{
    float3 disc;
    float3 halo;
};

moon_result night_compute_moon(float3 view_dir, float3 sun_dir)
{
    moon_result r;
    r.disc = float3(0, 0, 0);
    r.halo = float3(0, 0, 0);
    
    float3 moon_dir = -sun_dir;
    float moon_elev = dot(moon_dir, up_direction);
    
    // fade the moon and its halo as it dips below the horizon
    float horizon_fade = smoothstep(-0.05, 0.10, moon_elev);
    if (horizon_fade <= 0.0) return r;
    
    const float moon_radius = 0.018;
    const float sin_r       = sin(moon_radius);
    
    float cos_angle = dot(view_dir, moon_dir);
    if (cos_angle < 0.0) return r;
    
    // soft wide gaussian halo around the moon
    float angle_to_moon = acos(saturate(cos_angle));
    float halo_falloff  = exp(-angle_to_moon * angle_to_moon / 0.0040);
    r.halo = float3(0.55, 0.72, 0.95) * halo_falloff * 0.030 * horizon_fade;
    
    // outside the disc, only the halo contributes
    if (cos_angle < cos(moon_radius * 1.05)) return r;
    
    // tangent basis around the moon for disc-local coordinates
    float3 t_axis, b_axis;
    night_make_basis(moon_dir, t_axis, b_axis);
    
    // disc-local coordinates u,v in [-1,1]
    float u  = dot(view_dir, t_axis) / sin_r;
    float v  = dot(view_dir, b_axis) / sin_r;
    float r2 = u * u + v * v;
    if (r2 > 1.0) return r;
    
    // surface normal of the visible hemisphere point at this disc location
    float w  = safe_sqrt(1.0 - r2);
    float3 n = -moon_dir * w + t_axis * u + b_axis * v;
    
    // domain warped fbm for terrain, mare are dark basaltic plains, highlands are bright
    float3 sp     = n * 5.5;
    float warp    = night_fbm(sp * 0.6 + 7.3, 3);
    float terrain = night_fbm(sp + warp * 0.6, 5);
    
    float mare      = smoothstep(0.42, 0.62, terrain);
    float albedo_v  = lerp(0.16, 0.06, mare);
    
    // higher frequency noise adds crater-like darkening to the highlands
    float crater_n    = night_fbm(sp * 4.0 + 23.1, 3);
    float crater_mask = smoothstep(0.42, 0.55, crater_n);
    albedo_v *= lerp(1.0, 0.78, crater_mask * (1.0 - mare));
    
    // mare cool, highlands warm
    float3 tint   = lerp(float3(1.00, 0.94, 0.86), float3(0.78, 0.84, 0.95), mare);
    float3 albedo = albedo_v * tint;
    
    // perturb normal by terrain gradient so mare and highlands shade differently
    const float eps = 0.07;
    float dxp = night_fbm(sp + float3(eps, 0, 0), 4) - terrain;
    float dyp = night_fbm(sp + float3(0, eps, 0), 4) - terrain;
    float dzp = night_fbm(sp + float3(0, 0, eps), 4) - terrain;
    float3 n_perturbed = normalize(n + float3(dxp, dyp, dzp) * 0.55);
    
    // lunar light direction, rotated off the antisolar direction to give a waxing gibbous phase
    const float phase_angle = 0.45;
    float3 lunar_sun = normalize(-moon_dir * cos(phase_angle) + t_axis * sin(phase_angle));
    
    // illumination with a soft terminator
    float n_dot_l = dot(n_perturbed, lunar_sun);
    float lit     = smoothstep(-0.04, 0.06, n_dot_l);
    
    // hapke-like limb darkening, less pronounced than the sun
    float limb = pow(saturate(w), 0.55);
    
    // earthshine, faint cool blue glow on the unlit side
    float dark         = saturate(-n_dot_l);
    float3 earthshine  = float3(0.04, 0.06, 0.10) * dark * 0.45;
    
    // cool moonlight tint, intensity tuned for visibility against the night sky
    float3 lunar_light = float3(0.92, 0.96, 1.00) * 0.55;
    float3 surface     = albedo * lunar_light * lit * limb + earthshine;
    
    // anti-aliased disc edge
    float r_norm = sqrt(r2);
    float edge   = smoothstep(1.0, 0.96, r_norm);
    
    r.disc = surface * edge * horizon_fade;
    return r;
}

// night atmosphere combines a zenith to horizon gradient, a horizon airglow band and physical moonlight rayleigh scatter
float3 night_compute_atmosphere(float3 view_dir, float3 sun_dir, float3 cam_pos,
    Texture2D trans_lut, Texture2D ms_lut, SamplerState samp)
{
    // deep navy at zenith fading to a slightly warmer indigo near the horizon
    float h = saturate(view_dir.y * 0.5 + 0.5);
    float3 zenith_color  = float3(0.0035, 0.0070, 0.0180);
    float3 horizon_color = float3(0.0090, 0.0120, 0.0220);
    float3 base = lerp(horizon_color, zenith_color, h);
    
    // thin airglow band a few degrees above the horizon, faint green and orange
    float dy = view_dir.y - 0.04;
    float airglow_band = exp(-(dy * dy) / 0.0050);
    float3 airglow = float3(0.0070, 0.0095, 0.0050) * airglow_band;
    
    // moonlight rayleigh scatter, reuse the daytime atmosphere with the moon as light source
    // result is then scaled by an empirical moon to sun irradiance ratio
    float3 moon_dir = -sun_dir;
    float moon_elev = dot(moon_dir, up_direction);
    float3 moon_scatter = float3(0, 0, 0);
    if (moon_elev > -0.10)
    {
        float3 lum  = compute_sky_luminance(cam_pos, view_dir, moon_dir,
            trans_lut, ms_lut, samp, 0.5);
        float fade  = smoothstep(-0.05, 0.10, moon_elev);
        moon_scatter = lum * 3.5e-5 * fade;
    }
    
    return base + airglow + moon_scatter;
}

// =====================================================================
// volumetric clouds, planet curvature shell, nubis style
// only built for the main sky pass
// =====================================================================

#if !defined(LUT) && !defined(TRANSMITTANCE_LUT) && !defined(MULTISCATTER_LUT)

// shell geometry, narrow band for cumulus humilis style flat bottomed puffs
static const float cloud_inner_radius = earth_radius + 1500.0;
static const float cloud_outer_radius = earth_radius + 3500.0;
static const float cloud_layer_height = cloud_outer_radius - cloud_inner_radius;
static const float cloud_mid_alt      = (cloud_inner_radius + cloud_outer_radius) * 0.5 - earth_radius;

// noise frequencies in inverse meters, vertical multipliers give puffy clouds in a thin shell
static const float cloud_shape_scale     = 1.0 / 11000.0;
static const float cloud_shape_y_mult    = 2.5;
static const float cloud_detail_scale    = 1.0 / 2200.0;
static const float cloud_detail_y_mult   = 2.0;
static const float cloud_weather_scale   = 1.0 / 9000.0;

// physical and tuning constants
static const float cloud_extinction      = 0.10;
static const float cloud_wind_speed      = 1.5;
static const float cloud_detail_strength = 0.30;
static const float cloud_density_sharp   = 1.6;
static const float cloud_max_dist_low    = 22000.0;
static const float cloud_max_dist_high   = 65000.0;
// camera relative density fade so horizon rays do not stack many puff silhouettes into a veil
// fade is independent of the per ray march cap, kicks in based on absolute camera distance
// start late enough to leave the mid range cloud field untouched, end well past the march cap
static const float cloud_cam_fade_start  = 8000.0;
static const float cloud_cam_fade_end    = 22000.0;

// sampling budgets
static const int cloud_steps_max   = 128;
static const int cloud_steps_min   = 64;
static const int cloud_light_steps = 6;

struct cloud_result { float3 color; float alpha; float3 transmittance; float3 inscatter; };

float cloud_remap(float v, float a, float b, float c, float d)
{
    return c + saturate((v - a) / max(b - a, 1e-6)) * (d - c);
}

float cloud_hg(float cos_t, float g)
{
    // floor on the denominator caps the lobe at a sane peak so g near 1 cannot blow up to white
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cos_t, 0.01), 1.5));
}

// dual lobe hg with a silver lining lobe near the sun
float cloud_phase(float cos_t)
{
    float forward  = cloud_hg(cos_t,  0.80);
    float backward = cloud_hg(cos_t, -0.50);
    float silver   = cloud_hg(cos_t,  0.99);
    float base     = lerp(forward, backward, 0.5);
    return lerp(base, silver, saturate(cos_t) * 0.30);
}

// 0 at inner shell, 1 at outer
float cloud_height_frac(float3 pos)
{
    float r = length(pos - earth_center);
    return saturate((r - cloud_inner_radius) / cloud_layer_height);
}

// per type vertical density gradients
float cloud_grad_stratus(float h)
{
    return cloud_remap(h, 0.0,  0.10, 0.0, 1.0) *
           cloud_remap(h, 0.20, 0.35, 1.0, 0.0);
}

float cloud_grad_cumulus(float h)
{
    // sharp flat bottom around h 0.05, peak density 0.20 to 0.55, taper to wispy top 0.90
    // this narrow band keeps each puff compact instead of stretching across the shell
    float ramp_in  = cloud_remap(h, 0.05, 0.20, 0.0, 1.0);
    float fade_out = cloud_remap(h, 0.55, 0.90, 1.0, 0.0);
    return ramp_in * fade_out;
}

float cloud_grad_cumulonimbus(float h)
{
    return cloud_remap(h, 0.0,  0.10, 0.0, 1.0) *
           cloud_remap(h, 0.85, 1.0,  1.0, 0.0);
}

float cloud_height_gradient(float h, float cloud_type)
{
    float a = cloud_grad_stratus(h);
    float b = cloud_grad_cumulus(h);
    float c = cloud_grad_cumulonimbus(h);

    float t  = cloud_type * 2.0;
    float ab = lerp(a, b, saturate(t));
    return lerp(ab, c, saturate(t - 1.0));
}

// cheap procedural 2d weather, returns coverage and cloud type from two scales of 3d slices
float3 cloud_weather(float2 world_xz)
{
    // two octaves at different scales for broad regions with internal variation
    float3 uvw0 = float3(world_xz * cloud_weather_scale,             0.45);
    float3 uvw1 = float3(world_xz * cloud_weather_scale * 2.7 + 7.3, 0.55);

    float4 w0 = tex3d_cloud_shape.SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), uvw0, 0);
    float4 w1 = tex3d_cloud_shape.SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), uvw1, 0);

    // smoothstep on the raw noise gives true clear vs cloudy regions instead of uniform mid grey
    float raw        = w0.r * 0.6 + w1.r * 0.4;
    float coverage   = smoothstep(0.30, 0.75, raw);
    float cloud_type = saturate(w0.g * 0.6 + w1.b * 0.4);
    return float3(coverage, cloud_type, 0.0);
}

float3 cloud_animate(float3 pos, float time)
{
    float3 wind = buffer_frame.wind;
    float  wlen = length(wind);
    float3 wdir = wlen > 1e-4 ? wind / wlen : float3(1, 0, 0);
    return pos + wdir * (wlen * cloud_wind_speed) * time;
}

float cloud_density(float3 pos, float time, bool with_detail, float detail_fade)
{
    float h = cloud_height_frac(pos);
    if (h <= 0.0 || h >= 1.0) return 0.0;

    float3 weather        = cloud_weather(pos.xz);
    // multiplicative coverage so weather low gives clear sky and weather high gives dense regions
    // 1.6 cap gives a populated cumulus field at default coverage 0.45 while still leaving
    // gaps for the noise threshold, distance fade in the march keeps the horizon veil under control
    float  coverage_local = saturate(buffer_frame.cloud_coverage * 1.6 * weather.x);
    if (coverage_local <= 0.001) return 0.0;

    float3 sample_pos = cloud_animate(pos, time);

    // anisotropic uvw, vertical sampling is much faster so a 4 km shell carries multiple noise features
    float3 shape_uvw = sample_pos * cloud_shape_scale;
    shape_uvw.y     *= cloud_shape_y_mult;

    // base shape from perlin worley plus low frequency worley fbm
    float4 shape         = tex3d_cloud_shape.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), shape_uvw, 0);
    float  low_freq_fbm  = dot(shape.gba, float3(0.625, 0.25, 0.125));
    float  base_shape    = cloud_remap(shape.r, low_freq_fbm - 1.0, 1.0, 0.0, 1.0);

    // height gradient by cloud type
    float gradient = cloud_height_gradient(h, weather.y);
    base_shape *= gradient;

    // coverage remap, the key nubis trick
    float density = cloud_remap(base_shape, 1.0 - coverage_local, 1.0, 0.0, 1.0);
    if (density <= 0.0) return 0.0;

    if (with_detail && detail_fade > 0.001)
    {
        float3 detail_uvw  = sample_pos * cloud_detail_scale;
        detail_uvw.y      *= cloud_detail_y_mult;
        detail_uvw        += float3(0, time * 0.05, 0);
        float4 detail      = tex3d_cloud_detail.SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), detail_uvw, 0);
        float  detail_fbm  = dot(detail.rgb, float3(0.625, 0.25, 0.125));
        // less erosion at the base for rounded bottoms, more at the top for wispy edges
        float  erosion     = lerp(detail_fbm, 1.0 - detail_fbm, saturate(h * 5.0));
        density = saturate(cloud_remap(density, erosion * cloud_detail_strength * detail_fade, 1.0, 0.0, 1.0));
    }

    // power curve sharpens the density falloff so cloud edges read as crisp puffs
    // not soft fog, low values are pushed toward zero, high values stay near one
    return pow(density, cloud_density_sharp);
}

// random unit vectors for the sun light cone march
static const float3 cloud_cone_offsets[6] =
{
    float3( 0.38051305,  0.92453449, -0.02111345),
    float3(-0.50625799, -0.03590792, -0.86163418),
    float3(-0.32509218, -0.94557439,  0.01428793),
    float3( 0.09026238, -0.27376545,  0.95755165),
    float3( 0.28128598,  0.42443639, -0.86065785),
    float3(-0.16852403,  0.14748697,  0.97460106),
};

// optical depth toward the sun via a cone jittered march plus a long jump
float cloud_light_optical_depth(float3 pos, float3 light_dir, float time)
{
    float optical = 0.0;
    float step    = 50.0;
    float cone_r  = 50.0;

    [unroll]
    for (int i = 0; i < cloud_light_steps; i++)
    {
        float3 sample_pos = pos + light_dir * step + cloud_cone_offsets[i] * cone_r;
        float  h          = cloud_height_frac(sample_pos);
        if (h > 0.0 && h < 1.0)
        {
            // skip detail on far cone steps to save work
            float detail_fade  = 1.0 - float(i) / float(cloud_light_steps);
            optical           += cloud_density(sample_pos, time, i < 4, detail_fade) * step;
        }
        step   *= 1.6;
        cone_r *= 1.6;
    }

    // long jump captures distant self shadowing cheaply
    float3 far_pos  = pos + light_dir * 3000.0;
    optical        += cloud_density(far_pos, time, false, 0.0) * 1500.0;

    return optical;
}

// frostbite multi octave beer law with per octave dual lobe hg and silver lining
// each iteration is more attenuated, more isotropic, contributing less, modeling deeper bounces
float cloud_multi_scatter(float optical_depth, float cos_theta)
{
    float lum = 0.0;
    float a   = 1.0;
    float b   = 1.0;
    float c   = 1.0;

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float beer  = exp(-b * optical_depth * cloud_extinction);
        float fwd   = cloud_hg(cos_theta,  0.80 * c);
        float bwd   = cloud_hg(cos_theta, -0.50 * c);
        float silv  = cloud_hg(cos_theta,  0.85 * c);
        float phase = lerp(lerp(fwd, bwd, 0.5), silv, saturate(cos_theta) * 0.15);
        lum += a * beer * phase;
        a *= 0.5;
        b *= 0.5;
        c *= 0.5;
    }
    return lum;
}

// ambient sky from the multiscatter lut plus a simple ground bounce, scaled by light_color
// bottom of cloud receives less sky, more ground; top receives more sky, no ground
float3 cloud_ambient(float3 light_dir, float3 light_color, float h_frac,
                     Texture2D ms_lut, SamplerState samp)
{
    float2 ms_uv = float2(saturate(light_dir.y) * 0.5 + 0.5, 0.02);
    float3 sky   = ms_lut.SampleLevel(samp, ms_uv, 0).rgb;

    float3 ambient_sky   = sky * light_color;
    float3 ground_bounce = light_color * ground_albedo * saturate(light_dir.y);

    float top_w = saturate(h_frac);
    return ambient_sky * lerp(0.4, 1.0, top_w) + ground_bounce * lerp(0.5, 0.05, top_w);
}

// midpoint atmosphere extinction and inscatter from camera to cloud, scaled by light_color
void cloud_aerial(float3 cam_pos, float3 view_dir, float distance,
                  float3 light_dir, float3 light_color,
                  Texture2D trans_lut, SamplerState samp,
                  out float3 inscatter, out float3 trans_atm)
{
    float3 mid       = cam_pos + view_dir * (distance * 0.5);
    float  h         = max(0.0, get_height(mid));
    float  cos_theta = dot(view_dir, light_dir);

    float3 ext = get_extinction(h);
    float  rd  = get_rayleigh_density(h);
    float  md  = get_mie_density(h);

    float3 up      = normalize(mid - earth_center);
    float  cos_sun = dot(up, light_dir);
    float2 sun_uv  = transmittance_lut_params_to_uv(h + earth_radius, max(cos_sun, 0.0));
    float3 sun_t   = trans_lut.SampleLevel(samp, sun_uv, 0).rgb;
    sun_t         *= smoothstep(-0.05, 0.05, cos_sun);

    float3 scatter = (rayleigh_scatter * rd * rayleigh_phase(cos_theta) +
                      mie_scatter      * md * cornette_shanks_phase(cos_theta, mie_g)) * sun_t;

    trans_atm = exp(-ext * distance);
    inscatter = scatter * (1.0 - trans_atm) / max(ext, 1e-6) * light_color;
}

// horizontal march cap, smaller for grazing rays
float cloud_march_cap(float3 view_dir)
{
    float t = saturate(view_dir.y * 4.0 + 0.05);
    return lerp(cloud_max_dist_low, cloud_max_dist_high, t);
}

cloud_result cloud_compute(
    float3 cam_pos, float3 view_dir,
    float3 light_dir, float3 light_color,
    float time, float2 uv,
    Texture2D trans_lut, Texture2D ms_lut, SamplerState samp)
{
    cloud_result r;
    r.color         = float3(0, 0, 0);
    r.alpha         = 0.0;
    r.transmittance = float3(1, 1, 1);
    r.inscatter     = float3(0, 0, 0);

    if (buffer_frame.cloud_coverage <= 0.0) return r;

    // ray entry and exit through the spherical shell
    float2 inner_hit = ray_sphere_intersect(cam_pos, view_dir, earth_center, cloud_inner_radius);
    float2 outer_hit = ray_sphere_intersect(cam_pos, view_dir, earth_center, cloud_outer_radius);
    if (outer_hit.y < 0.0) return r;

    float cam_alt = get_height(cam_pos);
    float t_enter, t_exit;

    if (cam_alt < cloud_inner_radius - earth_radius)
    {
        // camera below the layer, march from inner shell to outer shell
        if (inner_hit.y < 0.0) return r;
        t_enter = inner_hit.y;
        t_exit  = outer_hit.y;
    }
    else if (cam_alt > cloud_outer_radius - earth_radius)
    {
        // camera above the layer, march from outer entry to inner entry or outer exit
        if (outer_hit.x < 0.0) return r;
        t_enter = outer_hit.x;
        t_exit  = inner_hit.x > 0.0 ? inner_hit.x : outer_hit.y;
    }
    else
    {
        // camera inside the layer
        t_enter = 0.0;
        t_exit  = inner_hit.x > 0.0 ? inner_hit.x : outer_hit.y;
    }

    // ground cull and march cap
    float2 ground_hit = ray_sphere_intersect(cam_pos, view_dir, earth_center, earth_radius);
    if (ground_hit.x > 0.0) t_exit = min(t_exit, ground_hit.x);

    float march_cap = cloud_march_cap(view_dir);
    t_exit = min(t_exit, t_enter + march_cap);

    if (t_exit <= t_enter) return r;

    float ray_len   = t_exit - t_enter;
    int   num_steps = clamp(int(ray_len / 60.0), cloud_steps_min, cloud_steps_max);
    float step_base = ray_len / float(num_steps);

    // jitter dominated by frame, small spatial component breaks step banding within a frame
    // temporal blend between frames covers the discrete step pattern over time
    float t_jitter   = frac(float(buffer_frame.frame) * 0.618033988749);
    float sp_jitter  = noise_interleaved_gradient(uv * buffer_frame.resolution_render, true);
    float jitter     = frac(t_jitter + sp_jitter * 0.2);

    float  t        = t_enter + step_base * jitter;
    float  trans    = 1.0;
    float3 lum      = float3(0, 0, 0);
    float  first_t  = 0.0;
    float  last_t   = 0.0;
    bool   any_hit  = false;

    float cos_theta = dot(view_dir, light_dir);
    float backlit   = saturate(-cos_theta * 0.5 + 0.5);

    int empty_streak = 0;

    [loop] for (int i = 0; i < cloud_steps_max; i++)
    {
        if (i >= num_steps) break;
        if (t >= t_exit)    break;
        if (trans < 0.005)  break;

        // camera relative density fade, overhead clouds at 1.5 to 3.5 km untouched,
        // horizon rays beyond the fade end contribute zero density so no veil stacks up
        float dist_fade = saturate(1.0 - (t - cloud_cam_fade_start) /
                                         (cloud_cam_fade_end - cloud_cam_fade_start));

        float3 pos = cam_pos + view_dir * t;

        // cheap density first for empty space skipping
        float density_cheap = cloud_density(pos, time, false, 0.0) * dist_fade;

        if (density_cheap <= 0.0)
        {
            empty_streak  = min(empty_streak + 1, 3);
            t            += step_base * (1.0 + float(empty_streak));
            continue;
        }

        empty_streak = 0;

        // distance based detail fade to suppress shimmer
        float detail_fade = saturate(1.0 - t / 50000.0);
        float density     = cloud_density(pos, time, true, detail_fade) * dist_fade;

        if (density > 0.0)
        {
            if (!any_hit) { first_t = t; any_hit = true; }
            last_t = t;

            float optical = cloud_light_optical_depth(pos, light_dir, time);
            // ms already bakes the dual lobe hg phase per octave, do not multiply by phase again
            float ms      = cloud_multi_scatter(optical, cos_theta);

            // powder dark edge term, only on the back side
            float  powder   = (1.0 - exp(-density * 6.0)) * backlit;
            float3 sun_lobe = (ms + powder * 0.20) * light_color;

            float  h_frac   = cloud_height_frac(pos);
            float3 ambient  = cloud_ambient(light_dir, light_color, h_frac, ms_lut, samp);

            // energy conserving step, radiance is in scattered radiance not the source term
            // so the integral collapses to radiance times the absorbed fraction of this step
            float3 radiance   = sun_lobe + ambient;
            float  sigma      = density * cloud_extinction;
            float  trans_step = exp(-sigma * step_base);
            float3 step_lum   = radiance * (1.0 - trans_step);

            lum   += trans * step_lum;
            trans *= trans_step;
        }

        t += step_base;
    }

    r.color = lum;
    r.alpha = saturate(1.0 - trans);

    if (any_hit)
    {
        // representative cloud distance is the geometric center of the hit span
        // not the first hit, so grazing rays do not under estimate atmospheric attenuation
        float mid_t = (first_t + last_t) * 0.5;
        cloud_aerial(cam_pos, view_dir, mid_t, light_dir, light_color, trans_lut, samp,
                     r.inscatter, r.transmittance);
    }

    return r;
}

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
    float3 cam_pos = get_camera_position();
    float cam_h = get_height(cam_pos);
    if (cam_h < 0.0)
        cam_pos = earth_center + normalize(cam_pos - earth_center) * (earth_radius + 1.0);
    else if (cam_h > atmosphere_radius - earth_radius)
        cam_pos = earth_center + normalize(cam_pos - earth_center) * (atmosphere_radius - 1.0);
    
    // sky luminance
    float3 luminance = compute_sky_luminance(cam_pos, view_dir, sun_dir, tex, tex2,
                                              GET_SAMPLER(sampler_bilinear_clamp), 0.5);
    
    // sun disc only, all night celestials are gathered below
    float3 sun_col = float3(0, 0, 0);
    
    // ground fade for the bottom hemisphere, fully gone within ~7 degrees below the horizon
    // hard cap on the luminance prevents the mie peak around the sun from producing a vertical
    // pillar straight down through the equirectangular bottom pole
    float ground_fade = below_horizon ? saturate(1.0 + orig_view.y * 8.0) : 1.0;

    if (below_horizon)
    {
        // clamp the mirrored sky so an intense sun mie spike cannot punch through the fade
        // dark warm grey ground tone, faded out away from the horizon
        float lum_avg = (luminance.r + luminance.g + luminance.b) * 0.333;
        float3 lum_capped = luminance * min(1.0, 1.0 / max(lum_avg, 1e-3));
        luminance = lum_capped * 0.15 * ground_fade;
    }
    else if (sun_elev > -0.02)
    {
        float3 cam_up    = normalize(cam_pos - earth_center);
        float2 sun_uv    = transmittance_lut_params_to_uv(length(cam_pos - earth_center), max(dot(cam_up, sun_dir), 0.0));
        float3 sun_trans = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), sun_uv, 0).rgb;
        sun_col          = compute_sun_disc(orig_view, sun_dir, sun_trans) * smoothstep(-0.02, 0.02, sun_elev);
    }
    
    // intensity scaling
    float intensity = lerp(0.5, 1.5, saturate(light.intensity / 100000.0));
    luminance *= day_factor;
    
    // night sky, atmosphere is sky-dome and goes through clouds, celestials are occluded by clouds
    float night_factor    = 1.0 - day_factor;
    float3 night_ambient    = float3(0, 0, 0);
    float3 night_celestials = float3(0, 0, 0);
    if (night_factor > 0.001)
    {
        float time_val = (float)buffer_frame.time * 0.001f;
        
        night_ambient = night_compute_atmosphere(view_dir, sun_dir, cam_pos,
            tex, tex2, GET_SAMPLER(sampler_bilinear_clamp));
        
        if (!below_horizon)
        {
            // stars and milky way ride behind the atmosphere and dim toward the horizon
            float3 ext = night_atmospheric_extinction(orig_view);
            night_celestials += night_compute_milky_way(orig_view) * ext;
            night_celestials += night_compute_stars(orig_view, time_val) * ext;
            
            // moon disc reads as a celestial, halo bleeds into the atmosphere lobe
            moon_result mr     = night_compute_moon(orig_view, sun_dir);
            night_ambient     += mr.halo;
            night_celestials  += mr.disc;
        }
        else
        {
            // mirrored direction would pillar the moon halo straight down, fade it out
            night_ambient *= ground_fade;
        }
        
        night_ambient    *= night_factor;
        night_celestials *= night_factor;
    }
    
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
        
        if (sun_elev > -0.10)
        {
            // sharper twilight cutoff so clouds go dark quickly once the sun dips below horizon
            float  day_fade = smoothstep(-0.06, 0.05, sun_elev);
            float2 sun_uv   = transmittance_lut_params_to_uv(earth_radius + cloud_mid_alt, max(sun_dir.y, 0.0));
            float3 sun_t    = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), sun_uv, 0).rgb;
            float3 sun_col  = sun_illuminance * sun_t * day_fade;

            clouds_sun = cloud_compute(cam_pos, orig_view, sun_dir, sun_col, time_val, uv,
                                       tex, tex2, GET_SAMPLER(sampler_bilinear_clamp));

            // alpha and inscatter follow the sun fade so cloud silhouettes vanish at night
            clouds_sun.alpha     *= day_fade;
            clouds_sun.inscatter *= day_fade;
        }

        if (moon_elev > 0.0 && sun_elev < 0.1)
        {
            // moonlight at the cloud mid altitude, cool tinted and ~0.5% of sun irradiance
            // real moon is ~1e-6 of sun, this is artistic so clouds remain just barely visible
            float  fade      = saturate((0.1 - sun_elev) * 5.0) * saturate(moon_elev * 3.0);
            float2 moon_uv   = transmittance_lut_params_to_uv(earth_radius + cloud_mid_alt, max(moon_dir.y, 0.0));
            float3 moon_t    = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), moon_uv, 0).rgb;
            float3 moon_tint = float3(0.55, 0.7, 1.0);
            float3 moon_col  = sun_illuminance * 0.005 * moon_tint * moon_t * fade;

            clouds_moon = cloud_compute(cam_pos, orig_view, moon_dir, moon_col, time_val, uv,
                                        tex, tex2, GET_SAMPLER(sampler_bilinear_clamp));

            clouds_moon.alpha     *= fade;
            clouds_moon.inscatter *= fade;
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
        // atmospheric perspective on the cloud, distant clouds blend into the sky color
        // emission attenuated by atmospheric transmittance, alpha faded by the same so the
        // silhouette weakens with distance instead of stamping a dense band along the horizon
        float  trans_lum       = dot(cloud_trans, float3(0.2126, 0.7152, 0.0722));
        float3 cloud_col_persp = cloud_color * cloud_trans + cloud_inscatter;
        float  cloud_alpha_eff = cloud_alpha * trans_lum;

        // celestials read as occluded once clouds become non trivial
        float celestial_occ = smoothstep(0.02, 0.15, cloud_alpha_eff);

        // sky dome is at infinity, no extra cloud_trans on it, otherwise the unoccluded
        // portion of the pixel under reads the sky color
        float3 sky_dome = (luminance + night_ambient) * intensity;

        final_color = sky_dome * (1.0 - cloud_alpha_eff) + cloud_col_persp * intensity;

        // celestial bodies behind clouds, strongly attenuated by cloud transmittance squared
        float3 celestials = (sun_col + night_celestials) * cloud_trans * cloud_trans;
        final_color += celestials * (1.0 - celestial_occ) * intensity;
    }
    else if (should_add_celestials)
    {
        // no clouds computed and either we ran clouds this frame or there is no coverage at all
        final_color = (luminance + night_ambient + sun_col + night_celestials) * intensity;
    }
    else
    {
        // checkerboard-skipped pixel with cloud coverage - DON'T add sun, use sky only
        // temporal blend will bring in the properly occluded result from neighboring frames
        final_color = (luminance + night_ambient) * intensity;
    }
    
    final_color = min(final_color, 100.0);
    
    // temporal accumulation with checkerboard reconstruction
    float4 prev    = tex_uav[tid.xy];
    float3 blended = final_color;

    if (!compute_clouds && has_clouds_coverage && prev.a > 0.5)
    {
        // skipped pixel reads its own previous value, this pixel was the computed one last
        // frame so the data is at most one frame stale, no box filter smear, no streaks
        // the spatial reconstruction box filter was averaging neighbors with subtly different
        // cloud edges and producing visible streaks once temporally accumulated
        blended = prev.rgb;
    }
    else if (cloud_alpha > 0.0 && prev.a > 0.5)
    {
        // computed pixel with clouds, mostly current frame so cloud silhouettes stay crisp
        // and do not ghost when wind moves the noise field, history smooths step banding
        float color_diff = dot(abs(final_color - prev.rgb), float3(0.299, 0.587, 0.114));
        float blend      = lerp(0.65, 0.9, saturate(color_diff * 3.0));
        blended          = lerp(prev.rgb, final_color, blend);
    }
    else if (prev.a > 0.5)
    {
        // no clouds this frame but have history, blend smoothly for transitions
        blended = lerp(prev.rgb, final_color, 0.5);
    }
    
    tex_uav[tid.xy] = float4(blended, 1.0);
}
#endif
