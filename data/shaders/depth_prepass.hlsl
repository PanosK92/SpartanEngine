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

//= INCLUDES =========
#include "common.hlsl"
//====================

gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    gbuffer_vertex vertex;
    vertex.position_clip = float4(0.0f, 0.0f, 0.0f, 0.0f); // degenerate position
    
    // hi-z occlusion culling (skipped if HIZ_DEPTH_PASS is defined)
    #ifndef HIZ_DEPTH_PASS
        uint aabb_index = (uint)pass_get_f3_value().z;
        if (visibility[aabb_index] == 0)
            return vertex;
    #endif
    
    // to world space
    vertex = transform_to_world_space(input, instance_id, buffer_pass.transform);

    // to clip space
    const bool is_tesselated = pass_get_f3_value().x == 1.0f;
    if (!is_tesselated)
    {
        vertex = transform_to_clip_space(vertex);
    }

    return vertex;
}

void main_ps(gbuffer_vertex vertex)
{
    const bool has_albedo       = pass_get_f3_value().y == 1.0f;
    const float alpha_threshold = get_alpha_threshold(vertex.position); // distance based alpha threshold

    if (has_albedo && GET_TEXTURE(material_texture_index_albedo).Sample(samplers[sampler_anisotropic_wrap], vertex.uv).a <= alpha_threshold)
        discard;
}
