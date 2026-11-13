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

// karis 2014 analytic split sum, same as reflections_shade, f90 is baked in as 1 so the bias
// term carries grazing fresnel, the gpu lut was built with compute_f90(0)=0 which wiped that term
float2 reflection_env_brdf(float roughness, float n_dot_v)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r        = roughness * c0 + c1;
    float  a004     = min(r.x * r.x, exp2(-9.28f * n_dot_v)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// screen-space raymarching constants
static const uint  g_refraction_max_steps     = 16;     // max ray steps for refraction
static const float g_refraction_max_distance  = 2.0f;   // max refraction distance
static const float g_refraction_thickness     = 0.1f;   // depth testing thickness
static const float g_refraction_step_length   = g_refraction_max_distance / (float)g_refraction_max_steps;

// how far the wash around an object reaches over the water in a lively sea, in metres, and the taps that search it
static const float contact_foam_radius = 1.6f;
static const uint  contact_foam_taps   = 20;
static const float contact_foam_grain  = 3.0f;

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
        ray_uv = screen_uv_to_render_uv(ray_uv);

        // check if the ray passed through the glass and hit geometry behind it
        float depth_z     = get_linear_depth(ray_uv);
        float depth_delta = ray_pos.z - depth_z;
        if (depth_delta > 0.0f && depth_delta < g_refraction_thickness && depth_z > depth_transparent + 0.01f)
            return ray_uv;

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
    float3 direction       = surface.thickness < 0.05f ? view_dir : refracted_dir;
    float3 origin          = surface.position + direction * max(surface.thickness, 0.01f);
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
            origin             = hit.position + direction * max(mat.thickness, 0.01f);
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

// whitewater where the sea works against a pile, a hull or a rock
// the search walks a disc on the water around the pixel, not a disc on the screen, so the foam keeps its width in
// metres at any view angle, each tap asks what opaque geometry the camera sees there and keeps it when it stands at
// the waterline within reach of the pixel, measured sideways and in height on their own, one 3d distance cannot tell
// a pile half a metre away from a wall half a metre down
float ocean_contact_foam(float3 water_position, float footprint, float wave_activity)
{
    // calm water lies against a wall without breaking
    float activity = smoothstep(0.01f, 0.06f, wave_activity);
    if (activity <= 0.0f)
    {
        return 0.0f;
    }

    // a bigger sea throws its wash further, once the reach is under a pixel there is nothing left to resolve
    float reach    = contact_foam_radius * lerp(0.7f, 1.0f, smoothstep(0.03f, 0.25f, wave_activity));
    float resolved = smoothstep(0.75f, 2.0f, reach / max(footprint, 1e-4f));
    if (resolved <= 0.0f)
    {
        return 0.0f;
    }

    float2 wind_axis = ocean_foam_axis();
    float nearest    = reach;
    [loop] for (uint i = 0; i < contact_foam_taps; ++i)
    {
        // golden angle spiral, even in area so a thin pile cannot hide between the taps
        float angle   = (float)i * 2.39996323f;
        float radius  = reach * sqrt(((float)i + 0.5f) / (float)contact_foam_taps);
        float3 probe  = water_position + float3(cos(angle), 0.0f, sin(angle)) * radius;
        float2 tap_uv = world_to_uv(probe);
        if (!is_valid_uv(tap_uv))
        {
            continue;
        }
        float depth = tex4.SampleLevel(samplers[sampler_point_clamp], tap_uv, 0.0f).r;
        if (depth <= 1e-5f)
        {
            continue;
        }

        float3 contact = get_position(depth, tap_uv);
        float2 away    = contact.xz - water_position.xz;
        float beside   = length(away);
        float above    = contact.y - water_position.y;
        // the shaft above the water and the shaft seen through it both mark where the object stands in the surface
        float at_surface = above >= 0.0f ? 1.0f - smoothstep(0.25f, 0.7f, above) : 1.0f - smoothstep(0.1f, 0.4f, -above);
        if (at_surface <= 0.0f || beside >= nearest)
        {
            continue;
        }

        // the terrain already owns a world space wash, a second camera dependent rim does not belong on the same beach
        float bed_valid;
        float bed = sample_ocean_terrain_height(contact.xz, bed_valid);
        if (bed_valid > 0.5f && abs(contact.y - bed) < 0.15f)
        {
            continue;
        }

        // the side the wind drives the sea against throws its wash further than the lee,
        // geometry that only brushes the waterline band counts as further away
        float windward = saturate(0.5f + 0.5f * dot(away / max(beside, 1e-3f), wind_axis));
        nearest        = min(nearest, beside / (at_surface * lerp(0.8f, 1.0f, windward)));
    }
    if (nearest >= reach)
    {
        return 0.0f;
    }

    // the wash is thrown out when a crest surges up the object and drains back as the trough pulls away, and it
    // slides off slowly with the wind, both move the edge of the collar and never thin its core, foam that only
    // fades in strength leaves a square pile wearing a uniform grey ring
    float surge   = saturate(0.5f + 0.35f * (water_position.y - buffer_frame.ocean_sea_level) / max(wave_activity, 0.05f));
    float2 drift  = buffer_frame.wind.xz * (float)buffer_frame.time * 0.015f;
    float breakup = ocean_foam_noise((water_position.xz - drift) * 1.3f) * 0.6f + ocean_foam_noise((water_position.xz - drift) * 4.0f) * 0.4f;
    breakup       = lerp(0.5f, breakup, 1.0f - smoothstep(0.2f, 0.6f, footprint * 1.3f));
    float extent  = reach * lerp(0.7f, 1.0f, surge) * lerp(0.45f, 1.0f, smoothstep(0.15f, 0.8f, breakup));
    // froth is packed against the object and stays dense over most of the wash, only its last third opens into strands
    float coverage = pow(1.0f - smoothstep(0.0f, extent, nearest), 0.6f);

    // the collar is a metre wide, the open sea raft has bubbles that size, so it wears the same raft at a finer scale
    return ocean_foam_raft(saturate(coverage * activity * resolved), (water_position.xz - drift) * contact_foam_grain, wind_axis, footprint * contact_foam_grain);
}

// what a white cap takes from the whole sky, the sun carries the foam at noon but at dusk and under cloud this is
// all the light there is, a fill derived from the sun goes black with it and leaves foam darker than the water it floats on
float3 ocean_foam_sky_light()
{
    // a few texels of the sky panorama near the top of its mip chain are the mean radiance of wide patches of sky,
    // a lambert cap under a uniform sky sends back that radiance times its albedo
    float mip        = max(pass_float(pass_reflections_apply::mip_count) - 3.0f, 0.0f);
    float3 radiance  = tex5.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(float3(0.0f, 1.0f, 0.0f)), mip).rgb * 2.0f;
    radiance        += tex5.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(normalize(float3(1.0f, 0.7f, 0.0f))), mip).rgb;
    radiance        += tex5.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(normalize(float3(-1.0f, 0.7f, 0.0f))), mip).rgb;
    radiance        += tex5.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(normalize(float3(0.0f, 0.7f, 1.0f))), mip).rgb;
    radiance        += tex5.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(normalize(float3(0.0f, 0.7f, -1.0f))), mip).rgb;
    return radiance / 6.0f;
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
        if (is_restir_pt_enabled() && restir_coverage(uv) >= 1.0f)
            return;
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
#ifdef RAY_TRACING_ENABLED
        if (cell_share > 0.0f)
        {
            // a lobe past the roughness cut was not traced, it is wide enough to see what the radiance cache cell under
            // the pixel gathers from its surroundings over the hemisphere, the lights themselves are already in the frame
            // as analytic highlights, the jitter dithers the cell edges and the temporal resolve smooths what is left
            float2 dither = float2(thread_id.xy) + frac((float)(buffer_frame.frame & 63u) * 0.6180339f) * 97.0f;
            float3 jitter = float3(hash(dither), hash(dither + 31.7f), hash(dither + 73.1f)) - 0.5f;
            float  cell_confidence;
            float  cell_openness;
            float3 cell_direct;
            float3 cell   = radiance_cache_lookup(position, normal, jitter, cell_confidence, cell_openness, cell_direct);
            reflection    = cell_share >= 1.0f ? cell * cell_confidence : lerp(reflection, cell, cell_share * cell_confidence);
        }
