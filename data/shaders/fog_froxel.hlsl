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

float3 fog_sky_ambient(float3 sample_pos, float3 ray_direction)
{
    const uint sky_mip = 7;
    float3 light_dir = float3(0.0f, 1.0f, 0.0f);
    bool has_sun = buffer_frame.cluster_light_count > 0u &&
        (light_parameters[0].flags & (1u << 0)) != 0u;
    if (has_sun)
    {
        light_dir = normalize(-light_parameters[0].direction);
    }

    float3 sun_sample_dir = float3(light_dir.x, max(light_dir.y, 0.0f), light_dir.z);
    sun_sample_dir = dot(sun_sample_dir, sun_sample_dir) > 1e-8f
        ? normalize(sun_sample_dir) : float3(0.0f, 1.0f, 0.0f);
    float3 sky_sun = tex.SampleLevel(
        GET_SAMPLER(sampler_trilinear_clamp),
        direction_sphere_uv(sun_sample_dir),
        sky_mip
    ).rgb;
    float3 sky_zenith = tex.SampleLevel(
        GET_SAMPLER(sampler_trilinear_clamp),
        direction_sphere_uv(float3(0.0f, 1.0f, 0.0f)),
        sky_mip
    ).rgb;

    const float sky_color_max = 50.0f;
    sky_sun    = min(sky_sun, sky_color_max);
    sky_zenith = min(sky_zenith, sky_color_max);

    float sun_lobe = pow(saturate(dot(ray_direction, light_dir)), 4.0f);

    // These low-frequency sky probes also need visibility. Without it every enclosed
    // room receives blue outdoor inscattering, which auto exposure then amplifies.
    // Keep directions deterministic so stationary fog does not introduce sampling noise.
    #ifdef RAY_TRACING_ENABLED
    if (is_ray_traced_shadows_enabled())
    {
        float sky_visibility = fog_trace_visibility(sample_pos, float3(0.0f, 1.0f, 0.0f), 10000.0f);
        sky_zenith *= sky_visibility;
        sky_sun *= sky_visibility;
    }
    #endif
    return lerp(sky_zenith, sky_sun, sun_lobe);
}

float3 fog_evaluate_light(
    uint light_index,
    float3 sample_pos,
    float3 ray_direction,
    uint3 thread_id,
    float2 uv,
    bool in_water,
    float3 sigma_s
)
{
    uint2 pixel = thread_id.xy;
    Surface surface = fog_build_surface(sample_pos, ray_direction, pixel, uv);
    Light light;
    light.Build(light_index, surface);
    if (!light.is_volumetric())
    {
        return 0.0f;
    }

    float3 light_dir;
    float local_atten;
    compute_volumetric_light_sample(light, sample_pos, light_dir, local_atten);
    if (local_atten <= 0.0f)
    {
        return 0.0f;
    }

    float visibility = 1.0f;
    if (light.has_shadows())
    {
    #ifdef RAY_TRACING_ENABLED
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

    float3 tint = 1.0f;
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
            tint = get_ocean_sun_transmission(sample_pos, light_dir);
            light_dir = -refract(-light_dir, float3(0.0f, 1.0f, 0.0f), 1.0f / 1.333f);
        }
    }

    float phase_g = in_water ? 0.72f : (light.is_directional() ? 0.4f : 0.6f);
    float phase = henyey_greenstein_phase(dot(ray_direction, light_dir), phase_g);
    return light.color * light.intensity * local_atten * visibility * phase * tint * sigma_s;
}

float3 fog_light_medium(float3 sample_pos, float3 ray_direction, uint3 thread_id, float2 uv, bool in_water, float3 sigma_s)
{
    if (!any(sigma_s > 0.0f)) return 0.0f;
    float3 ambient = fog_sky_ambient(sample_pos, ray_direction);
    if (in_water)
    {
        float depth = max(get_ocean_height(sample_pos.xz) - sample_pos.y, 0.0f);
        ambient *= exp(-get_ocean_extinction() * depth);
    }
    float3 rate = ambient * sigma_s;
    if (buffer_frame.cluster_light_count > 0u)
        rate += fog_evaluate_light(0u, sample_pos, ray_direction, thread_id, uv, in_water, sigma_s);
    [loop]
    for (uint k = 0u; k < buffer_frame.volumetric_light_count; k++)
        rate += fog_evaluate_light(volumetric_light_indices[k], sample_pos, ray_direction, thread_id, uv, in_water, sigma_s);
    return rate;
}

