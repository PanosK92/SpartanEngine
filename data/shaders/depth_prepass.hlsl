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

#define TRANSFORM_IGNORE_NORMALS
#define TRANSFORM_IGNORE_PREVIOUS_POSITION

//= INCLUDES =========
#include "common.hlsl"
//====================

gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    gbuffer_vertex vertex = transform_to_world_space(input, instance_id, buffer_pass.transform);

    Surface surface;
    surface.flags = GetMaterial().flags;
    if (!surface.is_tessellated())
    {
        vertex = transform_to_clip_space(vertex);
    }

    return vertex;
}

void main_ps(gbuffer_vertex vertex)
{
    const float3 f3_value     = pass_get_f3_value();
    const bool has_alpha_mask = f3_value.x == 1.0f;
    const bool has_albedo     = f3_value.y == 1.0f;
    const float alpha         = f3_value.z;

    float alpha_threshold = get_alpha_threshold(vertex.position);
    bool mask_alpha       = has_alpha_mask && GET_TEXTURE(material_mask).Sample(samplers[sampler_point_wrap], vertex.uv).r <= alpha_threshold;
    bool mask_albedo      = alpha == 1.0f && has_albedo && GET_TEXTURE(material_albedo).Sample(samplers[sampler_anisotropic_wrap], vertex.uv).a <= alpha_threshold;

    if (mask_alpha || mask_albedo)
        discard;
}
