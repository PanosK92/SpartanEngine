/*
Copyright(c) 2015-2025 Panos Karabelas

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

static const float sea_level  = 0.0f;
static const float snow_level = 100.0f;

static float get_snow_blend_factor(float3 position_world, float3 normal_world)
{
    const float snow_blend_speed = 0.15f; // transition sharpness
    const float noise_scale      = 0.2f;  // noise frequency for terrain/vegetation
    const float noise_strength   = 20.0f; // height variation for snow level
    const float slope_threshold  = 0.5f;  // ~45 degrees, snow diminishes
    const float slope_factor     = 0.2f;  // snow reduction on steep slopes
    const float wind_influence   = 0.5f;  // wind effect strength
    const float edge_jitter      = 0.15f; // subtle edge variation

    // calculate height-based snow distance
    float distance_to_snow = position_world.y - snow_level;

    // add perlin noise for organic snow level variation
    float2 noise_coords  = position_world.xz * noise_scale;
    float noise          = get_noise_perlin(noise_coords); // [0, 1]
    noise                = noise * 2.0f - 1.0f; // remap to [-1, 1]
    distance_to_snow    += noise * noise_strength; // perturb snow level

    // base snow blend factor
    float snow_blend_factor = saturate(1.0f - max(0.0f, -distance_to_snow) * snow_blend_speed);

    // modulate based on surface slope
    float slope_dot           = dot(normal_world, float3(0.0f, 1.0f, 0.0f)); // compare to world up
    float slope_factor_final  = lerp(slope_factor, 1.0f, smoothstep(slope_threshold, 1.0f, slope_dot));
    snow_blend_factor        *= slope_factor_final;

    // modulate based on wind direction
    float3 wind         = buffer_frame.wind;
    float wind_strength = length(wind);
    if (wind_strength > 0.0f)
    {
        float3 wind_dir      = wind / wind_strength; // normalize direction
        float wind_exposure  = dot(normal_world, wind_dir); // surface exposure to wind
        snow_blend_factor   *= lerp(1.0f - wind_influence, 1.0f, smoothstep(-1.0f, 1.0f, wind_exposure));
    }

    // add subtle noise for edge variation
    float edge_noise   = get_noise_perlin(noise_coords + float2(5.0f, 5.0f)); // offset noise
    snow_blend_factor *= lerp(1.0f - edge_jitter, 1.0f, edge_noise); // slight intensity variation

    return snow_blend_factor;
}
