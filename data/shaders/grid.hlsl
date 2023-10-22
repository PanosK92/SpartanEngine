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

static const float4 grid_color = float4(0.5f, 0.5f, 0.5f, 1.0f);

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    const float2 properties    = pass_get_f3_value().xy;
    const float line_interval  = properties.x;
    const float line_thickness = properties.y;
    
    // calculate the modulated distance for both x and y coordinates
    float mod_x = fmod(input.uv.x, line_interval);
    float mod_y = fmod(input.uv.y, line_interval);
    
    // use step function to determine if the pixel is near a line
    float line_x = step(line_thickness, mod_x) - step(line_interval - line_thickness, mod_x);
    float line_y = step(line_thickness, mod_y) - step(line_interval - line_thickness, mod_y);
    
    // combine line_x and line_y to decide if either is true, then color it as a line
    float is_line = max(1.0f - line_x, 1.0f - line_y);

    // either grid_color or 0 based on is_line
    return is_line * grid_color + (1.0f - is_line) * 0.0f;
}
