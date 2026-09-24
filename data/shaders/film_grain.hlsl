/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

static const float g_film_grain_intensity = 0.002f;
static const float g_film_grain_speed     = 3.0f;
static const float g_film_grain_mean      = 0.0f; // What gray level noise should tend to.
static const float g_film_grain_variance  = 0.5f; // Controls the contrast/variance of noise.

float gaussian(float z, float u, float o) {
    return (1.0 / (o * sqrt(2.0 * 3.1415))) * exp(-(((z - u) * (z - u)) / (2.0 * (o * o))));
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    
    const float2 uv = (thread_id.xy + 0.5f) / resolution_out;
    float4 color    = tex[thread_id.xy];

    // film grain
    float t          = buffer_frame.frame * float(g_film_grain_speed);
    float seed       = dot(uv, float2(12.9898, 78.233));
    float noise      = frac(sin(seed) * 43758.5453 + t);
    noise            = gaussian(noise, float(g_film_grain_mean), float(g_film_grain_variance) * float(g_film_grain_variance));
    float film_grain =  noise * g_film_grain_intensity;

    // iso noise
    float camera_iso = pass_get_f3_value().x;
    float iso_noise  = hash(frac(uv.x * uv.y * buffer_frame.frame)) * camera_iso * 0.000002f;
    
    // additive blending
    color.rgb += (film_grain + iso_noise) * 0.5f;

    tex_uav[thread_id.xy] = float4(saturate(color.rgb), 1.0f);
}
