/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "common.hlsl"
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "sky/cloud_shadow.hlsl"
#include "light_cluster.hlsl"
#include "spherical_harmonics.hlsl"
#include "fog_volume.hlsl"
#include "common_rain.hlsl"
#ifdef RAY_TRACING_ENABLED
#include "common_ray_surface.hlsl"
#endif
//===================================

Texture2D tex_transparent_sky_sh : register(t87);

// dual-source output: frame_render = radiance + transmittance * frame_render
struct transparent_output
{
    [[vk::location(0)]] [[vk::index(0)]] float4 radiance      : SV_Target0;
    [[vk::location(0)]] [[vk::index(1)]] float4 transmittance : SV_Target1;
};

static const float glass_ior_air               = 1.0f;
static const float glass_dispersion            = 0.03f;
static const uint  glass_frost_taps            = 8u;
static const float glass_parallax_scale        = 0.18f;
static const float glass_absorption_scale      = 50.0f;
static const uint  glass_refraction_max_steps  = 16u;
static const float glass_refraction_max_dist   = 2.0f;
static const float glass_refraction_thickness  = 0.1f;
static const float glass_refraction_step_len   = glass_refraction_max_dist / (float)glass_refraction_max_steps;

static float4 sample_material_texture(gbuffer_vertex vertex, uint texture_index)
{
    return GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv_misc.xy);
}

// subtle thin-film iridescence for clear windshield glass at grazing angles, heavy absorption keeps tinted lenses clean
float3 glass_thin_film_fresnel(float3 fresnel, float n_dot_v, float absorption)
{
    float clear   = saturate(1.0f - absorption * 2.0f);
    float grazing = pow(saturate(1.0f - n_dot_v), 3.0f);
    float phase   = (1.0f - n_dot_v) * 14.0f;
    float3 irid   = 0.5f + 0.5f * float3(cos(phase), cos(phase + 2.094395f), cos(phase + 4.188790f));
    return lerp(fresnel, fresnel * lerp(1.0f, irid, 0.4f), clear * grazing);
}