// One density/lighting injection for both air and water. No camera-medium switch.
[numthreads(8, 8, 4)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(thread_id >= uint3(fog_width, fog_height, fog_depth)))
        return;

    float2 uv = (float2(thread_id.xy) + 0.5f) / float2(fog_width, fog_height);
    float3 sample_pos = fog_froxel_world(float3(thread_id));
    float3 ray_direction = fog_view_direction(uv);
    float d0 = fog_slice_to_distance(float(thread_id.z) / float(fog_depth));
    float d1 = fog_slice_to_distance(float(thread_id.z + 1u) / float(fog_depth));
    // Nothing behind the farthest visible opaque depth in this tile can affect
    // its transport. Include the complete boundary cell and neighbouring rays
    // so silhouette edges and distorted water samples retain their media.
    float visible_distance = 0.0f;
    float opaque_distance = fog_far;
    [unroll]
    for (uint tap = 0u; tap < 5u; tap++)
    {
        float2 offset = tap == 4u ? 0.0f : float2((tap & 1u) ? 1.0f : -1.0f, (tap & 2u) ? 1.0f : -1.0f);
        float2 sample_uv = saturate(uv + offset / float2(fog_width, fog_height));
        float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), sample_uv, 0.0f).r;
        float distance_to_opaque = depth == 0.0f ? fog_far : length(get_position(depth, sample_uv) - get_camera_position());
        visible_distance = max(visible_distance, distance_to_opaque);
        if (tap == 4u) opaque_distance = distance_to_opaque;
    }
    if (d0 > visible_distance + (d1 - d0))
    {
        tex3d_uav[thread_id] = 0.0f;
        tex_fog_extinction_uav[thread_id] = 0.0f;
        tex_fog_water_source_uav[thread_id] = 0.0f;
        return;
    }
    FogMedium medium = fog_sample_medium(sample_pos, get_camera_position().y + ray_direction.y * d0, get_camera_position().y + ray_direction.y * d1);
    // Preserve both source terms in interface cells. Lighting the entire cell
    // from its submerged centre makes the air above distant water alternate dark/light.
    float water_length = (d1 - d0) * medium.water;
    float air_length = (d1 - d0) - water_length;
    bool water_first = ray_direction.y > 0.0f;
    float air_start = d0 + (water_first ? water_length : 0.0f);
    float water_start = d0 + (water_first ? 0.0f : air_length);
    // Sample the visible part of a boundary cell. A midpoint behind a mountain
    // or the seabed would incorrectly shadow the fog in front of that surface.
    float air_visible = clamp(opaque_distance - air_start, 0.0f, air_length);
    float water_visible = clamp(opaque_distance - water_start, 0.0f, water_length);
    float air_distance = air_start + air_visible * 0.5f;
    float water_distance = water_start + fog_segment_centroid(get_ocean_extinction().g, water_visible);
    float3 air_position = get_camera_position() + ray_direction * air_distance;
    medium.air_extinction = air_length > 0.0f ? fog_sample_medium(air_position, air_position.y, air_position.y).air_extinction : 0.0f;
    float3 scatter_rate = air_length > 0.0f
        ? fog_light_medium(get_camera_position() + ray_direction * air_distance, ray_direction,
            thread_id, uv, false, (medium.air_extinction * 0.95f).xxx) : 0.0f;
    float3 water_rate = water_length > 0.0f
        ? fog_light_medium(get_camera_position() + ray_direction * water_distance, ray_direction,
            thread_id, uv, true, get_ocean_scattering()) : 0.0f;
    tex_fog_water_source_uav[thread_id] = float4(water_rate, 0.0f);
    bool in_water = medium.water > 0.5f;

    // Extinction always belongs to this frame. History filters lighting only,
    // with signed optical density identifying water/air changes at moving waves.
    float density = dot(lerp(medium.air_extinction.xxx, get_ocean_extinction(), medium.water), float3(0.2126f, 0.7152f, 0.0722f));
    float metadata = in_water ? -density : density;
    if (pass_get_f3_value().x < 0.5f && density > 0.0f)
    {
        float4 previous = mul(float4(sample_pos, 1.0f), get_view_projection_previous_unjittered());
        float previous_distance = length(sample_pos - buffer_frame.camera_position_previous);
        float2 previous_uv = ndc_to_uv(previous.xy / max(previous.w, 1e-5f));
        float previous_u = fog_distance_to_slice(previous_distance);
        if (previous.w > 0.0f && is_valid_uv(previous_uv) && previous_distance < fog_far)
        {
            float4 history = tex3d.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), float3(previous_uv, previous_u), 0.0f);
            float density_change = abs(history.a - metadata) / max(density, 1e-5f);
            float motion = length((previous_uv - uv) * float2(fog_width, fog_height));
            float keep = exp(-max(buffer_frame.delta_time, 0.001f) / (in_water ? 0.012f : 0.12f));
            keep *= saturate(1.0f - density_change * 4.0f) * exp(-motion * 0.35f);
            // Reject newly lit/shadowed cells instead of dragging a beam behind a light.
            float change = abs(luminance(history.rgb) - luminance(scatter_rate))
                / max(max(luminance(history.rgb), luminance(scatter_rate)), 1e-6f);
            keep *= saturate(1.0f - change);
            scatter_rate = lerp(scatter_rate, history.rgb, keep);
        }
    }
    tex3d_uav[thread_id] = float4(scatter_rate, metadata);
    tex_fog_extinction_uav[thread_id] = float4(medium.air_extinction, medium.water, 0.0f, 0.0f);
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
    [loop]
    for (uint z = 0u; z < fog_depth; z++)
    {
        float3 scatter = tex_fog_air_source[uint3(thread_id.xy, z)].rgb;
        float2 material = max(tex_fog_extinction[uint3(thread_id.xy, z)].rg, 0.0f);
        float air = material.r;
        float water_fraction = saturate(material.g);
        float3 water_extinction = get_ocean_extinction();
        float3 water_scatter = tex_fog_water_source[uint3(thread_id.xy, z)].rgb;
        bool water_first = fog_view_direction((float2(thread_id.xy) + 0.5f) / float2(fog_width, fog_height)).y > 0.0f;
        float dt = fog_slice_to_distance(float(z + 1u) / float(fog_depth))
                 - fog_slice_to_distance(float(z) / float(fog_depth));
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
    FogTransport volume = sample_fog_volume(surface.uv, surface.is_sky() ? fog_far : surface.camera_to_pixel_length);
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
