/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "common.hlsl"
#include "fog_volume.hlsl"
#include "brdf.hlsl"
#include "spherical_harmonics.hlsl"
#include "ocean_whitewater.hlsl"

Texture2D tex_water_sky_sh : register(t87);
#ifdef RAY_TRACING_ENABLED
#include "common_ray_surface.hlsl"
#endif
//=============================

// refraction and transparency constants
static const float ior_water                  = 1.333f; // water IOR
static const float ior_air                    = 1.0f;   // air IOR
static const float chromatic_aberration       = 0.02f;  // water chromatic aberration strength
static const float glass_dispersion           = 0.03f;  // per channel ior spread for glass
static const uint  glass_frost_taps           = 8;      // frosted transmission disc taps
static const float glass_parallax_scale       = 0.18f;  // thin-shell uv shift per meter of optical path
static const float glass_absorption_scale     = 50.0f;  // maps authored absorption*thickness into beer lambert exponent
static const uint  glass_max_crossings        = 6;      // a hollow thin shell (bowl, bottle) puts three more interfaces behind the first

// tex4 is the opaque depth at output resolution, stretched from the scaled render subrect over the whole
// texture, so it takes screen uvs while tex2 (the opaque frame) and the g-buffer take render uvs

// ray traced reflections jitter the ray across the ggx lobe by surface roughness and a
// spatiotemporal denoiser reconstructs a roughness proportional blur across the full range

// screen-space raymarching constants
static const uint  g_refraction_max_steps     = 16;     // max ray steps for refraction
static const float g_refraction_max_distance  = 2.0f;   // max refraction distance
static const float g_refraction_thickness     = 0.1f;   // depth testing thickness
static const float g_refraction_step_length   = g_refraction_max_distance / (float)g_refraction_max_steps;

// Compute Fresnel for dielectrics using Schlick approximation
float3 compute_dielectric_fresnel(float cos_theta, float ior_outer, float ior_inner)
{
    // compute F0 for dielectric
    float f0_val = pow((ior_outer - ior_inner) / (ior_outer + ior_inner), 2.0f);
    float3 F0 = float3(f0_val, f0_val, f0_val);
    
    // Schlick Fresnel
    return F_Schlick(F0, get_f90(), cos_theta);
}

// subtle thin-film iridescence for clear windshield glass at grazing angles, heavy absorption kills it so taillight lenses stay clean
float3 glass_thin_film_fresnel(float3 fresnel, float n_dot_v, float absorption)
{
    float clear   = saturate(1.0f - absorption * 2.0f);
    float grazing = pow(saturate(1.0f - n_dot_v), 3.0f);
    float phase   = (1.0f - n_dot_v) * 14.0f;
    float3 irid   = 0.5f + 0.5f * float3(cos(phase), cos(phase + 2.094395f), cos(phase + 4.188790f));
    return lerp(fresnel, fresnel * lerp(1.0f, irid, 0.4f), clear * grazing);
}

