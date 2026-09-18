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

#include "atmosphere.hlsl"

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
    
    float3 luminance = float3(0.0, 0.0, 0.0);
    float3 trans = float3(1.0, 1.0, 1.0);
    float previous_t = t_min;
    
    for (int i = 0; i < scattering_samples; i++)
    {
        float u = float(i + 1) / scattering_samples;
        float t = t_min + (t_max - t_min) * u * u;
        float dt = t - previous_t;
        float3 sample_pos = position + view_dir * lerp(previous_t, t, jitter);
        previous_t = t;
        float height = get_height(sample_pos);
        
        if (height < 0.0 || height > atmosphere_radius - earth_radius)
            continue;
        
        float3 extinction = get_extinction(height);
        float3 total = atmosphere_source(sample_pos, view_dir, sun_dir, transmittance_lut, multiscatter_lut, samp);
        float3 scatter_int = total * atmosphere_segment_weight(extinction, dt);
        
        luminance += scatter_int * trans;
        trans *= exp(-extinction * dt);
        
        if (all(trans < 1e-6)) break;
    }
    
    // top of atmosphere radiance, the integral above already applied per path transmittance
    return luminance * get_sun_radiance_toa();
}

// sun disc with limb darkening and a solar aureole
float3 compute_sun_disc(float3 view_dir, float3 sun_dir, float3 transmittance)
{
    float cos_angle = dot(view_dir, sun_dir);
    if (cos_angle <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float angle = acos(saturate(cos_angle));
    float3 toa  = get_sun_radiance_toa() * transmittance;
    float x     = angle / buffer_frame.equatorial_x.w;

    // limb darkening across the geometric disc
    float r    = saturate(x);
    float mu   = safe_sqrt(1.0 - r * r);
    float limb = 0.3 + 0.93 * mu - 0.23 * mu * mu;

    // compact soft core, solid angle division is skipped on purpose, it produces ~600k nits
    // that the panorama hdr clamp flattens into a hard circle and erases every soft edge
    float core  = 1.0 - smoothstep(0.75, 1.4, x);
    float3 disc = toa * core * limb * 0.5;

    // wide solar aureole, must stay bright over several degrees or tonemap collapses the sun
    // into a flat cream sticker against the blue sky
    float3 aureole = toa * (
        exp(-x * x * 0.8)  * 0.45 +
        exp(-x * x * 0.05) * 0.35 +
        exp(-x * x * 0.008) * 0.18 +
        exp(-x * x * 0.0015) * 0.06
    );

    return disc + aureole;
}

// =====================================================================
// sky view lut (hillaire 2020)
// the per panorama pixel atmosphere integration is replaced by a small lut baked once per
// frame, the sky is azimuthally symmetric about the light's vertical plane so u only needs
// the [0, pi] azimuth difference, v is elevation with a square root warp that concentrates
// texels at the horizon where the gradient is steepest, the texture packs two half luts
// vertically, rows [0, h) hold the sun sky and rows [h, 2h) hold the moonlit night sky
// =====================================================================

static const float2 sky_view_lut_size = float2(192.0, 108.0); // resolution of one half

// horizontal unit vector of a direction, falls back to +x when degenerate
float2 sky_view_horizontal(float3 dir)
{
    float2 h   = float2(dir.x, dir.z);
    float  len = length(h);
    return len > 1e-5 ? h / len : float2(1.0, 0.0);
}

// forward mapping used when sampling, unit uv in [0,1]^2 for the upper hemisphere
float2 sky_view_dir_to_unit(float3 view_dir, float3 light_dir)
{
    float elevation = asin(saturate(view_dir.y));
    float v         = sqrt(elevation / (PI * 0.5));
    float cos_az    = clamp(dot(sky_view_horizontal(view_dir), sky_view_horizontal(light_dir)), -1.0, 1.0);
    float u         = acos(cos_az) / PI;
    return float2(u, v);
}

// inverse mapping used by the bake kernel, texel centers span the full unit range
float3 sky_view_unit_to_dir(float2 unit, float3 light_dir)
{
    float elevation = unit.y * unit.y * (PI * 0.5);
    float azimuth   = unit.x * PI;
    float2 fwd      = sky_view_horizontal(light_dir);
    float2 side     = float2(-fwd.y, fwd.x);
    float2 dir_h    = fwd * cos(azimuth) + side * sin(azimuth);
    float  cos_el   = cos(elevation);
    return float3(dir_h.x * cos_el, sin(elevation), dir_h.y * cos_el);
}

// bilinear fetch from the packed lut, half_index 0 samples the sun half, 1 the moon half
float3 sample_sky_view_lut(Texture2D lut, SamplerState samp, float3 view_dir, float3 light_dir, float half_index)
{
    float2 unit = sky_view_dir_to_unit(view_dir, light_dir);
    float2 uv;
    uv.x = (unit.x * (sky_view_lut_size.x - 1.0) + 0.5) / sky_view_lut_size.x;
    uv.y = (unit.y * (sky_view_lut_size.y - 1.0) + 0.5) / (sky_view_lut_size.y * 2.0) + half_index * 0.5;
    return lut.SampleLevel(samp, uv, 0).rgb;
}

// =====================================================================
// night sky
// =====================================================================

float night_hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
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

float3 night_rotate_about_axis(float3 dir, float3 axis, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return dir * c + cross(axis, dir) * s + axis * dot(axis, dir) * (1.0 - c);
}

float3 night_apply_earth_rotation(float3 view_dir)
{
    return normalize(view_dir.x * buffer_frame.equatorial_x.xyz +
        view_dir.y * buffer_frame.equatorial_y.xyz + view_dir.z * buffer_frame.equatorial_z.xyz);
}

// yale bsc5 catalog stars as unresolved pinpoints, magnitude drives flux and size
float3 night_compute_stars(float3 celestial_view, Texture2D stars_tex, Texture2D grid_tex, float time_seconds)
{
    float2 res;
    grid_tex.GetDimensions(res.x, res.y);
    if (res.x < 2.0 || res.y < 2.0)
    {
        return float3(0, 0, 0);
    }

    float2 uv = direction_sphere_uv(celestial_view);
    int2 grid_size = int2(res);
    int2 cell = int2(uv * res);
    cell = clamp(cell, int2(0, 0), grid_size - 1);

    // skysphere equirect is 4096 wide, one texel is the natural unresolved star size
    const float pixel_ang = PI2 / 4096.0;

    float3 result = float3(0, 0, 0);
    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            int2 c = cell + int2(dx, dy);
            c.x = (c.x + grid_size.x) % grid_size.x;
            // Do not count a polar cell more than once.
            if (c.y < 0 || c.y >= grid_size.y) continue;

            float4 cell_data = grid_tex.Load(int3(c, 0));
            uint offset = asuint(cell_data.x);
            uint count  = asuint(cell_data.y);
            count = min(count, 64u);

            [loop]
            for (uint i = 0; i < count; i++)
            {
                int idx = int(offset + i);
                float4 star = stars_tex.Load(int3(idx, 0, 0));
                float4 col  = stars_tex.Load(int3(idx, 1, 0));

                float3 star_dir = star.xyz;
                float ndot = dot(celestial_view, star_dir);
                // ~2.5 texels, enough for aa and the brightest diffraction arms
                if (ndot < 0.999995)
                {
                    continue;
                }

                float mag  = star.w;
                float flux = pow(10.0, -0.4 * mag);
                // relative brightness weight, mag -1.5 -> ~1, mag 6.5 -> ~0
                float bright = saturate((-mag + 6.5) / 8.0);
                float ang2   = max(0.0, 1.0 - ndot) * 2.0;

                // most stars stay inside one texel, only the brightest grow a little
                float core_r = pixel_ang * (0.22 + bright * bright * 0.85);
                float core   = exp(-ang2 / max(core_r * core_r, 1e-14));
                if (core < 1e-3)
                {
                    continue;
                }

                // energy near night_sky scale, strong magnitude contrast, no common floor
                float energy = flux * 0.035;
                float3 rgb   = col.rgb * energy;

                float seed    = frac(dot(star_dir, float3(12.9898, 78.233, 37.719)) * 43758.5453);
                float twinkle = 1.0 + bright * 0.18 * sin(time_seconds * (1.6 + seed * 2.4) + seed * PI2);
                float3 contrib = rgb * core * twinkle;

                // thin diffraction cross on the brightest only
                if (bright > 0.72)
                {
                    float3 t_axis, b_axis;
                    night_make_basis(star_dir, t_axis, b_axis);
                    float3 delta = celestial_view - star_dir * ndot;
                    float u = abs(dot(delta, t_axis));
                    float v = abs(dot(delta, b_axis));
                    float sw = pixel_ang * 0.10;
                    float sl = pixel_ang * (0.7 + bright * 1.6);
                    float spike = exp(-u / sw) * exp(-v / sl) + exp(-v / sw) * exp(-u / sl);
                    contrib += rgb * spike * bright * 0.28 * twinkle;
                }

                result += contrib;
            }
        }
    }
    return result;
}

