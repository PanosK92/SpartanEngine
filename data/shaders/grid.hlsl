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

// properties
static const float4 grid_color          = float4(0.5f, 0.5f, 0.5f, 1.0f);
static const float  fade_start          = 0.1f;
static const float  fade_end            = 0.5f;
static const float  anti_moire_start    = 0.0f;
static const float  anti_moire_strength = 100.0f;

// increase line thickness the further away we get, this counteracts the moire pattern
float compute_anti_moire_factor(float2 uv, float moire_start)
{
    float2 centered_uv         = uv - 0.5f;
    float distance_from_center = length(centered_uv);
    float moire_end            = 1.0f;
    return lerp(1.0f, anti_moire_strength, saturate((distance_from_center - moire_start) / (moire_end - moire_start)));
}

// fade out the further away we get from the center, this prevents us from seeing the edge of the grid
float compute_fade_factor(float2 uv, float fade_start, float fade_end)
{
    float2 centered_uv         = uv - 0.5f;
    float distance_from_center = length(centered_uv);
    return saturate((fade_end - distance_from_center) / (fade_end - fade_start));
}

float4 mainPS(Pixel_PosUv input)
{
    const float2 properties   = pass_get_f3_value().xy;
    const float line_interval = properties.x;
    float line_thickness      = properties.y;

    float fade_factor       = compute_fade_factor(input.uv, fade_start, fade_end);
    float anti_moire_factor = compute_anti_moire_factor(input.uv, anti_moire_start);
    
    // anti-moire
    line_thickness *= anti_moire_factor;
    
    // calculate the modulated distance for both x and y coordinates
    float mod_x = fmod(input.uv.x, line_interval);
    float mod_y = fmod(input.uv.y, line_interval);
    
    // use step function to determine if the pixel is near a line
    float line_x = step(line_thickness, mod_x) - step(line_interval - line_thickness, mod_x);
    float line_y = step(line_thickness, mod_y) - step(line_interval - line_thickness, mod_y);
    
    // combine line_x and line_y to decide if either is true, then color it as a line
    float is_line = max(1.0f - line_x, 1.0f - line_y);

    // write both on the color render target and the reactive mask
    return fade_factor * (is_line * grid_color + (1.0f - is_line) * 0.0f);
}
