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

#include "common.hlsl"
#include "fog.hlsl"
#include "shadow_mapping.hlsl"
#include "light_cluster.hlsl"

static const uint particle_render_billboard   = 0u;
static const uint particle_render_volumetric  = 1u;
static const uint volume_width                = 160u;
static const uint volume_height               = 90u;
static const uint volume_depth                = 64u;
static const uint volume_voxel_count          = volume_width * volume_height * volume_depth;
static const float volume_max_distance        = 96.0f;
static const float volume_density_scale       = 2048.0f;
static const float volume_color_scale         = 2048.0f;
static const float volume_extinction_scale    = 0.28f;
static const float volume_shadow_min_light    = 0.12f;

uint volume_index(uint3 voxel)
{
    return voxel.x + voxel.y * volume_width + voxel.z * volume_width * volume_height;
}

float henyey_greenstein(float cos_theta, float g)
{
    g = clamp(g, -0.9f, 0.9f);
    float g2 = g * g;
    float d  = max(1.0f + g2 - 2.0f * g * cos_theta, 0.0001f);
    return (1.0f - g2) / (4.0f * PI * d * sqrt(d));
}

float3 volume_world_position(uint3 voxel)
{
    float2 uv = (float2(voxel.xy) + 0.5f) / float2((float)volume_width, (float)volume_height);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 view_far = mul(float4(ndc, 1.0f, 1.0f), get_projection_inverted());
    float3 view_dir = normalize(view_far.xyz / view_far.w);
    float3 world_dir = normalize(mul(float4(view_dir, 0.0f), get_view_inverted()).xyz);
    float distance_camera = ((float)voxel.z + 0.5f) * (volume_max_distance / (float)volume_depth);
    return get_camera_position() + world_dir * distance_camera;
}

Surface build_volume_surface(float3 position, float3 ray_direction, uint2 pixel, float2 uv)
{
    Surface surface;
    surface.flags                  = 0u;
    surface.albedo                 = 1.0f;
    surface.alpha                  = 1.0f;
    surface.roughness              = 1.0f;
    surface.roughness_alpha        = 1.0f;
    surface.metallic               = 0.0f;
    surface.clearcoat              = 0.0f;
    surface.clearcoat_roughness    = 0.0f;
    surface.anisotropic            = 0.0f;
    surface.anisotropic_rotation   = 0.0f;
    surface.sheen                  = 0.0f;
    surface.subsurface_scattering  = 0.0f;
    surface.occlusion              = 1.0f;
    surface.emissive               = 0.0f;
    surface.F0                     = 0.04f;
    surface.pos                    = pixel;
    surface.uv                     = uv;
    surface.depth                  = 0.0f;
    surface.position               = position;
    surface.normal                 = -ray_direction;
    surface.bent_normal            = surface.normal;
    surface.camera_to_pixel        = ray_direction;
    surface.camera_to_pixel_length = distance(position, get_camera_position());
    surface.diffuse_energy         = 1.0f;

    return surface;
}

#if defined(VOLUME_CLEAR)

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint index = dispatch_thread_id.x;
    if (index >= volume_voxel_count)
    {
        return;
    }

    particle_volume_density[index]         = 0u;
    particle_volume_color[index * 3u + 0u] = 0u;
    particle_volume_color[index * 3u + 1u] = 0u;
    particle_volume_color[index * 3u + 2u] = 0u;
}

#elif defined(VOLUME_SPLAT)

float volume_hash(uint3 value, uint seed)
{
    uint h = value.x * 73856093u ^ value.y * 19349663u ^ value.z * 83492791u ^ seed;
    h = (h ^ (h >> 16u)) * 2246822519u;
    h = (h ^ (h >> 13u)) * 3266489917u;
    h = h ^ (h >> 16u);
    return (float)(h & 0x00ffffffu) * (1.0f / 16777215.0f);
}

