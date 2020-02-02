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

static const uint g_sscs_steps              = 32;
static const float g_sscs_rejection_depth   = 0.02f;
static const float g_sscs_ray_max_distance  = 0.1f;
static const float g_sscs_bias              = 0.005f;

//= INLUCES =============
#include "Dithering.hlsl"
//=======================

float ScreenSpaceShadows(Texture2D tex_depth, float2 uv, float3 light_dir)
{
    // Origin view space position
    float3 temp  		= get_position_from_depth(tex_depth, uv);
    float3 origin_pos	= mul(float4(temp, 1.0f), g_view).xyz;

	// Compute vector that points to the light
	float3 light_dir_view = mul(float4(-light_dir, 0.0f), g_view).xyz;

	// Compute ray
	float3 ray_pos 		= origin_pos;
	float3 ray_dir 		= light_dir_view;
	float step_length	= g_sscs_ray_max_distance / (float)g_sscs_steps;
	float3 ray_step		= ray_dir * step_length;

	// Apply dithering
	ray_pos += ray_dir * dither_temporal_fallback(uv, 0.0f, 1.0f);

    // Ray march towards the light
    for (uint i = 0; i < g_sscs_steps; i++)
    {
        // Step ray
        ray_pos         += ray_step;
		float2 ray_uv   = project_uv(ray_pos, g_projection);

		if (!is_saturated(ray_uv))
			break;

		// Compare depth
		float depth_sampled = get_linear_depth(tex_depth, ray_uv);
		float depth_delta   = ray_pos.z - depth_sampled - g_sscs_bias;

        // Occlusion test
        if (depth_delta > 0.0f && depth_delta < g_sscs_rejection_depth)
			return 0;
    }

    return 1;
}