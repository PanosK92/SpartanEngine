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

//= INCLUDES ============
#include "fog.hlsl"
#include "sky/clouds.hlsl"
#include "fog_medium.hlsl"
//=======================

#if defined(FOG_INJECT)

struct FogSkyProbe
{
    float3 sun;
    float3 zenith;
    float3 light_dir;
};

// the probe is identical for every cell, build it once per thread
FogSkyProbe fog_build_sky_probe()
{
    const uint sky_mip = 7;
    FogSkyProbe probe;
    probe.light_dir = float3(0.0f, 1.0f, 0.0f);
    bool has_sun = buffer_frame.cluster_light_count > 0u &&
        (light_parameters[0].flags & (1u << 0)) != 0u;
    if (has_sun)
    {
        probe.light_dir = normalize(-light_parameters[0].direction);
    }

    float3 sun_sample_dir = float3(probe.light_dir.x, max(probe.light_dir.y, 0.0f), probe.light_dir.z);
    sun_sample_dir = dot(sun_sample_dir, sun_sample_dir) > 1e-8f
        ? normalize(sun_sample_dir) : float3(0.0f, 1.0f, 0.0f);
    probe.sun = tex.SampleLevel(
        GET_SAMPLER(sampler_trilinear_clamp),
        direction_sphere_uv(sun_sample_dir),
        sky_mip
    ).rgb;
    probe.zenith = tex.SampleLevel(
        GET_SAMPLER(sampler_trilinear_clamp),
        direction_sphere_uv(float3(0.0f, 1.0f, 0.0f)),
        sky_mip
    ).rgb;

    const float sky_color_max = 50.0f;
    probe.sun    = min(probe.sun, sky_color_max);
    probe.zenith = min(probe.zenith, sky_color_max);
    return probe;
}

float3 fog_sky_ambient(FogSkyProbe probe, float2 uv, float distance, float3 ray_direction)
{
    float3 sky_sun = probe.sun;
    float3 sky_zenith = probe.zenith;
    float sun_lobe = pow(saturate(dot(ray_direction, probe.light_dir)), 4.0f);

    // These low-frequency sky probes also need visibility. Without it every enclosed
    // room receives blue outdoor inscattering, which auto exposure then amplifies.
    // Visibility is traced once per coarse cell by the sky visibility pass.
    #ifdef RAY_TRACING_ENABLED
    if (is_ray_traced_shadows_enabled())
    {
        float sky_visibility = tex_fog_sky_visibility.SampleLevel(
            GET_SAMPLER(sampler_trilinear_clamp),
            float3(uv, fog_distance_to_slice(distance)),
            0.0f
        ).r;
        sky_zenith *= sky_visibility;
        sky_sun *= sky_visibility;
    }
    #endif
    return lerp(sky_zenith, sky_sun, sun_lobe);
}

float3 fog_evaluate_light(
    Light light,
    float3 sample_pos,
    float3 ray_direction,
    uint2 pixel,
    bool in_water,
    float3 sigma_s,
    float angular_footprint
)
{
    float3 light_dir;
    float local_atten;
    compute_volumetric_light_sample(light, sample_pos, light_dir, local_atten);
    if (local_atten <= 0.0f)
    {
        return 0.0f;
    }

    float3 tint = 1.0f;
    if (in_water && light.is_directional())
    {
        // Sunlight cannot survive hundreds of optical depths. Avoid tracing
        // shadows for deep cells whose incident energy is already negligible.
        float depth = max(buffer_frame.ocean_sea_level - sample_pos.y, 0.0f);
        if (depth * get_ocean_extinction().b > 12.0f) return 0.0f;
    }

    float visibility = 1.0f;
    if (light.has_shadows())
    {
    #ifdef RAY_TRACING_ENABLED
        // the atlas is not rendered with ray traced shadows, the tlas holds every caster
        if (is_ray_traced_shadows_enabled())
        {
            visibility = fog_trace_shadow(light, sample_pos);
        }
        else
    #endif
        {
            visibility = visible(sample_pos, light, pixel);
        }
    }

    if (light.is_directional() && visibility > 0.0f)
    {
        visibility *= cloud_shadow_sample(
            tex5,
            GET_SAMPLER(sampler_bilinear_clamp),
            sample_pos,
            light_dir,
            get_camera_position()
        );

        if (in_water)
        {
            // Occluded sunlight contributes exactly zero. Its expensive FFT
            // caustic quadrature is only needed after visibility is known.
            // In particular, cells below the island's seabed never need it.
            if (visibility <= 0.0f) return 0.0f;
            float footprint = max(angular_footprint * length(sample_pos - get_camera_position()), 0.03f);
            tint = get_ocean_sun_transmission(sample_pos, light_dir, footprint);
            light_dir = -refract(-light_dir, float3(0.0f, 1.0f, 0.0f), 1.0f / 1.333f);
        }
    }

    // Keep sun shafts visible from oblique underwater views as well as looking
    // directly toward the sun; the previous narrow g=0.72 lobe hid most of them.
    float phase_g = in_water || light.is_directional() ? 0.4f : 0.6f;
    float phase = henyey_greenstein_phase(dot(ray_direction, light_dir), phase_g);
    return light.color * light.intensity * local_atten * visibility * phase * tint * sigma_s;
}