// Compute refracted direction using Snell's law (returns zero if total internal reflection)
float3 compute_refracted_dir(float3 incident_dir, float3 normal, float ior_outer, float ior_inner)
{
    float ior_ratio   = ior_outer / ior_inner;
    float cos_theta_i = -dot(incident_dir, normal);
    float sin_theta_i = sqrt(max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    float sin_theta_t = ior_ratio * sin_theta_i;
    
    // check for total internal reflection
    if (sin_theta_t >= 1.0f)
        return float3(0.0f, 0.0f, 0.0f);
    
    // compute refracted direction
    float cos_theta_t = sqrt(max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    return normalize(ior_ratio * incident_dir + (ior_ratio * cos_theta_i - cos_theta_t) * normal);
}

// A thin pane bends the ray only until it exits the far face. Project that lateral
// displacement at the opaque receiver depth so distant road markings stay in place.
float2 thin_pane_uv(Surface surface, float3 incident, float3 refracted, float2 uv)
{
    float2 screen_uv = render_uv_to_screen_uv(uv);
    float depth = tex4.SampleLevel(samplers[sampler_point_clamp], screen_uv, 0.0f).r;
    if (depth <= 0.0f || depth >= surface.depth)
        return uv;

    float3 exit_step = refracted * (surface.thickness / max(abs(dot(refracted, surface.normal)), 0.001f));
    float3 step_view = world_to_view(exit_step, false);
    float3 ray_view = world_to_view(incident, false);
    float3 receiver = world_to_view(get_position(depth, screen_uv));
    float3 offset = step_view - ray_view * (step_view.z / max(ray_view.z, 0.001f));
    float2 shifted_uv = view_to_uv(receiver + offset);
    if (!is_valid_uv(shifted_uv))
        return uv;

    float shifted_depth = tex4.SampleLevel(samplers[sampler_point_clamp], shifted_uv, 0.0f).r;
    return shifted_depth < surface.depth ? screen_uv_to_render_uv(shifted_uv) : uv;
}

// screen space raymarch along the refracted direction, returns the hit render uv or the straight through uv, handles curved glass correctly
float2 compute_refraction_uv(float3 surface_pos_ws, float3 refracted_dir_ws, float depth_transparent, float2 uv)
{
    // convert to view space
    float3 ray_pos  = world_to_view(surface_pos_ws);
    float3 ray_dir  = world_to_view(refracted_dir_ws, false);
    float3 ray_step = ray_dir * g_refraction_step_length;

    // offset starting position slightly to avoid self-intersection with the glass surface
    ray_pos += ray_dir * 0.02f;

    for (uint i = 0; i < g_refraction_max_steps; i++)
    {
        ray_pos       += ray_step;
        float2 ray_uv  = view_to_uv(ray_pos);

        if (!is_valid_uv(ray_uv))
            break;

        // check if the ray passed through the glass and hit geometry behind it
        float depth_z     = linearize_depth(tex4.SampleLevel(samplers[sampler_point_clamp], ray_uv, 0.0f).r);
        float depth_delta = ray_pos.z - depth_z;
        if (depth_delta > 0.0f && depth_delta < g_refraction_thickness && depth_z > depth_transparent + 0.01f)
            return screen_uv_to_render_uv(ray_uv);

        // early exit if the ray is too far from any surface
        if (abs(depth_delta) > g_refraction_thickness * 10.0f && i > 4)
            break;
    }

    return uv;
}

#ifdef RAY_TRACING_ENABLED
struct GlassRefraction
{
    float2 uv;          // where the lit opaque frame shows the hit
    bool   on_screen;   // the camera sees the hit, read it from the frame at uv
    float3 radiance;    // shaded at the hit when it is hidden or off screen
    float3 throughput;  // further panes crossed on the way
    float  distance;    // from the glass to the hit
};

// ray traced refraction, a thin pane is a parallel slab so the ray leaves it along the view direction,
// a thick body keeps the bent direction, further panes are crossed with their fresnel and absorption,
// the hit is read from the lit opaque frame when the camera sees it and shaded at the hit otherwise
GlassRefraction trace_glass_refraction(Surface surface, float3 view_dir, float3 refracted_dir, uint2 pixel)
{
    GlassRefraction result = (GlassRefraction)0;
    result.throughput      = 1.0f;
    float3 direction       = surface.thin_walled ? view_dir : refracted_dir;
    float3 origin          = surface.position;
    if (surface.thin_walled)
        origin += refracted_dir * (surface.thickness / max(abs(dot(refracted_dir, surface.normal)), 0.001f));
    float  mip_count       = pass_float(pass_reflections_apply::mip_count);
    float  pixel_angle     = 2.0f * tan(buffer_frame.camera_fov * 0.5f) / get_render_resolution_active().x;
    float2 cone            = float2(surface.camera_to_pixel_length * pixel_angle, pixel_angle + surface.roughness_alpha);
    [loop]
    for (uint crossing = 0; crossing < glass_max_crossings; crossing++)
    {
        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = direction;
        ray.TMin      = 0.001f;
        ray.TMax      = 1000.0f;
        RaySurface hit = ray_surface_trace(ray, cone);
        if (!hit.hit)
        {
            result.radiance = tex5.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(direction), 0.0f).rgb;
            result.distance = fog_far;
            return result;
        }
        result.distance += hit.hit_distance;
        cone             = ray_cone_propagate(cone, hit.hit_distance);

        MaterialParameters mat = material_parameters[hit.material_index];
        if (mat.color.a < 1.0f)
        {
            float cosine       = max(abs(dot(hit.normal, direction)), 0.15f);
            float path         = max(mat.thickness, 0.001f) / cosine;
            result.throughput *= (1.0f - F_Schlick(0.04f, 1.0f, cosine)) * pow(max(hit.albedo, 0.02f), max(mat.absorption, 0.0f) * path * glass_absorption_scale);
            origin             = hit.position;
            continue;
        }

        float3 hit_view = world_to_view(hit.position);
        float2 hit_uv   = view_to_uv(hit_view);
        if (hit_view.z > 0.0f && is_valid_uv(hit_uv))
        {
            float depth_scene = linearize_depth(tex4.SampleLevel(samplers[sampler_point_clamp], hit_uv, 0.0f).r);
            if (abs(depth_scene - hit_view.z) < hit_view.z * 0.02f + 0.05f)
            {
                result.uv        = screen_uv_to_render_uv(hit_uv);
                result.on_screen = true;
                return result;
            }
        }
        result.radiance = ray_surface_shade_secondary(hit, -direction, tex5, mip_count, float2(pixel), pass_uint(pass_reflections_apply::light_count));
        return result;
    }
    // Reaching the crossing budget does not mean the ray escaped the room.
    // Overlapping panes/manifestation strips can exhaust it as the camera moves.
    // Fall back to the opaque background, rather than replacing the room with sky.
    result.uv = surface.uv;
    result.on_screen = true;
    return result;
}
#endif

// Unit-albedo Lambertian radiance under the same lights, shadows and sky as the scene.
float3 ocean_whitewater_light(Surface surface, uint2 pixel)
{
    float3 sky_sh[9];
    sh_load_l2(tex_water_sky_sh, sky_sh);
    return tex3.Load(int3(pixel, 0)).rgb + sh_irradiance_l2(surface.normal, sky_sh, 1.0f) * INV_PI;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 uv = (thread_id.xy + 0.5f) / resolution_out;

    // cheap gbuffer probes before Surface.Build, most pixels are opaque and skip or take the add-only path
    float4 sample_albedo   = tex_albedo.SampleLevel(samplers[sampler_point_clamp], uv, 0);
    float  alpha           = sample_albedo.a;
    if (alpha == 0.0f)
    {
        return; // sky
    }

    uint2  texel           = gbuffer_texel(uv);
    uint   normal_packed   = tex_normal[texel].x;
    uint   material_packed = tex_material[texel];
    float4 sample_material = gbuffer_unpack_material(material_packed);
    MaterialParameters mat = material_parameters[gbuffer_unpack_material_index(normal_packed, material_packed)];
    bool is_water_pixel    = (mat.flags & uint(1U << 13)) != 0;
    bool is_glass_pixel    = alpha > 0.0f && alpha < 1.0f;

    if (!is_water_pixel && !is_glass_pixel)
    {
        float3 reflection = tex[thread_id.xy].rgb;
        float  cell_share = 0.0f;
#ifdef RAY_TRACING_ENABLED
        cell_share        = is_ray_traced_reflections_enabled() ? get_rt_reflection_cached(uv) : 0.0f;
#endif
        if (cell_share <= 0.0f && dot(reflection, reflection) < 1e-8f)
        {
            return;
        }

        // opaque add-only path, matches the bottom branch without ssao or refraction work
        float  depth               = tex_depth.SampleLevel(samplers[sampler_point_clamp], uv, 0).r;
        float3 position            = get_position(depth, render_uv_to_screen_uv(uv));
        float3 camera_to_pixel     = normalize(position - get_camera_position());
        float3 normal              = gbuffer_unpack_normal(normal_packed);
        float  n_dot_v             = saturate(dot(normal, -camera_to_pixel));
#ifdef RAY_TRACING_ENABLED
        if (cell_share > 0.0f)
        {
            // a cache cell is metres wide and blind to the crevice the pixel sits in, the traced share already
            // stops at real occluders, so only the cell is attenuated by the bent normal visibility cone
            float3 bent_normal = normal;
            float  visibility  = sample_material.a;
            if (is_ssao_enabled() && alpha >= 0.999f)
            {
                float4 ssao_sample = tex_ssao.SampleLevel(samplers[sampler_point_clamp], uv, 0);
                if (!any(isnan(ssao_sample.rgb)) && dot(ssao_sample.rgb, ssao_sample.rgb) > 1e-6f)
                {
                    bent_normal = normalize(ssao_sample.rgb);
                }
                visibility = min(visibility, ssao_sample.a);
            }
            float  roughness          = sample_material.r;
            float3 dominant_direction = get_dominant_specular_direction(normal, -camera_to_pixel, roughness);
            float  cell_occlusion     = specular_occlusion_bent(bent_normal, visibility, dominant_direction, n_dot_v, roughness, roughness * roughness);

            // a lobe past the roughness cut was not traced, it is wide enough to see what the radiance cache cell under
            // the pixel gathers from its surroundings over the hemisphere, the lights themselves are already in the frame
            // as analytic highlights, the jitter dithers the cell edges and the temporal resolve smooths what is left
            float2 dither = float2(thread_id.xy) + frac((float)(buffer_frame.frame & 63u) * 0.6180339f) * 97.0f;
            float3 jitter = float3(hash(dither), hash(dither + 31.7f), hash(dither + 73.1f)) - 0.5f;
            float  cell_confidence;
            float  cell_openness;
            float3 cell_direct;
            float3 cell   = radiance_cache_lookup(position, normal, jitter, cell_confidence, cell_openness, cell_direct) * cell_occlusion;
            reflection    = cell_share >= 1.0f ? cell * cell_confidence : lerp(reflection, cell, cell_share * cell_confidence);
        }
#endif
        float3 albedo              = sample_albedo.rgb;
        float  metallic            = sample_material.g;
        float3 F0                  = lerp(0.04f, albedo, metallic);

        // one traced ray lights both lobes, the lobe it was not aimed at sees the same surroundings
        float2 base_brdf           = reflection_env_brdf(sample_material.r, n_dot_v);
        float3 base                = (F0 * base_brdf.x + base_brdf.y) * compute_multiscatter_energy_split_sum(F0, base_brdf);
        float  clearcoat           = saturate(mat.clearcoat * (1.0f - gbuffer_decal_coverage(sample_material.b, mat)));
        float3 layers              = base;
        if (clearcoat > 0.0f)
        {
            float2 coat_brdf = reflection_env_brdf(saturate(mat.clearcoat_roughness), n_dot_v);
            float  coat_f    = (0.04f * coat_brdf.x + coat_brdf.y) * clearcoat;
            float3 coat_tint = lerp(float3(1.0f, 1.0f, 1.0f), mat.coat_tint.rgb, saturate(mat.coat_tint.a));
            layers           = base * (1.0f - coat_f) + coat_f * coat_tint;
        }
        float3 specular_reflection = reflection * layers;
        tex_uav[thread_id.xy]     += float4(specular_reflection, 0.0f);
        return;
    }

    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // fft ocean foam, the water color below is built purely from reflection and refraction so the diffuse
    // foam written into the g-buffer never surfaces, it has to be injected here from the same cascade map
    float foam, foam_churn;
    ocean_whitewater(surface, foam, foam_churn);

    // get background color
    float3 background = tex2[thread_id.xy].rgb;
    float3 refraction = background;
    
    // determine material IOR
    float ior_material = surface.is_water() ? ior_water : max(surface.ior, 1.0001f);
    
    // compute view direction and angle
    float3 view_dir_normalized = normalize(surface.camera_to_pixel);
    float n_dot_v = saturate(dot(surface.normal, -view_dir_normalized));
    
    // compute Fresnel: F = reflection amount, (1-F) = refraction amount
    float3 F = compute_dielectric_fresnel(n_dot_v, ior_air, ior_material);
    
    // compute refraction for transparent surfaces
    if (surface.is_water() || surface.is_glass())
    {
        float2 uv = (thread_id.xy + 0.5f) / resolution_out;
        float depth_transparent = linearize_depth(surface.depth);
        
        // compute refracted direction
        float3 refracted_dir = compute_refracted_dir(view_dir_normalized, surface.normal, ior_air, ior_material);
        
        // check for total internal reflection
        if (dot(refracted_dir, refracted_dir) < 0.001f)
        {
            // total internal reflection: all light reflects, no refraction
            refraction = background;
            F = float3(1.0f, 1.0f, 1.0f);
        }
        else
        {
            if (surface.is_water())
            {
                // point sample, bilinear blends silhouette depths into values that land anywhere
                float depth_background = linearize_depth(tex4.SampleLevel(samplers[sampler_point_clamp], render_uv_to_screen_uv(uv), 0.0f).r);
                // At the beach, a foreground depth sample must not turn a
                // centimetre of water into a ten-metre absorbing column.
                float bed_valid;
                float bed = sample_ocean_terrain_height(surface.position.xz, bed_valid);
                float clearance = max(surface.position.y - bed, 0.0f);
                float fallback_thickness = bed_valid > 0.5f
                    ? min(clearance / max(abs(view_dir_normalized.y), 0.15f), 10.0f) : 10.0f;
                float thickness = depth_background > depth_transparent
                    ? clamp(depth_background - depth_transparent, 0.0f, 10.0f) : fallback_thickness;

                // a physically bent ray shifts the whole underwater image systematically in one direction,
                // and wherever the shifted sample is unavailable in screen space the pixel has to fall back
                // toward the straight view, a constant shift next to its own fallback reads as two copies of
                // the object, so distort with the zero mean wave slope instead, the image wobbles around its
                // true position and always stays one object, the offset still grows with the water column so
                // it vanishes at the waterline and deeper content shimmers more
                // snell shifts the bed by about a quarter of column * slope in metres, projected by view depth so ripples never scramble it
                float2 offset = surface.normal.xz * 0.25f * min(thickness, 4.0f) / max(depth_transparent * 1.2f, 1.0f) * get_render_uv_scale();

                // the wobble can still land on geometry in front of the water, e.g. the part of a pillar
                // above the surface, halve it until the sample is submerged, the offset is small so the
                // collapse is invisible when it happens
                float2 refracted_uv = uv;
                float depth_shown   = depth_transparent + thickness;
                float scale         = 1.0f;
                [unroll]
                for (int i = 0; i < 4; i++)
                {
                    float2 uv_try   = uv + offset * scale;
                    float depth_try = linearize_depth(tex4.SampleLevel(samplers[sampler_point_clamp], render_uv_to_screen_uv(uv_try), 0.0f).r);
                    if (depth_try > depth_transparent && is_valid_render_uv(uv_try))
                    {
                        refracted_uv = uv_try;
                        depth_shown  = depth_try;
                        break;
                    }
                    scale *= 0.5f;
                }

                // chromatic aberration along the refraction delta
                float2 delta = refracted_uv - uv;
                refraction.r = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv + delta * (1.0f + chromatic_aberration), 0.0f).r;
                refraction.g = tex2.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).g;
                refraction.b = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv + delta * (1.0f - chromatic_aberration), 0.0f).b;

                // The same voxel field owns the submerged column seen from either side.
                float2 screen_uv = render_uv_to_screen_uv(refracted_uv);
                float background_depth = tex4.SampleLevel(samplers[sampler_point_clamp], screen_uv, 0.0f).r;
                float3 background_position = get_position(background_depth, screen_uv);
                float background_distance = length(background_position - get_camera_position());
                // Match this UV's radial ray at the transparent surface's view depth.
                float3 view_ray = normalize(get_position(surface.depth, screen_uv) - get_camera_position());
                float start_distance = depth_transparent / max(dot(view_ray, normalize(mul(float4(0.0f, 0.0f, 1.0f, 0.0f), get_view_inverted()).xyz)), 0.01f);
                refraction = fog_transmit_water_segment(refraction, screen_uv, start_distance,
                    background_depth == 0.0f ? fog_far : max(start_distance, background_distance));
            }
            else
            {
                float shell_thickness = max(surface.thickness, 0.001f);
                float2 refracted_uv    = uv;
                bool   traced_hidden     = false;
                float3 traced_radiance   = 0.0f;
                float3 traced_throughput = 1.0f;
                float  traced_distance   = 0.0f;
#ifdef RAY_TRACING_ENABLED
                if (pass_bool(pass_reflections_apply::ray_traced_refraction))
                {
                    GlassRefraction traced = trace_glass_refraction(surface, view_dir_normalized, refracted_dir, thread_id.xy);
                    traced_throughput      = traced.throughput;
                    if (traced.on_screen)
                    {
                        refracted_uv = traced.uv;
                    }
                    else
                    {
                        traced_hidden   = true;
                        traced_radiance = traced.radiance;
                        traced_distance = traced.distance;
                    }
                }
                else
#endif
                if (surface.thin_walled)
                {
                    refracted_uv = thin_pane_uv(surface, view_dir_normalized, refracted_dir, uv);
                }
                else
                {
                    float optical_path = shell_thickness / max(n_dot_v, 0.15f);
                    float3 tangent = view_dir_normalized - surface.normal * dot(view_dir_normalized, surface.normal);
                    float2 fallback_uv = uv + tangent.xz * (optical_path * glass_parallax_scale) * get_render_uv_scale();
                    refracted_uv = compute_refraction_uv(surface.position, refracted_dir, depth_transparent,
                        is_valid_render_uv(fallback_uv) ? fallback_uv : uv);
                }

                float2 delta = refracted_uv - uv;

                // dispersion, the ior rises toward blue so each channel bends by a slightly different amount
                refraction.r = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv + delta * (1.0f - glass_dispersion), 0.0f).r;
                refraction.g = tex2.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).g;
                refraction.b = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv + delta * (1.0f + glass_dispersion), 0.0f).b;

                // frosted blur from roughness and authored thickness, screen depth is only an upper bound so thin windshields stay sharp