// procedural galactic plane, fbm density modulated along a tilted band with dust rifts and a bulge
float3 night_compute_milky_way(float3 view_dir)
{
    const float3 galactic_up = float3(0.42064, 0.55179, 0.72260);
    const float3 bulge_dir   = float3(-0.79602, -0.09950, 0.59702);

    float lat  = abs(dot(view_dir, galactic_up));
    float band = exp(-lat * lat / 0.040);
    if (band < 0.001)
    {
        return float3(0, 0, 0);
    }

    float3 fp     = view_dir * 4.0;
    float density = night_fbm(fp, 4);
    density = saturate(density * 1.6 - 0.35);

    float dust = night_fbm(fp * 2.5 + 11.7, 4);
    dust       = saturate(dust * 1.3 - 0.40);
    density    = saturate(density - dust * 0.7);

    float bulge     = pow(saturate(dot(view_dir, bulge_dir)), 6.0);
    float intensity = density * (0.5 + bulge * 1.5);

    float3 cool = float3(0.55, 0.70, 1.00);
    float3 warm = float3(1.00, 0.85, 0.65);
    float3 col  = lerp(cool, warm, bulge);

    return col * intensity * band * 0.0018;
}

// moon split into disc and halo so they can be composited differently
struct moon_result
{
    float3 disc;
    float3 halo;
};