float3 fog_ambient_medium(FogSkyProbe probe, float2 uv, float distance, float3 sample_pos, float3 ray_direction, bool in_water)
{
    float3 ambient_transmission = 1.0f;
    if (in_water)
    {
        float depth = max(buffer_frame.ocean_sea_level - sample_pos.y, 0.0f);
        ambient_transmission = exp(-get_ocean_extinction() * depth);
    }
    return any(ambient_transmission > 1e-5f)
        ? fog_sky_ambient(probe, uv, distance, ray_direction) * ambient_transmission : 0.0f;
}

// light parameters do not depend on the sample, build each light once per cell
float3 fog_light_samples(
    uint light_index,
    Surface surface,
    float3 positions[2],
    uint count,
    float3 ray_direction,
    bool in_water,
    float3 sigma_s,
    float angular_footprint
)
{
    Light light;
    light.Build(light_index, surface);
    // Water sunlight is part of the ocean transport, including caustic shafts.
    // The light's optional atmospheric-fog flag must not turn the ocean black
    // or remove its shafts (several worlds intentionally disable air beams).
    if (!light.is_volumetric() && !(in_water && light.is_directional()))
    {
        return 0.0f;
    }

    float3 rate = 0.0f;
    [loop]
    for (uint i = 0u; i < count; i++)
    {
        rate += fog_evaluate_light(light, positions[i], ray_direction, surface.pos, in_water, sigma_s, angular_footprint);
    }
    return rate / float(count);
}

float3 fog_light_medium(
    float3 positions[2],
    uint count,
    float3 ray_direction,
    uint3 thread_id,
    float2 uv,
    bool in_water,
    float3 sigma_s,
    float angular_footprint = 0.0f
)
{
    if (!any(sigma_s > 0.0f)) return 0.0f;
    Surface surface = fog_build_surface(positions[0], ray_direction, thread_id.xy, uv);
    float3 rate = 0.0f;
    if (buffer_frame.cluster_light_count > 0u)
        rate += fog_light_samples(0u, surface, positions, count, ray_direction, in_water, sigma_s, angular_footprint);
    [loop]
    for (uint k = 0u; k < buffer_frame.volumetric_light_count; k++)
        rate += fog_light_samples(volumetric_light_indices[k], surface, positions, count, ray_direction, in_water, sigma_s, angular_footprint);
    return rate;
}

