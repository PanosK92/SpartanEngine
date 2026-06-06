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
        light_emissive       = tex2.SampleLevel(samplers[sampler_bilinear_clamp], direction_sphere_uv(view_dir_sky), 0).rgb;
        alpha                = 0.0f;
        distance_from_camera = FLT_MAX_16;
    }
    // fill opaque and transparent pixels based on pass type
    else if ((pass_is_opaque() && surface.is_opaque()) || (pass_is_transparent() && surface.is_transparent()))
    {
        light_diffuse        = tex3.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        light_specular       = tex4.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        // hdr boost so emission crosses the bloom threshold, albedo emission needs a stronger boost
        bool        is_emissive_from_albedo = (surface.flags & uint(1U << 15)) != 0;
        const float emission_strength       = is_emissive_from_albedo ? 250.0f : 25.0f;
        light_emissive       = surface.emissive * surface.albedo * emission_strength;
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
