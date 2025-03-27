/*
Copyright(c) 2016-2025 Panos Karabelas

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
static const float3 up_direction     = float3(0, 1, 0);                  // up direction
static const float3 earth_center     = float3(0, -6371e3, 0);            // earth center at -radius (meters), y-up
static const float earth_radius      = 6371e3;                           // earth radius in meters
static const float atmosphere_height = 100e3;                            // atmosphere thickness in meters
static const float h_rayleigh        = 7994.0;                           // rayleigh scale height in meters
static const float h_mie             = 1200.0;                           // mie scale height in meters
static const float3 beta_rayleigh    = float3(5.8e-6, 13.5e-6, 33.1e-6); // rayleigh scattering coefficients (m^-1)
static const float3 beta_mie         = float3(2e-5, 2e-5, 2e-5);         // mie scattering coefficients (m^-1)
static const float g_mie             = 0.8;                              // mie phase asymmetry factor (forward scattering)
static const int num_view_samples    = 6;                                // samples along view ray
static const int num_sun_samples     = 6;                                // samples along sun ray

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
        float sun_elevation      = saturate(dot(sun_dir, up_direction) + 1.0);
        float mie                = lerp(0.01f, 0.04f, sun_elevation);
        float mie_g              = lerp(-0.9f, -0.6f, sun_elevation);
        float3 directional_light = compute_mie_scatter_color(view_dir, sun_dir, mie, mie_g) * 0.3f;
        float3 sun_disc          = compute_mie_scatter_color(view_dir, sun_dir, 0.001f, -0.998f);
        float fade_out_factor    = saturate(sun_elevation * 10.0f);
        
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
            // create
            float2 star_uv = uv * 100.0f;
            float2 hash    = hash22(star_uv);
            brightness     = step(0.999f, hash.x);

            // twinkle
            float twinkle  = 0.5f + 0.5f * sin((float) buffer_frame.time * 2.0 + hash.y * 6.28318f);
            brightness    *= twinkle;
    
            // fade in
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
    float t1        = -b - sqrt_disc; // near intersection
    float t2        = -b + sqrt_disc; // far intersection
    
    if (t2 > 0)
        return t2;
    else if (t1 > 0)
        return t1;
    else
        return -1.0;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : sv_dispatchthreadid)
{
    // get output texture dimensions
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);

    // compute uv coordinates
    float2 uv = (float2(thread_id.xy) + 0.5f) / resolution;

    // convert uv to view direction (spherical mapping for skysphere)
    float phi             = uv.x * 2.0 * PI; // azimuth: 0 to 2π
    float theta           = -uv.y * PI;      // zenith: 0 (top) to π (bottom)
    float sin_theta       = sin(theta);
    float cos_theta       = cos(theta);
    float3 view_direction = float3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));

    // get sun direction from light
    Light light;
    light.Build();
    float3 sun_direction = -light.forward; // points from world to sun

    // compute atmosphere
    float3 atmosphere_color = 0.0f;
    {
        float t_max = intersect_sphere(buffer_frame.camera_position, view_direction, earth_center, earth_radius + atmosphere_height);
        if (t_max < 0)
        {
            tex_uav[thread_id.xy] = float4(atmosphere_color, 1.0f); // no intersection
            return;
        }

        float ds                  = t_max / num_view_samples;
        float3 integral_rayleigh  = 0.0;
        float3 integral_mie       = 0.0;
        float3 optical_depth_view = 0.0;

        for (int i = 0; i < num_view_samples; i++)
        {
            float t         = (i + 0.5) * ds;
            float3 position = buffer_frame.camera_position + t * view_direction;
            float height    = length(position - earth_center) - earth_radius;
            if (height < 0)
                break;

            float density_rayleigh = exp(-height / h_rayleigh);
            float density_mie      = exp(-height / h_mie);
            float3 t_view          = exp(-optical_depth_view);

            // check if the sun is occluded by the earth
            float s_earth = intersect_sphere(position, sun_direction, earth_center, earth_radius);
            float3 t_sun;
            if (s_earth > 0)
            {
                t_sun = 0.0; // sun is blocked by the earth, no light reaches this point
            }
            else
            {
                // sun is visible, compute transmission through the atmosphere
                float s_max = intersect_sphere(position, sun_direction, earth_center, earth_radius + atmosphere_height);
                if (s_max < 0)
                {
                    t_sun = 0.0; // ray doesn’t intersect atmosphere (shouldn’t happen here)
                }
                else
                {
                    float ds_sun             = s_max / num_sun_samples;
                    float3 optical_depth_sun = 0.0;

                    for (int j = 0; j < num_sun_samples; j++)
                    {
                        float s          = (j + 0.5) * ds_sun;
                        float3 sun_pos   = position + s * sun_direction;
                        float height_sun = length(sun_pos - earth_center) - earth_radius;
                        if (height_sun < 0)
                            break;

                        float density_r_sun  = exp(-height_sun / h_rayleigh);
                        float density_m_sun  = exp(-height_sun / h_mie);
                        optical_depth_sun   += (density_r_sun * beta_rayleigh + density_m_sun * beta_mie) * ds_sun;
                    }
                    t_sun = exp(-optical_depth_sun);
                }
            }

            float cos_theta_phase = dot(view_direction, sun_direction);
            float phase_rayleigh  = (3.0 / (16.0 * PI)) * (1.0 + cos_theta_phase * cos_theta_phase);
            float phase_mie       = (1.0 - g_mie * g_mie) / 
                                   (4.0 * PI * pow(1.0 + g_mie * g_mie - 2.0 * g_mie * cos_theta_phase, 1.5));

            integral_rayleigh += density_rayleigh * phase_rayleigh * t_sun * t_view * ds;
            integral_mie      += density_mie * phase_mie * t_sun * t_view * ds;

            optical_depth_view += (density_rayleigh * beta_rayleigh + density_mie * beta_mie) * ds;
        }

        atmosphere_color = (beta_rayleigh * integral_rayleigh + beta_mie * integral_mie) * light.intensity;
    }
    

    // artistic touches
    float3 sun_color  = sun::compute_color(view_direction, sun_direction);
    float3 star_color = stars::compute_color(uv, sun_direction);

    // compose output
    tex_uav[thread_id.xy] = float4(atmosphere_color + sun_color + star_color, 1.0);
}
