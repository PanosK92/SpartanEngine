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
// how much of the skylight a billboard collects, kept in step with volume_sky_fill so the billboard
// and volumetric paths do not disagree on smoke color when an emitter switches between them
static const float particle_sky_fill           = 0.14f;
// how fast the texture churn boils, and how many cycles of it fit across one puff
static const float churn_speed                 = 1.1f;
static const float churn_frequency             = 5.0f;

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

// diameter gained per metre travelled through still air, the entrainment hypothesis for a turbulent jet
// puts this near a fifth for the radius of a real plume, this is well under that because a particle here
// is one parcel of a plume rather than the whole thing and the authored ramp still carries the envelope
static const float entrainment_rate      = 0.06;
// diameter gained per second once a parcel has stalled, real smoke keeps spreading by turbulent
// diffusion after it has given up its momentum, and that slow thinning is what dissipates a trail
static const float entrainment_diffusion = 0.12;

// curl noise
//
// the field this replaced was a single frequency triple of sines and cosines, and its divergence is not
// zero, so wherever it was positive a parcel was pulled apart and wherever it was negative it was
// squeezed, which inflates a puff into a smooth ball no matter how strong the field is. the curl of a
// vector potential is divergence free by construction, so it can only shear, fold and stretch a parcel
// and never change its volume, and that is what tears a plume into filaments instead of lumps.
//
// three octaves, because turbulence has texture at every scale and one swirl frequency reads as a
// pattern. the amplitude falls as the cube root of the length scale across the inertial range, so each
// octave that halves the eddy size carries about four fifths of the velocity of the one below it.
static const uint  curl_octaves      = 3u;
static const float curl_frequency    = 0.55; // coarsest eddy is a little under two metres across
static const float curl_falloff      = 0.79; // two to the minus one third
static const float curl_drift_speed  = 0.42;

// each octave drifts in its own direction, a single shared direction slides the whole field past the
// smoke like a projected texture and the eye reads the slide rather than the swirl
static const float3 curl_drift[3] =
{
    float3( 0.31, -0.62,  0.19),
    float3(-0.47,  0.28, -0.55),
    float3( 0.22,  0.51,  0.44)
};

float curl_hash(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float curl_value_noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float2 a = float2(0.0, 1.0);
    return lerp(lerp(lerp(curl_hash(i + a.xxx), curl_hash(i + a.yxx), f.x),
                     lerp(curl_hash(i + a.xyx), curl_hash(i + a.yyx), f.x), f.y),
                lerp(lerp(curl_hash(i + a.xxy), curl_hash(i + a.yxy), f.x),
                     lerp(curl_hash(i + a.xyy), curl_hash(i + a.yyy), f.x), f.y), f.z);
}

// three scalar fields offset far enough apart on the lattice that they share no features, together they
// are the vector potential the curl is taken of
float3 curl_potential(float3 p)
{
    return float3(curl_value_noise(p),
                  curl_value_noise(p + float3( 71.3, 19.7,  43.1)),
                  curl_value_noise(p + float3(-37.9, 61.5, -11.3)));
}

// central differences, the discrete curl of a discrete potential is divergence free on the same stencil,
// so the property that matters survives the finite difference
float3 curl_noise(float3 p)
{
    const float e = 0.35; // in cells, wide enough that the hash lattice does not alias through

    float3 px0 = curl_potential(p - float3(e, 0.0, 0.0));
    float3 px1 = curl_potential(p + float3(e, 0.0, 0.0));
    float3 py0 = curl_potential(p - float3(0.0, e, 0.0));
    float3 py1 = curl_potential(p + float3(0.0, e, 0.0));
    float3 pz0 = curl_potential(p - float3(0.0, 0.0, e));
    float3 pz1 = curl_potential(p + float3(0.0, 0.0, e));

    float inv = 1.0 / (2.0 * e);

    return float3((py1.z - py0.z) - (pz1.y - pz0.y),
                  (pz1.x - pz0.x) - (px1.z - px0.z),
                  (px1.y - px0.y) - (py1.x - py0.x)) * inv;
}