// One density/lighting injection for both air and water. No camera-medium switch.
[numthreads(8, 8, 4)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(thread_id >= uint3(fog_width, fog_height, fog_depth)))
        return;

    float2 uv = (float2(thread_id.xy) + 0.5f) / float2(fog_width, fog_height);
    float3 ray_direction = fog_view_direction(uv);
    float d0 = fog_slice_to_distance(float(thread_id.z) / float(fog_depth));
    float d1 = fog_slice_to_distance(float(thread_id.z + 1u) / float(fog_depth));
    float centre_distance = fog_slice_to_distance((float(thread_id.z) + 0.5f) / float(fog_depth));
    float3 sample_pos = get_camera_position() + ray_direction * centre_distance;
    // Filter unresolved density noise to the cell footprint, along and across the ray.
    float cell_footprint = max((d1 - d0) * 0.5f, centre_distance / float(fog_height));
    FogMedium medium = fog_sample_medium(sample_pos, get_camera_position().y + ray_direction.y * d0, get_camera_position().y + ray_direction.y * d1, cell_footprint);
    // Preserve both source terms in interface cells. Lighting the entire cell
    // from its submerged centre makes the air above distant water alternate dark/light.
    float water_length = (d1 - d0) * medium.water;
    float air_length = (d1 - d0) - water_length;
    bool water_first = ray_direction.y > 0.0f;
    float air_start = d0 + (water_first ? water_length : 0.0f);
    float water_start = d0 + (water_first ? 0.0f : air_length);
    // Density and lighting belong to world-space media, independent of opaque
    // screen depth. Clamping a column to its centre pixel imprints silhouettes
    // into neighbouring rays and changes entire cells while approaching terrain.
    float air_distance = air_start + air_length * 0.5f;
    float water_distance = water_start + fog_segment_centroid(get_ocean_extinction().g, water_length);
    float water_sample_start = water_start;
    float water_sample_length = water_length;
    // Limit the source sample to the physical water column, not the screen's
    // opaque depth. A far cell can extend beneath the seabed by hundreds of
    // metres; lighting it there makes alternating black coastal rings.
    if (water_length > 0.0f && ray_direction.y < -0.001f)
    {
        float sea_entry = (buffer_frame.ocean_sea_level - get_camera_position().y) / ray_direction.y;
        // Far cells can be wider than the entire optically visible water
        // column. Anchor their representative lighting to the ocean entry,
        // rather than letting the sample jump with the frustum's slice phase.
        // Dense near-camera / underwater cells retain their local sampling.
        bool distant_entry = get_camera_position().y > buffer_frame.ocean_sea_level && sea_entry > fog_detail_far;
        float entry_distance = distant_entry ? sea_entry : water_start;
        float column = distant_entry ? 8.0f / max(get_ocean_extinction().b, 1e-5f) : water_length;
        float3 entry = get_camera_position() + ray_direction * entry_distance;
        float valid;
        float bed = sample_ocean_terrain_height(entry.xz, valid);
        if (valid > 0.5f)
            column = min(column, max(entry.y - bed, 0.0f) / -ray_direction.y);
        water_distance = entry_distance + fog_segment_centroid(get_ocean_extinction().g, column);
        water_sample_start = entry_distance;
        water_sample_length = column;
    }
    float3 air_position = get_camera_position() + ray_direction * air_distance;
    float3 scatter_rate = 0.0f;
    FogSkyProbe sky = fog_build_sky_probe();
    float air_extinction = 0.0f;
    if (air_length > 0.0f)
    {
        // Density is smooth within a cell. Pure air cells reuse the filtered
        // centre sample; interface cells resample the air segment they keep.
        air_extinction = medium.air_extinction;
        if (medium.water >= 0.001f)
        {
            air_extinction = fog_sample_medium(air_position, air_position.y, air_position.y, cell_footprint).air_extinction;
        }
        // Two strata along the ray resolve shadow edges, which carry the shafts.
        // Diffuse skylight is low frequency, evaluate it once per cell.
        float3 positions[2];
        positions[0] = get_camera_position() + ray_direction * (air_start + air_length * 0.25f);
        positions[1] = get_camera_position() + ray_direction * (air_start + air_length * 0.75f);
        float3 sigma_s = (air_extinction * 0.95f).xxx;
        scatter_rate = fog_light_medium(positions, 2u, ray_direction, thread_id, uv, false, sigma_s);
        scatter_rate += fog_ambient_medium(sky, uv, air_distance, air_position, ray_direction, false) * sigma_s;
    }
    medium.air_extinction = air_extinction;
    float3 water_position = get_camera_position() + ray_direction * water_distance;
    float3 water_rate = 0.0f;
    if (water_length > 0.0f)
    {
        float angular_footprint = max(
            length(fog_view_direction(uv + float2(1.0f / float(fog_width), 0.0f)) - ray_direction),
            length(fog_view_direction(uv + float2(0.0f, 1.0f / float(fog_height))) - ray_direction));
        // Two spatial strata resolve variation along nearby cells as well as
        // across their footprint. Distant interface samples retain the stable
        // physical ocean-entry anchor above, including its seabed bound.
        uint water_samples = d1 <= fog_detail_far ? 2u : 1u;
        float3 positions[2];
        [unroll] for (uint j = 0u; j < 2u; ++j)
        {
            float offset = water_samples == 2u ? (float(j) * 2.0f - 1.0f) * 0.288675135f * water_sample_length : 0.0f;
            float d = water_samples == 2u ? clamp(water_distance + offset, water_sample_start, water_sample_start + water_sample_length) : water_distance;
            positions[j] = get_camera_position() + ray_direction * d;
        }
        water_rate = fog_light_medium(positions, water_samples, ray_direction, thread_id, uv, true, get_ocean_scattering(), angular_footprint);
        water_rate += fog_ambient_medium(sky, uv, water_distance, water_position, ray_direction, true) * get_ocean_scattering();
    }
    // History texels represent cells, not their displaced lighting quadrature
    // points. Reproject the grid centre so a stationary camera samples exactly
    // the same texel each frame. Reprojecting the shortened air-segment midpoint
    // repeatedly mixed neighbouring cells, accumulating coastal rings.
    // Interface cells stay current; their split cannot be reprojected as air.
    float density = medium.air_extinction;
    if (pass_get_f3_value().x < 0.5f && density > 0.0f && medium.water < 0.001f)
    {
        float4 previous = mul(float4(sample_pos, 1.0f), get_view_projection_previous_unjittered());
        float previous_distance = length(sample_pos - buffer_frame.camera_position_previous);
        float2 previous_uv = ndc_to_uv(previous.xy / max(previous.w, 1e-5f));
        float previous_u = fog_distance_to_slice(previous_distance);
        if (previous.w > 0.0f && is_valid_uv(previous_uv) && previous_distance < fog_far)
        {
            float4 history = tex3d.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), float3(previous_uv, previous_u), 0.0f);
            float density_change = abs(history.a - density) / max(density, 1e-5f);
            float keep = exp(-max(buffer_frame.delta_time, 0.001f) / 0.18f);
            keep *= 1.0f - smoothstep(0.25f, 0.75f, density_change);
            // Reprojection already accounts for camera motion. Retain small
            // shadow changes to filter grid aliasing; reject real light changes.
            float change = abs(luminance(history.rgb) - luminance(scatter_rate))
                / max(max(luminance(history.rgb), luminance(scatter_rate)), 1e-6f);
            keep *= 1.0f - smoothstep(0.5f, 1.0f, change);
            scatter_rate = lerp(scatter_rate, history.rgb, keep);
        }
    }
    if (pass_get_f3_value().x < 0.5f && medium.water > 0.999f && d1 <= fog_detail_far)
    {
        float4 previous = mul(float4(sample_pos, 1.0f), get_view_projection_previous_unjittered());
        float previous_distance = length(sample_pos - buffer_frame.camera_position_previous);
        float2 previous_uv = ndc_to_uv(previous.xy / max(previous.w, 1e-5f));
        if (previous.w > 0.0f && is_valid_uv(previous_uv) && previous_distance < fog_detail_far)
        {
            float4 history = tex_fog_water_source.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp),
                float3(previous_uv, fog_distance_to_slice(previous_distance)), 0.0f);
            float keep = min(exp(-max(buffer_frame.delta_time, 0.001f) / 0.06f), 0.85f);
            // Alpha is coverage, not luminance. Reject samples straddling air.
            keep *= smoothstep(0.99f, 1.0f, history.a);
            float change = abs(luminance(history.rgb) - luminance(water_rate))
                / max(max(luminance(history.rgb), luminance(water_rate)), 1e-6f);
            keep *= 1.0f - smoothstep(0.6f, 1.0f, change);
            water_rate = lerp(water_rate, history.rgb, keep);
        }
    }
    tex_fog_water_source_uav[thread_id] = float4(water_rate, water_length > 0.0f ? 1.0f : 0.0f);
    tex3d_uav[thread_id] = float4(scatter_rate, density);
    tex_fog_extinction_uav[thread_id] = float4(medium.air_extinction, medium.water, 0.0f, 0.0f);
}

