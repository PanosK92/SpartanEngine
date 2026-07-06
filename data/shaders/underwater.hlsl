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

// half thickness of the meniscus seam as a fraction of the near plane distance
static const float meniscus_scale   = 0.02f;
// vertical spacing of the seam blur taps in texels
static const float seam_blur_texels = 2.0f;

// wave height above a world xz, same cascade sum the refraction composite and buoyancy use
float get_ocean_height(float2 world_xz)
{
    float height = buffer_frame.ocean_sea_level;
    [loop] for (uint c = 0; c < buffer_frame.ocean_cascade_count; ++c)
    {
        float2 cascade_uv = world_xz / buffer_frame.ocean_cascade_length[c];
        height           += tex_ocean_displacement.SampleLevel(samplers[sampler_bilinear_wrap], float3(cascade_uv, (float)c), 0.0f).y;
    }
    return height;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    if (any(thread_id.xy >= uint2(resolution)))
    {
        return;
    }

    float2 uv    = (thread_id.xy + 0.5f) / resolution;
    float4 color = tex[thread_id.xy];

    // signed height of this lens sample above the waves, evaluated on the near plane so the waterline crosses the screen along the actual swell instead of a flat row of pixels
    float3 position_near = get_position(1.0f, uv); // reversed z, ndc z of one is the near plane
    float  height_above  = position_near.y - get_ocean_height(position_near.xz);
    float  meniscus_half = buffer_frame.camera_near * meniscus_scale;

    if (height_above >= meniscus_half)
    {
        tex_uav[thread_id.xy] = color;
        return;
    }

    // the water body is lit by the downwelling sun and sky, attenuated by the water above the camera so the scene darkens with depth, same optics as the refraction composite
    float3 camera_position = get_camera_position();
    float  camera_depth    = max(get_ocean_height(camera_position.xz) - camera_position.y, 0.0f);
    float3 sky_down        = tex2.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(float3(0.0f, 1.0f, 0.0f)), 7).rgb;
    float3 downwelling     = get_sun_radiance() * saturate(-light_parameters[0].direction.y) * (1.0f / PI) + sky_down;
    float3 body_radiance   = ocean_scatter_albedo * downwelling * exp(-ocean_extinction * camera_depth);

    // beer lambert along the view ray, the depth buffer contains the water surface so looking up the column ends there, sky and distant pixels converge to the body color
    float  depth         = get_depth(uv * get_render_uv_scale());
    float  path_length   = length(get_position(depth, uv) - camera_position);
    float3 transmittance = exp(-ocean_extinction * path_length);
    float3 underwater    = color.rgb * transmittance + body_radiance * (1.0f - transmittance);
    color.rgb            = lerp(color.rgb, underwater, saturate(-height_above / meniscus_half));

    // the meniscus itself, a thick blurred darkened seam of water where the surface crosses the lens
    float band = 1.0f - saturate(abs(height_above) / meniscus_half);
    if (band > 0.0f)
    {
        float3 blurred = 0.0f;
        [unroll] for (int i = -3; i <= 3; i++)
        {
            blurred += tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(0.0f, (float)i * seam_blur_texels / resolution.y), 0.0f).rgb;
        }
        blurred          /= 7.0f;
        float3 seam_tint  = float3(0.3f, 0.3f, 0.3f) + ocean_scatter_albedo * 2.0f;
        color.rgb         = lerp(color.rgb, blurred * seam_tint, band);
    }

    tex_uav[thread_id.xy] = validate_output(color);
}
