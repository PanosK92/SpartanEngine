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

static const float3 UP_VECTOR            = float3(0, -1, 0);
static const float3 RAYLEIGH_BETA        = float3(5.8e-6, 13.5e-6, 33.1e-6);
static const float EARTH_RADIUS          = 6371e3;
static const float ATMOSPHERE_HEIGHT     = 100e3;
static const float3 BASE_STARLIGHT_COLOR = float3(0.05f, 0.05f, 0.1f);

struct space
{
    static float3 hash(float3 p)
    {
        float3 dots = float3(
            dot(p, float3(127.1, 311.7, 74.7)),
            dot(p, float3(269.5, 183.3, 246.1)),
            dot(p, float3(113.5, 271.9, 124.6))
        );
        
        return frac(sin(dots) * 43758.5453123) * 2.0 - 1.0;
    }
    
    static float noise(float3 p)
    {
        float3 i = floor(p);
        float3 f = frac(p);
        float3 u = f * f * (3.0 - 2.0 * f);
        
        // hash values
        float3 h[8] = {
            hash(i),
            hash(i + float3(1.0, 0.0, 0.0)),
            hash(i + float3(0.0, 1.0, 0.0)),
            hash(i + float3(1.0, 1.0, 0.0)),
            hash(i + float3(0.0, 0.0, 1.0)),
            hash(i + float3(1.0, 0.0, 1.0)),
            hash(i + float3(0.0, 1.0, 1.0)),
            hash(i + float3(1.0, 1.0, 1.0))
        };

        // offsets
        float3 f1 = f - float3(1.0, 0.0, 0.0);
        float3 f2 = f - float3(0.0, 1.0, 0.0);
        float3 f3 = f - float3(1.0, 1.0, 0.0);
        float3 f4 = f - float3(0.0, 0.0, 1.0);
        float3 f5 = f - float3(1.0, 0.0, 1.0);
        float3 f6 = f - float3(0.0, 1.0, 1.0);
        float3 f7 = f - float3(1.0, 1.0, 1.0);

        // dots
        float4 n0123 = float4(
            dot(h[0], f),
            dot(h[1], f1),
            dot(h[2], f2),
            dot(h[3], f3)
        );
        float4 n4567 = float4(
            dot(h[4], f4),
            dot(h[5], f5),
            dot(h[6], f6),
            dot(h[7], f7)
        );

        // interpolation
        float2 lerp_xy = lerp(
            lerp(n0123.xy, n0123.zw, u.y),
            lerp(n4567.xy, n4567.zw, u.y),
            u.z
        );
        
        return lerp(lerp_xy.x, lerp_xy.y, u.x);
    }
    
    static float3 compute_star_color(float2 uv, float3 atmosphere_color, float time)
    {
        // spherical coordinates
        float reversed_phi    = uv.x * PI2;
        float reversed_theta  = (1.0f - uv.y) * PI;
        float sin_theta       = sin(reversed_theta);
        
        float3 view_direction = float3(
            sin_theta * cos(reversed_phi),
            cos(reversed_theta),
            sin_theta * sin(reversed_phi)
        );

        // constants
        const float SCALE         = 1000.0f;
        const float PROBABILITY   = 18.5f;
        const float EXPOSURE      = 1000000.0f;
        const float FLICKER_SPEED = 0.2f;

        float stars_noise_base  = noise(view_direction * SCALE);
        float stars_noise       = pow(saturate(stars_noise_base), PROBABILITY) * EXPOSURE;
        stars_noise            *= lerp(0.5, 1.5, noise(mad(view_direction, SCALE * 0.5f, time * FLICKER_SPEED)));
        
        float atmos_luminance  = dot(atmosphere_color, float3(0.299, 0.587, 0.114));
        float intensity        = saturate(0.05f - atmos_luminance);
        
        return stars_noise * intensity;
    }

    static float3 compute_color(float2 uv, float3 atmosphere_color, float time)
    {
        return mad(BASE_STARLIGHT_COLOR, 0.05f, compute_star_color(uv, atmosphere_color, time));
    }
};

struct sun
{
    static float3 compute_mie_scatter_color(float3 view_direction, float3 sun_direction, float mie, float mie_g)
    {
        float eye_cos  = -dot(view_direction, sun_direction);
        float mie_g2   = mie_g * mie_g;
        float temp     = mad(mie_g, -2.0 * eye_cos, 1.0 + mie_g2);
        temp           = smoothstep(0.0, 0.01f, temp) * temp;
        
        float eye_cos2 = eye_cos * eye_cos;
        
        return mie * (1.5 * ((1.0 - mie_g2) / (2.0 + mie_g2)) * (1.0 + eye_cos2) / temp);
    }

    static float3 compute_color(float3 view_dir, float3 sun_dir)
    {
        float sun_elevation      = saturate(dot(sun_dir, UP_VECTOR) + 1.0);
        float mie                = lerp(0.01f, 0.04f, sun_elevation);
        float mie_g              = lerp(-0.9f, -0.6f, sun_elevation);

        float3 directional_light = compute_mie_scatter_color(view_dir, sun_dir, mie, mie_g) * 0.3f;
        float3 sun_disc          = compute_mie_scatter_color(view_dir, sun_dir, 0.001f, -0.998f);
        
        float fade_out_factor    = saturate(sun_elevation * 10.0f);
        
        return (directional_light + sun_disc) * fade_out_factor;
    }
};

struct atmosphere
{
    static float3 compute_color(float3 view_dir, float3 sun_dir, float3 position)
    {
        const float h0            = 7994.0;
        const float hm            = 1200.0;
        const float3 earth_center = float3(0, -EARTH_RADIUS, 0);
    
        float h                   = length(position - earth_center) - EARTH_RADIUS;
        float3 p0                 = position + view_dir * (h0 - h);
        float3 p1                 = position + view_dir * (hm - h);
        float3 view_ray_length    = p1 - p0;
        
        float cos_theta           = dot(view_dir, sun_dir);
        float phase               = mad(cos_theta, cos_theta, 1.0f) * 2.2f;
        
        float inv_up_dot          = 1.0 / dot(view_dir, UP_VECTOR);
        float optical_depth_r     = exp(-h / h0) * length(view_ray_length) * inv_up_dot;
        float3 scatter            = RAYLEIGH_BETA * phase * optical_depth_r;
    
        float sun_elevation       = saturate(dot(sun_dir, UP_VECTOR) + 1.0);
        float fade_out_factor     = saturate(sun_elevation * 5.0f);
        
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

    float2 uv = (float2(thread_id.xy) + 0.5f) / resolution_out;
    
    // spherical mapping
    float phi             = uv.x * PI2;
    float theta           = (1.0f - uv.y) * PI;
    float sin_theta       = sin(theta);
    float3 view_direction = float3(
        sin_theta * cos(phi),
        cos(theta),
        sin_theta * sin(phi)
    );

    Light light;
    light.Build();

    float3 atmos_color = atmosphere::compute_color(view_direction, light.forward, buffer_frame.camera_position) * light.intensity * 0.03f;
    float3 final_color = mad(
        sun::compute_color(view_direction, light.forward) * light.color.rgb,
        light.intensity,
        atmos_color + space::compute_color(uv, atmos_color, (float)buffer_frame.time)
    );
      
    tex_uav[thread_id.xy] = float4(final_color, 1.0f);
}
