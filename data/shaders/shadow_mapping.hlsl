/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// shadow mapping constants
static const uint  g_shadow_sample_count            = 32;
static const float g_shadow_filter_size             = 2.5f;
static const float g_shadow_cascade_blend_threshold = 0.8f;
static const uint  g_penumbra_sample_count          = 8;
static const float g_penumbra_filter_size           = 128.0f;
static const float g_shadow_sample_reciprocal       = 1.0f / (float)g_shadow_sample_count;
static const float g_minimum_penumbra_size          = 0.5f;
static const float g_maximum_penumbra_size          = 64.0f;
static const float g_base_bias_texels               = 0.6f;
static const float g_slope_bias_texels              = 2.2f;
static const float g_local_shadow_near              = 0.05f; // must match the near plane in Light::UpdateProjectionMatrix
static const float g_local_light_size               = 0.05f; // point and spot lights have no emitter size, treat them as a small bulb
static const float g_local_penumbra_separation_max  = 0.25f; // blocker to receiver distance over light to blocker distance
static const float g_local_penumbra_search          = 0.1f;  // blocker search radius as a fraction of the emitter size
static const float g_local_bias_texels              = 1.0f;  // the footprint of one bilinear tap
static const float g_local_bias_cos_min             = 0.15f; // past this grazing angle the bias stops growing, or it would outrun every nearby blocker

static const float2 g_vogel_samples_penumbra[g_penumbra_sample_count] =
{
    float2(0.250000f, 0.000000f),
    float2(-0.319290f, 0.292496f),
    float2(0.048872f, -0.556877f),
    float2(0.402445f, 0.524917f),
    float2(-0.738535f, -0.130636f),
    float2(0.699604f, -0.445032f),
    float2(-0.234003f, 0.870484f),
    float2(-0.446273f, -0.859268f)
};

// rotate 2d vector by angle
float2 rotate_2d(float2 v, float angle)
{
    float sine, cosine;
    sincos(angle, sine, cosine);
    return float2(
        v.x * cosine - v.y * sine,
        v.x * sine + v.y * cosine
    );
}

// get vogel disk sample from lookup table
float2 vogel_disk_sample(uint sample_index, uint sample_count, float angle)
{
    float2 sample;
    
    if (sample_count == g_penumbra_sample_count)
    {
        sample = g_vogel_samples_penumbra[sample_index];
    }
    else
    {
        // fallback calculation
        const float golden_angle = 2.399963f;
        float radius             = sqrt(sample_index + 0.5f) / sqrt(sample_count);
        float theta              = sample_index * golden_angle + angle;
        float sine, cosine;
        sincos(theta, sine, cosine);
        return float2(cosine, sine) * radius;
    }
    
    // apply rotation
    return rotate_2d(sample, angle);
}

// Orthographic reverse-Z stores nearer blockers at larger depth values. Convert
// their separation to metres before applying the sun's angular radius, so tall
// trees soften on the ground while contact shadows retain leaf detail.
float2 directional_filter_radius(Light light, float3 coords, float receiver_depth, float2 texel)
{
    float4x4 transform = light_get_transform(light, (uint)coords.z);
    float2 world_to_uv = 0.5f * float2(
        length(float3(transform[0][0], transform[1][0], transform[2][0])),
        length(float3(transform[0][1], transform[1][1], transform[2][1])));
    float world_to_depth = length(float3(transform[0][2], transform[1][2], transform[2][2]));
    const float sun_angular_radius = 0.00465f;
    float2 search_radius = max(texel * 2.0f, 50.0f * sun_angular_radius * world_to_uv);
    float separation_sum = 0.0f;
    uint blocker_count = 0;
    for (uint i = 0; i < g_penumbra_sample_count; i++)
    {
        float2 uv = coords.xy + g_vogel_samples_penumbra[i] * search_radius;
        if (any(uv < 0.0f) || any(uv > 1.0f))
            continue;
        float blocker = light_sample_depth(light, float3(uv, coords.z));
        if (blocker > receiver_depth + 1e-6f)
        {
            separation_sum += (blocker - receiver_depth) / max(world_to_depth, 1e-6f);
            blocker_count++;
        }
    }
    float separation = separation_sum / max(float(blocker_count), 1.0f);
    return clamp(separation * sun_angular_radius * world_to_uv, texel * 1.5f, texel * 32.0f);
}

