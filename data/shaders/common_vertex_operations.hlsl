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

float4 apply_wind_to_vertex(uint instance_id, float4 world_position, float time)
{
    const float3 wind_direction = float3(1, 0, 0);
    const float sway_extent      = 0.0001f; // oscillation amplitude
    const float sway_speed       = 2.0f;    // oscillation frequency
    const float sway_more_on_top = 2.0f;    // sway at the top more
  
    float wave_factor   = sin((time * sway_speed) + world_position.x + float(instance_id) * 0.1f); // offset by instance_id
    float height_factor = pow(world_position.y, sway_more_on_top);

    float3 offset       = wind_direction * wave_factor * height_factor * sway_extent;
    world_position.xyz += offset;
    
    return world_position;
}

float4 compute_screen_space_position(Vertex_PosUvNorTan input, uint instance_id, matrix transform, matrix view_projection)
{
    float4 position = input.position;
    position.w      = 1.0f;
    
    float4 world_position = mul(position, transform);

    #if INSTANCED
    world_position = mul(world_position, input.instance_transform);
    world_position = apply_wind_to_vertex(instance_id, world_position, buffer_frame.time);
    #endif

    return mul(world_position, view_projection);
}
