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

//= INCLUDES =========
#include "common.hlsl"
#ifdef RENDER
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "light_cluster.hlsl"
#endif
//====================

static const uint particle_blend_alpha         = 0u;
static const uint particle_blend_premultiplied = 1u;
static const uint particle_blend_additive      = 2u;
static const uint particle_lighting_lit        = 0u;
static const uint particle_lighting_unlit      = 1u;
static const uint particle_lighting_emissive   = 2u;

// xorshift-based rng
float rng(uint seed)
{
    seed = seed * 747796405u + 2891336453u;
    seed = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    seed = (seed >> 22u) ^ seed;
    return float(seed) / 4294967295.0;
}

float3 random_direction(uint seed)
{
    float z     = rng(seed) * 2.0 - 1.0;
    float theta = rng(seed * 16807u + 1u) * 6.28318530718;
    float r     = sqrt(max(0.0, 1.0 - z * z));
    return float3(r * cos(theta), r * sin(theta), z);
}

float3 random_in_sphere(uint seed)
{
    float3 dir = random_direction(seed);
    float  t   = pow(rng(seed * 48271u + 3u), 1.0 / 3.0);
    return dir * t;
}

float3 safe_normalize(float3 value, float3 fallback)
{
    float len_sq = dot(value, value);
    if (len_sq <= 0.000001)
    {
        return fallback;
    }

    return value * rsqrt(len_sq);
}

float3 random_in_cone(uint seed, float3 axis, float cone_angle)
{
    axis = safe_normalize(axis, float3(0.0, 1.0, 0.0));

    float cos_angle = cos(saturate(cone_angle / 3.14159265) * 3.14159265);
    float cos_theta = lerp(1.0, cos_angle, rng(seed * 69621u + 5u));
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    float phi       = rng(seed * 31337u + 11u) * 6.28318530718;

    float3 up      = abs(axis.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = safe_normalize(cross(up, axis), float3(1.0, 0.0, 0.0));
    float3 bitan   = cross(axis, tangent);

    return safe_normalize(axis * cos_theta + (tangent * cos(phi) + bitan * sin(phi)) * sin_theta, axis);
}

#ifdef EMIT

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint emitter_index    = (uint)pass_get_f3_value().x;
    EmitterParams emitter = particle_emitter[emitter_index];
    if (emitter.emitter_count == 0 || emitter.range_count == 0)
    {
        return;
    }

    uint emit_count = emitter.emit_count;
    if (dispatch_thread_id.x >= emit_count)
    {
        return;
    }

    // allocate a slot inside this emitter range
    uint raw_slot;
    InterlockedAdd(particle_counter[emitter_index], 1, raw_slot);
    uint slot = emitter.range_start + (raw_slot % emitter.range_count);

    // high-entropy seed from frame, thread id, and raw slot
    uint seed = dispatch_thread_id.x * 7919u + emitter.frame * 104729u + raw_slot * 2654435761u;

    // random position within emission sphere
    float3 offset = random_in_sphere(seed) * emitter.radius;

    // bias the launch upward and blend toward the emitter direction when requested
    float3 dir_random = random_direction(seed + 277803737u);
    dir_random.y      = abs(dir_random.y) * 0.7 + 0.3;
    dir_random        = safe_normalize(dir_random, float3(0.0, 1.0, 0.0));

    float3 dir_cone = random_in_cone(seed + 97127u, emitter.emission_direction, emitter.emission_cone_angle);
    float3 dir      = safe_normalize(lerp(dir_random, dir_cone, saturate(emitter.directional_blend)), dir_random);

    // per-particle jitter so no two puffs share size, lifetime or speed, this breaks up the uniform blob look
    float r_size  = 0.6 + 0.8 * rng(seed + 9001u);
    float r_life  = 0.7 + 0.6 * rng(seed + 33u);
    float r_speed = 0.5 + 1.0 * rng(seed + 7u);

    Particle p;
    p.position     = emitter.position + offset;
    p.lifetime     = emitter.lifetime * r_life;
    p.velocity     = dir * emitter.start_speed * r_speed + emitter.emitter_velocity * emitter.velocity_inheritance;
    p.max_lifetime = p.lifetime;
    p.color         = emitter.start_color;
    p.size          = emitter.start_size * r_size;
    p.emitter_index = emitter_index;
    p.start_size    = emitter.start_size * r_size;
    p.end_size      = emitter.end_size * r_size;

    particle_buffer_a[slot] = p;
}