#elif defined(FOG_SKY_VISIBILITY)

// diffuse skylight is low frequency, trace its visibility once per coarse cell
[numthreads(8, 8, 4)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint3 dims;
    tex_fog_sky_visibility_uav.GetDimensions(dims.x, dims.y, dims.z);
    if (any(thread_id >= dims))
        return;
    float visibility = 1.0f;
    #ifdef RAY_TRACING_ENABLED
    if (is_ray_traced_shadows_enabled())
    {
        float2 uv = (float2(thread_id.xy) + 0.5f) / float2(dims.xy);
        float distance = fog_slice_to_distance((float(thread_id.z) + 0.5f) / float(dims.z));
        float3 position = get_camera_position() + fog_view_direction(uv) * distance;
        visibility = fog_trace_visibility(position, float3(0.0f, 1.0f, 0.0f), 10000.0f);
    }
    #endif
    tex_fog_sky_visibility_uav[thread_id] = visibility;
}

#elif defined(FOG_INTEGRATE)

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= fog_width || thread_id.y >= fog_height)
        return;
    float3 transmittance = 1.0f;
    float3 inscatter = 0.0f;
    tex_fog_scattering_uav[uint3(thread_id.xy, 0u)] = 0.0f;
    tex_fog_transmittance_uav[uint3(thread_id.xy, 0u)] = 1.0f;
    // loop invariants, the column direction and water extinction never change along the ray
    bool water_first = fog_view_direction((float2(thread_id.xy) + 0.5f) / float2(fog_width, fog_height)).y > 0.0f;
    float3 water_extinction = get_ocean_extinction();
    float d_start = 0.0f;
    [loop]
    for (uint z = 0u; z < fog_depth; z++)
    {
        float3 scatter = tex_fog_air_source[uint3(thread_id.xy, z)].rgb;
        float2 material = max(tex_fog_extinction[uint3(thread_id.xy, z)].rg, 0.0f);
        float air = material.r;
        float water_fraction = saturate(material.g);
        float3 water_scatter = tex_fog_water_source[uint3(thread_id.xy, z)].rgb;
        float d_end = fog_slice_to_distance(float(z + 1u) / float(fog_depth));
        float dt = d_end - d_start;
        d_start = d_end;
        float3 weight, step_t;
        [unroll] for (uint channel = 0u; channel < 3u; channel++)
        {
            weight[channel] = fog_layered_weight(air, water_extinction[channel], scatter[channel],
                water_scatter[channel], dt, water_fraction, water_first, dt);
            step_t[channel] = fog_layered_transmittance(air, water_extinction[channel], dt,
                water_fraction, water_first, dt);
        }
        inscatter += transmittance * weight;
        transmittance *= step_t;
        tex_fog_scattering_uav[uint3(thread_id.xy, z + 1u)] = float4(inscatter, 0.0f);
        tex_fog_transmittance_uav[uint3(thread_id.xy, z + 1u)] = float4(transmittance, 1.0f);
    }
}

#elif defined(FOG_COMPOSITE)

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    if (any(thread_id.xy >= uint2(resolution)))
        return;
    Surface surface;
    surface.Build(thread_id.xy, resolution, true, false);
    FogTransport volume = sample_fog_volume(render_uv_to_screen_uv(surface.uv), surface.is_sky() ? fog_far : surface.camera_to_pixel_length);
    float4 color = tex_uav[thread_id.xy];
    float mode = pass_get_f3_value().x;
    if (mode < 0.5f)
        color.rgb = color.rgb * volume.transmittance + volume.scattering;
    else if (mode < 1.5f)
        color.rgb = volume.scattering;
    else if (mode < 2.5f)
        color.rgb = volume.transmittance;
    tex_uav[thread_id.xy] = validate_output(color);
}
#endif
