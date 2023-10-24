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

// thse functions are shared between depth_prepass.hlsl and g_buffer.hlsl, this is because the calculations have to be exactly the same

struct wind
{
    static float hash(float n)
    {
        return frac(sin(n) * 43758.5453f);
    }

    static float perlin_noise(float x)
    {
        float i = floor(x);
        float f = frac(x);
        f = f * f * (3.0 - 2.0 * f);

        return lerp(hash(i), hash(i + 1.0), f);
    }

    static float4 apply_to_vertex(uint instance_id, float4 world_position, float time)
    {
        const float3 wind_direction = float3(1, 0, 0);
        const float sway_extent     = 0.08f; // oscillation amplitude
        const float sway_speed      = 4.0f;  // oscillation frequency
        const float phase_offset    = float(instance_id) * 0.1f;

        if (material_vertex_animate_wind())
        {
            // base sine wave
            float base_wave = sin((time * sway_speed) + world_position.x + phase_offset);

            // additional Perlin noise
            float noise_factor = perlin_noise(world_position.x * 0.1f + time) - 0.5f;

            // combine multiple frequencies
            float combined_wave = base_wave + 0.5 * sin((time * sway_speed * 1.5f) + world_position.x + phase_offset);

            // calculate final offset
            float3 offset = wind_direction * (combined_wave + noise_factor) * sway_extent;

            world_position.xyz += offset;
        }

        return world_position;
    }
};

float4 compute_screen_space_position(Vertex_PosUvNorTan input, uint instance_id, matrix transform, matrix view_projection)
{
    float4 position = input.position;
    position.w      = 1.0f;
    
    float4 world_position = mul(position, transform);

    #if INSTANCED
    world_position = mul(world_position, input.instance_transform);
    world_position = wind::apply_to_vertex(instance_id, world_position, buffer_frame.time);
    #endif

    return mul(world_position, view_projection);
}