#elif defined(SIMULATE)

static const float collision_restitution = 0.3;  // velocity retained after bounce
static const float collision_offset      = 0.05; // push off surface, must exceed depth buffer precision to prevent re-triggering

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    EmitterParams emitter = particle_emitter[0];
    if (emitter.emitter_count == 0)
        return;

    uint index = dispatch_thread_id.x;
    if (index >= emitter.max_particles)
        return;

    Particle p = particle_buffer_a[index];

    // skip dead or uninitialized particles
    if (p.lifetime <= 0.0 || p.max_lifetime <= 0.0)
        return;

    // pull the params of the emitter that spawned this particle, clamp guards against
    // uninitialized slots whose emitter_index is garbage and would read out of bounds
    uint ei = min(p.emitter_index, emitter.emitter_count - 1);
    emitter = particle_emitter[ei];

    float dt = emitter.delta_time;

    // integrate gravity
    p.velocity.y += emitter.gravity_modifier * 9.81 * dt;

    // air drag, smoke loses its launch momentum quickly and settles instead of flying in a straight line
    p.velocity *= saturate(1.0 - emitter.drag * dt);

    // curl-like turbulence so the plume swirls and billows organically rather than expanding as a clean ball
    float  ts   = (float)buffer_frame.time;
    float3 tp   = p.position * 1.5;
    float3 turb = float3(sin(tp.y + ts * 1.3) + cos(tp.z * 0.7 - ts),
                         sin(tp.z + ts * 1.1) + cos(tp.x * 0.8 + ts),
                         sin(tp.x + ts * 0.9) + cos(tp.y * 0.6 - ts));
    p.velocity += turb * emitter.turbulence_strength * dt;
    p.velocity += buffer_frame.wind * emitter.wind_influence * dt;

    // predict next position
    float3 new_pos = p.position + p.velocity * dt;

    // depth buffer collision - project the future position to screen space
    // and check if it would penetrate nearby scene geometry
    float4 clip_new = mul(float4(new_pos, 1.0), buffer_frame.view_projection);
    if (clip_new.w > 0.0)
    {
        float3 ndc_new = clip_new.xyz / clip_new.w;
        float2 uv_new  = ndc_new.xy * float2(0.5, -0.5) + 0.5;

        // only test if the projected position is on screen
        if (uv_new.x > 0.0 && uv_new.x < 1.0 && uv_new.y > 0.0 && uv_new.y < 1.0)
        {
            int2 pixel = int2(uv_new * buffer_frame.resolution_render);

            float scene_depth_raw = tex_depth.Load(int3(pixel, 0)).r;
            if (scene_depth_raw > 0.0) // skip sky (no geometry)
            {
                float linear_particle = linearize_depth(ndc_new.z);
                float linear_scene    = linearize_depth(scene_depth_raw);
                float penetration     = linear_particle - linear_scene;

                // the maximum distance a particle can travel in one frame, plus a small margin;
                // if penetration exceeds this, the surface is too far away to be a real collision
                // (it's just an occluder between the camera and the particle)
                float collision_thickness = length(p.velocity) * dt + 0.5;

                if (penetration > 0.0 && penetration < collision_thickness)
                {
                    float3 surface_normal = get_normal(pixel);
                    float normal_length   = length(surface_normal);

                    if (normal_length > 0.1) // valid normal
                    {
                        surface_normal /= normal_length;

                        // reconstruct the world-space surface position
                        float3 surface_pos = get_position(scene_depth_raw, uv_new);

                        // reflect velocity off the surface and dampen
                        p.velocity = reflect(p.velocity, surface_normal) * collision_restitution;

                        // place particle at the surface with a small offset to prevent re-penetration
                        new_pos = surface_pos + surface_normal * collision_offset;
                    }
                }
            }
        }
    }

    // commit the new position
    p.position = new_pos;

    // age the particle
    p.lifetime -= dt;
    if (p.lifetime <= 0.0)
    {
        p.lifetime = 0.0;
        particle_buffer_a[index] = p;
        return;
    }

    // normalized age: 0 = just born, 1 = about to die
    float t = 1.0 - saturate(p.lifetime / p.max_lifetime);

    // interpolate color over lifetime, size grows with an ease-out curve so the puff expands fast at birth
    // then settles, size uses the particle's own birth values so emitter changes never resize live particles
    float te = 1.0 - (1.0 - t) * (1.0 - t);
    p.color  = lerp(emitter.start_color, emitter.end_color, t);
    p.size   = lerp(p.start_size, p.end_size, te);

    particle_buffer_a[index] = p;
}