// compute shadow factor using vogel disk sampling, directional cascades only, local lights filter in compute_shadow_local
float vogel_depth(Light light, Surface surface, float3 sample_coords, float receiver_depth, float filter_size_multiplier = 1.0f)
{
    uint cascade_index = (uint)sample_coords.z;
    bool far_directional = light.is_directional() && cascade_index == 1;

    // The CPU already supplies 1 / slice resolution, not 1 / atlas resolution.
    // A single fully lit/shadowed center tap does not prove the entire filter
    // footprint is uniform: skipping the other taps exposes the texel grid.
    float2 texel_size_cascade_local = light_get_atlas_texel_size(light, cascade_index);
    float  rotation_angle           = float(cascade_index) * 0.785398163f;
    float  shadow_factor            = 0.0f;
    
    // far cascade uses a fixed kernel, pcss blocker search is larger than a far texel and self shadows the whole slice
    float2 filter_size = texel_size_cascade_local * g_shadow_filter_size;
    if (light.is_directional() && !far_directional)
    {
        filter_size = directional_filter_radius(light, sample_coords, receiver_depth, texel_size_cascade_local);
    }
    filter_size *= filter_size_multiplier;
    
    // sample shadow map
    for (uint i = 0; i < g_shadow_sample_count; i++)
    {
        float2 offset      = vogel_disk_sample(i, g_shadow_sample_count, rotation_angle) * filter_size;
        float2 sample_uv   = sample_coords.xy + offset;

        if (any(sample_uv < 0.0f) || any(sample_uv > 1.0f))
        {
            shadow_factor += 1.0f;
            continue;
        }
        // Keep bilinear taps inside this tile instead of reading a neighbor.
        float2 uv_clamped = clamp(sample_uv, texel_size_cascade_local * 0.5f, 1.0f - texel_size_cascade_local * 0.5f);

        // depth comparison
        float depth_sample = light_compare_depth(
            light,
            float3(uv_clamped, sample_coords.z),
            receiver_depth
        );
        shadow_factor      += depth_sample;
    }

    return shadow_factor * g_shadow_sample_reciprocal;
}

// compute depth bias using slope-scaled technique in world space
float3 compute_normal_offset(Surface surface, Light light, uint cascade_index)
{
    // calculate world-space texel size from projection matrix
    float world_frustum_width =
        2.0f /
        length(
            light_get_transform(
                light,
                cascade_index
            )[0].xyz
        );
    float texel_size_world =
        world_frustum_width *
        light_get_atlas_texel_size(
            light,
            cascade_index
        ).x;

    // compute slope
    float3 light_dir = light.is_directional() ? normalize(-light.forward.xyz) : normalize(light.position - surface.position);
    float n_dot_l    = dot(surface.normal, light_dir);
    float slope      = sqrt(saturate(1.0f - n_dot_l * n_dot_l));

    // far cascade texels are meters wide, extra bias keeps the terrain from self shadowing into a solid rectangle
    float cascade_bias = (light.is_directional() && cascade_index == 1) ? 2.0f : 1.0f;
    float offset_amount = (g_base_bias_texels + (slope * g_slope_bias_texels)) * texel_size_world * cascade_bias;
    
    float normal_sign = surface.is_foliage() && surface.subsurface_scattering > 0.0f && n_dot_l < 0.0f ? -1.0f : 1.0f;
    return surface.normal * (offset_amount * normal_sign);
}