#if defined(API_WEBGPU)
                // webgpu can't filter depth textures, and one filtered tap would make every tex4 read in this shader bind a dummy
                float depth_background = linearize_depth(tex4.SampleLevel(samplers[sampler_point_clamp], render_uv_to_screen_uv(refracted_uv), 0.0f).r);
#else
                float depth_background = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], render_uv_to_screen_uv(refracted_uv), 0.0f).r);
#endif
                float screen_thickness = clamp(depth_background - depth_transparent, 0.0f, 4.0f);
                float blur_metric      = min(max(shell_thickness * 40.0f, screen_thickness * 0.25f), screen_thickness + shell_thickness * 20.0f);
                float blur_radius      = min(surface.roughness_alpha * blur_metric / max(depth_transparent, 0.5f), 0.03f);
                if (blur_radius > 0.0002f)
                {
                    // golden angle disc with a per pixel rotation, taa resolves the residual noise
                    float  rotation = noise_interleaved_gradient(thread_id.xy) * PI2;
                    float3 sum      = 0.0f;
                    [unroll]
                    for (uint t = 0; t < glass_frost_taps; t++)
                    {
                        float  angle  = (t + 0.5f) * 2.399963f + rotation;
                        float  radius = blur_radius * sqrt((t + 0.5f) / glass_frost_taps);
                        float2 tap_uv = refracted_uv + float2(cos(angle), sin(angle)) * radius * get_render_uv_scale();
                        sum          += tex2.SampleLevel(samplers[sampler_bilinear_clamp], tap_uv, 0.0f).rgb;
                    }
                    refraction = sum / glass_frost_taps;
                }

                if (traced_hidden)
                {
                    refraction = fog_transmit_segment(traced_radiance, render_uv_to_screen_uv(uv), surface.camera_to_pixel_length,
                        min(surface.camera_to_pixel_length + traced_distance, fog_far));
                }
                else
                {
                    refraction = lerp(background, refraction, screen_fade(render_uv_to_screen_uv(refracted_uv)));
                    float2 screen_uv = render_uv_to_screen_uv(refracted_uv);
                    float background_depth = tex4.SampleLevel(samplers[sampler_point_clamp], screen_uv, 0.0f).r;
                    float background_distance = length(get_position(background_depth, screen_uv) - get_camera_position());
                    refraction = fog_transmit_segment(refraction, screen_uv, surface.camera_to_pixel_length,
                        background_depth == 0.0f ? fog_far : background_distance);
                }
                refraction *= traced_throughput;
            }
        }
    }
    
    // compute specular reflection using fresnel and brdf split sum
    float3 reflection = tex[thread_id.xy].rgb;

    // weigh with the roughness and f0 of the lobe the tracer carried or the split sum integrates a
    // lobe that was never sampled
    float  coat                 = get_rt_reflection_coat(surface.clearcoat, surface.metallic);
    float  reflection_roughness = lerp(surface.roughness, surface.clearcoat_roughness, coat);
    float2 brdf                 = reflection_env_brdf(reflection_roughness, n_dot_v);

    // transparent uses ior f0, opaque with clearcoat uses coat f0 0.04 because that is the lobe we traced
    float  f0_dielectric = pow((ior_air - ior_material) / (ior_air + ior_material), 2.0f);
    float3 F0_dielectric = float3(f0_dielectric, f0_dielectric, f0_dielectric);
    float3 F0_opaque     = lerp(surface.F0, float3(0.04f, 0.04f, 0.04f), coat);
    float3 F0_brdf       = (surface.is_water() || surface.is_glass()) ? F0_dielectric : F0_opaque;

    float3 reflectance = (F0_brdf * brdf.x + brdf.y) * compute_multiscatter_energy_split_sum(F0_brdf, brdf);
    if (surface.is_water()) reflectance = saturate(reflectance);
    float3 specular_reflection = reflection * reflectance;

    if (surface.is_alpha_blended())
    {
        // the lit layer is already premultiplied by alpha, the background shows straight through the rest
        float3 lit            = tex_uav[thread_id.xy].rgb + specular_reflection * surface.alpha;
        tex_uav[thread_id.xy] = validate_output(float4(lit + background * (1.0f - surface.alpha), 1.0f));
    }
    else if (surface.is_glass() && !surface.is_water())
    {
        // pbr glass, fresnel can pick up a thin-film rainbow on clear panes, beer lambert uses absorption*thickness so alpha stays a soft coverage term
        float3 F_glass           = glass_thin_film_fresnel(F, n_dot_v, surface.absorption);
        float  shell_thickness   = max(surface.thickness, 0.001f);
        float  optical_path      = max(shell_thickness / max(n_dot_v, 0.15f), shell_thickness);
        float3 transmission_tint = pow(max(surface.albedo, 0.02f), max(surface.absorption, 0.0f) * optical_path * glass_absorption_scale);
        if (surface.thin_walled)
            F_glass = 2.0f * F_glass / (1.0f + F_glass);
        float3 reflection_total  = tex_uav[thread_id.xy].rgb + reflection * F_glass;
        float3 kT                = float3(1.0f, 1.0f, 1.0f) - F_glass;
        float3 transmission      = refraction * kT * transmission_tint;
        tex_uav[thread_id.xy]    = validate_output(float4(reflection_total + transmission, 1.0f));
    }
    else if (surface.is_water())
    {
        // water has no diffuse layer, light_composition zeroed it, so the column below the surface
        // has to be transmitted in here, the frame only holds its analytic specular
        float3 kT = 1.0f - reflectance;
        // The volume has already integrated incident light along the refracted path.
        // Entrained bubbles replace part of that column radiance as the breaker aerates.
        float bubble_fraction = 0.14f * foam_churn * foam_churn;
        float3 whitewater_light = 0.0f;
        if (bubble_fraction > 0.0f || foam > 0.0f)
            whitewater_light = ocean_whitewater_light(surface, thread_id.xy);
        float3 bubble_return = whitewater_light * 0.75f * exp(-get_ocean_extinction() * 0.45f);
        float3 surface_color = specular_reflection + kT * lerp(refraction, bubble_return, bubble_fraction);

        // the swash sheet thins to nothing at its front, cut by the terrain facets it would end in straight
        // edges, so its last centimetres fade into the wet sand behind a lobed outline
        float coverage = ocean_shore_coverage(surface, render_uv_to_screen_uv(surface.uv));
        foam           *= saturate(coverage * 2.5f);

        float3 water = tex_uav[thread_id.xy].rgb + surface_color;
        // Foam covers both the mirror and the transmitted column. Its albedo
        // sets the reflected fraction and retains the spectrum of incoming light.
        float foam_albedo = lerp(0.55f, 0.85f, smoothstep(0.15f, 0.9f, foam));
        water = lerp(water, whitewater_light * foam_albedo, saturate(foam));
        tex_uav[thread_id.xy] = float4(lerp(background, water, coverage), tex_uav[thread_id.xy].a);
    }
    else
    {
        // an opaque pixel already holds its complete shading, so the traced lobe is the only thing
        // missing, ibl zeroes its specular when rt reflections are on, adding the frame back in as a
        // transmission term counted every opaque surface twice, auto exposure then halved the whole
        // image and the reflection read as a faint tint on top of a washed out base
        tex_uav[thread_id.xy] += float4(specular_reflection, 0.0f);
    }

}
