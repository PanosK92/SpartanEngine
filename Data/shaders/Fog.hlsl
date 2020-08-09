/*
Copyright(c) 2016-2020 Panos Karabelas

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

static const float fog_start        = -0.5f;
static const float fog_end          = 5.0f;
static const float fog_start_height = -0.5f;
static const float fog_end_height   = 7.0f;

float get_fog_factor(const float pixel_y, const float pixel_z)
{
    float depth_factor  = saturate(1.0f - (fog_end - pixel_z)           / (fog_end - fog_start + FLT_MIN));
    float height_factor = saturate(1.0f - (fog_end_height - pixel_y)    / (fog_end_height - fog_start_height + FLT_MIN));
    
    return depth_factor * height_factor * g_fog_density;
}


float get_fog_factor(const Surface surface) { return get_fog_factor(surface.position.y, surface.camera_to_pixel_length); }
