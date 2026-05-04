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

//= INCLUDES =================
#include "common.hlsl"
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "fog.hlsl"
//============================

// Cloud shadow map sampling
// tex3 is bound as the cloud shadow map in Pass_Light
float sample_cloud_shadow(float3 world_pos)
{
    // skip if cloud shadows are disabled
    if (buffer_frame.cloud_shadows <= 0.0 || buffer_frame.cloud_coverage <= 0.0)
        return 1.0;
    
    // cloud shadow map covers 10km x 10km area
    float shadow_map_size = 10000.0;
    
    // get shadow map dimensions for texel size calculation
    float2 shadow_dims;
    tex3.GetDimensions(shadow_dims.x, shadow_dims.y);
    float texel_size = shadow_map_size / shadow_dims.x;
    
    // snap center to texel grid (must match cloud_shadow.hlsl)
    float2 snapped_center = floor(buffer_frame.camera_position.xz / texel_size) * texel_size;
    
    float2 relative_pos = world_pos.xz - snapped_center;
    float2 uv = relative_pos / shadow_map_size + 0.5;
    
    // out of bounds check
    if (any(uv < 0.0) || any(uv > 1.0))
        return 1.0;
    
    // sample cloud shadow (tex3 is the cloud shadow map)
    float shadow = tex3.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, 0).r;
    
    return shadow;
}

// ray traced shadow sampling
// tex4 is bound as the ray traced shadow texture in Pass_Light
float sample_ray_traced_shadow(float2 uv)
{
    if (!is_ray_traced_shadows_enabled())
        return 1.0;
    
    // sample ray traced shadow (tex4 is the ray traced shadow texture)
    float shadow = tex4.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, 0).r;
    
    return shadow;
}

// inline ray traced shadow for any light type
// uses a stationary halton(2,3) sample set rotated by a per pixel hash
// the pattern never changes per frame so the residual noise does not crawl
// requires the tlas descriptor to be bound by the caller pass
#ifdef RAY_TRACING_ENABLED
static const uint k_inline_shadow_spp = 16;

// halton (2,3) low discrepancy sequence, 16 points
static const float2 k_halton_2_3[16] =
{
    float2(0.500000f, 0.333333f),
    float2(0.250000f, 0.666667f),
    float2(0.750000f, 0.111111f),
    float2(0.125000f, 0.444444f),
    float2(0.625000f, 0.777778f),
    float2(0.375000f, 0.222222f),
    float2(0.875000f, 0.555556f),
    float2(0.062500f, 0.888889f),
    float2(0.562500f, 0.037037f),
    float2(0.312500f, 0.370370f),
    float2(0.812500f, 0.703704f),
    float2(0.187500f, 0.148148f),
    float2(0.687500f, 0.481481f),
    float2(0.437500f, 0.814815f),
    float2(0.937500f, 0.259259f),
    float2(0.031250f, 0.592593f)
};

// stationary per pixel hash (does not change per frame), used to rotate the halton disk
float spatial_hash_unit(float2 pixel_xy)
{
    return frac(52.9829189f * frac(pixel_xy.x * 0.06711056f + pixel_xy.y * 0.00583715f));
}

// concentric mapping from [-1,1]^2 square to unit disk
float2 concentric_disk(float2 u)
{
    if (u.x == 0.0f && u.y == 0.0f)
        return float2(0.0f, 0.0f);

    float r;
    float theta;
    if (abs(u.x) > abs(u.y))
    {
        r     = u.x;
        theta = (PI * 0.25f) * (u.y / u.x);
    }
    else
    {
        r     = u.y;
        theta = (PI * 0.5f) - (PI * 0.25f) * (u.x / u.y);
    }
    return r * float2(cos(theta), sin(theta));
}

