/*
Copyright(c) 2016-2023 Panos Karabelas

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

float3 compute_rayleigh_scatter_color(float3 view_dir, float3 sun_dir, float3 position, float brightness)
{
    // constants
    const float3 rayleigh_beta    = float3(5.8e-6, 13.5e-6, 33.1e-6);
    const float h0                = 7994.0;
    const float hm                = 1200.0;
    const float earth_radius      = 6371e3; // in meters
    const float atmosphere_radius = 6471e3; // in meters, assuming a 100km atmosphere
    const float3 earth_center     = float3(0, -earth_radius, 0);
    
    // calculations for rayleigh scattering
    float h                = length(position - earth_center) - earth_radius;
    float3 p0              = position + view_dir * (h0 - h);
    float3 p1              = position + view_dir * (hm - h);
    float3 view_ray_length = p1 - p0;
    float cos_theta        = dot(view_dir, sun_dir);
    float phase            = 0.75f * (1.0f + cos_theta * cos_theta);
    float optical_depth_r  = exp(-h / h0) * length(view_ray_length) /  dot(view_dir, float3(0, -1, 0));
    float3 scatter         = rayleigh_beta * phase * optical_depth_r;
    
    // calculate angle from horizon
    float3 up_dir   = float3(0, -1, 0); // assuming up is in the +y direction
    float sun_angle = saturate(1.0f - dot(sun_dir, up_dir));

    // color shift towards orange/red at the horizon
    float3 horizon_color    = float3(1.0, 0.5, 0.0); // example orange color
    float3 adjusted_scatter = lerp(scatter, scatter * horizon_color, sun_angle) * (1.0f - sun_angle);

    return adjusted_scatter * brightness * 0.05f;
}

float3 compute_mie_scatter_color(float3 view_dir, float3 sun_dir, float brightness)
{
    const float mie    = 0.001f;
    const float mie_g  = -0.99f;
    const float mie_g2 = mie_g * mie_g;

    // mie scattering calculations
    float eye_cos     = -dot(view_dir, sun_dir);
    float eye_cos2    = eye_cos * eye_cos;
    float temp        = 1.0 + mie_g2 - 2.0 * mie_g * eye_cos;
    temp              = smoothstep(0.0, 0.01f, temp) * temp;
    float mie_scatter = (1.5 * ((1.0 - mie_g2) / (2.0 + mie_g2)) * (1.0 + eye_cos2) / temp) * mie * brightness;

    // calculate angle from horizon
    float3 up_dir   = float3(0, -1, 0);
    float sun_angle = saturate(1.0f - dot(sun_dir, up_dir)); // 0 at horizon, 1 at zenith

    // color shift towards orange/red at the horizon
    float3 horizon_color = float3(1.0, 0.5, 0.0); // example orange color, you should tune this
    float3 mie_color     = lerp(1.0f, horizon_color, sun_angle); // white color when sun is high

    return mie_color * mie_scatter;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution = pass_get_resolution_out();
    if (any(thread_id.xy >= resolution))
        return;

    // convert spherical map UV to direction
    float2 uv             = (float2(thread_id.xy) + 0.5f) / resolution;
    float phi             = uv.x * 2.0 * PI;
    float theta           = (1.0f - uv.y) * PI;
    float3 view_direction = normalize(float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi)));

    // rayleigh scatter - this is the actual atmospheric color
    float3 rayleigh_color = compute_rayleigh_scatter_color(view_direction, buffer_light.direction, buffer_frame.camera_position, buffer_light.intensity);

    // mie scatter - this is the sun disc and the directional light coming from that disc
    float3 mie_color = compute_mie_scatter_color(view_direction, buffer_light.direction, buffer_light.intensity);
    
    tex_uav[thread_id.xy] = float4(rayleigh_color + mie_color, 1.0f);
}