moon_result night_compute_moon(float3 view_dir, float3 moon_dir)
{
    moon_result r;
    r.disc = float3(0, 0, 0);
    r.halo = float3(0, 0, 0);
    
    float moon_elev = dot(moon_dir, up_direction);
    
    // fade the moon and its halo as it dips below the horizon
    float horizon_fade = smoothstep(-0.05, 0.10, moon_elev);
    if (horizon_fade <= 0.0)
    {
        return r;
    }
    
    float moon_radius = buffer_frame.celestial_sun.w;
    const float sin_r       = sin(moon_radius);
    
    float cos_angle = dot(view_dir, moon_dir);
    if (cos_angle < 0.0)
    {
        return r;
    }
    
    // soft wide gaussian halo around the moon, scaled to night_sky so it does not wash the sky blue
    float angle_to_moon = acos(saturate(cos_angle));
    float halo_falloff  = exp(-angle_to_moon * angle_to_moon / 0.0040);
    r.halo = float3(0.55, 0.72, 0.95) * halo_falloff * 0.0045 * horizon_fade;
    
    // outside the disc, only the halo contributes
    if (cos_angle < cos(moon_radius * 1.05))
    {
        return r;
    }
    
    // tangent basis around the moon for disc-local coordinates
    float3 t_axis, b_axis;
    night_make_basis(moon_dir, t_axis, b_axis);
    
    // disc-local coordinates u,v in [-1,1]
    float u  = dot(view_dir, t_axis) / sin_r;
    float v  = dot(view_dir, b_axis) / sin_r;
    float r2 = u * u + v * v;
    if (r2 > 1.0)
    {
        return r;
    }
    
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
    
    // The actual Sun direction lights the lunar surface and orients the terminator.
    float3 lunar_sun = buffer_frame.celestial_sun.xyz;
    
    // illumination with a soft terminator
    float n_dot_l = dot(n_perturbed, lunar_sun);
    float lit     = smoothstep(-0.04, 0.06, n_dot_l);
    
    // hapke-like limb darkening, less pronounced than the sun
    float limb = pow(saturate(w), 0.55);
    
    // earthshine, faint cool blue glow on the unlit side
    float dark         = saturate(-n_dot_l);
    float3 earthshine  = float3(0.04, 0.06, 0.10) * dark * 0.45;
    
    // cool moonlight, disc peak sits above night_sky but below a daylight sun
    float3 lunar_light = float3(0.92, 0.96, 1.00) * 0.045;
    float3 surface     = albedo * lunar_light * lit * limb + earthshine * 0.015;
    
    // anti-aliased disc edge
    float r_norm = sqrt(r2);
    float edge   = smoothstep(1.0, 0.96, r_norm);
    
    r.disc = surface * edge * horizon_fade;
    return r;
}

