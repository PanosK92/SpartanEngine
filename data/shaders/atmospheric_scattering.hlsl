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

static const float rayleigh                    = 0.2f;                                             // rayleigh scattering constant for air molecules
static const float mie_coefficient             = 0.01f;                                            // mie scattering constant for aerosol particles
static const float mie_directional_g           = 0.8f;                                             // asymmetry factor for the mie scattering phase function
static const float3 wavelength                 = float3(0.650f, 0.570f, 0.475f);                   // wavelengths of red, green, blue light
static const float3 inv_wavelength             = 1.0f / pow(wavelength, float3(4.0f, 4.0f, 4.0f)); // inverse wavelength to the fourth power for rayleigh scattering
static const float rayleigh_scale_height       = 8000.0f;                                          // scale height for rayleigh scattering (how quickly it decreases with altitude) in meters
static const float mie_scale_height            = 1200.0f;                                          // scale height for mie scattering (controls how atmospheric particles influence scattering)
static const float sun_angular_diameter_cos    = 0.999f;                                           // cosine of the sun's angular diameter, controls size of the sun's disk
static const float atmosphere_thickness_factor = 1.0f;                                             // factor for modifying the thickness of the atmosphere
static const float3 sunset_color               = float3(1.0f, 0.7f, 0.4f);                         // color used for sunrise/sunset interpolation

float saturate_zenith(float angle)
{
    return saturate((1.0 - angle / (PI / 2.0)));
}

float compute_atmosphere_thickness(float angle)
{
    float corrected_angle = saturate_zenith(angle);
    return atmosphere_thickness_factor * exp(-corrected_angle * 5.0);
}

float compute_sun_intensity(float zenith_angle)
{
    float corrected_angle = saturate_zenith(zenith_angle);
    return buffer_light.intensity * max(0.1, 1.0 - exp(-corrected_angle * 5.0));
}

float3 compute_sun_disc(float3 dir)
{
    float cos_angle = dot(dir, buffer_light.direction);
    float sun_disc_intensity = smoothstep(sun_angular_diameter_cos, sun_angular_diameter_cos + 0.001, cos_angle);
    return buffer_light.color.rgb * sun_disc_intensity * buffer_light.intensity;
}

float3 compute_rayleigh_scatter(float3 direction, float atmosphere_thickness, float3 buffer_light_direction)
{
    float rayleigh_optical_depth = exp(-direction.y / rayleigh_scale_height);
    return rayleigh * rayleigh_optical_depth * inv_wavelength * atmosphere_thickness;
}

float3 compute_mie_scatter(float3 direction, float atmosphere_thickness, float sun_intensity, float3 light_direction)
{
    float mie_optical_depth = exp(-direction.y / mie_scale_height);
    return mie_coefficient * mie_optical_depth *
           (1.0f - pow(mie_directional_g, 2.0f)) /
           (pow(1.0f + pow(mie_directional_g, 2.0f) -
           2.0f * mie_directional_g * dot(direction, light_direction), 1.5f)) *
           sun_intensity * atmosphere_thickness;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution = pass_get_resolution_out();
    if (any(thread_id.xy >= resolution))
        return;

    // convert spherical map uv to direction
    float2 uv        = (float2(thread_id.xy) + 0.5f) / resolution;
    float phi        = uv.x * 2.0 * PI;
    float theta      = (1.0f - uv.y) * PI;
    float3 direction = normalize(float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi)));

    // sun
    float sun_zenith_angle = acos(dot(direction, float3(0, 1, 0)));
    float sun_intensity    = compute_sun_intensity(sun_zenith_angle);
    float3 sun_disc        = compute_sun_disc(direction);
    
    // atmosphere
    float atmosphere_thickness = compute_atmosphere_thickness(sun_zenith_angle);
    float3 rayleigh_scatter    = compute_rayleigh_scatter(direction, atmosphere_thickness, buffer_light.direction);
    float3 mie_scatter         = compute_mie_scatter(direction, atmosphere_thickness, sun_intensity, buffer_light.direction);
    float3 sun_color           = lerp(buffer_light.color.rgb, sunset_color, pow(saturate_zenith(sun_zenith_angle), 1.5));
    
    // combine
    float3 scatter   = rayleigh_scatter + mie_scatter;
    float3 sky_color = scatter * sun_color + sun_disc;

    tex_uav[thread_id.xy] = float4(sky_color, 1.0f);
}
