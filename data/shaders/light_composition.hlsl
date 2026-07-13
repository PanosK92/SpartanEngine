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

// = INCLUDES ========
#include "common.hlsl"
#include "fog.hlsl"
//====================

// edge aware bilateral upsample of the half res restir gi texture, weights combine
// bilinear, depth and normal similarity to avoid edge bleed
float3 sample_gi_bilateral(float2 uv_dst, float depth_dst_lin, float3 normal_dst)
{
    float2 gi_size; tex6.GetDimensions(gi_size.x, gi_size.y);
    float2 gi_inv = 1.0f / gi_size;

    float2 ctr  = uv_dst * gi_size - 0.5f;
    float2 base = floor(ctr);
    float2 frac_uv = ctr - base;

    int2 corners[4];
    corners[0] = int2(base) + int2(0, 0);
    corners[1] = int2(base) + int2(1, 0);
    corners[2] = int2(base) + int2(0, 1);
    corners[3] = int2(base) + int2(1, 1);

    float w_bilin[4];
    w_bilin[0] = (1.0f - frac_uv.x) * (1.0f - frac_uv.y);
    w_bilin[1] = frac_uv.x          * (1.0f - frac_uv.y);
    w_bilin[2] = (1.0f - frac_uv.x) * frac_uv.y;
    w_bilin[3] = frac_uv.x          * frac_uv.y;

    float3 sum  = 0.0f;
    float  norm = 0.0f;

    int2 gi_max = int2(gi_size) - 1;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        int2 c = clamp(corners[i], int2(0, 0), gi_max);
        float2 src_uv = (float2(c) + 0.5f) * gi_inv;

        float d_raw = tex_depth.SampleLevel(samplers[sampler_point_clamp], src_uv, 0).r;
        if (d_raw <= 0.0f) continue;

        float d_lin = linearize_depth(d_raw);
        float3 n_src = get_normal(src_uv);

        // bilateral upsample weights, 64 / 16 is the schied 2017 starting point, ~4 deg and ~14 deg
        float depth_diff = abs(d_lin - depth_dst_lin) / max(depth_dst_lin, 1e-3f);
        float w_depth    = exp(-depth_diff * 64.0f);

        float ndot     = saturate(dot(normal_dst, n_src));
        float w_normal = pow(ndot, 16.0f);

        float w = w_bilin[i] * w_depth * w_normal;

        float3 src = tex6.Load(int3(c, 0)).rgb;
        sum  += src * w;
        norm += w;
    }

    if (norm > 1e-5f)
        return sum / norm;

    // all neighbors disagree on geometry, fall back to nearest texel to avoid halos
    return tex6.SampleLevel(samplers[sampler_point_clamp], uv_dst, 0).rgb;
}

// catmull rom filtered sky panorama fetch, sky pixels magnify the 4k panorama ~2.5x at a
// typical fov so plain bilinear smears cloud edges, the 5 tap cubic keeps them crisp, the
// neighborhood clamp suppresses ringing from the negative lobes around hdr spikes like the sun
float3 sample_sky_catmull_rom(float2 uv)
{
    float2 resolution;
    tex2.GetDimensions(resolution.x, resolution.y);

    float2 sample_position = uv * resolution;
    float2 tex_pos_1       = floor(sample_position - 0.5f) + 0.5f;
    float2 f               = sample_position - tex_pos_1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12       = w1 + w2;
    float2 offset_12 = w2 / w12;

    float2 inv_res    = 1.0f / resolution;
    float2 tex_pos_0  = (tex_pos_1 - 1.0f) * inv_res;
    float2 tex_pos_3  = (tex_pos_1 + 2.0f) * inv_res;
    float2 tex_pos_12 = (tex_pos_1 + offset_12) * inv_res;

    float3 s0 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_0.y),  0.0f).rgb;
    float3 s1 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_12.y), 0.0f).rgb;
    float3 s2 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_12.y), 0.0f).rgb;
    float3 s3 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_12.y), 0.0f).rgb;
    float3 s4 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_3.y),  0.0f).rgb;

    float3 result = 0.0f.xxx;
    result += s0 * w12.x * w0.y;
    result += s1 * w0.x  * w12.y;
    result += s2 * w12.x * w12.y;
    result += s3 * w3.x  * w12.y;
    result += s4 * w12.x * w3.y;

    float  weight_sum = w12.x + w12.y - w12.x * w12.y;
    float3 sky_min    = min(min(min(s0, s1), min(s2, s3)), s4);
    float3 sky_max    = max(max(max(s0, s1), max(s2, s3)), s4);
    return clamp(result * rcp(weight_sum), sky_min, sky_max);
}