#elif defined(RENDER)

// hardware rasterized billboards, the rop blends overlapping splats atomically so no two particles can
// race on the same pixel the way a compute read modify write does, this is what kills the checkerboard

struct ps_input
{
    float4 position       : SV_Position;
    float2 local          : TEXCOORD0; // quad local coords in the minus one to one range
    float3 color          : TEXCOORD1;
    float  alpha          : TEXCOORD2;
    float  lin_depth      : TEXCOORD3; // particle center linear depth
    float2 rot            : TEXCOORD4; // cos, sin of the per particle rotation
    float  use_tex        : TEXCOORD5;
    float3 position_world : TEXCOORD6;
    float  age_t          : TEXCOORD7;
    float4 render_params  : TEXCOORD8; // blend mode, lighting mode, emissive strength, soft depth
    float4 flipbook       : TEXCOORD9; // rows, columns, fps, random seed
};

ps_input main_vs(uint vertex_id : SV_VertexID)
{
    uint  emitter_index = (uint)pass_get_f3_value().x;
    float use_texture   = pass_get_f3_value().y;
    EmitterParams emitter = particle_emitter[emitter_index];

    uint index  = emitter.range_start + vertex_id / 6;
    uint corner = vertex_id % 6;

    ps_input o;
    // default to a degenerate off screen vertex so culled particles rasterize nothing
    o.position  = float4(2.0, 2.0, 2.0, 1.0);
    o.local     = 0.0;
    o.color     = 0.0;
    o.alpha     = 0.0;
    o.lin_depth = 0.0;
    o.rot       = float2(1.0, 0.0);
    o.use_tex   = use_texture;
    o.position_world = 0.0;
    o.age_t     = 0.0;
    o.render_params = float4((float)emitter.blend_mode, (float)emitter.lighting_mode, emitter.emissive_strength, emitter.soft_depth_scale);
    o.flipbook  = float4((float)emitter.flipbook_rows, (float)emitter.flipbook_columns, emitter.flipbook_fps, 0.0);

    Particle p = particle_buffer_a[index];

    // skip dead, invisible or foreign emitter particles
    if (p.lifetime <= 0.0 || p.max_lifetime <= 0.0 || p.color.a <= 0.0 || p.emitter_index != emitter_index)
    {
        return o;
    }

    // two triangles, corners laid out as a quad in the minus one to one range
    float2 quad[6] =
    {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0),
        float2(-1.0,  1.0), float2(1.0, -1.0), float2( 1.0, 1.0)
    };
    float2 c = quad[corner];

    // screen aligned billboard basis, optionally stretched along the authored plume direction
    float3 right = safe_normalize(buffer_frame.camera_right, float3(1.0, 0.0, 0.0));
    float3 up    = safe_normalize(cross(buffer_frame.camera_forward, right), float3(0.0, 1.0, 0.0));
    float3 flow_velocity = p.velocity - emitter.emitter_velocity * emitter.velocity_inheritance;
    float  velocity_len  = length(flow_velocity);
    float3 flow_axis     = safe_normalize(lerp(emitter.emission_direction, flow_velocity, 0.35), emitter.emission_direction);
    float3 velocity_axis = flow_axis - buffer_frame.camera_forward * dot(flow_axis, buffer_frame.camera_forward);
    velocity_axis = safe_normalize(velocity_axis, right);
    float stretch = saturate(velocity_len * 0.08) * emitter.velocity_stretch;
    right = safe_normalize(lerp(right, velocity_axis, saturate(stretch)), right);
    up    = safe_normalize(cross(buffer_frame.camera_forward, right), up);
    float half_size_x = p.size * (0.5 + stretch * 1.5);
    float half_size_y = p.size * (0.5 - saturate(stretch) * 0.2);
    float3 world = p.position + right * c.x * half_size_x + up * c.y * half_size_y;

    float4 clip   = mul(float4(world, 1.0),      buffer_frame.view_projection);
    float4 clip_c = mul(float4(p.position, 1.0), buffer_frame.view_projection);

    // age driven fade in, rotation and spin, matches the old compute look
    float age_t    = 1.0 - saturate(p.lifetime / p.max_lifetime);
    float fade_in  = saturate(age_t / 0.2);
    float base_ang = rng(index * 2654435761u) * 6.28318530718;
    float spin     = (rng(index * 40503u + 13u) * 2.0 - 1.0) * 1.2;
    float ang      = base_ang + spin * age_t;

    o.position  = clip;
    o.local     = c;
    o.color     = p.color.rgb;
    o.alpha     = p.color.a * fade_in;
    o.lin_depth = linearize_depth(clip_c.z / max(clip_c.w, 1e-6));
    o.rot       = float2(cos(ang), sin(ang));
    o.use_tex   = use_texture;
    o.position_world = p.position;
    o.age_t     = age_t;
    o.render_params = float4((float)emitter.blend_mode, (float)emitter.lighting_mode, emitter.emissive_strength, emitter.soft_depth_scale);
    o.flipbook  = float4((float)emitter.flipbook_rows, (float)emitter.flipbook_columns, emitter.flipbook_fps, rng(index * 1664525u + 1013904223u));
    return o;
}