// a parcel cannot be advected by structure finer than itself, it only gets sheared inside, so the fine
// octaves fade out as a puff entrains air and grows, which is why a young wisp boils and an old billow
// only drifts on the coarse eddies
float3 curl_turbulence(float3 position, float size, float time)
{
    float3 sum  = 0.0;
    float  freq = curl_frequency;
    float  amp  = 1.0;

    [unroll]
    for (uint i = 0u; i < curl_octaves; i++)
    {
        float eddy   = 1.0 / freq;
        float cutoff = saturate(eddy / max(size, 0.001) - 0.5);
        if (cutoff > 0.0)
        {
            sum += curl_noise(position * freq + curl_drift[i] * time * curl_drift_speed) * amp * cutoff;
        }

        freq *= 2.0;
        amp  *= curl_falloff;
    }

    return sum;
}

void apply_depth_collision(inout Particle p, inout float3 new_pos, EmitterParams emitter)
{
    float4 clip_new = mul(float4(new_pos, 1.0), buffer_frame.view_projection);
    if (clip_new.w <= 0.0)
    {
        return;
    }

    float3 ndc_new = clip_new.xyz / clip_new.w;
    float2 uv_new  = ndc_new.xy * float2(0.5, -0.5) + 0.5;
    if (uv_new.x <= 0.0 || uv_new.x >= 1.0 || uv_new.y <= 0.0 || uv_new.y >= 1.0)
    {
        return;
    }

    int2 pixel = int2(uv_new * buffer_frame.resolution_render);
    float scene_depth_raw = tex_depth.Load(int3(pixel, 0)).r;
    if (scene_depth_raw <= 0.0)
    {
        return;
    }
    if (length(tex_velocity.Load(int3(pixel, 0)).xy) > 0.0005)
    {
        return;
    }

    float linear_particle = linearize_depth(ndc_new.z);
    float linear_scene    = linearize_depth(scene_depth_raw);
    float penetration     = linear_particle - linear_scene;
    float surface_band    = max(p.size * 0.75, 0.18);
    float collision_thickness = length(p.velocity) * emitter.delta_time + surface_band + 0.35;
    if (penetration <= -surface_band || penetration >= collision_thickness)
    {
        return;
    }

    float3 surface_normal = get_normal(pixel);
    float normal_length   = length(surface_normal);
    if (normal_length <= 0.1)
    {
        return;
    }

    surface_normal /= normal_length;
    float3 surface_pos = get_position(scene_depth_raw, uv_new);
    float influence = saturate((surface_band - abs(penetration)) / max(surface_band, 0.001));
    influence *= influence;

    if (penetration > 0.0)
    {
        float into_surface = dot(p.velocity, surface_normal);
        if (into_surface < 0.0)
        {
            p.velocity -= surface_normal * into_surface * (1.0 + collision_restitution);
        }

        new_pos = surface_pos + surface_normal * (collision_offset + p.size * 0.35);
    }
    else
    {
        float into_surface = dot(p.velocity, surface_normal);
        if (into_surface < 0.0)
        {
            p.velocity -= surface_normal * into_surface * influence;
        }

        p.velocity += surface_normal * influence * emitter.turbulence_strength * 0.65 * emitter.delta_time;
        new_pos += surface_normal * influence * surface_band * 0.35 * emitter.delta_time;
    }
}

#ifdef RAY_TRACING_ENABLED

// six axis probes, a surface found along any of them pushes back the other way
static const float3 collision_probe[6] =
{
    float3( 1.0,  0.0,  0.0), float3(-1.0,  0.0,  0.0),
    float3( 0.0,  1.0,  0.0), float3( 0.0, -1.0,  0.0),
    float3( 0.0,  0.0,  1.0), float3( 0.0,  0.0, -1.0)
};

float trace_surface_distance(float3 origin, float3 direction, float max_distance)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001;
    ray.TMax      = max(max_distance, 0.002);

    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING ? -1.0 : query.CommittedRayT();
}