// 3x3 gaussian blur on the volumetric fog buffer to kill the per pixel raymarch jitter
float3 sample_volumetric_smooth(float2 uv)
{
    float2 vol_size;
    tex5.GetDimensions(vol_size.x, vol_size.y);
    float2 texel = 1.0f / vol_size;

    const float w_center = 4.0f / 16.0f;
    const float w_edge   = 2.0f / 16.0f;
    const float w_corner = 1.0f / 16.0f;

    float3 result = 0.0f;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv,                                       0).rgb * w_center;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv + float2( texel.x,        0.0f),       0).rgb * w_edge;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv + float2(-texel.x,        0.0f),       0).rgb * w_edge;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv + float2( 0.0f,           texel.y),    0).rgb * w_edge;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv + float2( 0.0f,          -texel.y),    0).rgb * w_edge;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv + float2( texel.x,        texel.y),    0).rgb * w_corner;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv + float2(-texel.x,        texel.y),    0).rgb * w_corner;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv + float2( texel.x,       -texel.y),    0).rgb * w_corner;
    result += tex5.SampleLevel(samplers[sampler_point_clamp], uv + float2(-texel.x,       -texel.y),    0).rgb * w_corner;
    return result;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // skip non transparent pixels during the transparent pass, the opaque pass already wrote them
    if (pass_is_transparent() && !surface.is_transparent())
        return;

    float3 light_diffuse       = 0.0f;
    float3 light_specular      = 0.0f;
    float3 light_emissive      = 0.0f;
    float3 light_atmospheric   = 0.0f;
    float3 light_gi            = 0.0f;
    float alpha                = 0.0f;
    float distance_from_camera = 0.0f;

    // fill in the sky pixels
    if (surface.is_sky() && pass_is_opaque())
    {
        // clamp y to the upper hemisphere so the dimmed below horizon sky does not show a dark band
        float3 view_dir_sky  = surface.camera_to_pixel;
        view_dir_sky.y       = max(view_dir_sky.y, 0.0f);
        view_dir_sky         = normalize(view_dir_sky);
        light_emissive       = sample_sky_catmull_rom(direction_sphere_uv(view_dir_sky));
        alpha                = 0.0f;
        distance_from_camera = FLT_MAX_16;
    }
    // fill opaque and transparent pixels based on pass type
    else if ((pass_is_opaque() && surface.is_opaque()) || (pass_is_transparent() && surface.is_transparent()))
    {
        light_diffuse        = tex3.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        light_specular       = tex4.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        // emissive is authored as a 0-1 scalar, calibrate it in physical luminance, 1.0 maps to
        // the nits below and converts to the radiometric scene units like every other light source
        bool        is_emissive_from_albedo = (surface.flags & uint(1U << 15)) != 0;
        const float emissive_nits           = is_emissive_from_albedo ? 100000.0f : 10000.0f;
        light_emissive       = surface.emissive * surface.albedo * photometric_to_radiometric(emissive_nits);
        alpha                = surface.alpha;
        distance_from_camera = surface.camera_to_pixel_length;

        // restir_pt outputs diffuse only albedo demodulated gi, re-apply the full res albedo here
        // ssao is not applied, the path tracer already accounts for indirect visibility
        if (is_restir_pt_enabled())
        {
            float depth_dst_lin = linearize_depth(surface.depth);
            light_gi            = sample_gi_bilateral(surface.uv, depth_dst_lin, surface.normal);
            // debug view stores raw radiance, skip re-modulation
            if (!is_restir_pt_debug())
                light_gi *= max(surface.albedo, 0.1f);
        }

        // water and glass are shaded from reflection and transmission inside the refraction
        // composite, a lambert albedo layer on top double counts and washes the fresnel structure
        // to a flat albedo sheet, the analytic specular in light_specular is real and stays
        if (surface.is_water() || surface.is_transparent())
        {
            light_diffuse = 0.0f;
            light_gi      = 0.0f;
        }

        // submerged geometry sits inside a glowing scattering medium but the refraction source is
        // copied before ibl runs, so it only ever receives direct sun and faces away from it read
        // pitch black through the surface, fill them with the water body radiance they are immersed
        // in, same optics as the refraction composite so object and column agree, fading with depth
        if (pass_is_opaque() && buffer_frame.ocean_enabled > 0.5f)
        {
            // actual displaced wave height above this point, not the flat sea level plane, so the
            // waterline on geometry follows the swell, the horizontal choppiness shift is ignored
            // which is fine for a soft ambient band
            float water_height = buffer_frame.ocean_sea_level;
            [loop] for (uint c = 0; c < buffer_frame.ocean_cascade_count; ++c)
            {
                float2 cascade_uv = surface.position.xz / buffer_frame.ocean_cascade_length[c];
                water_height     += tex_ocean_displacement.SampleLevel(samplers[sampler_bilinear_wrap], float3(cascade_uv, (float)c), 0.0f).y;
            }

            float depth_below = water_height - surface.position.y;
            if (depth_below > 0.0f)
            {
                float3 sky_down    = tex2.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(float3(0.0f, 1.0f, 0.0f)), 7).rgb;
                float3 downwelling = get_sun_radiance() * saturate(-light_parameters[0].direction.y) * (1.0f / PI) + sky_down;
                // ease in over the first 20cm so the waterline is a soft lap line instead of a hard cut
                float lap_band     = saturate(depth_below / 0.2f);
                light_diffuse     += ocean_scatter_albedo * downwelling * exp(-ocean_extinction * depth_below) * lap_band;

                // sun caustics, the wave focused beams land where the slant sun path meets the surface,
                // extinction along that path kills them with depth and distance fades the sub texel sparkle
                float3 to_sun = -light_parameters[0].direction;
                if (buffer_frame.ocean_caustics_intensity > 0.0f && to_sun.y > 0.01f)
                {
                    float  sun_path      = depth_below / to_sun.y;
                    float2 entry_xz      = surface.position.xz + to_sun.xz * sun_path;
                    float  n_dot_l       = saturate(dot(surface.normal, to_sun));
                    float  distance_fade = 1.0f - saturate(surface.camera_to_pixel_length / 200.0f);
                    float3 sun_incident  = get_sun_radiance() * n_dot_l * (1.0f / PI) * exp(-ocean_extinction * sun_path);
                    light_diffuse       += sun_incident * get_ocean_caustic(entry_xz, sun_path) * buffer_frame.ocean_caustics_intensity * distance_fade * lap_band;
                }
            }
        }
    }
    
    // fog
    // the haze color is built from two fixed world direction sky samples (sun and zenith) at a
    // coarse mip so no per pixel panorama detail imprints onto geometry, the per pixel variation
    // is a smooth cosine lobe of view to sun
    {
        float3 camera_position = get_camera_position();
        float3 view_dir        = normalize(surface.position - camera_position);

        Light light;
        light.Build(0, surface);
        float3 light_dir = normalize(-light.forward);

        // coarse mip, 32 x 16 texels, wider than any cloud or sun disc halo
        const uint sky_mip = 7;

        // sky color in the sun direction, lifted onto the horizon when the sun is below it
        float3 sun_sample_dir   = normalize(float3(light_dir.x, max(light_dir.y, 0.0f), light_dir.z));
        float2 sun_uv           = direction_sphere_uv(sun_sample_dir);
        float3 sky_color_sun    = tex2.SampleLevel(samplers[sampler_trilinear_clamp], sun_uv, sky_mip).rgb;

        // sky color at zenith for the cool side of the gradient
        float2 zenith_uv        = direction_sphere_uv(float3(0.0f, 1.0f, 0.0f));
        float3 sky_color_zenith = tex2.SampleLevel(samplers[sampler_trilinear_clamp], zenith_uv, sky_mip).rgb;

        // clamp both references so residual sun disc bleed does not punch the haze too bright
        const float sky_color_max = 50.0f;
        sky_color_sun    = min(sky_color_sun,    sky_color_max);
        sky_color_zenith = min(sky_color_zenith, sky_color_max);

        // smooth per pixel blend, the endpoints are screen wide constants
        float  cos_theta = saturate(dot(view_dir, light_dir));
        float  sun_lobe  = pow(cos_theta, 4.0f);
        float3 sky_color = lerp(sky_color_zenith, sky_color_sun, sun_lobe);

        float fog_atmospheric = get_fog_atmospheric(distance_from_camera, surface.position.y);
        float3 fog_volumetric = sample_volumetric_smooth(surface.uv);

        if (surface.is_sky())
        {
            // sky already integrates atmospheric scattering, only god rays are additive
            light_atmospheric = fog_volumetric;
        }
        else
        {
            // extinction based fog, surface lighting fades and sky inscatter fills the missing energy
            float fog_factor    = fog_atmospheric;
            float transmittance = 1.0f - fog_factor;
            light_diffuse  *= transmittance;
            light_specular *= transmittance;
            light_emissive *= transmittance;
            light_gi       *= transmittance;
            light_atmospheric = fog_factor * sky_color + fog_volumetric;
        }
    }

    // each pixel reaches this point once per frame so a straight write is safe
    tex_uav[thread_id.xy] = validate_output(float4(light_diffuse * surface.albedo + light_specular + light_emissive + light_atmospheric + light_gi, alpha));
}
