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

//= INCLUDES =========
#include "common.hlsl"
#include "ocean/synthesise_maps.hlsl"
//====================

gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    gbuffer_vertex vertex;

    MaterialParameters material = GetMaterial();
    Surface surface;
    surface.flags = material.flags;
    
    if (surface.is_ocean())
    {
        const float3 pass_values = pass_get_f3_value2();
        const float2 tile_xz_pos = pass_values.xy;
        const float tile_size = pass_values.z;
        const float2 tile_local_uv = ocean_get_world_space_uvs(input.uv, tile_xz_pos, tile_size);
        
        float4 displacement = float4(0.0f, 0.0f, 0.0f, 0.0f);
        //synthesize(tex2, displacement, world_space_tile_uv);
        synthesize_with_flow(tex2, displacement, tex5, tile_xz_pos, material.ocean_parameters.windDirection, tile_local_uv);
        
        input.position.xyz += displacement * material.ocean_parameters.displacementScale;
    }
    
    // transform to world space
    float3 position_world          = 0.0f;
    float3 position_world_previous = 0.0f;
    vertex                         = transform_to_world_space(input, instance_id, buffer_pass.transform, position_world, position_world_previous);

    // transform to clip space
    const bool is_tesselated = pass_get_f3_value().x == 1.0f;
    if (!is_tesselated)
    {
        vertex = transform_to_clip_space(vertex, position_world, position_world_previous);
    }

    return vertex;
}

void main_ps(gbuffer_vertex vertex)
{
    // distance based alpha threshold
    const bool has_albedo       = pass_get_f3_value().y == 1.0f;
    const float2 screen_uv      = vertex.position.xy / buffer_frame.resolution_render;
    const float3 position_world = get_position(vertex.position.z, screen_uv);
    const float alpha_threshold = get_alpha_threshold(position_world);

    if (has_albedo && GET_TEXTURE(material_texture_index_albedo).Sample(samplers[sampler_anisotropic_wrap], vertex.uv_misc.xy).a <= alpha_threshold)
        discard;
}
