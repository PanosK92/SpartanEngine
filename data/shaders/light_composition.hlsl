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

// must match restir_albedo_demodulator in restir_reservoir.hlsl, the half res restir shading
// divides the gi by this and the bilateral upsample plus the multiply below re-applies the
// full res value, recovering fine albedo detail that the upsample would otherwise blur away
float3 restir_albedo_demodulator(float3 albedo)
{
    return max(albedo, float3(0.04f, 0.04f, 0.04f));
}

// edge-aware bilateral upsample of the half-res restir gi texture (tex6)
// destination depth and normal come from the full-res g-buffer via Surface
// source depth and normal are read at gi texel centers from the same g-buffer
// weights combine bilinear, depth similarity, normal similarity to avoid edge bleed
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

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // create surface
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // skip non transparent pixels during the transparent composition pass, the opaque pass
    // already wrote final lighting + fog for opaque surfaces and the full sky texture for sky pixels
    // re running this for either would just stack a second fog term and the sky branch only fires
    // in the opaque pass which means sky pixels here would write light_atmospheric on its own and
    // overwrite the stars, moon and atmosphere that the opaque pass already composed
    if (pass_is_transparent() && !surface.is_transparent())
        return;

    // initialize
    float3 light_diffuse       = 0.0f;
    float3 light_specular      = 0.0f;
    float3 light_emissive      = 0.0f;
    float3 light_atmospheric   = 0.0f;
    float3 light_gi            = 0.0f;
    float alpha                = 0.0f;
    float distance_from_camera = 0.0f;

    // during the compute pass, fill in the sky pixels
    if (surface.is_sky() && pass_is_opaque())
    {
        // skysphere.hlsl mirrors below horizon directions to the upper hemisphere and dims
        // them to 0.3x for ibl correctness, sampling the texture at view_dir.y just below
        // zero therefore returns the dimmed sky and produces a hard dark band right where
        // the camera ray crosses the horizon (which is exactly where rays past the edge
        // of the floor land), clamping y to the upper hemisphere here keeps the visible
        // sky uniform across the horizon line so it can no longer show a dark strip
        float3 view_dir_sky  = surface.camera_to_pixel;
        view_dir_sky.y       = max(view_dir_sky.y, 0.0f);
        view_dir_sky         = normalize(view_dir_sky);
        light_emissive       = tex2.SampleLevel(samplers[sampler_bilinear_clamp], direction_sphere_uv(view_dir_sky), 0).rgb;
        alpha                = 0.0f;
        distance_from_camera = FLT_MAX_16;
    }
    // fill opaque/transparent pixels based on pass type
    else if ((pass_is_opaque() && surface.is_opaque()) || (pass_is_transparent() && surface.is_transparent()))
    {
        light_diffuse        = tex3.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        light_specular       = tex4.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        // hdr boost so emission crosses the bloom threshold, gbuffer stores emission in [0,1]
        // emissive from albedo gets a much stronger boost so plain colored materials with no
        // emissive texture still glow visibly at default bloom intensity
        bool        is_emissive_from_albedo = (surface.flags & uint(1U << 15)) != 0;
        const float emission_strength       = is_emissive_from_albedo ? 250.0f : 25.0f;
        light_emissive       = surface.emissive * surface.albedo * emission_strength;
        alpha                = surface.alpha;
        distance_from_camera = surface.camera_to_pixel_length;

        // restir_pt outputs gi already demodulated by the half res primary albedo so the
        // bilateral upsample averages a smoother lighting only signal and we re-apply the
        // full res albedo here, this preserves fine material detail that would otherwise be
        // lost when the half res restir shading + upsample blurs the albedo into the gi
        // gi is at restir_pt_scale of render resolution, so use a join-bilateral
        // upsample (depth + normal aware) to avoid bleeding across edges
        // also multiply by surface.occlusion to recover contact shadows that
        // restir's spatial reuse and denoiser smear away at small scales
        if (is_restir_pt_enabled())
        {
            float depth_dst_lin = linearize_depth(surface.depth);
            light_gi  = sample_gi_bilateral(surface.uv, depth_dst_lin, surface.normal);
            light_gi *= restir_albedo_demodulator(surface.albedo);
            light_gi *= surface.occlusion;
        }
    }
    
    // fog
    {
        uint sky_mip = 4; // it just looks good
    
        // compute view direction
        float3 camera_position = get_camera_position();
        float3 view_dir        = normalize(surface.position - camera_position);
    
        // sample sky in view direction, clamped to the upper hemisphere with a soft fade across
        // the horizon, the skysphere mirrors the lower hemisphere at 0.3x luminance for ibl which
        // produces a hard black band at the horizon when distant ground pixels (view_dir.y just
        // below zero) sample the dimmed half while sky pixels just above sample the bright half
        // atmospheric haze in real life is lit by the sky above regardless of which way the ray
        // points, so we lift y toward the upper hemisphere for the fog tint
        float3 view_dir_sky = float3(view_dir.x, max(view_dir.y, 0.0f), view_dir.z);
        view_dir_sky        = normalize(view_dir_sky);
        float2 view_uv        = direction_sphere_uv(view_dir_sky);
        float3 sky_color_view = tex2.SampleLevel(samplers[sampler_trilinear_clamp], view_uv, sky_mip).rgb;
    
        // sample sky in the light direction
        Light light;
        light.Build(0, surface); // light 0 is always directional
        float3 light_dir       = normalize(-light.forward);
        float2 light_uv        = direction_sphere_uv(light_dir);
        float3 sky_color_light = tex2.SampleLevel(samplers[sampler_trilinear_clamp], light_uv, sky_mip).rgb;
    
        // henyey-greenstein phase function for forward scattering
        float g = 0.8f; // forward scattering strength
        float cos_theta = dot(view_dir, light_dir);
        float phase     = (1.0f - g * g) / (4.0f * PI * pow(1.0f + g * g - 2.0f * g * cos_theta, 1.5f));
    
        // blend view and light contributions
        float light_weight = 0.4f; // light direction contribution weight
        float3 sky_color   = lerp(sky_color_view, sky_color_light, light_weight * phase);
    
        float fog_atmospheric = get_fog_atmospheric(distance_from_camera, surface.position.y);
        float3 fog_volumetric = tex5.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        
        if (surface.is_sky())
        {
            // sky already integrates atmospheric scattering inside skysphere.hlsl, the
            // previous code added fog_atmospheric * sky_color on top which roughly
            // doubled the sky brightness and amplified the discontinuity against any
            // unlit ground edge below, only volumetric beams (god rays) should be
            // additive on the sky
            light_atmospheric = fog_volumetric;
        }
        else
        {
            // extinction based fog blending, surface lighting fades along the optical
            // path while sky inscatter fills in the missing energy, distance based so
            // close geometry stays crisp and only far pixels haze toward the sky
            float fog_factor    = fog_atmospheric;
            float transmittance = 1.0f - fog_factor;
            light_diffuse  *= transmittance;
            light_specular *= transmittance;
            light_emissive *= transmittance;
            light_gi       *= transmittance;
            light_atmospheric = fog_factor * sky_color + fog_volumetric;
        }
    }

    // transparent surfaces sample the background via the refraction pass, no need to
    // blend with the existing opaque content here, opaque pixels in this pass were
    // already short circuited at the top, so each pixel reaches this point exactly
    // once per frame and a straight write is safe
    tex_uav[thread_id.xy] = validate_output(float4(light_diffuse * surface.albedo + light_specular + light_emissive + light_atmospheric + light_gi, alpha));
}
