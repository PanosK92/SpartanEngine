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

static const float sea_level  = 0.0f;
static const float snow_level = 75.0f;

static float get_snow_level_with_variation(float3 position_world)
{
    // define constants
    const float frequency = 0.3f;
    const float amplitude = 10.0f;

    // apply sine wave based on world position
    float sine_value = sin(position_world.x * frequency);

    // map sine value from [-1, 1] to [0, 1]
    sine_value = sine_value * 0.5 + 0.5;

    // apply height variation and add to base snow level
    return snow_level + sine_value * amplitude;
}

static float get_snow_blend_factor(float3 position_world)
{
    const float snow_blend_speed = 0.1f;
    const float max_blend_factor = 0.7f; // cap the max so we can still see the base texture
    
    const float snow_level  = get_snow_level_with_variation(position_world);
    float distance_to_snow  = position_world.y - snow_level;
    float snow_blend_factor = min(saturate(1.0 - max(0.0, -distance_to_snow) * snow_blend_speed), max_blend_factor);
    
    return snow_blend_factor;
}