float3 compute_refracted_dir(float3 incident_dir, float3 normal, float ior_outer, float ior_inner)
{
    float ior_ratio   = ior_outer / ior_inner;
    float cos_theta_i = -dot(incident_dir, normal);
    float sin_theta_i = sqrt(max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    float sin_theta_t = ior_ratio * sin_theta_i;
    if (sin_theta_t >= 1.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float cos_theta_t = sqrt(max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    return normalize(ior_ratio * incident_dir + (ior_ratio * cos_theta_i - cos_theta_t) * normal);
}

float2 compute_refraction_uv(float3 surface_pos_ws, float3 refracted_dir_ws, float depth_transparent, float2 uv)
{
    float3 ray_pos  = world_to_view(surface_pos_ws);
    float3 ray_dir  = world_to_view(refracted_dir_ws, false);
    float3 ray_step = ray_dir * glass_refraction_step_len;
    ray_pos        += ray_dir * 0.02f;

    for (uint i = 0u; i < glass_refraction_max_steps; i++)
    {
        ray_pos       += ray_step;
        float2 ray_uv  = view_to_uv(ray_pos);
        if (!is_valid_uv(ray_uv))
        {
            break;
        }

        float depth_z     = linearize_depth(tex4.SampleLevel(samplers[sampler_point_clamp], ray_uv, 0.0f).r);
        float depth_delta = ray_pos.z - depth_z;
        if (depth_delta > 0.0f && depth_delta < glass_refraction_thickness && depth_z > depth_transparent + 0.01f)
        {
            return screen_uv_to_render_uv(ray_uv);
        }

        if (abs(depth_delta) > glass_refraction_thickness * 10.0f && i > 4u)
        {
            break;
        }
    }

    return uv;
}

#ifdef RAY_TRACING_ENABLED
float trace_forward_light_visibility(Surface surface, Light light, bool is_glass)
{
    if (light.is_directional())
    {
        float3 direction = normalize(-light.forward);
        float offset     = max(0.01f + surface.camera_to_pixel_length * 0.0001f, rt_lod_shadow_offset(surface.camera_to_pixel_length));
        if (is_glass)
        {
            // Hardware-only opaque shadow query keeps specular highlights crisp on car/building glass without software leaf loops.
            RayDesc ray;
            ray.Origin    = surface.position + surface.normal * offset;
            ray.Direction = direction;
            ray.TMin      = 0.001f;
            ray.TMax      = 150.0f;
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
            q.TraceRayInline(tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0x01, ray);
            q.Proceed();
            return q.CommittedStatus() != COMMITTED_NOTHING ? 0.0f : 1.0f;
        }
        return rt_trace_visibility(surface.position + surface.normal * offset, direction, 150.0f);
    }

    float3 target        = light.is_area() ? light.compute_closest_point_on_area(surface.position) : light.position;
    float3 to_light      = target - surface.position;
    float distance       = length(to_light);
    float emitter_safety = light.is_area() ? min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f : 0.01f;
    if (distance <= emitter_safety + 0.002f)
    {
        return 1.0f;
    }

    float3 direction = to_light / distance;
    float offset     = max(0.001f + min(surface.camera_to_pixel_length * 0.00001f, 0.002f), rt_lod_shadow_offset(surface.camera_to_pixel_length));
    return rt_trace_visibility(surface.position + surface.normal * offset, direction, distance - emitter_safety);
}
#endif

void evaluate_forward_light(
    uint light_index,
    Surface surface,
    bool is_glass,
    bool is_front_face,
    inout float3 out_diffuse,
    inout float3 out_specular
)
{
    Light light;
    light.Build(light_index, surface);

    if (!light.can_contribute() || !any(light.radiance > 0.0f))
    {
        return;
    }

    float3 L_shadow = 1.0f;
    if (light.has_shadows())
    {
        const float rt_shadow_max_dist = light.is_directional() ? (is_glass ? 35.0f : 60.0f) : 18.0f;
        const bool trace_rt_shadow     = is_front_face && (!is_glass || light.is_directional()) && surface.camera_to_pixel_length < rt_shadow_max_dist;
        if (is_ray_traced_shadows_enabled() && trace_rt_shadow)
        {
#ifdef RAY_TRACING_ENABLED
            L_shadow = trace_forward_light_visibility(surface, light, is_glass);
#endif
        }
        else if (!is_ray_traced_shadows_enabled())
        {
            L_shadow = compute_shadow(surface, light);
        }
    }

    if (light.is_directional())
    {
        L_shadow *= cloud_shadow_sample(tex6, GET_SAMPLER(sampler_bilinear_clamp), surface.position, normalize(-light.forward), get_camera_position());
    }

    if (!any(L_shadow > 0.0f))
    {
        return;
    }

    light.radiance *= L_shadow;

    AngularInfo angular_info;
    angular_info.Build(light, surface);

    float original_roughness       = surface.roughness;
    float original_roughness_alpha = surface.roughness_alpha;
    if (light.is_area())
    {
        surface.roughness_alpha = light.compute_area_roughness_modification(surface.roughness_alpha, light.distance_to_pixel);
        surface.roughness       = sqrt(surface.roughness_alpha);
    }

    float3 L_specular = surface.anisotropic > 0.0f
        ? BRDF_Specular_Anisotropic(surface, angular_info)
        : BRDF_Specular_Isotropic(surface, angular_info);

    if (surface.clearcoat > 0.0f)
    {
        L_specular *= 1.0f - F_Schlick(0.04f, 1.0f, angular_info.v_dot_h) * surface.clearcoat;
        L_specular += BRDF_Specular_Clearcoat(surface, angular_info);
    }

    out_specular += L_specular * light.radiance;

    surface.roughness       = original_roughness;
    surface.roughness_alpha = original_roughness_alpha;

    if (!is_glass)
    {
        out_diffuse += BRDF_Diffuse(surface, angular_info) * light.radiance * surface.diffuse_energy;
    }
}

transparent_output main_ps(gbuffer_vertex vertex, bool is_front_face : SV_IsFrontFace)
{
    pass_load_draw_data_from_vertex(vertex.material_index);

    MaterialParameters material = GetMaterial();
    Surface surface             = (Surface)0;
    surface.flags               = material.flags;

    // keep the unflipped outward geometric normal for sky exposure and vehicle rain atlas lookups
    const float  face_sign       = is_front_face ? 1.0f : -1.0f;
    const float3 exterior_normal = normalize(vertex.normal);

    // flip normal and tangent on backfaces so interior views of thin panes always face the camera
    if (!is_front_face)
    {
        vertex.normal  = -vertex.normal;
        vertex.tangent = -vertex.tangent;
    }

    float2 render_res      = get_render_resolution_active();
    float2 screen_uv       = vertex.position.xy / render_res;
    float2 render_uv       = screen_uv_to_render_uv(screen_uv);
    float3 position_world  = get_position_for_view(vertex.position.z, screen_uv, vertex.view_id);
    float3 camera_position = get_camera_position_for_view(vertex.view_id);
    float3 camera_to_pixel = position_world - camera_position;
    float  distance        = fast_sqrt(max(dot(camera_to_pixel, camera_to_pixel), 1e-8f));
    float3 view_dir_in     = camera_to_pixel / distance; // camera -> surface
    float3 view_dir        = -view_dir_in;               // surface -> camera

    float3 dpdx_world     = ddx(position_world);
    float3 dpdy_world     = ddy(position_world);
    float3 face_normal    = cross(dpdx_world, dpdy_world);
    float  face_len_sq    = dot(face_normal, face_normal);
    face_normal           = face_len_sq > 0.0f ? face_normal * rsqrt(face_len_sq) : normalize(vertex.normal);
    face_normal          *= dot(face_normal, camera_to_pixel) > 0.0f ? -1.0f : 1.0f;

    float4 albedo    = material.color;
    float3 normal    = normalize(vertex.normal);
    float  roughness = material.roughness;
    float  metalness = material.metalness;
    float  occlusion = 1.0f;
    float3 emission  = 0.0f;

    if (surface.has_texture_albedo())
    {
        float4 albedo_sample = sample_material_texture(vertex, material_texture_index_albedo);
        if (material.is_albedo_srgb())
        {
            albedo_sample.rgb = srgb_to_linear(albedo_sample.rgb);
        }
        albedo *= albedo_sample;
    }

    if (surface.has_texture_emissive())
    {
        float3 emissive_sample = sample_material_texture(vertex, material_texture_index_emission).rgb;
        if (material.is_emissive_srgb())
        {
            emissive_sample = srgb_to_linear(emissive_sample);
        }
        emission = emissive_sample * material.emissive_color.rgb;
    }
    else if (material.has_emissive_color())
    {
        emission = material.emissive_color.rgb;
    }
    if (material.emissive_from_albedo())
    {
        emission = material.emissive_strength;
    }

    if (surface.has_texture_normal())
    {
        float3 normal_sample  = sample_material_texture(vertex, material_texture_index_normal).xyz;
        float3 tangent_normal = normalize(unpack(normal_sample));
        if (material.is_normal_green_up())
        {
            tangent_normal.y *= -uv_handedness(vertex.normal, vertex.tangent, dpdx_world, dpdy_world, ddx(vertex.uv_misc.xy), ddy(vertex.uv_misc.xy));
        }
        tangent_normal.z          = fast_sqrt(max(0.0f, 1.0f - dot(tangent_normal.xy, tangent_normal.xy)));
        tangent_normal.xy        *= saturate(max(0.01f, material.normal));
        float3x3 tangent_to_world = make_tangent_to_world_matrix(vertex.normal, vertex.tangent);
        normal                    = normalize(mul(tangent_normal, tangent_to_world).xyz);
    }

    if (material.has_texture_occlusion() || material.has_texture_roughness() || material.has_texture_metalness())
    {
        float4 packed = sample_material_texture(vertex, material_texture_index_packed);
        occlusion     = lerp(occlusion, packed.r, (float)material.has_texture_occlusion());
        roughness    *= lerp(1.0f, packed.g, (float)material.has_texture_roughness());
        metalness    *= lerp(1.0f, packed.b, (float)material.has_texture_metalness());
    }

    // rain beads and rivulets on car windshields and exposed glass (evaluated with the outward exterior normal)
    const bool rain_vehicle = (vertex.draw_flags & (1u << 7)) != 0u;
    float rain_exposed      = rain_weather_wetness() > 0.0f ? rain_exposure(position_world, exterior_normal) : 0.0f;
    bool rain_detail        = false;
    if (rain_exposed > 0.0f || (rain_vehicle && (buffer_frame.rain_vehicle.x > 0.0f || rain_car_active())))
    {
        float4x4 object_transform = draw_data[buffer_pass.draw_index].transform;
        RainSurface rain_surface;
        rain_surface.vehicle          = rain_vehicle;
        rain_surface.position         = position_world;
        rain_surface.geometric_normal = exterior_normal;
        rain_surface.object_origin    = object_transform[3].xyz;
        rain_surface.object_rotation  = float3x3(normalize(object_transform[0].xyz), normalize(object_transform[1].xyz), normalize(object_transform[2].xyz));
        rain_surface.porosity         = saturate((roughness - 0.3f) * 1.8f) * (1.0f - metalness);
        rain_surface.water            = 0.0f;
        rain_surface.footprint        = max(length(dpdx_world), length(dpdy_world));
        rain_surface.exposure         = rain_exposed;
        rain_surface.macro_height     = -1.0f;
        rain_surface.detail           = true;
        float3 rain_normal            = normal * face_sign;
        rain_detail                   = rain_apply(rain_surface, albedo.rgb, rain_normal, roughness, metalness, true) > 0.0f;
        normal                        = rain_normal * face_sign;
    }

    // geometric specular antialiasing on textured or rain-beaded glass
    if (surface.has_texture_normal() || rain_detail)
    {
        const float SPECULAR_AA_SIGMA2 = 0.25f;
        const float SPECULAR_AA_KAPPA  = 0.18f;
        float3 dndu                    = ddx(normal);
        float3 dndv                    = ddy(normal);
        float variance                 = SPECULAR_AA_SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
        float kernel                   = min(2.0f * variance, SPECULAR_AA_KAPPA);
        roughness                      = fast_sqrt(saturate(roughness * roughness + kernel));
    }

    // keep shading normal in the viewer's hemisphere so grazing glass never goes black
    if (dot(normal, view_dir) < 0.0f)
    {
        normal = normalize(normal - view_dir * (dot(normal, view_dir) - 0.001f));
    }

    const float alpha         = saturate(albedo.a);
    const bool  alpha_blended = material.alpha_blend > 0.0f;
    const bool  is_glass      = !alpha_blended;

    float ior_material  = max(material.ior, 1.0001f);
    float f0_dielectric = pow((glass_ior_air - ior_material) / (glass_ior_air + ior_material), 2.0f);

    surface.albedo                 = albedo.rgb;
    surface.alpha                  = alpha;
    surface.roughness              = saturate(roughness);
    surface.roughness_alpha        = max(surface.roughness * surface.roughness, 0.001f);
    surface.metallic               = is_glass ? 0.0f : saturate(metalness);
    surface.clearcoat              = material.clearcoat;
    surface.clearcoat_roughness    = material.clearcoat_roughness;
    surface.anisotropic            = material.anisotropic;
    surface.anisotropic_rotation   = material.anisotropic_rotation;
    surface.sheen                  = material.sheen;
    surface.sheen_color            = material.sheen_color.rgb;
    surface.sheen_roughness        = material.sheen_color.w;
    surface.alpha_blend            = material.alpha_blend;
    surface.ior                    = ior_material;
    surface.absorption             = material.absorption;
    surface.thickness              = material.thickness;
    surface.thin_walled            = material.is_thin_walled();
    surface.occlusion              = occlusion;
    surface.emissive               = material.emissive_from_albedo()
        ? emission * albedo.rgb * photometric_to_radiometric(lighting_emissive_nits_from_albedo)
        : emission * photometric_to_radiometric(lighting_emissive_nits_texture);
    surface.F0                     = is_glass ? float3(f0_dielectric, f0_dielectric, f0_dielectric) : lerp(float3(f0_dielectric, f0_dielectric, f0_dielectric), surface.albedo, surface.metallic);
    surface.pos                    = uint2(vertex.position.xy);
    surface.uv                     = render_uv;
    surface.depth                  = vertex.position.z;
    surface.position               = position_world;
    surface.normal                 = normal;
    surface.bent_normal            = normal;
    surface.camera_to_pixel        = camera_to_pixel;
    surface.camera_to_pixel_length = distance;
    surface.diffuse_energy         = 1.0f;

    // direct lighting: directional sun + clustered local lights
    float3 direct_diffuse  = 0.0f;
    float3 direct_specular = 0.0f;
    uint   total_lights    = buffer_frame.cluster_light_count;
    if (total_lights > 0u)
    {
        evaluate_forward_light(0u, surface, is_glass, is_front_face, direct_diffuse, direct_specular);
    }
    if (total_lights > 1u)
    {
        float4 hp_left   = mul(float4(surface.position, 1.0f), buffer_frame.view_projection);
        float3 ndc_left  = hp_left.xyz / hp_left.w;
        float2 uv_lookup = float2(ndc_left.x * 0.5f + 0.5f, 0.5f - ndc_left.y * 0.5f);
        float  view_z    = mul(float4(surface.position, 1.0f), buffer_frame.view).z;
        uint3  cid       = cluster_id_from_screen(uv_lookup, view_z);
        uint2  range     = cluster_light_grid[cluster_flat(cid)];

        for (uint k = 0u; k < range.y; k++)
        {
            uint light_idx = cluster_light_indices[range.x + k];
            evaluate_forward_light(light_idx, surface, is_glass, is_front_face, direct_diffuse, direct_specular);
        }
    }

    // reflections: prefiltered sky panorama upgraded with an inline RT ray when RT reflections are active
    float  n_dot_v      = saturate(dot(surface.normal, view_dir));
    float  mip_count    = pass_float(pass_transparent::mip_count);
    float3 reflect_dir  = get_dominant_specular_direction(surface.normal, view_dir, surface.roughness);
    float  mip_level    = surface.roughness * surface.roughness * max(mip_count - 1.0f, 0.0f);
    float  horizon      = saturate(1.0f + dot(reflect_dir, face_normal));
    float3 reflection   = tex5.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(reflect_dir), mip_level).rgb * (horizon * horizon);

#ifdef RAY_TRACING_ENABLED
    if (pass_bool(pass_transparent::ray_traced_reflections) && is_front_face && surface.roughness < 0.25f && distance < 18.0f)
    {
        float3 mirror_dir = reflect(-view_dir, surface.normal);
        if (dot(mirror_dir, face_normal) <= 0.0f)
        {
            mirror_dir = reflect(-view_dir, face_normal);
        }
        mirror_dir = normalize(mirror_dir);

        float base_offset   = 0.002f + distance * 0.0002f;
        float grazing       = 1.0f - n_dot_v;
        float pixel_angle   = 2.0f * tan(buffer_frame.camera_fov * 0.5f) / max(render_res.x, 1.0f);
        float2 cone         = float2(distance * pixel_angle, pixel_angle + surface.roughness_alpha);
        // the blas holds the full detail mesh while this pane may be drawn at a lod, step out past the lod error
        float normal_offset = max(base_offset, rt_lod_shadow_offset(distance));

        RayDesc refl_ray;
        refl_ray.Origin    = position_world + face_normal * normal_offset + mirror_dir * base_offset * grazing * 2.0f;
        refl_ray.Direction = mirror_dir;
        refl_ray.TMin      = 0.001f;
        refl_ray.TMax      = 25.0f;

        // trace against solids with FORCE_OPAQUE so glass reflections avoid expensive per-leaf cutout loops
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> refl_query;
        refl_query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, refl_ray);
        refl_query.Proceed();

        RaySurface hit = (RaySurface)0;
        if (refl_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            float2 b = refl_query.CommittedTriangleBarycentrics();
            hit = ray_surface_reconstruct(refl_query.CommittedRayT(), refl_query.CommittedInstanceIndex(), refl_query.CommittedPrimitiveIndex(), float3(1.0f - b.x - b.y, b.x, b.y), refl_ray, cone);
        }

        const uint secondary_lights = min(pass_uint(pass_transparent::light_count), 1u);
        float3 traced = hit.hit
            ? ray_surface_shade_secondary(hit, -mirror_dir, tex5, mip_count, vertex.position.xy, secondary_lights)
            : tex5.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(mirror_dir), mip_level).rgb;

        float rt_weight = (1.0f - smoothstep(0.15f, 0.25f, surface.roughness)) * (1.0f - smoothstep(12.0f, 18.0f, distance));
        reflection      = lerp(reflection, traced, rt_weight);
    }
#endif

    float3 surface_radiance = 0.0f;
    float3 transmittance    = 1.0f;

    if (is_glass)
    {
        float3 F       = F_Schlick(surface.F0, get_f90(), n_dot_v);
        float3 F_glass = glass_thin_film_fresnel(F, n_dot_v, surface.absorption);
        if (surface.thin_walled)
        {
            // infinite internal reflection series for a parallel dielectric slab
            F_glass = 2.0f * F_glass / (1.0f + F_glass);
        }

        float  shell_thickness   = max(surface.thickness, 0.001f);
        float  optical_path      = max(shell_thickness / max(n_dot_v, 0.15f), shell_thickness);
        float3 transmission_tint = pow(max(surface.albedo, 0.02f), max(surface.absorption, 0.0f) * optical_path * glass_absorption_scale);
        float3 kT                = (1.0f - F_glass) * transmission_tint;

        float3 refracted_term = 0.0f;
        float  refract_weight = 0.0f;
        float  rain_refract   = rain_detail ? saturate(length(surface.normal - face_normal) * 4.0f) : 0.0f;

        // thin-walled clear glass composites purely through hardware dual-source blending so stacked panes stay crisp;
        // thick-walled, frosted, or rain-beaded glass blends in refracted/blurred radiance from the post-fog background snapshot
        if (pass_bool(pass_transparent::has_refraction_source) && (!surface.thin_walled || surface.roughness > 0.06f || rain_refract > 0.0f))
        {
            refract_weight = surface.thin_walled ? max(smoothstep(0.06f, 0.20f, surface.roughness), rain_refract) : 1.0f;
            float depth_transparent = linearize_depth(surface.depth);
            float3 refracted_dir    = compute_refracted_dir(view_dir_in, surface.normal, glass_ior_air, ior_material);
            float2 refracted_uv     = render_uv;

            if (dot(refracted_dir, refracted_dir) >= 0.001f)
            {
                if (!surface.thin_walled)
                {
                    float3 tangent     = view_dir_in - surface.normal * dot(view_dir_in, surface.normal);
                    float2 fallback_uv = render_uv + tangent.xz * (optical_path * glass_parallax_scale) * get_render_uv_scale();
                    refracted_uv       = compute_refraction_uv(surface.position, refracted_dir, depth_transparent, is_valid_render_uv(fallback_uv) ? fallback_uv : render_uv);
                }
                else if (rain_refract > 0.0f)
                {
                    float3 bent_pos  = surface.position + (refracted_dir - view_dir_in) * 0.12f;
                    float2 candidate = world_to_uv(bent_pos) * get_render_uv_scale();
                    if (is_valid_render_uv(candidate))
                    {
                        float candidate_depth = linearize_depth(tex4.SampleLevel(samplers[sampler_point_clamp], render_uv_to_screen_uv(candidate), 0.0f).r);
                        if (candidate_depth > depth_transparent)
                        {
                            refracted_uv = candidate;
                        }
                    }
                }
            }

            float2 delta = refracted_uv - render_uv;
            float3 background = tex.SampleLevel(samplers[sampler_bilinear_clamp], render_uv, 0.0f).rgb;
            float3 refraction;
            refraction.r = tex.SampleLevel(samplers[sampler_bilinear_clamp], render_uv + delta * (1.0f - glass_dispersion), 0.0f).r;
            refraction.g = tex.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).g;
            refraction.b = tex.SampleLevel(samplers[sampler_bilinear_clamp], render_uv + delta * (1.0f + glass_dispersion), 0.0f).b;

            float depth_background = linearize_depth(tex4.SampleLevel(samplers[sampler_point_clamp], render_uv_to_screen_uv(refracted_uv), 0.0f).r);
            float screen_thickness = clamp(depth_background - depth_transparent, 0.0f, 4.0f);
            float blur_metric      = min(max(shell_thickness * 40.0f, screen_thickness * 0.25f), screen_thickness + shell_thickness * 20.0f);
            float blur_radius      = min(surface.roughness_alpha * blur_metric / max(depth_transparent, 0.5f), 0.03f);
            if (blur_radius > 0.0002f)
            {
                float  rotation = noise_interleaved_gradient(vertex.position.xy) * PI2;
                float3 sum      = 0.0f;
                [unroll]
                for (uint t = 0u; t < glass_frost_taps; t++)
                {
                    float  angle  = (t + 0.5f) * 2.399963f + rotation;
                    float  radius = blur_radius * sqrt((t + 0.5f) / glass_frost_taps);
                    float2 tap_uv = refracted_uv + float2(cos(angle), sin(angle)) * radius * get_render_uv_scale();
                    sum          += tex.SampleLevel(samplers[sampler_bilinear_clamp], tap_uv, 0.0f).rgb;
                }
                refraction = sum / (float)glass_frost_taps;
            }

            refraction     = lerp(background, refraction, screen_fade(render_uv_to_screen_uv(refracted_uv)));
            refracted_term = refraction * kT * refract_weight;
        }

        surface_radiance = direct_specular + reflection * F_glass + surface.emissive + refracted_term;
        transmittance    = kT * (1.0f - refract_weight);
    }
    else
    {
        float2 env_brdf       = tex3.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
        float  f90            = compute_f90(surface.F0);
        float3 spec_energy    = (surface.F0 * env_brdf.x + f90 * env_brdf.y) * compute_multiscatter_energy_split_sum(surface.F0, env_brdf);
        float3 F_rough        = surface.F0 + (max(1.0f - surface.roughness.xxx, surface.F0) - surface.F0) * f90 * pow(saturate(1.0f - n_dot_v), 5.0f);
        float3 diffuse_energy = compute_diffuse_energy(F_rough, surface.metallic);

        float3 sky_sh[9];
        sh_load_l2(tex_transparent_sky_sh, sky_sh);
        float3 ibl_diffuse = sh_irradiance_l2(surface.normal, sky_sh, max(surface.occlusion, 0.1f)) * INV_PI * diffuse_energy;

        surface_radiance = ((direct_diffuse + ibl_diffuse) * surface.albedo + direct_specular + reflection * spec_energy + surface.emissive) * surface.alpha;
        transmittance    = (1.0f - surface.alpha).xxx;
    }

    // apply camera-to-glass fog transmittance without double-adding scattering already on the background
    float3 final_radiance = surface_radiance;
    if (pass_bool(pass_transparent::apply_fog))
    {
        FogTransport glass_fog = sample_fog_volume(screen_uv, distance);
        final_radiance         = surface_radiance * glass_fog.transmittance + (1.0f - transmittance) * glass_fog.scattering;
    }

    transparent_output output;
    output.radiance      = validate_output(float4(final_radiance, surface.alpha));
    output.transmittance = float4(saturate(transmittance), 1.0f);
    return output;
}