float3 get_particle_normal(float2 local)
{
    float z      = sqrt(saturate(1.0 - dot(local, local)));
    float3 right = safe_normalize(buffer_frame.camera_right, float3(1.0, 0.0, 0.0));
    float3 up    = safe_normalize(cross(buffer_frame.camera_forward, right), float3(0.0, 1.0, 0.0));

    return safe_normalize(right * local.x + up * local.y - buffer_frame.camera_forward * z, -buffer_frame.camera_forward);
}

Surface build_particle_surface(float3 position_world, float3 albedo, uint2 pixel, float2 local)
{
    Surface surface;
    surface.flags                  = 0;
    surface.albedo                 = albedo;
    surface.alpha                  = 1.0;
    surface.roughness              = 0.9;
    surface.roughness_alpha        = surface.roughness * surface.roughness;
    surface.metallic               = 0.0;
    surface.clearcoat              = 0.0;
    surface.clearcoat_roughness    = 0.0;
    surface.anisotropic            = 0.0;
    surface.anisotropic_rotation   = 0.0;
    surface.sheen                  = 0.0;
    surface.subsurface_scattering  = 0.0;
    surface.occlusion              = 1.0;
    surface.emissive               = 0.0;
    surface.F0                     = 0.04;
    surface.pos                    = pixel;
    surface.uv                     = (float2(pixel) + 0.5) / buffer_frame.resolution_render;
    surface.depth                  = 0.0;
    surface.position               = position_world;
    surface.camera_to_pixel        = position_world - get_camera_position();
    surface.camera_to_pixel_length = length(surface.camera_to_pixel);
    surface.camera_to_pixel        = safe_normalize(surface.camera_to_pixel, buffer_frame.camera_forward);
    surface.normal                 = get_particle_normal(local);
    surface.bent_normal            = surface.normal;
    surface.diffuse_energy         = 1.0;

    return surface;
}

#ifdef RAY_TRACING_ENABLED
float trace_particle_shadow_ray(Light light, Surface surface)
{
    if (!is_ray_traced_shadows_enabled())
    {
        return 1.0;
    }

    float bias    = 0.005 + surface.camera_to_pixel_length * 0.0001;
    float3 origin = surface.position + surface.normal * bias;
    float3 direction;
    float t_max;

    if (light.is_directional())
    {
        direction = normalize(-light.forward);
        t_max     = 10000.0;
    }
    else
    {
        float3 target = light.is_area() ? light.compute_closest_point_on_area(surface.position) : light.position;
        float3 to_light = target - origin;
        float dist = length(to_light);
        if (dist <= 0.0001)
        {
            return 1.0;
        }

        direction = to_light / dist;
        t_max     = max(dist - bias * 2.0, 0.001);
    }

    if (dot(surface.normal, direction) <= 0.0)
    {
        return 1.0;
    }

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001;
    ray.TMax      = t_max;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;
}
#endif

float3 evaluate_particle_light(uint light_index, uint2 pixel, Surface surface)
{
    Light light;
    light.Build(light_index, surface);

    if (light.has_shadows())
    {
        float shadow = 1.0;
    #ifdef RAY_TRACING_ENABLED
        if (is_ray_traced_shadows_enabled())
        {
            shadow = trace_particle_shadow_ray(light, surface);
        }
        else
    #endif
        {
            shadow = compute_shadow(surface, light);
        }

        light.radiance *= shadow;
    }

    if (!any(light.radiance > 0.0))
    {
        return 0.0;
    }

    AngularInfo angular_info;
    angular_info.Build(light, surface);

    return BRDF_Diffuse(surface, angular_info) * light.radiance;
}