void splat_voxel(uint3 voxel, Particle particle, EmitterParams emitter, float falloff)
{
    uint index = volume_index(voxel);
    float density = saturate(particle.color.a) * max(emitter.volume_density, 0.0f) * falloff;
    if (density <= 0.0001f)
    {
        return;
    }

    float shadow = lerp(1.0f, exp(-density * 1.25f), emitter.volume_shadowing);
    float3 color = saturate(particle.color.rgb) * shadow;

    uint density_u = (uint)min(density * volume_density_scale, 65535.0f);
    uint3 color_u  = (uint3)min(color * density * volume_color_scale, 65535.0f);

    InterlockedAdd(particle_volume_density[index], density_u);
    InterlockedAdd(particle_volume_color[index * 3u + 0u], color_u.r);
    InterlockedAdd(particle_volume_color[index * 3u + 1u], color_u.g);
    InterlockedAdd(particle_volume_color[index * 3u + 2u], color_u.b);
}

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint index = dispatch_thread_id.x;
    EmitterParams root = particle_emitter[0];
    if (index >= root.max_particles || root.emitter_count == 0u)
    {
        return;
    }

    Particle particle = particle_buffer_a[index];
    if (particle.lifetime <= 0.0f || particle.max_lifetime <= 0.0f || particle.color.a <= 0.0f)
    {
        return;
    }

    uint emitter_index = min(particle.emitter_index, root.emitter_count - 1u);
    EmitterParams emitter = particle_emitter[emitter_index];
    if (emitter.render_mode != particle_render_volumetric)
    {
        return;
    }

    float4 clip = mul(float4(particle.position, 1.0f), buffer_frame.view_projection);
    if (clip.w <= 0.0f)
    {
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f)
    {
        return;
    }

    float distance_camera = distance(particle.position, get_camera_position());
    if (distance_camera <= 0.01f || distance_camera >= volume_max_distance)
    {
        return;
    }

    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float3 center = float3(uv * float2(volume_width, volume_height), saturate(distance_camera / volume_max_distance) * volume_depth);
    uint particle_seed = asuint(particle.max_lifetime) ^ (asuint(particle.start_size) * 1664525u) ^ (emitter_index * 1013904223u);
    float radius_xy = clamp(particle.size / max(distance_camera, 0.25f) * volume_height * 1.55f, 1.0f, 7.0f);
    float radius_z  = clamp(particle.size / volume_max_distance * volume_depth * 2.0f, 1.0f, 4.5f);
    float radius_x  = radius_xy * lerp(0.72f, 1.08f, volume_hash(uint3(3u, 17u, 41u), particle_seed));
    float radius_y  = radius_xy * lerp(0.65f, 1.00f, volume_hash(uint3(5u, 23u, 59u), particle_seed));
    float radius_d  = radius_z  * lerp(0.78f, 1.16f, volume_hash(uint3(7u, 29u, 67u), particle_seed));
    int3 radius = int3((int)ceil(max(radius_x, radius_y)), (int)ceil(max(radius_x, radius_y)), (int)ceil(radius_d));
    int3 c = int3(floor(center));

    [loop]
    for (int z = -radius.z; z <= radius.z; z++)
    {
        [loop]
        for (int y = -radius.y; y <= radius.y; y++)
        {
            [loop]
            for (int x = -radius.x; x <= radius.x; x++)
            {
                int3 v = c + int3(x, y, z);
                if (any(v < 0) || v.x >= (int)volume_width || v.y >= (int)volume_height || v.z >= (int)volume_depth)
                {
                    continue;
                }

                float3 voxel_center = float3(v) + 0.5f;
                float3 delta = float3((voxel_center.x - center.x) / max(radius_x, 0.001f),
                                      (voxel_center.y - center.y) / max(radius_y, 0.001f),
                                      (voxel_center.z - center.z) / max(radius_d, 0.001f));
                float dist_sq = dot(delta, delta);
                uint3 voxel_u = (uint3)v;
                float coarse_noise = volume_hash(voxel_u / 2u, particle_seed);
                float edge = smoothstep(0.12f, 0.95f, dist_sq);
                dist_sq += (coarse_noise - 0.5f) * 0.42f * edge;
                if (dist_sq >= 1.0f)
                {
                    continue;
                }

                float fine_noise = volume_hash(voxel_u, particle_seed ^ 0x9e3779b9u);
                float falloff = (1.0f - dist_sq) * (1.0f - dist_sq);
                falloff *= lerp(0.55f, 1.18f, fine_noise);
                splat_voxel((uint3)v, particle, emitter, falloff);
            }
        }
    }
}

#elif defined(VOLUME_RESOLVE)

#ifdef RAY_TRACING_ENABLED
float trace_volume_shadow_ray(Light light, float3 sample_pos, float3 light_dir)
{
    float bias = 0.015f;
    float3 origin = sample_pos + light_dir * bias;
    float3 direction;
    float t_max;

    if (light.is_directional())
    {
        direction = normalize(-light.forward);
        t_max = 10000.0f;
    }
    else
    {
        float3 target = light.is_area() ? light.compute_closest_point_on_area(sample_pos) : light.position;
        float3 to_light = target - origin;
        float dist = length(to_light);
        if (dist <= bias)
        {
            return 1.0f;
        }

        direction = to_light / dist;
        t_max = max(dist - bias, 0.001f);
    }

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001f;
    ray.TMax      = t_max;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
}
#endif