// distance along the slice axis of a reverse-z perspective shadow depth, near maps to 1 and the light range to 0
float local_shadow_linear_depth(Light light, float depth)
{
    float range = max(light.far, g_local_shadow_near + 1e-3f);
    return g_local_shadow_near * range / (g_local_shadow_near + depth * (range - g_local_shadow_near));
}

// the inverse, the shadow depth of a distance along the slice axis
float local_shadow_depth(Light light, float z)
{
    float range = max(light.far, g_local_shadow_near + 1e-3f);
    return g_local_shadow_near * (range - z) / (max(z, g_local_shadow_near) * (range - g_local_shadow_near));
}

// where a point lands in the shadow slices of a local light, xy is the slice uv and w the distance along the slice axis.
// a point or area light looks through the six faces of a cube and a spot light through one frustum
float4 local_shadow_project(Light light, float3 position, out uint slice_index)
{
    slice_index = 0u;
    if (light.is_point() || light.is_area())
    {
        float3 light_to_point = position - light.position;
        float3 abs_dir        = abs(light_to_point);
        slice_index = (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z) ? (light_to_point.x > 0.0f ? 0u : 1u) :
                      (abs_dir.y >= abs_dir.z)                           ? (light_to_point.y > 0.0f ? 2u : 3u) :
                                                                           (light_to_point.z > 0.0f ? 4u : 5u);
    }

    float4 clip = mul(float4(position, 1.0f), light_get_transform(light, slice_index));
    return float4(ndc_to_uv(clip.xy / clip.w), clip.z / clip.w, clip.w);
}