float3 evaluate_particle_lighting(uint2 pixel, Surface surface)
{
    float3 ambient = surface.albedo * 0.08;
    float3 result  = ambient;
    uint total_lights = buffer_frame.cluster_light_count;

    if (total_lights > 0u)
    {
        result += evaluate_particle_light(0u, pixel, surface);
    }

    if (total_lights > 1u)
    {
        float4 hp_left = mul(float4(surface.position, 1.0), buffer_frame.view_projection);
        if (hp_left.w > 0.0)
        {
            float3 ndc_left  = hp_left.xyz / hp_left.w;
            float2 uv_lookup = float2(ndc_left.x * 0.5 + 0.5, 0.5 - ndc_left.y * 0.5);
            float  view_z    = mul(float4(surface.position, 1.0), buffer_frame.view).z;
            uint3  cid       = cluster_id_from_screen(uv_lookup, view_z);
            uint   flat_id   = cluster_flat(cid);
            uint2  range     = cluster_light_grid[flat_id];

            for (uint k = 0u; k < range.y; k++)
            {
                uint light_index = cluster_light_indices[range.x + k];
                result += evaluate_particle_light(light_index, pixel, surface);
            }
        }
    }

    return max(result, ambient);
}

float4 main_ps(ps_input input) : SV_Target0
{
    // radial disc, discard outside the unit circle so the quad never reads as a square
    float dist = length(input.local);
    if (dist > 1.0)
    {
        discard;
    }
    float falloff = 1.0 - dist * dist;

    // soft depth test against the scene so billboards do not bleed through surfaces
    int2  pixel        = int2(input.position.xy);
    float linear_scene = linearize_depth(tex_depth.Load(int3(pixel, 0)).r);
    float soft_factor  = saturate((linear_scene - input.lin_depth) * input.render_params.w);
    if (soft_factor <= 0.0)
    {
        discard;
    }

    float3 base_color = input.color;
    float alpha_mask  = 1.0;
    if (input.use_tex > 0.5)
    {
        // rotate the sample coords so each particle shows the texture at its own angle
        float2 r_xy   = float2(input.local.x * input.rot.x - input.local.y * input.rot.y,
                               input.local.x * input.rot.y + input.local.y * input.rot.x);
        float2 tex_uv = r_xy * 0.5 + 0.5;

        float rows    = max(input.flipbook.x, 1.0);
        float columns = max(input.flipbook.y, 1.0);
        float frames  = rows * columns;
        if (frames > 1.0)
        {
            float frame_progress = input.flipbook.z > 0.0 ? input.age_t * input.flipbook.z : input.age_t * frames;
            float frame_index    = fmod(floor(frame_progress + input.flipbook.w * frames), frames);
            float column_index   = fmod(frame_index, columns);
            float row_index      = floor(frame_index / columns);
            tex_uv = (tex_uv + float2(column_index, row_index)) / float2(columns, rows);
        }

        float4 sample = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), tex_uv, 0);
        base_color   *= sample.rgb;
        alpha_mask    = sample.a;
    }

    uint lighting_mode = (uint)round(input.render_params.y);
    float3 lit_color = base_color;
    if (lighting_mode == particle_lighting_lit)
    {
        Surface surface = build_particle_surface(input.position_world, base_color, uint2(pixel), input.local);
        lit_color = evaluate_particle_lighting(uint2(pixel), surface);
    }
    else if (lighting_mode == particle_lighting_emissive)
    {
        lit_color = base_color * max(input.render_params.z, 1.0);
    }

    float alpha = saturate(alpha_mask * input.alpha * soft_factor * falloff);
    if (alpha <= 0.0)
    {
        discard;
    }

    float camera_distance = distance(input.position_world, get_camera_position());
    float height_fade     = saturate((input.position_world.y + 20.0) / 80.0);
    float fog_factor      = saturate((1.0 - exp(-camera_distance * 0.00005)) * (0.55 + height_fade * 0.2));
    float3 fog_color      = lerp(float3(0.55, 0.58, 0.62), get_sun_color() * 0.12, height_fade);
    lit_color = lerp(lit_color, fog_color, fog_factor);

    uint blend_mode = (uint)round(input.render_params.x);
    if (blend_mode == particle_blend_additive)
    {
        return float4(lit_color * alpha, 0.0);
    }

    if (blend_mode == particle_blend_premultiplied)
    {
        return float4(lit_color * alpha, alpha);
    }

    return float4(lit_color, alpha);
}

#endif