// an inline ray query returns a hit distance but no surface normal, that needs vertex data and a hit
// shader, so the normal is recovered from the probe set instead, each probe that lands close pushes back
// along its own direction and the sum behaves like the gradient of a distance field, which also picks a
// sensible direction in a corner where any single ray would just choose one of the two walls
void apply_traced_collision(inout Particle p, inout float3 new_pos, EmitterParams emitter)
{
    float dt     = emitter.delta_time;
    float radius = max(p.size * 0.5, 0.05);
    float travel = distance(new_pos, p.position);

    // the reach covers this frame's travel as well as the particle itself, so a fast puff cannot tunnel
    // through a thin wall between two frames the way a position only test does
    float reach = radius + travel + 0.06;

    float3 push    = 0.0;
    float  nearest = reach;
    uint   hits    = 0u;
    bool   pinched = false;

    [unroll]
    for (uint a = 0u; a < 3u; a++)
    {
        float3 dir_pos = collision_probe[a * 2u + 0u];
        float3 dir_neg = collision_probe[a * 2u + 1u];
        float  d_pos   = trace_surface_distance(p.position, dir_pos, reach);
        float  d_neg   = trace_surface_distance(p.position, dir_neg, reach);

        // geometry on both sides of one axis and closer than the particle is a gap it does not fit
        // through, an exhaust recess is exactly that, and pushing out of one wall only drives it into
        // the other so it sits in the pipe jittering instead of leaving
        if (d_pos >= 0.0 && d_neg >= 0.0 && min(d_pos, d_neg) < radius)
        {
            pinched = true;
        }

        if (d_pos >= 0.0)
        {
            float w = 1.0 - saturate(d_pos / reach);
            push   -= dir_pos * w * w;
            nearest = min(nearest, d_pos);
            hits++;
        }

        if (d_neg >= 0.0)
        {
            float w = 1.0 - saturate(d_neg / reach);
            push   -= dir_neg * w * w;
            nearest = min(nearest, d_neg);
            hits++;
        }
    }

    // enclosed on nearly every side, so there is no direction left to resolve towards, let it travel and
    // sort itself out once it is clear rather than pinning it here
    if (pinched || hits >= 5u || length(push) <= 0.0001)
    {
        return;
    }

    float3 normal   = normalize(push);
    float clearance = nearest - radius;
    float proximity = 1.0 - saturate(clearance / max(reach - radius, 0.001));

    // smoke does not bounce off a wall, it flows along it, so the part of the velocity heading into the
    // surface is removed rather than reflected, a bounce is right for a spark and wrong for a gas
    float into_surface = dot(p.velocity, normal);
    if (into_surface < 0.0)
    {
        p.velocity -= normal * into_surface * proximity;
    }

    // displaced air has nowhere left to go but along the surface, this is the wall jet that spreads
    // burnout smoke outward across the tarmac instead of letting it stack up against it
    float3 tangent    = p.velocity - normal * dot(p.velocity, normal);
    float tangent_len = length(tangent);
    if (tangent_len > 0.001)
    {
        p.velocity += (tangent / tangent_len) * proximity * emitter.rollup_strength * dt;
    }

    new_pos = p.position + p.velocity * dt;

    // hard contact, a particle never ends a frame inside geometry
    if (clearance < 0.0)
    {
        new_pos += normal * (collision_offset - clearance);
    }
}

#endif

void apply_moving_emitter_push(inout Particle p, EmitterParams emitter)
{
    float speed = length(emitter.emitter_velocity);
    if (speed < 3.0)
    {
        return;
    }

    float3 wake_dir = emitter.emitter_velocity / speed;
    float3 wake_center = emitter.position + wake_dir * 0.35;
    float3 rel = p.position - wake_center;
    float axial = dot(rel, wake_dir);
    if (axial < -2.2 || axial > 1.8)
    {
        return;
    }

    float3 radial = rel - wake_dir * axial;
    radial.y *= 0.65;
    float radial_len = length(radial);
    float push_radius = 1.25 + saturate(speed / 45.0) * 0.55;
    if (radial_len >= push_radius)
    {
        return;
    }

    float3 push_dir = safe_normalize(radial + float3(0.0, 0.12, 0.0), float3(0.0, 1.0, 0.0));
    float radial_t = saturate(1.0 - radial_len / push_radius);
    float axial_t = 1.0 - saturate(abs(axial) / 2.2);
    float strength = radial_t * radial_t * axial_t * saturate(speed / 28.0);
    float age_t = 1.0 - saturate(p.lifetime / max(p.max_lifetime, 0.0001));
    strength *= saturate(age_t * 3.0);

    float push = strength * (2.4 + speed * 0.045) * emitter.delta_time;
    p.velocity += push_dir * push;
    p.velocity -= wake_dir * dot(p.velocity, wake_dir) * strength * 0.04 * emitter.delta_time;
}