[numthreads(8, 8, 4)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= volume_width || dispatch_thread_id.y >= volume_height || dispatch_thread_id.z >= volume_depth)
    {
        return;
    }

    uint index = volume_index(dispatch_thread_id);
    uint density_u = particle_volume_density[index];
    float density = saturate((float)density_u / volume_density_scale);
    float3 color = 0.0f;
    if (density_u > 0u)
    {
        color.r = (float)particle_volume_color[index * 3u + 0u] / ((float)density_u * volume_color_scale / volume_density_scale);
        color.g = (float)particle_volume_color[index * 3u + 1u] / ((float)density_u * volume_color_scale / volume_density_scale);
        color.b = (float)particle_volume_color[index * 3u + 2u] / ((float)density_u * volume_color_scale / volume_density_scale);

    #ifdef RAY_TRACING_ENABLED
        if ((is_ray_traced_shadows_enabled() || is_restir_pt_enabled()) && buffer_frame.cluster_light_count > 0u)
        {
            float3 sample_pos = volume_world_position(dispatch_thread_id);
            float2 uv = (float2(dispatch_thread_id.xy) + 0.5f) / float2((float)volume_width, (float)volume_height);
            float3 ray_direction = normalize(sample_pos - get_camera_position());
            Surface surface = build_volume_surface(sample_pos, ray_direction, dispatch_thread_id.xy, uv);

            Light light;
            light.Build(0u, surface);
            if (light.has_shadows())
            {
                float3 light_dir;
                float local_atten;
                compute_volumetric_light_sample(light, sample_pos, light_dir, local_atten);
                if (local_atten > 0.0f)
                {
                    float visibility = trace_volume_shadow_ray(light, sample_pos, light_dir);
                    color *= lerp(volume_shadow_min_light, 1.0f, visibility);
                }
            }
        }
    #endif
    }

    tex3d_uav[dispatch_thread_id] = float4(saturate(color), density);
}

#elif defined(VOLUME_COMPOSITE)

float3 evaluate_volume_light(uint light_index, float3 sample_pos, float3 ray_direction, uint2 pixel, float2 uv)
{
    Surface surface = build_volume_surface(sample_pos, ray_direction, pixel, uv);

    Light light;
    light.Build(light_index, surface);

    float3 light_dir;
    float local_atten;
    compute_volumetric_light_sample(light, sample_pos, light_dir, local_atten);
    if (local_atten <= 0.0f)
    {
        return 0.0f;
    }

    surface.normal = light_dir;
    surface.bent_normal = light_dir;

    float visibility = 1.0f;
    if (light.has_shadows())
    {
    #ifdef RAY_TRACING_ENABLED
        if (is_ray_traced_shadows_enabled() || is_restir_pt_enabled())
        {
            visibility = 1.0f;
        }
        else
    #endif
        {
            visibility = compute_shadow(surface, light);
        }
    }
    float phase      = henyey_greenstein(dot(ray_direction, light_dir), light.is_directional() ? 0.22f : 0.45f);

    return light.color * light.intensity * local_atten * visibility * phase;
}

float3 evaluate_volume_lighting(float3 sample_pos, float3 ray_direction, uint2 pixel, float2 uv)
{
    float3 lighting = get_sun_color() * 0.025f;
    uint total_lights = buffer_frame.cluster_light_count;

    if (total_lights > 0u)
    {
        lighting += evaluate_volume_light(0u, sample_pos, ray_direction, pixel, uv);
    }

    if (total_lights > 1u)
    {
        float view_z = mul(float4(sample_pos, 1.0f), get_view()).z;
        uint3 cid    = cluster_id_from_screen(uv, view_z);
        uint flat_id = cluster_flat(cid);
        uint2 range  = cluster_light_grid[flat_id];

        [loop]
        for (uint k = 0u; k < range.y; k++)
        {
            uint light_index = cluster_light_indices[range.x + k];
            lighting += evaluate_volume_light(light_index, sample_pos, ray_direction, pixel, uv);
        }
    }

    return lighting;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_thread_id.xy;
    if (pixel.x >= (uint)buffer_frame.resolution_render.x || pixel.y >= (uint)buffer_frame.resolution_render.y)
    {
        return;
    }

    float2 uv = (float2(pixel) + 0.5f) / buffer_frame.resolution_render;
    float depth_raw = tex_depth.Load(int3(pixel, 0)).r;
    float scene_distance = min(linearize_depth(depth_raw), volume_max_distance);
    if (scene_distance <= 0.05f)
    {
        return;
    }

    const uint step_count = 48u;
    float step_length = scene_distance / (float)step_count;
    float transmittance = 1.0f;
    float3 scattering = 0.0f;
    float3 scene_pos = get_position(depth_raw, uv);
    float3 ray_direction = normalize(scene_pos - get_camera_position());

    [loop]
    for (uint i = 0u; i < step_count; i++)
    {
        float slice = ((float)i + 0.5f) / (float)step_count;
        float distance_sample = slice * scene_distance;
        float3 volume_uv = float3(uv, saturate(distance_sample / volume_max_distance));
        float4 sample = tex3d.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), volume_uv, 0);
        float density = sample.a;
        if (density > 0.0001f)
        {
            float optical_depth = density * step_length * volume_extinction_scale;
            float opacity = 1.0f - exp(-optical_depth);
            float3 sample_pos = get_camera_position() + ray_direction * distance_sample;
            float3 lighting = evaluate_volume_lighting(sample_pos, ray_direction, pixel, uv);
            float3 color = sample.rgb * lighting;
            scattering += transmittance * opacity * color;
            transmittance *= exp(-optical_depth);
            if (transmittance < 0.01f)
            {
                break;
            }
        }
    }

    if (any(scattering > 0.0f) || transmittance < 0.999f)
    {
        float4 frame = tex_uav[pixel];
        frame.rgb = frame.rgb * transmittance + scattering;
        tex_uav[pixel] = frame;
    }
}

#endif
