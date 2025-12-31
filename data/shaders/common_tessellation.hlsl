/*
Copyright(c) 2015-2026 Panos Karabelas

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

#define MAX_POINTS 3
#define TESS_FACTOR 64
#define TESS_DISTANCE 32.0f
#define TESS_DISTANCE_SQUARED (TESS_DISTANCE * TESS_DISTANCE)

// hull shader constant data
struct HsConstantDataOutput
{
    float edges[3] : SV_TessFactor;     // edge tessellation factors
    float inside : SV_InsideTessFactor; // inside tessellation factor
};

// hull shader (control point phase) - pass-through
[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant_function")]
[outputcontrolpoints(MAX_POINTS)]
[maxtessfactor(TESS_FACTOR)]
gbuffer_vertex main_hs(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch, uint cp_id : SV_OutputControlPointID)
{
    return input_patch[cp_id]; // pass through unchanged
}

// hull shader (patch constant function) - dynamic tessellation
HsConstantDataOutput patch_constant_function(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch, uint patch_id : SV_PrimitiveID)
{
    HsConstantDataOutput output;
   
    // calculate distance from camera to triangle center
    float3 avg_pos = 0.0f;
    for (int i = 0; i < 3; i++)
    {
        float clip_w      = input_patch[i].position.w;
        float2 ndc        = input_patch[i].position.xy / clip_w;
        float depth       = input_patch[i].position.z / clip_w;
        float2 screen_uv  = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        avg_pos          += get_position(depth, screen_uv);
    }
    avg_pos                /= 3.0f;
    float3 to_camera        = avg_pos - buffer_frame.camera_position;
    float distance_squared  = dot(to_camera, to_camera);
    float tess_factor       = (distance_squared <= TESS_DISTANCE_SQUARED) ? TESS_FACTOR : 1.0f;
    
    // set tessellation factors
    output.edges[0] = tess_factor;
    output.edges[1] = tess_factor;
    output.edges[2] = tess_factor;
    output.inside   = tess_factor;
    return output;
}

// domain shader

float4 barycentric(float4 a, float4 b, float4 c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

float3 barycentric(float3 a, float3 b, float3 c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

float2 barycentric(float2 a, float2 b, float2 c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

float barycentric(float a, float b, float c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

[domain("tri")]
gbuffer_vertex main_ds(HsConstantDataOutput input, float3 bary_coords : SV_DomainLocation, const OutputPatch<gbuffer_vertex, 3> patch)
{
    gbuffer_vertex vertex;

    // interpolate attributes (clip space)
    vertex.position          = barycentric(patch[0].position, patch[1].position, patch[2].position, bary_coords);
    vertex.position_previous = barycentric(patch[0].position_previous, patch[1].position_previous, patch[2].position_previous, bary_coords);
    vertex.normal            = normalize(barycentric(patch[0].normal, patch[1].normal, patch[2].normal, bary_coords));
    vertex.tangent           = normalize(barycentric(patch[0].tangent, patch[1].tangent, patch[2].tangent, bary_coords));
    vertex.uv_misc.xy        = barycentric(patch[0].uv_misc.xy, patch[1].uv_misc.xy, patch[2].uv_misc.xy, bary_coords);
    vertex.uv_misc.z         = barycentric(patch[0].uv_misc.z, patch[1].uv_misc.z, patch[2].uv_misc.z, bary_coords);
    vertex.uv_misc.w         = patch[0].uv_misc.w; // instance_id is constant per patch

    // reconstruct world positions from interpolated clip
    float clip_w             = vertex.position.w;
    float clip_previous_w    = vertex.position_previous.w;
    float3 position          = get_position(vertex.position.z / clip_w, vertex.position.xy / clip_w);
    float3 position_previous = get_position(vertex.position_previous.z / clip_previous_w, vertex.position_previous.xy / clip_previous_w);

    // fade based on real distance
    float distance_from_cam = fast_length(position - buffer_frame.camera_position);
    float fade_factor       = saturate((TESS_DISTANCE - distance_from_cam) / 4.0f);

    MaterialParameters material = GetMaterial();
    Surface surface; surface.flags = material.flags;
    bool tessellated =
        input.edges[0] > 1.0f ||
        input.edges[1] > 1.0f ||
        input.edges[2] > 1.0f ||
        input.inside   > 1.0f;

    if (tessellated && fade_factor > 0.0f)
    {
        // height displacement
        if (surface.has_texture_height())
        {
            float height       = GET_TEXTURE(material_texture_index_packed).SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), vertex.uv_misc.xy, 0.0f).a * 0.04f;
            float3 disp        = vertex.normal * height * material.height * fade_factor;
            position          += disp;
            position_previous += disp;
        }

        // terrain noise
        if (surface.is_terrain())
        {
            float height       = noise_perlin(position.xz * 8.0f) * 0.1f;
            float3 disp        = vertex.normal * height * fade_factor;
            position          += disp;
            position_previous += disp;
        }
    }

    // write final clip space
    vertex.position          = mul(float4(position, 1.0f), buffer_frame.view_projection);
    vertex.position_previous = mul(float4(position_previous, 1.0f), buffer_frame.view_projection_previous);

    return vertex;
}
