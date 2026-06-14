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
    uint emitter_index    = (uint)pass_get_f3_value().x;
    EmitterParams emitter = particle_emitter[emitter_index];
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

    // bias the launch upward and outward so the smoke billows off the tire instead of firing in every direction
    float3 dir = random_direction(seed + 277803737u);
    dir.y      = abs(dir.y) * 0.7 + 0.3;
    dir        = normalize(dir);

    // per-particle jitter so no two puffs share size, lifetime or speed, this breaks up the uniform blob look
    float r_size  = 0.6 + 0.8 * rng(seed + 9001u);
    float r_life  = 0.7 + 0.6 * rng(seed + 33u);
    float r_speed = 0.5 + 1.0 * rng(seed + 7u);

    Particle p;
    p.position     = emitter.position + offset;
    p.lifetime     = emitter.lifetime * r_life;
    p.velocity     = dir * emitter.start_speed * r_speed;
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
    p.velocity *= saturate(1.0 - 1.2 * dt);

    // curl-like turbulence so the plume swirls and billows organically rather than expanding as a clean ball
    float  ts   = (float)buffer_frame.time;
    float3 tp   = p.position * 1.5;
    float3 turb = float3(sin(tp.y + ts * 1.3) + cos(tp.z * 0.7 - ts),
                         sin(tp.z + ts * 1.1) + cos(tp.x * 0.8 + ts),
                         sin(tp.x + ts * 0.9) + cos(tp.y * 0.6 - ts));
    p.velocity += turb * 0.3 * dt;

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
    float4 position  : SV_Position;
    float2 local     : TEXCOORD0; // quad local coords in the minus one to one range
    float3 color     : TEXCOORD1;
    float  alpha     : TEXCOORD2;
    float  lin_depth : TEXCOORD3; // particle center linear depth
    float2 rot       : TEXCOORD4; // cos, sin of the per particle rotation
    float  use_tex   : TEXCOORD5;
};

ps_input main_vs(uint vertex_id : SV_VertexID)
{
    uint  emitter_index = (uint)pass_get_f3_value().x;
    float use_texture   = pass_get_f3_value().y;

    uint index  = vertex_id / 6;
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

    // screen aligned billboard basis, up derived from the camera forward and right
    float3 right = buffer_frame.camera_right;
    float3 up    = normalize(cross(buffer_frame.camera_forward, right));
    float  half_size = p.size * 0.5;
    float3 world = p.position + (right * c.x + up * c.y) * half_size;

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
    return o;
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
    float soft_factor  = saturate((linear_scene - input.lin_depth) * 20.0);
    if (soft_factor <= 0.0)
    {
        discard;
    }

    float3 contribution;
    if (input.use_tex > 0.5)
    {
        // rotate the sample coords so each particle shows the texture at its own angle
        float2 r_xy   = float2(input.local.x * input.rot.x - input.local.y * input.rot.y,
                               input.local.x * input.rot.y + input.local.y * input.rot.x);
        float2 tex_uv = r_xy * 0.5 + 0.5;
        float4 sample = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), tex_uv, 0);
        contribution  = input.color * sample.rgb * sample.a * input.alpha * soft_factor * falloff;
    }
    else
    {
        contribution = input.color * input.alpha * soft_factor * falloff;
    }

    return float4(contribution, 0.0);
}

#endif