// point, spot and area lights. the taps sit on a disc that faces the light and is sized in metres, each one is projected on
// its own, so a kernel that crosses the edge of a cube face carries on in the next face instead of reading past the slice
float compute_shadow_local(Surface surface, Light light)
{
    float3 to_light  = light.position - surface.position;
    float  distance  = length(to_light);
    float3 direction = to_light / max(distance, 1e-4f);

    uint   slice_index;
    float4 center = local_shadow_project(light, surface.position, slice_index);
    if (center.w <= g_local_shadow_near)
    {
        return 1.0f;
    }

    // size of a shadow texel at the receiver, the horizontal scale of the projection, 1 / tan(fov / 2), sits in the first column
    float4x4 transform   = light_get_transform(light, slice_index);
    float    proj_scale  = length(float3(transform[0][0], transform[1][0], transform[2][0]));
    float    texel_world = 2.0f * distance / max(proj_scale, 1e-6f) * light_get_atlas_texel_size(light, slice_index).x;

    // every tap is compared against the plane of the receiver and not against one depth. one bias for the whole kernel either
    // self shadows at its rim or, as a normal offset, slides the shadow of a nose centimetres across a cheek the light grazes,
    // where a texel covers several times its width. only the side of the kernel where the plane climbs towards the light is
    // let off, the other side keeps the depth of the receiver, the shading normal is not exact enough to follow the plane down
    float3 normal      = dot(surface.normal, direction) < 0.0f ? -surface.normal : surface.normal;
    float  n_dot_l     = dot(normal, direction);
    float  plane_slope = 1.0f / max(n_dot_l, g_local_bias_cos_min);
    float  tan_theta   = sqrt(saturate(1.0f - n_dot_l * n_dot_l)) * plane_slope;
    float  bias_base   = texel_world * g_local_bias_texels * (1.0f + tan_theta);

    // half a texel lifts the receiver off its own facets
    float3 position = surface.position + normal * (0.5f * texel_world);
    float3 tangent;
    float3 bitangent;
    find_best_axis_vectors(direction, tangent, bitangent);

    // blocker search, only what rises above the plane of the receiver and sits in the cone from the receiver to the emitter
    // blocks it. on a surface the light grazes half of the taps are nearer the light without being in its way, and a brow
    // centimetres to the side says nothing about the shadow of a nose, counting either one blurs that shadow away
    float emitter_radius = light.is_area() ? sqrt(light.area_width * light.area_height * INV_PI) : g_local_light_size;
    float search_radius  = clamp(emitter_radius * g_local_penumbra_search, 2.0f * texel_world, g_penumbra_filter_size * texel_world);
    float blocker_sum    = 0.0f;
    uint  blocker_count  = 0;
    for (uint i = 0; i <= g_penumbra_sample_count; i++)
    {
        // the first tap is the receiver itself, the disc has no sample at its center
        float2 offset = i == 0 ? float2(0.0f, 0.0f) : g_vogel_samples_penumbra[i - 1] * search_radius;
        float3 across = tangent * offset.x + bitangent * offset.y;
        float3 tap    = position + across;
        uint   tap_slice;
        float4 tap_uv = local_shadow_project(light, tap, tap_slice);
        if (tap_uv.w <= g_local_shadow_near || any(tap_uv.xy < 0.0f) || any(tap_uv.xy > 1.0f))
            continue;

        // the slices store the distance along their axis, the plane is measured from the light
        float axis_scale = tap_uv.w / length(tap - light.position);
        float blocker    = local_shadow_linear_depth(light, light_sample_depth(light, float3(tap_uv.xy, (float)tap_slice))) / axis_scale;
        float cone_radius = emitter_radius * (distance - blocker) / distance + texel_world;
        float plane       = distance - bias_base - max(-dot(across, normal), 0.0f) * plane_slope;
        if (blocker < plane && length(offset) <= cone_radius)
        {
            blocker_sum += blocker;
            blocker_count++;
        }
    }

    // similar triangles, an emitter this wide casts a penumbra this wide at the receiver. the kernel only widens where the
    // receiver faces the light, across a receiver the light grazes a shadow is a few texels wide, a wide kernel sees mostly
    // the receiver itself and the bias that takes to hide outruns the blocker, which wiped the shadow of a nose off a cheek
    float separation = 0.0f;
    if (blocker_count > 0)
    {
        float blocker = blocker_sum / float(blocker_count);
        float facing  = n_dot_l * n_dot_l;
        separation    = clamp((distance - blocker) / max(blocker, 1e-3f), 0.0f, g_local_penumbra_separation_max) * facing * facing;
    }
    float filter_texel  = g_shadow_filter_size * texel_world;
    float filter_radius = clamp(emitter_radius * separation, g_minimum_penumbra_size * filter_texel, g_maximum_penumbra_size * filter_texel);

    float shadow = 0.0f;
    for (uint j = 0; j < g_shadow_sample_count; j++)
    {
        float2 offset = vogel_disk_sample(j, g_shadow_sample_count, 0.0f) * filter_radius;
        float3 across = tangent * offset.x + bitangent * offset.y;
        float3 tap    = position + across;
        uint   tap_slice;
        float4 tap_uv = local_shadow_project(light, tap, tap_slice);
        if (tap_uv.w <= g_local_shadow_near || any(tap_uv.xy < 0.0f) || any(tap_uv.xy > 1.0f))
        {
            shadow += 1.0f;
            continue;
        }

        // keep bilinear taps inside this slice instead of reading a neighbor
        float2 texel      = light_get_atlas_texel_size(light, tap_slice);
        float2 uv         = clamp(tap_uv.xy, texel * 0.5f, 1.0f - texel * 0.5f);
        float  axis_scale = tap_uv.w / length(tap - light.position);
        float  plane      = distance - bias_base - max(-dot(across, normal), 0.0f) * plane_slope;
        shadow           += light_compare_depth(light, float3(uv, (float)tap_slice), local_shadow_depth(light, plane * axis_scale));
    }

    return shadow * g_shadow_sample_reciprocal;
}