#endif
        float  n_dot_v             = saturate(dot(normal, -camera_to_pixel));
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
        if (is_restir_pt_enabled())
            specular_reflection *= 1.0f - restir_coverage(uv);
        tex_uav[thread_id.xy]     += float4(specular_reflection, 0.0f);
        return;
    }

    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // fft ocean foam, the water color below is built purely from reflection and refraction so the diffuse
    // foam written into the g-buffer never surfaces, it has to be injected here from the same cascade map
    float foam = 0.0f;
    float foam_churn = 0.0f;
    float wave_activity = 0.0f;
    OceanShore shore = (OceanShore)0;
    if (surface.is_water() && buffer_frame.ocean_enabled > 0.5f)
    {
        // recover the undisplaced fft grid so foam sits on the crest that produced it
        float2 grid_xz = get_ocean_grid_xz(surface.position.xz);
        float view_distance = length(surface.position - get_camera_position());
        foam = get_ocean_foam(grid_xz, surface.position, view_distance, wave_activity, shore, foam_churn);
        float contact = ocean_contact_foam(surface.position, ocean_foam_footprint(view_distance, surface.position), wave_activity);
        foam = max(foam, contact);
        foam_churn = max(foam_churn, contact);
    }

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
                float optical_path    = max(shell_thickness / max(n_dot_v, 0.15f), shell_thickness);

                // thin-shell parallax, bend the background by the authored thickness even when the raymarch fails
                float3 view_tangent = view_dir_normalized - surface.normal * dot(view_dir_normalized, surface.normal);
                float2 parallax     = view_tangent.xz * (optical_path * glass_parallax_scale) * get_render_uv_scale();
                float2 uv_parallax  = uv + parallax;
                if (!is_valid_render_uv(uv_parallax))
                {
                    uv_parallax = uv;
                }

                float2 refracted_uv = compute_refraction_uv(surface.position, refracted_dir, depth_transparent, uv_parallax);
                if (dot(refracted_uv - uv_parallax, refracted_uv - uv_parallax) < 1e-10f)
                {
                    refracted_uv = uv_parallax;
                }

                // the traced ray replaces the screen march, what it hits off screen or behind other geometry is shaded at the hit
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
#endif

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

    float3 specular_reflection = reflection * (F0_brdf * brdf.x + brdf.y) * compute_multiscatter_energy_split_sum(F0_brdf, brdf);

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
        float  coverage          = saturate(1.0f - surface.alpha * 0.25f);
        float3 reflection_total  = tex_uav[thread_id.xy].rgb + specular_reflection;
        float3 kT                = float3(1.0f, 1.0f, 1.0f) - F_glass;
        float3 transmission      = refraction * kT * transmission_tint * coverage;
        tex_uav[thread_id.xy]    = validate_output(float4(reflection_total + transmission, 1.0f));
    }
    else if (surface.is_water())
    {
        // water has no diffuse layer, light_composition zeroed it, so the column below the surface
        // has to be transmitted in here, the frame only holds its analytic specular
        float3 kT            = float3(1.0f, 1.0f, 1.0f) - F;
        float3 surface_color = specular_reflection + refraction * kT;

        // a steep shore wave is thin enough near its crest for daylight to scatter through it, suspended
        // sand and plankton turn that light green, strongest looking through the face toward the sun
        if (shore.height > 0.05f)
        {
            float3 to_sun    = -light_parameters[0].direction;
            float visibility = is_ray_traced_shadows_enabled()
                ? saturate(tex3.SampleLevel(samplers[sampler_bilinear_clamp], surface.uv, 0.0f).r) : 1.0f;
            float forward    = pow(saturate(dot(view_dir_normalized, to_sun) * 0.5f + 0.5f), 3.0f);
            float glow       = pow(shore.crest, 0.7f) * lerp(0.5f, 1.0f, shore.face);
            float thin       = (0.25f + 0.75f * glow) * (1.0f - 0.4f * shore.breaking) * shore.steepness;
            float body       = smoothstep(0.05f, 0.5f, shore.height);
            float3 light_in  = get_sun_radiance() * visibility * saturate(to_sun.y + 0.2f) + get_sky_fill_radiance() * 0.5f;
            float3 scatter   = light_in * float3(0.06f, 0.36f, 0.24f) * (0.8f + 1.5f * forward) * thin * body / PI;
            surface_color   += scatter * kT;
        }

        if (buffer_frame.ocean_enabled > 0.5f)
        {
            float3 to_sun    = -light_parameters[0].direction;
            float visibility = is_ray_traced_shadows_enabled()
                ? saturate(tex3.SampleLevel(samplers[sampler_bilinear_clamp], surface.uv, 0.0f).r) : 1.0f;
            float3 light_in  = get_sun_radiance() * visibility * saturate(to_sun.y + 0.2f) / PI + ocean_foam_sky_light();

            // the open sea glows the same way on a smaller scale, the top of a wind wave is a thin wedge of water, light
            // that enters its back leaves through the face turned to the camera, brightest looking toward the sun
            float open_sea = 1.0f - smoothstep(0.05f, 0.5f, shore.height);
            float lift     = (surface.position.y - buffer_frame.ocean_sea_level) / max(wave_activity, 0.05f);
            float crest    = smoothstep(0.6f, 2.6f, lift) * smoothstep(0.03f, 0.2f, wave_activity);
            if (crest * open_sea > 0.0f)
            {
                float2 view_flat = view_dir_normalized.xz / max(length(view_dir_normalized.xz), 1e-3f);
                float face       = smoothstep(0.0f, 0.3f, -dot(surface.normal.xz, view_flat));
                float forward    = pow(saturate(dot(view_dir_normalized, to_sun) * 0.5f + 0.5f), 3.0f);
                surface_color   += light_in * float3(0.03f, 0.26f, 0.22f) * (0.35f + 1.6f * forward) * lerp(0.35f, 1.0f, face) * crest * open_sea * kT;
            }

            // broken water is full of fine bubbles that linger under and around the foam long after the cap has thinned,
            // they scatter daylight back out and turn the sea milky turquoise in the gaps of the lace
            surface_color += light_in * float3(0.1f, 0.3f, 0.28f) * (0.55f * smoothstep(0.02f, 0.6f, foam_churn)) * kT;
        }

        // the swash sheet thins to nothing at its front, cut by the terrain facets it would end in straight
        // edges, so its last centimetres fade into the wet sand behind a lobed outline
        float2 edge_uv  = render_uv_to_screen_uv((thread_id.xy + 0.5f) / resolution_out);
        float  bed_raw  = tex4.SampleLevel(samplers[sampler_point_clamp], edge_uv, 0.0f).r;
        float  column   = bed_raw > 0.0f ? surface.position.y - get_position(bed_raw, edge_uv).y : 10.0f;
        float  lobes    = ocean_foam_noise(surface.position.xz * 0.45f) * 0.65f + ocean_foam_noise(surface.position.xz * 1.7f) * 0.35f;
        float  coverage = smoothstep(0.0f, 0.006f + 0.04f * lobes, column);
        foam           *= saturate(coverage * 2.5f);

        float3 water = tex_uav[thread_id.xy].rgb + surface_color;
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

    // overlay lit foam on top of the composited water, whitewater is a bright near-white lambertian cap so it
    // must replace the reflection and refraction below rather than tint them, additive blending only desaturates the water
    if (foam > 0.0f)
    {
        // whitewater receives the sun as a wrap-lit diffuse term plus diffuse skylight as an ambient fill, near-white albedo
        float  n_dot_l   = saturate(dot(surface.normal, -light_parameters[0].direction));
        float3 sun_diff  = get_sun_radiance() * (n_dot_l * 0.5f + 0.5f) * (1.0f / PI);
        float visibility = is_ray_traced_shadows_enabled()
            ? saturate(tex3.SampleLevel(samplers[sampler_bilinear_clamp], surface.uv, 0.0f).r) : 1.0f;
        sun_diff *= visibility;
        float3 incoming  = sun_diff + ocean_foam_sky_light();

        // a deep pile of bubbles scatters light many times and goes near white, a thin raft is a few bubble
        // walls over the water, darker and tinted by the sea it lets through, so foam is a veil, not paint
        float  luma       = luminance(incoming);
        float  thickness  = smoothstep(0.15f, 0.9f, foam);
        float  albedo     = lerp(0.5f, 0.85f, thickness);
        float3 tint       = lerp(float3(0.8f, 0.92f, 0.94f), float3(0.97f, 0.98f, 1.0f), thickness);
        float3 foam_color = lerp(incoming, luma.xxx, 0.35f) * tint * albedo;

        float3 base  = tex_uav[thread_id.xy].rgb;
        float  cover = saturate(foam * 0.95f);
        tex_uav[thread_id.xy].rgb = lerp(base, foam_color, cover);
    }
}
