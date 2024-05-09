/*
Copyright(c) 2016-2024 Panos Karabelas

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

struct space
{
    static float3 hash(float3 p)
    {
        p = float3(dot(p, float3(127.1, 311.7, 74.7)),
                   dot(p, float3(269.5, 183.3, 246.1)),
                   dot(p, float3(113.5, 271.9, 124.6)));
    
        return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
    }
    
    static float noise(float3 p)
    {
        float3 i = floor(p);
        float3 f = frac(p);
        float3 u = f * f * (3.0 - 2.0 * f);

        float3 h0 = hash(i);
        float3 h1 = hash(i + float3(1.0, 0.0, 0.0));
        float3 h2 = hash(i + float3(0.0, 1.0, 0.0));
        float3 h3 = hash(i + float3(1.0, 1.0, 0.0));
        float3 h4 = hash(i + float3(0.0, 0.0, 1.0));
        float3 h5 = hash(i + float3(1.0, 0.0, 1.0));
        float3 h6 = hash(i + float3(0.0, 1.0, 1.0));
        float3 h7 = hash(i + float3(1.0, 1.0, 1.0));

        float n0 = dot(h0, f);
        float n1 = dot(h1, f - float3(1.0, 0.0, 0.0));
        float n2 = dot(h2, f - float3(0.0, 1.0, 0.0));
        float n3 = dot(h3, f - float3(1.0, 1.0, 0.0));
        float n4 = dot(h4, f - float3(0.0, 0.0, 1.0));
        float n5 = dot(h5, f - float3(1.0, 0.0, 1.0));
        float n6 = dot(h6, f - float3(0.0, 1.0, 1.0));
        float n7 = dot(h7, f - float3(1.0, 1.0, 1.0));

        return lerp(lerp(lerp(n0, n1, u.x), lerp(n2, n3, u.x), u.y),
                    lerp(lerp(n4, n5, u.x), lerp(n6, n7, u.x), u.y),
                    u.z);
    }
    
    static float3 compute_star_color(float2 uv, float3 atmosphere_color, float time)
    {
        // reverse spherical mapping to counteract distortion
        float reversed_phi    = uv.x * 2.0 * PI;
        float reversed_theta  = (1.0f - uv.y) * PI;
        float3 view_direction = normalize(float3(sin(reversed_theta) * cos(reversed_phi), cos(reversed_theta), sin(reversed_theta) * sin(reversed_phi)));

        // parameters
        float scale         = 1000.0f;
        float probability   = 18.5f;
        float exposure      = 1000000.0f;
        float flicker_speed = 0.2f;

        float stars_noise_base  = noise(view_direction * scale);
        float stars_noise       = pow(clamp(stars_noise_base, 0.0f, 1.0f), probability) * exposure;
        stars_noise            *= lerp(0.5, 1.5, noise(view_direction * scale * 0.5f + time * flicker_speed));
        float intensity         = saturate(1.0f - luminance(atmosphere_color) - 0.95f);

        return float3(stars_noise, stars_noise, stars_noise)  * intensity;
    }

    static float3 compute_color(float2 uv, float3 atmosphere_color, float time)
    {
        const float3 base_starlight_color = float3(0.05f, 0.05f, 0.1f); // soft, cool blue-gray tone
        const float3 star_color           = compute_star_color(uv, atmosphere_color, time);

        return base_starlight_color * 0.05f + star_color;
    };
};

struct sun
{
    static float3 compute_mie_scatter_color(float3 view_direction, float3 sun_direction, float mie = 0.01f, float mie_g = -0.95f)
    {
        const float mie_g2 = mie_g * mie_g;

        float eye_cos     = -dot(view_direction, sun_direction);
        float eye_cos2    = eye_cos * eye_cos;
        float temp        = 1.0 + mie_g2 - 2.0 * mie_g * eye_cos;
        temp              = smoothstep(0.0, 0.01f, temp) * temp;
        float mie_scatter = (1.5 * ((1.0 - mie_g2) / (2.0 + mie_g2)) * (1.0 + eye_cos2) / temp) * mie;

        return mie_scatter;
    }

    static float3 compute_color(float3 view_dir, float3 sun_dir)
    {
        float sun_elevation = 1.0f - saturate(1.0f - dot(sun_dir, float3(0, -1, 0)));
        float mie           = lerp(0.01f, 0.04f, 1.0f - sun_elevation);
        float mie_g         = lerp(-0.9f, -0.6f, 1.0f - sun_elevation);

        float3 directional_light = compute_mie_scatter_color(view_dir, sun_dir, mie, mie_g) * 0.3f;
        float3 sun_disc          = compute_mie_scatter_color(view_dir, sun_dir, 0.001f, -0.998f);
        
         // fade out as sun goes below the horizon
        float fade_out_factor = saturate(sun_elevation / 0.1f); // adjust 0.1f for fade speed
        return (directional_light + sun_disc) * fade_out_factor;
    }
};

struct atmosphere
{
    static float3 compute_color(float3 view_dir, float3 sun_dir, float3 position)
    {
        // constants
        const float3 rayleigh_beta    = float3(5.8e-6, 13.5e-6, 33.1e-6);
        const float h0                = 7994.0;
        const float hm                = 1200.0;
        const float earth_radius      = 6371e3; // in meters
        const float atmosphere_radius = 6471e3; // in meters, assuming a 100km atmosphere
        const float3 earth_center     = float3(0, -earth_radius, 0);
    
        // rayleigh scattering
        float h                = length(position - earth_center) - earth_radius;
        float3 p0              = position + view_dir * (h0 - h);
        float3 p1              = position + view_dir * (hm - h);
        float3 view_ray_length = p1 - p0;
        float cos_theta        = dot(view_dir, sun_dir);
        float phase            = (1.0f + cos_theta * cos_theta) * 2.2f; // 2.0 is empirically chosen, it looks good
        float optical_depth_r  = exp(-h / h0) * length(view_ray_length) / dot(view_dir, float3(0, -1, 0));
        float3 scatter         = rayleigh_beta * phase * optical_depth_r;
    
        // weaken as the sun approaches the horizon
        float sun_elevation   = 1.0f - saturate(1.0f - dot(sun_dir, float3(0, -1, 0)));
        float fade_out_factor = saturate(sun_elevation / 0.2f); // adjust 0.1f for fade speed
        return scatter * fade_out_factor;
    }
};

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    // convert spherical map UV to direction
    float2 uv             = (float2(thread_id.xy) + 0.5f) / resolution_out;
    float phi             = uv.x * 2.0 * PI;
    float theta           = (1.0f - uv.y) * PI;
    float3 view_direction = normalize(float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi)));

    // create light
    Light light;
    light.Build();

    // compute individual factors that contribute to what we see when we look up there
    float3 color  = atmosphere::compute_color(view_direction, light.forward, buffer_frame.camera_position) * light.intensity * 0.03f;
    color        += sun::compute_color(view_direction, light.forward) * light.color.rgb * light.intensity;
    color        += space::compute_color(uv, color, (float)buffer_frame.time);
      
    tex_uav[thread_id.xy] = float4(color, 1.0f);
}
