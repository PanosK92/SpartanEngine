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

//= INCLUDES =========
#include "common.hlsl"
//====================

// this function is shared between depth_prepass.hlsl, g_buffer.hlsl and depth_light.hlsl
// this is because the calculations have to be exactly the same and therefore produce identical depth values
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

// tessellation

#define MAX_POINTS 3

struct HsConstantDataOutput
{
    float edges[3] : SV_TessFactor;
    float inside   : SV_InsideTessFactor;
};

HsConstantDataOutput patch_constant_function(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch)
{
    HsConstantDataOutput output;

    output.edges[0] = 4.0f;
    output.edges[1] = 4.0f;
    output.edges[2] = 4.0f;
    output.inside   = 4.0f;

    return output;
}

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant_function")]
[outputcontrolpoints(MAX_POINTS)]
[maxtessfactor(15)]
gbuffer_vertex main_hs(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch, uint cp_id : SV_OutputControlPointID)
{
    return input_patch[cp_id];
}

[domain("tri")]
gbuffer_vertex main_ds(HsConstantDataOutput input, float3 uvw_coord : SV_DomainLocation, const OutputPatch<gbuffer_vertex, 3> patch)
{
    gbuffer_vertex output;

    output.position = uvw_coord.x * patch[0].position + uvw_coord.y * patch[1].position + uvw_coord.z * patch[2].position;
    output.uv       = uvw_coord.x * patch[0].uv + uvw_coord.y * patch[1].uv + uvw_coord.z * patch[2].uv;
    output.normal   = normalize(uvw_coord.x * patch[0].normal + uvw_coord.y * patch[1].normal + uvw_coord.z * patch[2].normal);
    output.tangent  = normalize(uvw_coord.x * patch[0].tangent + uvw_coord.y * patch[1].tangent + uvw_coord.z * patch[2].tangent);

    // apply displacement
    float displacement           = GET_TEXTURE(material_height).SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), output.uv, 0.0f).r;
    float displacement_strength  = GetMaterial().height * 10.0f;
    output.position             += output.normal * displacement * displacement_strength;

    // pass through unchanged attributes
    output.position_ss_current  = mul(float4(output.position, 1.0), buffer_frame.view_projection);
    output.position_ss_previous = mul(float4(output.position, 1.0), buffer_frame.view_projection_previous);
    output.position_clip        = output.position_ss_current;

    return output;
}