float trace_inline_shadow_ray(Light light, Surface surface, float2 pixel_xy)
{
    // self intersection bias scaled by camera distance
    float dist_to_camera = length(surface.position - get_camera_position());
    float bias           = 0.005f + dist_to_camera * 0.0001f;
    float3 origin        = surface.position + surface.normal * bias;

    // stationary per pixel rotation to break stratification banding without temporal motion
    float rot_angle = spatial_hash_unit(pixel_xy) * PI2;
    float cos_r     = cos(rot_angle);
    float sin_r     = sin(rot_angle);

    // precompute area light basis once across samples
    float3 area_right = 0.0f;
    float3 area_up    = 0.0f;
    if (light.is_area())
    {
        light.compute_area_light_basis(area_right, area_up);
    }

    // precompute a tangent frame for point/spot/directional source jitter
    float3 to_light_center = light.position - origin;
    float  center_dist     = length(to_light_center);
    float3 light_dir_unit  = light.is_directional() ? normalize(-light.forward) : (center_dist > 0.0001f ? to_light_center / center_dist : float3(0.0f, 1.0f, 0.0f));
    float3 up_axis         = abs(light_dir_unit.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent         = normalize(cross(up_axis, light_dir_unit));
    float3 bitangent       = cross(light_dir_unit, tangent);

    // safety margin for area lights so rays don't hit the emitter's own visible mesh
    // we approximate the emitter mesh radius from the smaller of the two area dims
    float emitter_safety = 0.0f;
    if (light.is_area())
    {
        emitter_safety = min(light.area_width, light.area_height) * 0.5f + 0.005f;
    }

    float visibility_sum = 0.0f;
    float valid_samples  = 0.0f;

    [unroll]
    for (uint s = 0; s < k_inline_shadow_spp; s++)
    {
        // halton point in [0,1)^2 mapped to disk in [-1,1]^2
        float2 u    = k_halton_2_3[s] * 2.0f - 1.0f;
        float2 disk = concentric_disk(u);

        // rotate by stationary per pixel angle
        float2 disk_r = float2(disk.x * cos_r - disk.y * sin_r,
                               disk.x * sin_r + disk.y * cos_r);

        float3 direction;
        float  t_max;

        if (light.is_directional())
        {
            // jittered cone around the sun direction, half angle around 0.5 degrees
            const float angular_radius = 0.0093f;
            direction                  = normalize(light_dir_unit + (tangent * disk_r.x + bitangent * disk_r.y) * angular_radius);
            t_max                      = 10000.0f;
        }
        else if (light.is_area())
        {
            // sample a deterministic point inside the rectangle for soft area shadows
            float3 sample_point = light.position
                                + area_right * disk_r.x * (light.area_width  * 0.5f)
                                + area_up    * disk_r.y * (light.area_height * 0.5f);
            float3 to_light = sample_point - origin;
            float  dist     = length(to_light);
            if (dist < 0.0001f)
            {
                visibility_sum += 1.0f;
                valid_samples  += 1.0f;
                continue;
            }
            direction = to_light / dist;
            t_max     = max(dist - emitter_safety, bias);
        }
        else
        {
            // point or spot, jitter inside a small spherical source for soft penumbra
            if (center_dist < 0.0001f)
            {
                visibility_sum += 1.0f;
                valid_samples  += 1.0f;
                continue;
            }
            const float light_radius = 0.05f;
            float3 jittered_target   = light.position + (tangent * disk_r.x + bitangent * disk_r.y) * light_radius;
            float3 to_jit            = jittered_target - origin;
            float  dist              = length(to_jit);
            direction                = to_jit / dist;
            t_max                    = max(dist - bias * 2.0f, bias);
        }

        // back facing samples carry no light energy, ignore them entirely from the average
        if (dot(surface.normal, direction) <= 0.0f)
            continue;

        valid_samples += 1.0f;

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = direction;
        ray.TMin      = 0.001f;
        ray.TMax      = max(t_max, 0.001f);

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
        query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
        query.Proceed();

        visibility_sum += query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
    }

    // divide by valid samples so glancing surfaces aren't artificially darkened
    return valid_samples > 0.0f ? (visibility_sum / valid_samples) : 1.0f;
}
#endif

// Subsurface scattering with wrapped diffuse and thickness estimation
float3 subsurface_scattering(Surface surface, Light light, AngularInfo angular_info)
{
    // material-dependent scattering parameters
    const float wrap_factor        = 0.5f;  // wrapped lighting factor (0 = no wrap, 1 = full wrap)
    const float sss_exponent       = 3.0f;  // translucency falloff sharpness
    const float thickness_exponent = 1.5f;  // edge thickness falloff
    const float sss_scale          = 0.8f;  // overall scattering strength multiplier
    const float min_scatter        = 0.05f; // minimum ambient scattering
    
    // compute key vectors (use geometric normal for sss)
    float3 L = normalize(-light.to_pixel);
    float3 V = normalize(-surface.camera_to_pixel);
    float3 N = surface.normal;
    
    // Wrapped diffuse: allows light to wrap around surface, simulating subsurface penetration
    float n_dot_l_wrapped = saturate((dot(N, L) + wrap_factor) / (1.0f + wrap_factor));
    float wrapped_diffuse = n_dot_l_wrapped * n_dot_l_wrapped; // square for smoother falloff
    
    // Back-scattering translucency: light passing through from behind using distorted normal
    const float distortion = 0.4f;
    float3 N_distorted     = normalize(N + L * distortion);
    float back_scatter     = saturate(dot(V, -N_distorted));
    back_scatter           = pow(back_scatter, sss_exponent);
    
    // Combine forward (wrapped diffuse) and backward (translucency) scattering
    float sss_term = lerp(back_scatter, wrapped_diffuse, saturate(dot(N, L) * 0.5f + 0.5f));
    sss_term = max(sss_term, min_scatter); // ensure minimum scattering
    
    // Thickness modulation: stronger scattering at thin edges (view-dependent)
    float n_dot_v = saturate(dot(N, V));
    float view_thickness = pow(1.0f - n_dot_v, thickness_exponent);
    
    // Light-dependent: backlit areas show more scattering
    float n_dot_l = saturate(dot(N, L));
    float light_thickness = pow(1.0f - n_dot_l, 1.0f);
    
    // combine thickness terms
    float thickness_modulation = saturate(view_thickness + light_thickness * 0.5f);
    
    // compute light contribution with proper radiance
    float3 light_radiance = light.radiance;
    
    // apply material strength and scale
    float sss_strength = surface.subsurface_scattering * sss_scale;
    
    // Color tinting: preserve material color for subsurface scattering
    float3 sss_color = surface.albedo;
    
    // combine all terms
    return light_radiance * sss_term * thickness_modulation * sss_strength * sss_color;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get resolution and build surface data
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, true);

    // early exit for mismatched pass/surface types
    bool early_exit_1 = pass_is_opaque() && surface.is_transparent() && !surface.is_sky();
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque();
    if (early_exit_1 || early_exit_2)
        return;

    // initialize output accumulators
    float3 out_diffuse    = 0.0f;
    float3 out_specular   = 0.0f;
    float3 out_volumetric = 0.0f;

    // pre-compute common terms (alpha and occlusion)
    // reflection (specular) for transparents is fresnel weighted via F_Schlick inside each
    // brdf lobe, multiplying it again by surface.alpha would steal energy from the reflection
    // path and make glass look dim under direct lights, only opaques use alpha as a generic
    // coverage factor here, diffuse for transparents stays 0 since clear/tinted glass has no
    // lambertian component, the apparent color of tinted glass comes from absorption during
    // transmission which is applied in the refraction pass
    bool   is_transparent       = surface.is_transparent();
    float3 specular_precomputed = is_transparent ? float3(surface.occlusion, surface.occlusion, surface.occlusion) : float3(surface.alpha * surface.occlusion, surface.alpha * surface.occlusion, surface.alpha * surface.occlusion);
    float3 diffuse_precomputed  = is_transparent ? float3(0.0f, 0.0f, 0.0f)                                        : float3(surface.alpha * surface.occlusion, surface.alpha * surface.occlusion, surface.alpha * surface.occlusion);
    
    // loop over all lights and accumulate contributions
    uint light_count = pass_get_f3_value().x;
    for (uint i = 0; i < light_count; i++)
    {
        Light light;
        light.Build(i, surface);

        // when restir pt is enabled, all analytical lights are evaluated via nee inside the
        // restir spatial pass with ray-traced visibility, so skip the brdf / shadow path here to
        // avoid double counting and let real path-traced shadows emerge from the rays themselves
        // volumetric fog still runs below since it does not feed the surface brdf
        bool skip_surface_lighting = is_restir_pt_enabled();

        // per-light accumulators
        float  L_shadow        = 1.0f;
        float3 L_specular_sum  = 0.0f;
        float3 L_diffuse_term  = 0.0f;
        float3 L_subsurface    = 0.0f;
        float3 L_volumetric    = 0.0f;

        if (!surface.is_sky() && !skip_surface_lighting)
        {
            // compute shadow term
            // ray traced shadows are mutually exclusive with rasterized/screen-space shadows
            bool can_use_rt_shadows = light.has_shadows() && is_ray_traced_shadows_enabled();

            if (can_use_rt_shadows && light.is_directional())
            {
                // dedicated screen space pass produces high quality multi sample sun shadows
                L_shadow        = sample_ray_traced_shadow(surface.uv);
                light.radiance *= L_shadow;
            }
        #ifdef RAY_TRACING_ENABLED
            else if (can_use_rt_shadows)
            {
                // inline ray traced shadow for point spot and area lights, 1 spp jittered for taa
                L_shadow        = trace_inline_shadow_ray(light, surface, float2(thread_id.xy));
                light.radiance *= L_shadow;
            }
        #endif
            else if (light.has_shadows())
            {
                // rasterized shadow mapping fallback
                L_shadow = compute_shadow(surface, light);

                // combine with screen-space shadows if available
                if (light.has_shadows_screen_space() && surface.is_opaque())
                {
                    L_shadow = min(L_shadow, tex_uav_sss[int3(thread_id.xy, light.screen_space_shadows_slice_index)].x);
                }

                // apply shadow to light radiance
                light.radiance *= L_shadow;
            }
            
            // apply cloud shadows for directional lights (always, regardless of shadow method)
            if (light.is_directional())
            {
                float cloud_shadow = sample_cloud_shadow(surface.position);
                L_shadow = min(L_shadow, cloud_shadow);
                light.radiance *= cloud_shadow;
            }

            // build angular information for brdf calculations
            AngularInfo angular_info;
            angular_info.Build(light, surface);

            // for area lights widen the specular distribution to match the light's angular
            // extent so the highlight does not collapse into a concentrated peak, the karis
            // energy normalization factor is multiplied back into the brdf result so smooth
            // surfaces do not produce a near lambertian glow that overwhelms everything else
            // (this is critical for transparent surfaces like glass where the over bright
            // specular smear would otherwise occlude the refracted background)
            float original_roughness       = surface.roughness;
            float original_roughness_alpha = surface.roughness_alpha;
            float area_specular_norm       = 1.0f;
            if (light.is_area())
            {
                surface.roughness_alpha = light.compute_area_roughness_modification(surface.roughness_alpha, light.distance_to_pixel, area_specular_norm);
                surface.roughness       = sqrt(surface.roughness_alpha);
            }

            float3 L_specular_lobes = 0.0f;
            {
                if (surface.anisotropic > 0.0f)
                {
                    L_specular_lobes += BRDF_Specular_Anisotropic(surface, angular_info);
                }
                else
                {
                    L_specular_lobes += BRDF_Specular_Isotropic(surface, angular_info);
                }

                if (surface.clearcoat > 0.0f)
                {
                    L_specular_lobes += BRDF_Specular_Clearcoat(surface, angular_info);
                }

                if (surface.sheen > 0.0f)
                {
                    L_specular_lobes += BRDF_Specular_Sheen(surface, angular_info);
                }

                if (surface.subsurface_scattering > 0.0f)
                {
                    L_subsurface += subsurface_scattering(surface, light, angular_info);
                }
            }

            L_specular_sum += L_specular_lobes * area_specular_norm;

            surface.roughness       = original_roughness;
            surface.roughness_alpha = original_roughness_alpha;

            // diffuse for transparents is gated to 0 via diffuse_precomputed below so the
            // brdf is evaluated unconditionally here, the apparent color of tinted glass
            // comes from absorption during transmission handled in the refraction pass
            L_diffuse_term += BRDF_Diffuse(surface, angular_info);
        }

        // compute volumetric fog contribution
        if (light.is_volumetric())
        {
            L_volumetric += compute_volumetric_fog(surface, light, thread_id.xy);
        }
        
        // combine per-light terms with radiance and precomputed factors
        float3 write_diffuse    = L_diffuse_term * light.radiance * diffuse_precomputed * surface.diffuse_energy + L_subsurface;
        float3 write_specular   = L_specular_sum * light.radiance * specular_precomputed;
        float  write_shadow     = L_shadow;
        float3 write_volumetric = L_volumetric;

        // accumulate into output buffers
        out_diffuse    += write_diffuse;
        out_specular   += write_specular;
        out_volumetric += write_volumetric;
    }

    // write results to output buffers
    tex_uav[thread_id.xy]  = validate_output(float4(out_diffuse,    1.0f));
    tex_uav2[thread_id.xy] = validate_output(float4(out_specular,   1.0f));
    tex_uav3[thread_id.xy] = validate_output(float4(out_volumetric, 1.0f));
}