// night atmosphere, uses the shared night radiometric levels from common.hlsl
float3 night_compute_atmosphere(float3 view_dir, float3 moon_dir, Texture2D sky_view_lut, SamplerState samp)
{
    float3 base = night_sky_radiance(view_dir.y);
    
    float dy = view_dir.y - 0.04;
    float airglow_band = exp(-(dy * dy) / 0.0050);
    float3 airglow = night_airglow_rad * airglow_band;
    
    float moon_elev = dot(moon_dir, up_direction);
    float3 moon_scatter = float3(0, 0, 0);
    if (moon_elev > -0.10)
    {
        float3 lum  = sample_sky_view_lut(sky_view_lut, samp, view_dir, moon_dir, 1.0);
        float fade  = smoothstep(-0.05, 0.10, moon_elev);
        moon_scatter = lum * night_moon_to_sun * fade * buffer_frame.celestial_moon.w;
    }
    
    return base + airglow + moon_scatter;
}

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
    
    float2 uv = float2(tid.xy) / (res - 1.0);
    float cos_sun = uv.x * 2.0 - 1.0;
    // Include sea level and concentrate resolution in the dense lower atmosphere.
    float height = uv.y * uv.y * (atmosphere_radius - earth_radius);
    
    tex_uav[tid.xy] = float4(compute_multiscatter(height, cos_sun, tex, GET_SAMPLER(sampler_bilinear_clamp)), 1.0);
}

