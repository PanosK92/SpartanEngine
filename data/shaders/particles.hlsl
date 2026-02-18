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
//====================

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

#ifdef EMIT

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    EmitterParams emitter = particle_emitter[0];
    if (emitter.emitter_count == 0)
        return;

    uint emit_count = (uint)(emitter.emission_rate * emitter.delta_time);
    if (dispatch_thread_id.x >= emit_count)
        return;

    // allocate a slot in the ring buffer (wraps around via modulo)
    uint raw_slot;
    InterlockedAdd(particle_counter[0], 1, raw_slot);
    uint slot = raw_slot % emitter.max_particles;

    // high-entropy seed from frame, thread id, and raw slot
    uint seed = dispatch_thread_id.x * 7919u + emitter.frame * 104729u + raw_slot * 2654435761u;

    // random position within emission sphere
    float3 offset = random_in_sphere(seed) * emitter.radius;

    // random direction for velocity
    float3 dir = random_direction(seed + 277803737u);

    Particle p;
    p.position     = emitter.position + offset;
    p.lifetime     = emitter.lifetime;
    p.velocity     = dir * emitter.start_speed;
    p.max_lifetime = emitter.lifetime;
    p.color        = emitter.start_color;
    p.size         = emitter.start_size;
    p.padding      = float3(0.0, 0.0, 0.0);

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

    float dt = emitter.delta_time;

    // integrate gravity
    p.velocity.y += emitter.gravity_modifier * 9.81 * dt;

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

    // interpolate color and size over lifetime
    p.color = lerp(emitter.start_color, emitter.end_color, t);
    p.size  = lerp(emitter.start_size, emitter.end_size, t);

    particle_buffer_a[index] = p;
}

#elif defined(RENDER)

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

    // skip dead or invisible particles
    if (p.lifetime <= 0.0 || p.max_lifetime <= 0.0 || p.color.a <= 0.0)
        return;

    // project to clip space
    float4 clip = mul(float4(p.position, 1.0), buffer_frame.view_projection);
    if (clip.w <= 0.0)
        return;

    float3 ndc = clip.xyz / clip.w;

    // frustum cull
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0)
        return;

    // ndc to pixel coordinates
    float2 uv    = ndc.xy * float2(0.5, -0.5) + 0.5;
    float2 pixel = uv * buffer_frame.resolution_render;
    int2 center  = int2(pixel);

    // screen-space radius from world-space size
    float3 right_ws = buffer_frame.camera_right * (p.size * 0.5);
    float4 clip_r   = mul(float4(p.position + right_ws, 1.0), buffer_frame.view_projection);
    float3 ndc_r    = clip_r.xyz / clip_r.w;
    float2 pixel_r  = (ndc_r.xy * float2(0.5, -0.5) + 0.5) * buffer_frame.resolution_render;
    int radius_px   = clamp((int)length(pixel_r - pixel), 1, 64);

    // early-out: if the particle center is well behind the scene surface, skip entirely
    float linear_particle = linearize_depth(ndc.z);
    float center_scene    = linearize_depth(tex_depth.Load(int3(center, 0)).r);
    if ((center_scene - linear_particle) < -0.1)
        return;

    float base_alpha = p.color.a;
    if (base_alpha <= 0.001)
        return;

    // splat a circular billboard (additive) with per-pixel depth testing
    int2 res = int2(buffer_frame.resolution_render);
    for (int y = -radius_px; y <= radius_px; y++)
    {
        for (int x = -radius_px; x <= radius_px; x++)
        {
            int2 coord = center + int2(x, y);

            if (coord.x < 0 || coord.y < 0 || coord.x >= res.x || coord.y >= res.y)
                continue;

            float dist = length(float2(x, y)) / max(1.0, (float)radius_px);
            if (dist > 1.0)
                continue;

            // per-pixel depth test so billboard edges don't bleed through surfaces
            float pixel_scene = linearize_depth(tex_depth.Load(int3(coord, 0)).r);
            float depth_diff  = pixel_scene - linear_particle;
            float soft_factor = saturate(depth_diff * 20.0);

            // smooth radial falloff
            float falloff      = 1.0 - dist * dist;
            float3 contribution = p.color.rgb * base_alpha * soft_factor * falloff;

            tex_uav[coord] += float4(contribution, 0.0);
        }
    }
}

#endif