// height of the shear layer where the wall jet meets the still air above it
static const float rollup_layer_height = 0.55;
// how far downstream the shoulder vortices stay coherent
static const float wake_pair_length    = 4.0;

// smoking tire aerodynamics
//
// four mechanisms carry the look in real footage. the tread injects the smoke as a wall jet running
// backwards along the tarmac at a fraction of the slip speed. that jet decelerates into the still air
// above it and the shear layer between them rolls up, which is what stands the plume on end behind the
// tire. the smoke gasses off rubber near three hundred degrees so it climbs on its own. and once the
// car is moving the tread shoulders shed a counter rotating pair with streamwise axes that drags the
// wake outboard and lifts it.
//
// the tread also drags a boundary layer around itself by no slip, but that layer is thin and the
// tarmac blocks the bottom of the loop while separation kills it over the crown, so it is confined to
// the rear lower quadrant rather than being a free orbit around the axle
void apply_tire_aerodynamics(inout Particle p, EmitterParams emitter, float age)
{
    if (emitter.vortex_strength == 0.0 &&
        emitter.rollup_strength <= 0.0 &&
        emitter.thermal_strength <= 0.0 &&
        emitter.wake_strength <= 0.0)
    {
        return;
    }

    float dt = emitter.delta_time;

    // the emitter lives in the contact patch, so its own height is the tarmac datum
    float  height     = max(p.position.y - emitter.position.y, 0.0);
    float3 axis       = safe_normalize(emitter.vortex_axis, float3(1.0, 0.0, 0.0));
    float3 horizontal = float3(p.velocity.x, 0.0, p.velocity.z);
    float  jet_speed  = length(horizontal);
    float  tire_radius = emitter.vortex_radius;

    // tread boundary layer
    if (emitter.vortex_strength != 0.0 && tire_radius > 0.0)
    {
        float3 rel    = p.position - emitter.vortex_center;
        float  along  = dot(rel, axis);
        float3 radial = rel - axis * along;
        float  r      = length(radial);
        if (r > 0.0001)
        {
            // no slip carries air around the tread, the layer is thin so it decays off the surface
            float radial_fade = r <= tire_radius ? 1.0 : exp(-(r - tire_radius) / (tire_radius * 0.6));

            // the swirl is only as wide as the tread
            float axial_fade = saturate(1.0 - abs(along) / (tire_radius * 1.5));

            // without the crown clamp the circulation carries smoke forward over the top of the wheel,
            // which never happens because the flow has separated long before it gets there
            float crown_fade = saturate(1.0 - rel.y / tire_radius);

            float3 tangential = cross(axis, radial) / r;
            p.velocity += tangential * emitter.vortex_strength * radial_fade * axial_fade * crown_fade * dt;
        }
    }

    // shear rollup, emergent, this is the curl and it needs no hand placed vortex
    if (emitter.rollup_strength > 0.0 && jet_speed > 0.001)
    {
        float ground_prox = saturate(1.0 - height / rollup_layer_height);
        float lift        = jet_speed * ground_prox * emitter.rollup_strength;
        p.velocity.y += lift * dt;

        // the rise is bought with jet momentum, the sheet stalls as it stands up
        p.velocity -= horizontal * (emitter.rollup_strength * ground_prox * 0.35) * dt;

        // a wall jet cannot go down, so it fans sideways, and which way is decided by the side of the
        // tread the parcel already sits on
        float side = dot(p.position - emitter.position, axis) >= 0.0 ? 1.0 : -1.0;
        p.velocity += axis * side * lift * 0.45 * dt;
    }

    // thermal, strong at birth then fading as cold air mixes in, the climb it already banked carries on
    if (emitter.thermal_strength > 0.0)
    {
        p.velocity.y += emitter.thermal_strength * exp(-age * emitter.thermal_decay) * dt;
    }

    float travel_speed = length(emitter.emitter_velocity);
    if (emitter.wake_strength <= 0.0 || travel_speed < 2.0 || tire_radius <= 0.0)
    {
        return;
    }

    // counter rotating shoulder pair
    float3 travel = emitter.emitter_velocity / travel_speed;
    float3 rel_e  = p.position - emitter.position;
    float  behind = -dot(rel_e, travel);
    if (behind < -0.2 || behind > wake_pair_length)
    {
        return;
    }

    // one core per shoulder, a tread half width outboard and just clear of the tarmac
    float  side  = dot(rel_e, axis) >= 0.0 ? 1.0 : -1.0;
    float3 core  = emitter.position + axis * side * tire_radius * 0.5 + float3(0.0, tire_radius * 0.6, 0.0);
    float3 rel_c = p.position - core;
    float3 plane = rel_c - travel * dot(rel_c, travel);
    float  d     = length(plane);
    if (d < 0.0001)
    {
        return;
    }

    // rankine core, and the pair diffuses as it trails away
    float core_radius = tire_radius * 0.8;
    float profile     = d < core_radius ? d / core_radius : core_radius / d;
    float decay       = saturate(1.0 - behind / wake_pair_length);

    // signing the axis by side is what makes the two cores counter rotate, each one then sweeps the
    // ground level wake outboard and lifts it once it is clear of the tread
    float3 spin = cross(travel * side, plane) / d;
    p.velocity += spin * emitter.wake_strength * profile * decay * dt;
}

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

    // entrainment
    //
    // a turbulent parcel grows by dragging ambient air in across its own surface, and the rate it does
    // that at is set by how fast it is moving through that air. the air it swallows arrives with no
    // momentum of its own, so the parcel keeps the momentum it was born with spread across a cross
    // section that keeps growing, and the speed has to fall to pay for it. that coupling is what makes a
    // real plume shoot out, balloon and stall, and it is missing when a fixed size ramp runs beside a
    // fixed drag coefficient with neither knowing about the other, which is what produced a puff that
    // reached its final size on a timer regardless of what it was doing
    //
    // a shrinking authored ramp is a spark or a flame tapering out, not a gas entraining air, so that
    // keeps the plain interpolation and nothing here touches it
    bool entrains = p.end_size > p.start_size;
    if (entrains)
    {
        float3 relative = p.velocity - buffer_frame.wind * emitter.wind_influence;
        float  size_old = max(p.size, 0.0001);
        float  size_new = size_old + (entrainment_rate * length(relative) + entrainment_diffusion) * dt;

        p.size = size_new;

        // a parcel in a jet, not an isolated puff, so momentum spreads over a cross section rather than a
        // volume and the exponent is one, an isolated puff would be three and stops far too abruptly
        p.velocity *= size_old / size_new;
    }

    // divergence free turbulence, the plume shears and folds instead of inflating
    // the clock is wrapped because the drift offset is multiplied by the octave frequency and an
    // unwrapped one runs out of fractional precision after a while and the field starts to quantise
    float ts = fmod((float)buffer_frame.time, 600.0);
    p.velocity += curl_turbulence(p.position, p.size, ts) * emitter.turbulence_strength * dt;
    p.velocity += buffer_frame.wind * emitter.wind_influence * dt;

    float age_for_collision = 1.0 - saturate(p.lifetime / max(p.max_lifetime, 0.0001));
    apply_tire_aerodynamics(p, emitter, age_for_collision);

    // the shoulder pair already models this wake properly, the generic radial push would double it
    if (emitter.wake_strength <= 0.0)
    {
        apply_moving_emitter_push(p, emitter);
    }

    // predict next position
    float3 new_pos = p.position + p.velocity * dt;

    // fresh puffs need a short grace period to leave tight emitters such as exhaust tips
    if (age_for_collision > 0.18)
    {
    #ifdef RAY_TRACING_ENABLED
        if (emitter.collision_traced != 0u)
        {
            // an exhaust tip sits down inside the bodywork, so collision has to stay off until the
            // particle has actually left the emitter or it fights the pipe it just came out of, this is a
            // distance because the grace above is a fraction of lifetime and a slow puff covers almost no
            // ground in that time while a scrubbing tire covers metres
            if (distance(p.position, emitter.position) > emitter.collision_clearance)
            {
                apply_traced_collision(p, new_pos, emitter);
            }
        }
        else
    #endif
        {
            apply_depth_collision(p, new_pos, emitter);
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

    // fade each particle from its own current value so emitter script changes do not pulse old smoke
    float te = 1.0 - (1.0 - t) * (1.0 - t);
    float end_pull = saturate(dt * (0.35 + t * 1.4));
    p.color.rgb = lerp(p.color.rgb, emitter.end_color.rgb, end_pull);
    p.color.a = min(p.color.a, lerp(p.color.a, emitter.end_color.a, end_pull));
    float life_left = saturate(p.lifetime / max(p.max_lifetime, 0.0001));
    p.color.a = min(p.color.a, emitter.start_color.a * smoothstep(0.0, 0.25, life_left));

    // for an entraining parcel the authored ramp is a floor rather than the answer, so an effect still
    // reaches the size it was authored to reach when it is moving too slowly to entrain its way there,
    // and there is deliberately no ceiling, a parcel still growing at the end of its life is exactly what
    // the tail of a dissipating trail looks like
    float size_ramp = lerp(p.start_size, p.end_size, te);
    p.size = entrains ? max(p.size, size_ramp) : size_ramp;

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
    float  churn          : TEXCOORD10;
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
    o.churn     = 0.0;

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
    o.churn     = emitter.churn_strength;
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

// a smoke quad covers a large part of the screen and puffs stack many deep, so a ray query per light
// per pixel multiplies into billions of traversals, only the primary light is allowed to trace and
// the local lights fall back to the shadow atlas
float3 evaluate_particle_light(uint light_index, uint2 pixel, Surface surface, bool allow_ray_traced_shadow)
{
    Light light;
    light.Build(light_index, surface);

    if (light.has_shadows())
    {
        float shadow = 1.0;
    #ifdef RAY_TRACING_ENABLED
        if (allow_ray_traced_shadow && is_ray_traced_shadows_enabled())
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
    // skylight, the flat eight percent term had no illuminant at all so the warm sun was the only
    // thing with a hue and smoke read brown, the divide by pi keeps it in the same units as the
    // diffuse lobe below
    float3 sky     = get_sky_fill_radiance() * particle_sky_fill / 3.14159265;
    float3 ambient = surface.albedo * (0.02 + sky);
    float3 result  = ambient;
    uint total_lights = buffer_frame.cluster_light_count;

    if (total_lights > 0u)
    {
        result += evaluate_particle_light(0u, pixel, surface, true);
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

            // a dense city cluster holds dozens of lights, each one costs a shadow lookup on every
            // overlapping smoke pixel, the nearest few carry the look so the tail is dropped
            const uint max_local_lights = 4u;
            uint light_count = min(range.y, max_local_lights);

            for (uint k = 0u; k < light_count; k++)
            {
                uint light_index = cluster_light_indices[range.x + k];
                result += evaluate_particle_light(light_index, pixel, surface, false);
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

    // an untextured particle gets its whole shape from this radial ramp, but a textured one already
    // carries a silhouette in its alpha and multiplying the ramp over the top of it rounds every puff
    // back into a smooth ball, which is what makes a dense plume read as a heap of spheres, so a
    // textured particle only gets a thin edge feather to hide the circular cut
    bool  textured = input.use_tex > 0.5;
    float falloff  = textured ? saturate((1.0 - dist) * 3.0) : (1.0 - dist * dist);

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
    if (textured)
    {
        // rotate the sample coords so each particle shows the texture at its own angle
        float2 r_xy   = float2(input.local.x * input.rot.x - input.local.y * input.rot.y,
                               input.local.x * input.rot.y + input.local.y * input.rot.x);
        float2 tex_uv = r_xy * 0.5 + 0.5;

        // churn, drag the sample coords around with a scrolling field so the inside of the puff boils
        // while its outline holds, a still texture can only rotate and scale which is what makes smoke
        // read as ambient fog no matter how good the flow field moving it is
        if (input.churn > 0.0)
        {
            // the per particle seed offsets the phase, without it every puff in the plume boils in step
            float  t    = (float)buffer_frame.time * churn_speed + input.flipbook.w * 6.28318530718;
            float2 w    = tex_uv * churn_frequency;
            float2 warp = float2(sin(w.y + t)       + cos(w.x * 1.7 - t * 0.8),
                                 sin(w.x * 1.3 - t) + cos(w.y * 0.8 + t * 0.9));

            // the boil coarsens as the puff expands and its fine structure is torn apart
            tex_uv = saturate(tex_uv + warp * input.churn * (0.6 + input.age_t * 0.7));
        }

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
