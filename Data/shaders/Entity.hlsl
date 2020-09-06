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

//= INCLUDES =========
#include "Common.hlsl"
//====================

struct PixelInputType
{
    float4 position     : SV_POSITION;
    float2 uv           : TEXCOORD;
    float3 normal       : NORMAL;
    float3 positionWS   : POSITIONT_WS;
};

PixelInputType mainVS(Vertex_PosUvNorTan input)
{
    PixelInputType output;

    input.position.w    = 1.0f;
    output.positionWS   = mul(input.position, g_transform).xyz;
    output.position     = mul(float4(output.positionWS, 1.0f), g_view_projection_unjittered);
    output.normal       = mul(input.normal, (float3x3)g_transform);
    output.uv           = input.uv;

    return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
	float4 color = 0.0f;

#ifdef TRANSFORM
    float3 color_diffuse = g_transform_axis.xyz;
    float3 color_ambient = color_diffuse * 0.3f;
    float3 color_specular = 1.0f;
    float3 lightPos = float3(10.0f, 10.0f, 10.0f);
    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(lightPos - input.positionWS);
    float lambertian = max(dot(lightDir, normal), 0.0f);
    float specular = 0.0f;

    if (lambertian > 0.0f)
    {
        // Blinn phong
        float3 viewDir = normalize(g_camera_position - input.positionWS);
        float3 halfDir = normalize(lightDir + viewDir);
        float specAngle = max(dot(halfDir, normal), 0.0f);
        specular = pow(specAngle, 16.0f);
    }

    color = float4(color_ambient + lambertian * color_diffuse + color_specular * specular, 1.0f);
#endif

#ifdef OUTLINE
    float normal_threshold = 0.2f;

    float2 uv               = project(input.positionWS.xyz, g_view_projection_unjittered).xy;
    float scale             = 1.0f;
    float halfScaleFloor    = floor(scale * 0.5f);
    float halfScaleCeil     = ceil(scale * 0.5f);

    // Sample X pattern
    float3 normal0 = tex_normal.Sample(sampler_point_clamp, uv - g_texel_size * halfScaleFloor).rgb;                                             // bottom left
    float3 normal1 = tex_normal.Sample(sampler_point_clamp, uv + g_texel_size * halfScaleCeil).rgb;                                              // top right
    float3 normal2 = tex_normal.Sample(sampler_point_clamp, uv + float2(g_texel_size.x * halfScaleCeil, -g_texel_size.y * halfScaleFloor)).rgb;  // bottom right
    float3 normal3 = tex_normal.Sample(sampler_point_clamp, uv + float2(-g_texel_size.x * halfScaleFloor, g_texel_size.y * halfScaleCeil)).rgb;  // top left

    // Compute edge normal
    float3 normalFiniteDifference0 = normal1 - normal0;
    float3 normalFiniteDifference1 = normal3 - normal2;
    float edge_normal = sqrt(dot(normalFiniteDifference0, normalFiniteDifference0) + dot(normalFiniteDifference1, normalFiniteDifference1));

    // Compute view direction bias
    float3 view         = get_view_direction(uv);
    float3 normal       = tex_normal.Sample(sampler_point_clamp, uv).rgb;
    float view_dir_bias = dot(view, normal) * 0.5f + 0.5f;

    if (edge_normal * view_dir_bias < normal_threshold)
        discard;

    color = float4(0.6f, 0.6f, 1.0f, 1.0f);
#endif

	return color;
}