#elif defined(SKY_VIEW_LUT)
// bakes the full 32 step atmosphere march into a small direction indexed lut once per frame,
// the panorama bake then replaces its per pixel march with one bilinear fetch, the night path
// previously marched a second time for moon scatter which the moon half absorbs as well
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    float2 res;
    tex_uav.GetDimensions(res.x, res.y);
    if (any(tid.xy >= uint2(res)))
    {
        return;
    }
    
    float  half_h    = res.y * 0.5;
    bool   moon_half = float(tid.y) >= half_h;
    float2 texel     = float2(float(tid.x), moon_half ? float(tid.y) - half_h : float(tid.y));
    float2 unit      = float2(texel.x / (res.x - 1.0), texel.y / (half_h - 1.0));
    
    Light light;
    Surface surface;
    light.Build(0, surface);
    float3 sun_dir  = normalize(-light.forward);
    float  sun_elev = dot(sun_dir, up_direction);
    
    // the moon half is only read when the night factor exceeds its cutoff, skip the march during the day
    if (moon_half && sun_elev >= 0.35)
    {
        tex_uav[tid.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    
    float3 light_dir = moon_half ? buffer_frame.celestial_moon.xyz : sun_dir;
    float3 view_dir  = sky_view_unit_to_dir(unit, light_dir);
    float3 cam_pos   = clamp_camera_to_atmosphere(get_camera_position());
    
    float3 luminance = compute_sky_luminance(cam_pos, view_dir, light_dir, tex, tex2,
                                             GET_SAMPLER(sampler_bilinear_clamp), 0.5);
    tex_uav[tid.xy] = float4(luminance, 1.0);
}

#else
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    float2 res;
    tex_uav.GetDimensions(res.x, res.y);

    // bake mode, set by Pass_Skysphere via the first push constant float
    //   warmup > 0, the first n frames after a sun or app start change, the cpu
    //            issues a full sized dispatch and every output pixel runs the full bake, the
    //            value is the progressive blend for that frame (1, 1/2, 1/3 ...) so the first
    //            frame fully replaces the panorama, no ghost of the old sky survives, and the
    //            burst converges to the exact mean of the jittered bakes
    //   warmup = 0.0, steady state, the cpu issues a quarter resolution dispatch (1/16 of
    //            the waves) and each thread writes exactly one full resolution pixel chosen
    //            from a 4x4 tile by a phase that cycles with buffer_frame.frame, so every
    //            pixel refreshes every 16 frames or ~267 ms at 60 fps. the coarse dispatch
    //            is the real saver, an early-return scheme does not skip wave time because
    //            gpu lockstep execution pays for every wave with one active lane
    const float warmup_blend = buffer_pass.values[0].x;
    const bool  warmup       = warmup_blend > 0.0;
    uint2 pixel;
    if (warmup)
    {
        pixel = tid.xy;
    }
    else
    {
        // bayer ordered refresh, decorrelates staleness inside each 4x4 tile so cloud motion reads as soft noise instead of a raster combing pattern
        static const uint2 bayer_order[16] = { uint2(0, 0), uint2(2, 2), uint2(2, 0), uint2(0, 2), uint2(1, 1), uint2(3, 3), uint2(3, 1), uint2(1, 3), uint2(1, 0), uint2(3, 2), uint2(3, 0), uint2(1, 2), uint2(0, 1), uint2(2, 3), uint2(2, 1), uint2(0, 3) };
        pixel = tid.xy * 4u + bayer_order[buffer_frame.frame & 15u];
    }
    if (any(pixel >= uint2(res))) return;

    float2 uv = (float2(pixel) + 0.5) / res;
    
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
    float3 cam_pos = clamp_camera_to_atmosphere(get_camera_position());
    
    // sky luminance, one fetch from the per frame sky view lut instead of a 32 step march
    float3 luminance = sample_sky_view_lut(tex3, GET_SAMPLER(sampler_bilinear_clamp), view_dir, sun_dir, 0.0);
    
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
        float3 sun_trans = planet_transmittance(tex, GET_SAMPLER(sampler_bilinear_clamp),
            length(cam_pos - earth_center), dot(cam_up, orig_view));
        sun_col          = compute_sun_disc(orig_view, sun_dir, sun_trans);
    }
    
    // night sky, atmosphere is sky-dome, celestials ride behind the atmosphere
    // the physical sky needs no day factor, the earth shadow in the transmittance lut fades it naturally
    float night_factor      = 1.0 - day_factor;
    // stars only after the sun is below the horizon, never on a blue daytime sky
    float star_visibility = smoothstep(0.02, -0.12, sun_elev);
    // Celestial light traverses the same air as the Sun, including altitude and ozone.
    float3 view_trans = planet_transmittance(tex, GET_SAMPLER(sampler_bilinear_clamp),
        length(cam_pos - earth_center), dot(normalize(cam_pos - earth_center), orig_view));
    float3 night_ambient    = float3(0, 0, 0);
    float3 night_celestials = float3(0, 0, 0);
    if (night_factor > 0.001)
    {
        float3 celestial_view = night_apply_earth_rotation(orig_view);
        // Topocentric lunar ephemeris, independent of the solar direction.
        float3 moon_dir       = buffer_frame.celestial_moon.xyz;

        night_ambient = night_compute_atmosphere(view_dir, moon_dir, tex3, GET_SAMPLER(sampler_bilinear_clamp));

        if (!below_horizon)
        {
            float3 ext = view_trans;
            if (star_visibility > 0.001)
            {
                night_celestials += night_compute_milky_way(celestial_view) * ext * star_visibility;
                night_celestials += night_compute_stars(celestial_view, tex5, tex6, (float)buffer_frame.time) * ext * star_visibility;
            }


        }
        else
        {
            night_ambient *= ground_fade;
        }

        night_ambient    *= night_factor;
        night_celestials *= night_factor;
    }

    moon_result lunar = night_compute_moon(orig_view, buffer_frame.celestial_moon.xyz);
    float3 lunar_color = below_horizon ? float3(0,0,0) :
        (lunar.disc * view_trans + lunar.halo * night_factor * buffer_frame.celestial_moon.w);
    float3 final_color = luminance + night_ambient + sun_col + night_celestials + lunar_color;
    // chroma preserving clamp so the sun disc and any other hdr spike carry the directional
    // light's temperature into the panorama instead of clipping every channel to the cap
    final_color        = hdr_clamp_chroma(final_color, 100.0);

    // Each texel updates only once per 16 frames. A 0.06 blend added seconds of
    // lag and left trails behind moving celestial bodies; the LUT is deterministic.
    // The first warmup frame must not read uninitialized texture contents.
    if (warmup && warmup_blend < 1.0)
    {
        final_color = lerp(tex_uav[pixel].rgb, final_color, warmup_blend);
    }

    tex_uav[pixel] = float4(final_color, 1.0);
}
#endif