// metres of solid between where a local light enters a body and a point on its far side, negative when the slices do not
// cover the point. a slice holds the first surface the light meets, everything from there to the point counts as solid
float local_shadow_thickness(Light light, float3 position)
{
    float  distance = length(position - light.position);
    uint   slice_index;
    float4 uv = local_shadow_project(light, position, slice_index);
    if (uv.w <= g_local_shadow_near || any(uv.xy < 0.0f) || any(uv.xy > 1.0f))
    {
        return -1.0f;
    }

    // four taps around the point, a single one steps with the texel grid where depth cannot be filtered.
    // a tap that looks past the body reads the wall behind it, that is no measurement, counting it as no solid lit speckles along the outline
    float2 texel      = light_get_atlas_texel_size(light, slice_index);
    float  axis_scale = uv.w / max(distance, 1e-4f);
    float  thickness  = 0.0f;
    for (uint i = 0; i < 4; i++)
    {
        float2 offset = float2((i & 1u) != 0u ? 0.5f : -0.5f, (i & 2u) != 0u ? 0.5f : -0.5f) * texel;
        float  entry  = local_shadow_linear_depth(light, light_sample_depth(light, float3(uv.xy + offset, (float)slice_index))) / axis_scale;
        if (entry > distance)
        {
            return -1.0f;
        }
        thickness += distance - entry;
    }

    return thickness * 0.25f;
}

// main shadow computation function
float compute_shadow(Surface surface, Light light)
{
    float3 to_light = light.position - surface.position;
    float dist_sq   = dot(to_light, to_light);

    // quadratic falloff for point/spot lights
    float fade = 1.0f;
    if (light.is_point() || light.is_spot())
    {
        fade = saturate(1.0f - dist_sq / (light.far * light.far));
        fade = fade * fade; 
        if (fade <= 0.0f)
            return 1.0f; 
    }

    float shadow = 1.0f;

    // point, spot and area lights
    if (!light.is_directional())
    {
        shadow = lerp(1.0f, compute_shadow_local(surface, light), fade);
    }
    // directional light (cascaded shadow maps)
    else 
    {
        const uint near_cascade = 0;
        
        // sample near cascade
        float3 position_world = surface.position + compute_normal_offset(surface, light, near_cascade);
        float4 clip_pos_near  = mul(
            float4(position_world, 1.0f),
            light_get_transform(light, near_cascade)
        );
        float3 ndc_near       = clip_pos_near.xyz / clip_pos_near.w;
        float2 uv_near        = ndc_to_uv(ndc_near.xy);
        
        bool near_valid = cascade_contains(ndc_near);
        
        float shadow_near = 1.0f;
        if (near_valid)
        {
            shadow_near = vogel_depth(light, surface, float3(uv_near, near_cascade), ndc_near.z, 1.0f);
        }
        shadow = shadow_near;

        // directional cascade blending
        if (light.is_directional())
        {
            const uint far_cascade = 1;
            
            // sample far cascade with re-calculated offset
            float3 position_world_far = surface.position + compute_normal_offset(surface, light, far_cascade);
            float4 clip_pos_far       = mul(
                float4(position_world_far, 1.0f),
                light_get_transform(light, far_cascade)
            );
            float3 ndc_far            = clip_pos_far.xyz / clip_pos_far.w;
            float2 uv_far             = ndc_to_uv(ndc_far.xy);
            
            bool far_valid = cascade_contains(ndc_far);
            
            if (near_valid && far_valid)
            {
                float shadow_far = vogel_depth(light, surface, float3(uv_far, far_cascade), ndc_far.z, 1.0f);

                // smooth blend based on distance from center
                float2 ndc_dist_from_center = abs(ndc_near.xy);
                float max_dist              = max(ndc_dist_from_center.x, ndc_dist_from_center.y);
                float blend_start           = 0.8f;
                float blend_end             = 0.95f;
                float blend_factor          = smoothstep(blend_start, blend_end, max_dist);
                
                shadow = lerp(shadow_near, shadow_far, blend_factor);
            }
            else if (!near_valid && far_valid)
            {
                shadow = vogel_depth(light, surface, float3(uv_far, far_cascade), ndc_far.z, 1.0f);
            }

            if (far_valid)
            {
                shadow = lerp(1.0f, shadow, cascade_edge_fade(ndc_far));
            }
        }
    }

    return shadow;
}
