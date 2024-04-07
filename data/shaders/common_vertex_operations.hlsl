/*
Copyright(c) 2016-2024 Panos Karabelas

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

// this function is shared between depth_prepass.hlsl, g_buffer.hlsl and depth_light.hlsl, this is because the calculations have to be exactly the same

float4 transform_to_world_space(Vertex_PosUvNorTan input, uint instance_id, matrix transform, float time)
{
    float3 position = mul(input.position, transform).xyz;

    Surface surface;
    surface.flags = GetMaterial().flags;
    
    #if INSTANCED // implies vegetation
    matrix instance = input.instance_transform;
    position = mul(float4(position, 1.0f), instance).xyz;
    if (surface.vertex_animate_wind()) // vegetation
    {
        float3 animation_pivot = float3(instance._31, instance._32, instance._33); // position
        position = vertex_processing::vegetation::apply_wind(instance_id, position, animation_pivot, time);
        position = vertex_processing::vegetation::apply_player_bend(position, animation_pivot);
    }
    #endif

    if (surface.vertex_animate_water())
    {
        position = vertex_processing::water::apply_wave(position, time);
        position = vertex_processing::water::apply_ripple(position, time);
    }

    return float4(position, 1.0f);
}
