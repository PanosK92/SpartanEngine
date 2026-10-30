/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
#include "fog_volume.hlsl"
#include "common_rain.hlsl"
#ifdef RENDER
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "light_cluster.hlsl"
#include "rt_visibility.hlsl"
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
static const float leaf_sky_fill               = 0.6f;
static const float leaf_translucency           = 0.5f;
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

// Leaves (render modes 2 and 5) reuse the smoke wake fields for their rigid body state:
// wake_axis    = plate normal, w > 0.5 while resting
// wake_origin  = angular velocity, w = seconds spent slow in contact
// birth_effect = in-plane tangent, w = fraction of open sky above it (lit leaves have no emissive)
// ground_plane = normal of the surface touched last frame, w = seconds alive (0 = unseeded litter)
bool is_leaf_mode(uint render_mode)
{
    return render_mode == 2u || render_mode == 5u;
}

bool in_view_frustum(float3 position)
{
    float4 clip = mul(float4(position, 1.0), buffer_frame.view_projection);
    if (clip.w <= 0.0)
    {
        return false;
    }

    float2 ndc = clip.xy / clip.w;
    return all(abs(ndc) < 1.05);
}

// height of the topmost static surface over a point, the shelter grid rain uses, off screen roofs included
float cover_height(float3 position)
{
    float cell_size = buffer_frame.rain_occlusion.z;
    if (cell_size <= 0.0)
    {
        return rain_height_exposed;
    }

    return rain_occluder_height(int2(floor(position.xz / cell_size)));
}

static const uint foliage_material_bits = (1u << 11) | (1u << 12) | (1u << 21);
// foliage plus anything that sways in the wind, which is how tree bark and branches are told apart from roofs
static const uint vegetation_material_bits = foliage_material_bits | (1u << 9);

bool under_cover(float3 position)
{
    return position.y < cover_height(position) - 0.25;
}

#ifdef EMIT

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint emitter_index    = pass_uint(pass_particles::emitter_index);
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
    // A leaf is one persistent body. A birth never replaces a leaf in flight, and a resting
    // leaf only yields its slot when nobody can watch it vanish: outside the view or far away.
    if (is_leaf_mode(emitter.render_mode))
    {
        Particle resident = particle_buffer_a[slot];
        bool alive = resident.owner_id_low == emitter.owner_id_low && resident.owner_id_high == emitter.owner_id_high &&
            resident.emitter_index == emitter_index && resident.lifetime > 0.0;
        bool watched = in_view_frustum(resident.position) && distance(resident.position, get_camera_position()) < 45.0;
        if (alive && (resident.wake_axis.w < 0.5 || watched))
            return;
    }

    // high-entropy seed from frame, thread id, and raw slot
    uint seed = dispatch_thread_id.x * 7919u + emitter.frame * 104729u + raw_slot * 2654435761u;

    // random position within emission sphere
    float3 offset = random_in_sphere(seed) * emitter.radius;
    // Contact-patch smoke starts above the road, not inside a sphere half buried in it.
    if (emitter.rollup_strength > 0.0)
        offset.y = abs(offset.y);
    // rain fills a uniform slab over the camera, a sphere would thin out overhead and at the sides
    if (emitter.rain_occluded != 0u)
    {
        float r     = sqrt(rng(seed + 1301u)) * emitter.radius;
        float theta = rng(seed + 1303u) * 6.28318530718;
        offset      = float3(r * cos(theta), (rng(seed + 1307u) * 2.0 - 1.0) * emitter.radius * 0.45, r * sin(theta));
    }

    // Thin the edge of litter patches so neighboring carpets merge without a
    // visible circular boundary. The leaves themselves remain fully opaque.
    if (emitter.render_mode == 5u && rng(seed + 1889u) >
        1.0 - smoothstep(0.6, 1.0, length(offset.xz) / max(emitter.radius, 0.001))) return;

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

    Particle p = (Particle)0;
    // Distribute births over this frame's travelled segment instead of leaving
    // disconnected clusters at the current position of a fast-moving emitter.
    p.position     = emitter.position + offset - emitter.emitter_velocity * emitter.delta_time * rng(seed + 47u);
    // leaves come down from the open sky, a birth under a roof is a leaf falling out of the ceiling
    if (is_leaf_mode(emitter.render_mode))
    {
        // the shelter grid takes tree colliders for roofs, it only decides when the canopy cannot be looked through
        bool traced = false;
    #ifdef RAY_TRACING_ENABLED
        traced = emitter.collision_traced != 0u;
    #endif
        if (!traced && under_cover(p.position))
            return;
    #ifdef RAY_TRACING_ENABLED
        if (traced)
        {
            // a canopy is where leaves come from, so swaying vegetation is looked through and only a roof blocks the birth
            RayDesc ray;
            ray.Origin    = p.position;
            ray.Direction = float3(0, 1, 0);
            ray.TMin      = 0.001;
            ray.TMax      = 60.0;
            // a crown stacks many leaf cards, too few hops reads a dense canopy as a roof
            for (uint hop = 0u; hop < 16u; hop++)
            {
                RayQuery<RAY_FLAG_CULL_NON_OPAQUE> query;
                query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
                while (query.Proceed()) {}
                if (query.CommittedStatus() == COMMITTED_NOTHING)
                    break;

                uint flags = material_parameters[geometry_infos[query.CommittedInstanceIndex()].material_index].flags;
                if ((flags & vegetation_material_bits) == 0u || hop == 15u)
                    return;

                ray.TMin = query.CommittedRayT() + 0.05;
            }
        }
    #endif
    }
    p.lifetime     = emitter.lifetime * r_life;
    p.velocity     = dir * emitter.start_speed * r_speed + emitter.emitter_velocity * emitter.velocity_inheritance;
    p.previous_position = p.position;
    p.previous_size = emitter.start_size * r_size;
    p.ground_plane = emitter.ground_plane;
    p.max_lifetime = p.lifetime;
    p.color         = emitter.start_color;
    p.size          = emitter.start_size * r_size;
    p.emitter_index = emitter_index;
    p.owner_id_low = emitter.owner_id_low;
    p.owner_id_high = emitter.owner_id_high;
    p.start_size    = emitter.start_size * r_size;
    p.end_size      = emitter.end_size * r_size;
    p.birth_color  = emitter.start_color;
    p.wake_origin  = float4(emitter.position, max(emitter.wake_strength, emitter.rollup_strength * 3.0));
    float3 travel  = float3(emitter.emitter_velocity.x, 0.0, emitter.emitter_velocity.z);
    float3 forward = safe_normalize(cross(emitter.vortex_axis, float3(0, 1, 0)), float3(0, 0, 1));
    p.wake_axis    = float4(length(travel) > 2.0 ? normalize(travel) : forward, emitter.vortex_radius);
    p.birth_effect = float4(emitter.thermal_strength, emitter.thermal_decay, emitter.rollup_strength, emitter.emissive_strength);
    if (is_leaf_mode(emitter.render_mode))
    {
        float3 normal  = random_direction(seed + 523u);
        float3 tangent = safe_normalize(cross(normal, random_direction(seed + 811u)), safe_normalize(cross(normal, float3(0, 1, 0)), float3(1, 0, 0)));
        p.wake_axis    = float4(normal, 0.0);
        p.wake_origin  = float4(random_direction(seed + 997u) * (1.0 + 3.0 * rng(seed + 1009u)), 0.0);
        p.birth_effect = float4(tangent, 1.0);
        p.ground_plane = float4(0.0, 0.0, 0.0, emitter.render_mode == 5u ? 0.0 : 0.0001);
        // each leaf is born with its own dryness, a fixed property, so nothing recolours it later
        p.birth_color.rgb  = lerp(p.birth_color.rgb, emitter.end_color.rgb, rng(seed + 947u) * 0.7);
        p.birth_color.rgb *= lerp(float3(0.42, 0.55, 0.30), float3(1.18, 0.95, 0.66), rng(seed + 941u));
        p.color = p.birth_color;
    }

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

    int2 pixel = int2(uv_new * get_render_resolution_active());
    float scene_depth_raw = tex_depth.Load(int3(pixel, 0)).r;
    if (scene_depth_raw <= 0.0)
    {
        return;
    }
    // Screen velocity includes camera motion. It cannot identify moving geometry:
    // rejecting it disables road collision from a moving chase camera.

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

static const uint leaf_probe_unknown = 0u; // off screen, occluded or foliage, the depth buffer cannot tell
static const uint leaf_probe_free    = 1u; // visibly in front of the surface behind it
static const uint leaf_probe_contact = 2u; // touching or slightly through the visible surface
// The visible surface at the leaf's pixel decides everything the depth buffer can know: a leaf
// in front of it is in open air, a leaf slightly behind it has passed into it, and a leaf deep
// behind it is merely occluded. Foliage cards are skipped when the ray traced fallback can see
// the solid ground under them, otherwise a leaf born inside a canopy would perch on its cards.
uint leaf_depth_probe(float3 position, float reach, bool skip_foliage, out float3 surface_point, out float3 surface_normal, out float height)
{
    surface_point  = position;
    surface_normal = float3(0, 1, 0);
    height         = 0.0;

    float4 clip = mul(float4(position, 1.0), buffer_frame.view_projection);
    if (clip.w <= 0.0)
    {
        return leaf_probe_unknown;
    }

    float3 ndc = clip.xyz / clip.w;
    float2 uv  = ndc.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv <= 0.0) || any(uv >= 1.0))
    {
        return leaf_probe_unknown;
    }

    int2 pixel  = int2(uv * get_render_resolution_active());
    float depth = tex_depth.Load(int3(pixel, 0)).r;
    if (depth <= 0.0)
    {
        return leaf_probe_free;
    }

    uint2 texel         = gbuffer_texel(uint2(max(pixel, 0)));
    uint normal_packed  = tex_normal[texel].x;
    float3 normal       = gbuffer_unpack_normal(normal_packed);
    float normal_length = length(normal);
    if (normal_length <= 0.1)
    {
        return leaf_probe_unknown;
    }

    if (skip_foliage && (material_parameters[gbuffer_unpack_material_index(normal_packed, tex_material[texel])].flags & foliage_material_bits) != 0u)
    {
        return leaf_probe_unknown;
    }

    surface_normal = normal / normal_length;
    float3 visible = get_position(depth, uv);
    height         = dot(position - visible, surface_normal);
    float gap      = linearize_depth(ndc.z) - linearize_depth(depth);
    float band     = 0.03 + 0.002 * clip.w;
    if (gap <= 0.0)
    {
        if (height > band)
        {
            return leaf_probe_free;
        }
    }
    else if (gap > reach + band || height > band)
    {
        return leaf_probe_unknown;
    }

    surface_point = position - surface_normal * height;
    return leaf_probe_contact;
}

// Swept ray against solid triangles, used where the depth buffer has no answer.
bool leaf_trace_contact(float3 previous, float3 next, EmitterParams emitter, out float3 contact_position, out float3 normal, bool through_vegetation = false)
{
    contact_position = next;
    normal = float3(0, 1, 0);
#ifdef RAY_TRACING_ENABLED
    if (emitter.collision_traced != 0u)
    {
        float3 travel = next - previous;
        float distance = length(travel);
        if (distance < 0.00001) return false;
        RayDesc ray;
        ray.Origin = previous;
        ray.Direction = travel / distance;
        ray.TMin = 0.001;
        ray.TMax = distance + 0.012;
        // Ignore foliage cards and grass; land on the actual road, sidewalk,
        // terrain or other solid triangle, including surfaces behind the camera.
        RayQuery<RAY_FLAG_CULL_NON_OPAQUE> query;
        query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x09, ray);
        while (query.Proceed()) {}
        // the carpet lies on the ground under a tree, not on the branches the ray crosses on the way down
        for (uint hop = 0u; through_vegetation && hop < 4u && query.CommittedStatus() != COMMITTED_NOTHING; hop++)
        {
            if ((material_parameters[geometry_infos[query.CommittedInstanceIndex()].material_index].flags & vegetation_material_bits) == 0u)
                break;

            ray.TMin = query.CommittedRayT() + 0.05;
            query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x09, ray);
            while (query.Proceed()) {}
        }
        if (query.CommittedStatus() == COMMITTED_NOTHING) return false;
        GeometryInfo geo = geometry_infos[query.CommittedInstanceIndex()];
        uint base = geo.index_offset + query.CommittedPrimitiveIndex() * 3u;
        float3 a = geometry_vertices[geo.vertex_offset + geometry_indices[base]].position;
        float3 b = geometry_vertices[geo.vertex_offset + geometry_indices[base + 1u]].position;
        float3 c = geometry_vertices[geo.vertex_offset + geometry_indices[base + 2u]].position;
        float3x3 transform = float3x3(geo.object_to_world_0.xyz, geo.object_to_world_1.xyz, geo.object_to_world_2.xyz);
        normal = safe_normalize(cross(mul(b - a, transform), mul(c - a, transform)), float3(0, 1, 0));
        if (dot(normal, ray.Direction) > 0) normal = -normal;
        contact_position = previous + ray.Direction * query.CommittedRayT();
        return true;
    }
#endif
    return false;
}

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
float3 apply_tire_aerodynamics(inout Particle p, EmitterParams emitter, float age_seconds)
{
    if (emitter.vortex_strength == 0.0 &&
        p.birth_effect.z <= 0.0 &&
        p.birth_effect.x <= 0.0 &&
        p.wake_origin.w <= 0.0)
    {
        return 0.0;
    }

    float dt = emitter.delta_time;

    // the emitter lives in the contact patch, so its own height is the tarmac datum
    float  height     = max(p.position.y - p.wake_origin.y, 0.0);
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
    if (p.birth_effect.z > 0.0 && jet_speed > 0.001)
    {
        float ground_prox = saturate(1.0 - height / rollup_layer_height);
        float lift        = jet_speed * ground_prox * p.birth_effect.z;
        p.velocity.y += lift * dt;

        // the rise is bought with jet momentum, the sheet stalls as it stands up
        p.velocity -= horizontal * (1.0 - exp(-p.birth_effect.z * ground_prox * 0.35 * dt));

        // a wall jet cannot go down, so it fans sideways, and which way is decided by the side of the
        // tread the parcel already sits on
        float3 shed_axis = safe_normalize(cross(float3(0, 1, 0), p.wake_axis.xyz), axis);
        float side = dot(p.position - p.wake_origin.xyz, shed_axis) >= 0.0 ? 1.0 : -1.0;
        p.velocity += shed_axis * side * lift * 0.45 * dt;
    }

    // thermal, strong at birth then fading as cold air mixes in, the climb it already banked carries on
    if (p.birth_effect.x > 0.0)
    {
        // Cooling uses seconds, not a fraction of the authored lifetime. Mixing cools
        // large billows faster; a long-lived trail must not remain a hot chimney.
        float mixed = sqrt(saturate(p.start_size / max(p.size, 0.001)));
        p.velocity.y += p.birth_effect.x * exp(-age_seconds * p.birth_effect.y) * mixed * dt;
    }

    if (p.wake_origin.w <= 0.0 || p.wake_axis.w <= 0.0)
    {
        return 0.0;
    }

    // Superpose BOTH Lamb-Oseen cores. Selecting only the nearest core creates a
    // discontinuity across the tread centre. The finite core has no singularity.
    // Anchor the shed wake at birth so it survives lift-off and steering changes.
    float3 travel = p.wake_axis.xyz;
    float3 across = safe_normalize(cross(float3(0, 1, 0), travel), float3(1, 0, 0));
    float radius = p.wake_axis.w;
    float3 origin = p.wake_origin.xyz + buffer_frame.wind * emitter.wind_influence * age_seconds * 0.5;
    float core_radius_sq = radius * radius * 0.30 + age_seconds * 0.045;
    float fade = exp(-age_seconds * 0.65);
    float3 flow = 0.0;
    [unroll] for (uint i = 0; i < 2; i++)
    {
        float side = i == 0 ? -1.0 : 1.0;
        float3 core = origin + across * side * (radius * 0.65 + age_seconds * 0.12)
            + float3(0, radius * 0.65 + age_seconds * 0.18, 0);
        float3 rel = p.position - core;
        float axial = dot(rel, travel);
        float3 radial = rel - travel * axial;
        float r2 = dot(radial, radial);
        float profile = (1.0 - exp(-r2 / core_radius_sq)) / max(r2, 0.0001);
        float envelope = exp(-axial * axial / (wake_pair_length * wake_pair_length));
        flow += cross(travel * side, radial) * profile * p.wake_origin.w * radius * 0.45 * fade * envelope;
    }
    return flow;
}

// Counter-rotating streamwise vortices shed behind moving bodies. Samples stay
// in world space, overlap smoothly, and lose circulation as the wake ages.
float3 body_wake_velocity(float3 position, EmitterParams emitter)
{
    if (emitter.render_mode < 2u || emitter.render_mode > 5u) return 0;
    float3 flow = 0;
    float weights = 0;
    float strongest = 0;
    [loop] for (uint i = 0; i < 64; ++i)
    {
        ParticleWakeSample wake = particle_wake[i];
        float4 center = wake.center_strength;
        if (center.w <= 0) break; // CPU packs active samples contiguously
        float4 axis = wake.axis_length;
        float4 shape = wake.shape;
        float3 delta = position - center.xyz;
        float along = dot(delta, axis.xyz);
        float radius = shape.y + shape.z * 0.55;
        if (abs(along) > axis.w || abs(delta.y) > radius * 1.8) continue;
        float3 right = cross(float3(0, 1, 0), axis.xyz);
        float across = dot(delta, right);
        if (abs(across) > shape.x + radius * 1.8) continue;
        float envelope = 1.0 - smoothstep(0.25, 1.0, abs(along) / axis.w);
        envelope *= 1.0 - smoothstep(0.6, 1.8, abs(delta.y) / radius);
        envelope *= 1.0 - smoothstep(shape.x, shape.x + radius * 1.8, abs(across));
        float decay = pow(saturate(1.0 - shape.z / 1.8), 1.5);
        float strength = center.w * decay;
        float3 swirl = 0;
        [unroll] for (int side = -1; side <= 1; side += 2)
        {
            float3 radial = right * (across - side * shape.x) + float3(0, delta.y, 0);
            float r2 = dot(radial, radial) / (radius * radius);
            // Finite core: the pair lifts between vortices and rolls outward above.
            swirl -= side * cross(axis.xyz, radial) / radius * exp(-r2);
        }
        float3 local_flow = axis.xyz * 0.65 + swirl * 1.5 + float3(0, 0.32, 0);
        flow += local_flow * strength * envelope;
        weights += envelope;
        strongest = max(strongest, envelope);
    }
    // Overlapping trail samples must not multiply one car's energy.
    return flow * strongest / max(weights, 0.001);
}

static const float leaf_contact_offset = 0.01; // resting height of the blade above its support
static const float leaf_settle_speed   = 0.12; // below this a leaf in contact counts as stopped
static const float leaf_settle_time    = 0.35; // seconds stopped before it goes to sleep

// Slow gusts over a still mean wind, so a few resting leaves skitter instead of all or none.
float leaf_gust(float3 position, float time)
{
    return 0.5 + 1.5 * pow(curl_value_noise(position * 0.06 + float3(time * 0.35, time * 0.05, time * 0.2)), 3.0);
}

void write_dead(inout Particle p, uint index)
{
    p.lifetime = 0.0;
    particle_buffer_a[index] = p;
}

// How much open sky a leaf sees, so one blown under a roof is not lit like one on the open road.
// Two rays every eighth frame, filtered over time, is enough for a value that changes this slowly.
bool update_leaf_sky(inout Particle p, uint index, EmitterParams emitter)
{
    if (((emitter.frame + index) & 7u) != 0u)
    {
        return false;
    }

    // without ray tracing the shelter grid still knows whether a roof is overhead
    if (emitter.collision_traced == 0u)
    {
        p.birth_effect.w = lerp(p.birth_effect.w, under_cover(p.position) ? 0.15 : 1.0, 0.2);
        return true;
    }

#ifdef RAY_TRACING_ENABLED
    float3 up   = dot(p.ground_plane.xyz, p.ground_plane.xyz) > 0.5 ? p.ground_plane.xyz : float3(0, 1, 0);
    float open  = 0.0;
    [unroll]
    for (uint i = 0u; i < 2u; i++)
    {
        RayDesc ray;
        ray.Origin    = p.position + up * 0.03;
        ray.Direction = safe_normalize(up + random_direction(index * 9781u + emitter.frame * 31u + i * 7u), up);
        ray.TMin      = 0.001;
        ray.TMax      = 40.0;
        RayQuery<RAY_FLAG_CULL_NON_OPAQUE> query;
        query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
        while (query.Proceed()) {}
        open += query.CommittedStatus() == COMMITTED_NOTHING ? 0.5 : 0.0;
    }

    p.birth_effect.w = lerp(p.birth_effect.w, open, 0.2);
#endif
    return true;
}

// A leaf is a thin plate. It is one body for its whole life: it tumbles in the air, collides with
// whatever the depth buffer (or the ray traced scene, where the depth buffer cannot see) says is
// there, slides under gravity, wind and friction, and sleeps once it has stopped on a surface it can
// rest on. A sleeping leaf costs one depth fetch a frame and wakes up again when its support goes
// away or when the wind or a passing car can overcome its static friction.
void simulate_leaf(inout Particle p, uint index, EmitterParams emitter)
{
    float dt      = emitter.delta_time;
    float time    = fmod((float)buffer_frame.time, 600.0f);
    float3 camera = get_camera_position();
    bool traced   = emitter.collision_traced != 0u;
    bool litter   = emitter.render_mode == 5u;

    // the pool is bounded per neighbourhood, so it is distance that frees a leaf, never a timer
    if (distance(p.position, camera) > (litter ? 60.0 : 160.0) || dot(float4(p.position, 1.0), emitter.ground_plane) < -3.0)
    {
        write_dead(p, index);
        return;
    }

    float gravity  = max(-emitter.gravity_modifier, 0.0) * 9.81;
    float friction = 0.55 + 0.5 * rng(index + 617u);
    float k_normal = emitter.drag * 9.0;  // broadside drag, a falling leaf is a parachute
    float k_plane  = emitter.drag * 2.0;  // edge-on drag, the same leaf is a glider
    // walls and a roof keep the breeze out, a leaf drifting in through a door drops near it
    float sheltered = under_cover(p.position) ? 0.12 : lerp(0.12, 1.0, saturate(p.birth_effect.w));
    float3 wind     = buffer_frame.wind * emitter.wind_influence * leaf_gust(p.position, time) * sheltered;
    float3 body    = body_wake_velocity(p.position, emitter);

    float3 contact, normal;
    float height;

    // Litter is the carpet that was already there before anyone looked. It is laid on real triangles
    // where it cannot be seen appearing, a birth that would pop in nearby is dropped instead.
    if (litter && p.ground_plane.w == 0.0)
    {
        bool found = false;
        if (traced)
        {
            found = leaf_trace_contact(p.position, p.position - float3(0, 64, 0), emitter, contact, normal, true);
        }
        else
        {
            contact = float3(p.position.x, cover_height(p.position), p.position.z);
            normal  = float3(0, 1, 0);
            found   = contact.y > rain_height_exposed * 0.5 && contact.y < p.position.y;
        }

        if (!found || normal.y < 0.45)
        {
            write_dead(p, index);
            return;
        }
        else
        {
            float3 surface_point, surface_normal;
            float3 seat  = contact + normal * leaf_contact_offset;
            bool visible = leaf_depth_probe(seat, 0.05, true, surface_point, surface_normal, height) != leaf_probe_unknown;
            if (visible && distance(seat, camera) < 35.0 && emitter.seed_visible == 0u)
            {
                write_dead(p, index);
                return;
            }

            float3 tilt = safe_normalize(normal + random_direction(index * 7919u + 3u) * 0.12, normal);
            p.position          = seat;
            p.previous_position = seat;
            p.velocity          = 0.0;
            p.wake_axis         = float4(tilt, 1.0);
            p.wake_origin       = 0.0;
            p.birth_effect.xyz  = safe_normalize(cross(tilt, random_direction(index * 104729u + 5u)), float3(1, 0, 0));
            p.ground_plane      = float4(normal, visible ? 0.0001 : 30.0);
            particle_buffer_a[index] = p;
            return;
        }
    }

    float3 n       = safe_normalize(p.wake_axis.xyz, float3(0, 1, 0));
    float3 omega   = p.wake_origin.xyz;
    float3 support = p.ground_plane.xyz;

    if (p.wake_axis.w > 0.5)
    {
        // asleep, only the questions that can wake it are asked
        float3 surface_point, surface_normal;
        uint probe       = leaf_depth_probe(p.position, 0.06, traced, surface_point, surface_normal, height);
        bool unsupported = probe == leaf_probe_free;
        if (probe == leaf_probe_unknown && traced && ((emitter.frame + index) & 15u) == 0u)
        {
            unsupported = !leaf_trace_contact(p.position + support * 0.03, p.position - support * 0.06, emitter, contact, normal);
        }

        // ray traced and rasterized geometry disagree by a few centimetres on tessellated ground,
        // the leaf follows the surface it is drawn against instead of sinking under it
        bool sunk = probe == leaf_probe_contact && height < -0.004 && height > -0.05 && dot(surface_normal, support) > 0.8;

        float3 air     = wind * 0.5 + body;
        float air_n    = dot(air, support);
        float3 air_t   = air - support * air_n;
        float load     = gravity * max(support.y, 0.0);
        bool blown     = k_plane * length(air_t) > friction * load * (1.0 + rng(index + 211u) * 0.6) || k_normal * air_n > load * 1.5;

        bool fading_in = p.ground_plane.w < 1.0;
        if (fading_in)
        {
            p.ground_plane.w += dt;
        }
        bool sky_changed = update_leaf_sky(p, index, emitter);

        if (!unsupported && !blown)
        {
            if (sunk)
            {
                p.position          = surface_point + surface_normal * leaf_contact_offset;
                p.previous_position = p.position;
            }

            if (sunk || fading_in || sky_changed)
            {
                particle_buffer_a[index] = p;
            }
            return;
        }

        // wake into the same body, the air that lifted it sets it spinning
        p.wake_axis.w = 0.0;
        p.position   += support * 0.01;
        p.velocity    = air_t * 0.35 + support * (blown ? 0.3 + min(abs(air_n), 2.0) * 0.5 : 0.0);
        omega         = cross(support, safe_normalize(air_t, float3(1, 0, 0))) * (2.0 + length(air_t));
        p.wake_origin.w = 0.0;
    }

    p.ground_plane.w = min(p.ground_plane.w + dt, 30.0);
    bool touching    = dot(support, support) > 0.5;

    // the wind near a surface is slower, the boundary layer shelters anything lying in it
    float3 air = (touching ? wind * 0.5 : wind) + body;
    p.velocity.y -= gravity * dt;

    // plate drag, strong across the blade and weak along it, so a falling leaf glides sideways
    float3 relative = p.velocity - air;
    float rel_n     = dot(relative, n);
    float3 rel_t    = relative - n * rel_n;
    relative        = n * rel_n * exp(-k_normal * dt) + rel_t * exp(-k_plane * dt);
    p.velocity      = air + relative;
    p.velocity     += curl_turbulence(p.position, p.size, time) * emitter.turbulence_strength * dt;

    // aerodynamic torque turns the blade broadside to the flow it falls through, it overshoots,
    // and that overshoot plus small eddies is the pendulum flutter and tumble of a real leaf
    float rel_speed  = length(relative);
    float3 flow      = relative / max(rel_speed, 0.0001);
    float3 broadside = flow * (dot(n, flow) >= 0.0 ? 1.0 : -1.0);
    float3 eddy      = curl_potential(p.position * 1.3 + float3(rng(index) * 97.0, rng(index + 1u) * 97.0, time * 0.6)) - 0.5;
    float3 torque    = cross(n, broadside) * rel_speed * 6.0 + eddy * (3.0 + rel_speed * 4.0);
    omega            = (omega + torque * dt) * exp(-1.5 * dt);
    float spin       = length(omega);
    if (spin > 20.0)
    {
        omega *= 20.0 / spin;
    }

    float3 new_pos = p.position + p.velocity * dt;
    float travel   = length(new_pos - p.position);

    // the depth buffer answers for everything it can see, the ray traced scene for the rest
    float3 surface_point, surface_normal;
    uint probe = leaf_depth_probe(new_pos, travel + 0.08, traced, surface_point, surface_normal, height);
    bool hit   = probe == leaf_probe_contact;
    if (hit)
    {
        contact = surface_point;
        normal  = surface_normal;
    }
    else if (probe == leaf_probe_unknown && traced)
    {
        float3 reach_end = new_pos - (touching ? support * 0.03 : float3(0, 0, 0));
        hit = leaf_trace_contact(p.position, reach_end, emitter, contact, normal);
    }
    else if (probe == leaf_probe_unknown)
    {
        // no ray tracing: the shelter grid is a coarse collider for the roofs and ground nobody sees
        float top = cover_height(new_pos);
        if (p.position.y >= top - 0.02 && new_pos.y < top + leaf_contact_offset)
        {
            hit     = true;
            contact = float3(new_pos.x, top, new_pos.z);
            normal  = float3(0, 1, 0);
        }
    }

    float timer = p.wake_origin.w;
    if (hit)
    {
        float h_new = dot(new_pos - contact, normal);
        if (h_new > leaf_contact_offset + 0.04)
        {
            hit = false;
        }
    }

    if (hit)
    {
        float h_new = dot(new_pos - contact, normal);
        float into  = max(leaf_contact_offset - h_new, 0.0);

        // depenetration beyond the leaf's own approach is the surface moving into it, a bumper or a
        // door, and the leaf leaves with that surface's speed instead of being teleported out
        float approach      = max(-dot(new_pos - p.position, normal), 0.0);
        float surface_speed = min(max(into - approach - 0.01, 0.0) / max(dt, 0.0001), 25.0);
        new_pos += normal * into;

        float v_n = dot(p.velocity, normal);
        if (v_n < surface_speed)
        {
            p.velocity += normal * (surface_speed - v_n + max(-v_n, 0.0) * 0.1);
        }

        // Coulomb friction, gravity and wind pressing the blade down set how hard it grips
        float load   = gravity * max(normal.y, 0.0) + k_normal * max(-dot(air, normal), 0.0) * 0.1;
        float3 v_t   = p.velocity - normal * dot(p.velocity, normal);
        float v_t_len = length(v_t);
        if (v_t_len > 0.0)
        {
            p.velocity -= v_t * min(friction * load * dt / v_t_len, 1.0);
        }

        // contact lays the blade down on the surface and kills the tumble
        float side = dot(n, normal) >= 0.0 ? 1.0 : -1.0;
        n          = safe_normalize(lerp(n, normal * side, 1.0 - exp(-14.0 * dt)), n);
        omega     *= exp(-10.0 * dt);
        support    = normal;

        float speed = length(p.velocity - normal * surface_speed);
        bool still  = normal.y > 0.5 && speed < leaf_settle_speed && abs(dot(n, normal)) > 0.95 && surface_speed <= 0.0;
        timer       = still ? timer + dt : 0.0;
    }
    else
    {
        support = 0.0;
        timer   = 0.0;
    }

    // integrate the orientation, the tangent follows so the blade never twists on screen
    float3 t = p.birth_effect.xyz;
    n        = safe_normalize(n + cross(omega, n) * dt, n);
    t        = t + cross(omega, t) * dt;
    t        = safe_normalize(t - n * dot(t, n), safe_normalize(cross(n, float3(0, 1, 0)), float3(1, 0, 0)));

    p.position     = new_pos;
    p.birth_effect = float4(t, p.birth_effect.w);
    p.ground_plane = float4(support, p.ground_plane.w);
    p.color.a      = p.birth_color.a;
    update_leaf_sky(p, index, emitter);

    if (timer > leaf_settle_time)
    {
        p.velocity  = 0.0;
        p.wake_axis = float4(n, 1.0);
        p.wake_origin = 0.0;
        particle_buffer_a[index] = p;
        return;
    }

    p.wake_axis   = float4(n, 0.0);
    p.wake_origin = float4(omega, timer);
    particle_buffer_a[index] = p;
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
    if (p.owner_id_low != emitter.owner_id_low || p.owner_id_high != emitter.owner_id_high ||
        index < emitter.range_start || index >= emitter.range_start + emitter.range_count)
    {
        p.lifetime = 0;
        particle_buffer_a[index] = p;
        return;
    }

    float dt = emitter.delta_time;

    p.previous_position = p.position;
    p.previous_size = p.size;

    if (is_leaf_mode(emitter.render_mode))
    {
        simulate_leaf(p, index, emitter);
        return;
    }

    float3 body_flow = body_wake_velocity(p.position, emitter);

    // integrate gravity
    p.velocity.y += emitter.gravity_modifier * 9.81 * dt;

    // Drag approaches the surrounding air velocity instead of accelerating with wind forever.
    // Exponential relaxation gives the same damping at different frame rates.
    float3 air_velocity = buffer_frame.wind * emitter.wind_influence + body_flow;
    p.velocity = lerp(air_velocity, p.velocity, exp(-emitter.drag * dt));

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
        p.velocity = air_velocity + relative * (size_old / size_new);
    }

    // divergence free turbulence, the plume shears and folds instead of inflating
    // the clock is wrapped because the drift offset is multiplied by the octave frequency and an
    // unwrapped one runs out of fractional precision after a while and the field starts to quantise
    float ts = fmod((float)buffer_frame.time, 600.0f);
    p.velocity += curl_turbulence(p.position, p.size, ts) * emitter.turbulence_strength * dt;

    float age_for_collision = 1.0 - saturate(p.lifetime / max(p.max_lifetime, 0.0001));
    float age_seconds = max(p.max_lifetime - p.lifetime, 0.0);
    float3 wake_velocity = apply_tire_aerodynamics(p, emitter, age_seconds);

    // the shoulder pair already models this wake properly, the generic radial push would double it
    // rain follows the camera, it must not be shoved aside by it
    if (p.wake_origin.w <= 0.0 && emitter.rain_occluded == 0u)
    {
        apply_moving_emitter_push(p, emitter);
    }

    // Advect through the vortex field without accumulating its velocity as acceleration.
    // Collision sees the total transport velocity; the free velocity is restored afterwards.
    p.velocity += wake_velocity;
    float3 new_pos = p.position + p.velocity * dt;

    // a drop ends on the first thing above it, the ground in the open, a roof over a garage
    if (emitter.rain_occluded != 0u)
    {
        float cell_size = buffer_frame.rain_occlusion.z;
        if (cell_size > 0.0 && new_pos.y < rain_occluder_height(int2(floor(new_pos.xz / cell_size))))
        {
            p.lifetime = 0.0;
            particle_buffer_a[index] = p;
            return;
        }
    }

    // fresh puffs need a short grace period to leave tight emitters such as exhaust tips
    const bool ballistic = dot(p.ground_plane.xyz, p.ground_plane.xyz) > 0.5;
    if (ballistic && dot(float4(new_pos, 1.0), p.ground_plane) < 0.0)
    {
        p.lifetime = 0.0;
        particle_buffer_a[index] = p;
        return;
    }
    // Tire smoke is born against the road. A lifetime-relative grace period can
    // leave it without collision for a full second, allowing the wake to bury it.
    bool collision_ready = p.birth_effect.z > 0.0 ? age_seconds > 0.03 : age_for_collision > 0.18;
    if (!ballistic && emitter.rain_occluded == 0u && collision_ready && distance(p.position, emitter.position) > emitter.collision_clearance)
    {
    #ifdef RAY_TRACING_ENABLED
        if (emitter.collision_traced != 0u)
        {
            apply_traced_collision(p, new_pos, emitter);
        }
        else
    #endif
        {
            apply_depth_collision(p, new_pos, emitter);
        }
    }

    // commit the new position
    p.position = new_pos;
    p.velocity -= wake_velocity;

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

    // A narrow young jet expands into billows, then disperses. Birth colour is immutable:
    // changing the throttle must not recolour or erase smoke already left behind.
    float te = entrains ? 1.0 - (1.0 - t) * (1.0 - t) : t;
    p.color.rgb = lerp(p.birth_color.rgb, emitter.end_color.rgb, t);

    // for an entraining parcel the authored ramp is a floor rather than the answer, so an effect still
    // reaches the size it was authored to reach when it is moving too slowly to entrain its way there,
    // and there is deliberately no ceiling, a parcel still growing at the end of its life is exactly what
    // the tail of a dissipating trail looks like
    float size_ramp = lerp(p.start_size, p.end_size, te);
    p.size = entrains ? max(p.size, size_ramp) : size_ramp;
    float dilution = entrains ? pow(saturate(p.start_size / max(p.size, 0.001)), 0.65) : 1.0;
    float fade = entrains ? smoothstep(0.45, 1.0, t) : smoothstep(0.0, 1.0, t);
    p.color.a = lerp(p.birth_color.a, emitter.end_color.a, fade) * dilution;

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
    float4 clip_current   : TEXCOORD11;
    float4 clip_previous  : TEXCOORD12;
    nointerpolation float2 emissive_shape : TEXCOORD13; // short flame, small spark
    nointerpolation float2 nature_shape : TEXCOORD14; // render mode, age in seconds
    nointerpolation float3 contact_normal : TEXCOORD15;
};

ps_input main_vs(uint vertex_id : SV_VertexID)
{
    uint  emitter_index = pass_uint(pass_particles::emitter_index);
    float use_texture   = pass_bool(pass_particles::use_texture) ? 1.0f : 0.0f;
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
    o.clip_current = o.clip_previous = float4(0, 0, 0, 1);
    o.emissive_shape = 0.0;
    o.nature_shape = float2((float)emitter.render_mode, 0.0);
    o.contact_normal = 0.0;

    Particle p = particle_buffer_a[index];
    bool leaf = is_leaf_mode(emitter.render_mode);

    // skip dead, invisible or foreign emitter particles
    if (p.lifetime <= 0.0 || p.max_lifetime <= 0.0 || p.color.a <= 0.0 || p.emitter_index != emitter_index ||
        p.owner_id_low != emitter.owner_id_low || p.owner_id_high != emitter.owner_id_high)
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
    bool luminous = emitter.lighting_mode == particle_lighting_emissive && emitter.blend_mode == particle_blend_additive;
    float taper = p.end_size / max(p.start_size, 0.001);
    bool flame = luminous && p.max_lifetime < 0.30 && taper > 0.10 && taper < 0.6;
    bool spark = luminous && p.start_size < 0.075 && taper <= 0.12;
    o.emissive_shape = float2(flame ? 1.0 : 0.0, spark ? 1.0 : 0.0);

    // screen aligned billboard basis, optionally stretched along the authored plume direction
    float3 right = safe_normalize(buffer_frame.camera_right, float3(1.0, 0.0, 0.0));
    float3 up    = safe_normalize(cross(buffer_frame.camera_forward, right), float3(0.0, 1.0, 0.0));
    float3 flow_velocity = p.velocity - emitter.emitter_velocity * emitter.velocity_inheritance;
    float  velocity_len  = length(flow_velocity);
    float3 flow_axis     = safe_normalize(lerp(emitter.emission_direction, flow_velocity, 0.35), emitter.emission_direction);
    float3 velocity_axis = flow_axis - buffer_frame.camera_forward * dot(flow_axis, buffer_frame.camera_forward);
    float projected_flow_length = length(velocity_axis);
    velocity_axis = safe_normalize(velocity_axis, right);
    float stretch = saturate(velocity_len * 0.08) * emitter.velocity_stretch;
    float alignment = (flame || spark) ? 1.0 : saturate(stretch);
    right = safe_normalize(lerp(right, velocity_axis, alignment), right);
    up    = safe_normalize(cross(buffer_frame.camera_forward, right), up);
    float half_size_x = p.size * (0.5 + stretch * 1.5);
    float half_size_y = p.size * (0.5 - saturate(stretch) * 0.2);
    // A flame starts at its nozzle and extends downstream. Centred, fully
    // stretched quads put half the jet back over the bumper in a chase view.
    // Preserve foreshortening when looking along the exhaust direction.
    if (flame)
        half_size_x *= max(projected_flow_length, 0.2);
    // a raindrop is a couple of millimetres across, what the eye sees is the streak it draws during one exposure
    if (emitter.rain_occluded != 0u)
    {
        half_size_x = max(velocity_len * 0.011 * projected_flow_length, p.size);
        half_size_y = p.size * 0.5;
    }
    float longitudinal = c.x + (flame ? 1.0 : 0.0);
    float3 world = p.position + right * longitudinal * half_size_x + up * c.y * half_size_y;
    // a leaf is drawn in its own simulated plane in flight and at rest alike, so landing is not a
    // switch between two looks, and it is lit two sided from the face the camera sees
    float3 leaf_offset = 0.0;
    if (leaf)
    {
        float3 normal  = safe_normalize(p.wake_axis.xyz, float3(0, 1, 0));
        float3 tangent = safe_normalize(p.birth_effect.xyz - normal * dot(p.birth_effect.xyz, normal), safe_normalize(cross(float3(0, 0, 1), normal), float3(1, 0, 0)));
        leaf_offset    = (tangent * c.x + cross(normal, tangent) * c.y) * p.size * 0.5;
        world          = p.position + leaf_offset;
        o.contact_normal = dot(normal, get_camera_position() - p.position) < 0.0 ? -normal : normal;
    }

    float4 clip   = mul(float4(world, 1.0),      buffer_frame.view_projection);
    float4 clip_c = mul(float4(p.position, 1.0), buffer_frame.view_projection);

    // age driven fade in, rotation and spin, matches the old compute look
    float age_t    = 1.0 - saturate(p.lifetime / p.max_lifetime);
    o.nature_shape.y = max(p.max_lifetime - p.lifetime, 0.0);
    // negative marks a leaf drawn in its own plane, minus two when the camera sees its underside
    if (leaf) o.nature_shape.y = dot(p.wake_axis.xyz, get_camera_position() - p.position) < 0.0 ? -2.0 : -1.0;
    float fade_in  = saturate(age_t / (luminous ? 0.04 : 0.2));
    if (leaf) fade_in = saturate(p.ground_plane.w / 0.75);
    float base_ang = rng(index * 2654435761u) * 6.28318530718;
    float spin     = (rng(index * 40503u + 13u) * 2.0 - 1.0) * 1.2;
    float ang      = base_ang + spin * age_t;
    // The texture's flame tip points along the jet; random full rotations read as
    // disconnected fire stickers. A little angular variation keeps the edge alive.
    if (flame)
        ang = -1.57079633 + spin * 0.16;

    float3 previous_right = normalize(float3(buffer_frame.view_previous[0][0], buffer_frame.view_previous[1][0], buffer_frame.view_previous[2][0]));
    float3 previous_up = normalize(float3(buffer_frame.view_previous[0][1], buffer_frame.view_previous[1][1], buffer_frame.view_previous[2][1]));
    float3 previous_forward = normalize(cross(previous_right, previous_up));
    float3 previous_projected_flow = flow_axis - previous_forward * dot(flow_axis, previous_forward);
    float3 previous_axis = safe_normalize(previous_projected_flow, previous_right);
    previous_right = safe_normalize(lerp(previous_right, previous_axis, alignment), previous_right);
    previous_up = safe_normalize(cross(previous_forward, previous_right), previous_up);
    float previous_length_scale = flame ? max(length(previous_projected_flow), 0.2) : 1.0;
    float3 previous_world = p.previous_position + previous_right * longitudinal * p.previous_size * (0.5 + stretch * 1.5) * previous_length_scale
        + previous_up * c.y * p.previous_size * (0.5 - saturate(stretch) * 0.2);
    if (leaf) previous_world = p.previous_position + leaf_offset;
    o.clip_current = mul(float4(world, 1), (pass_is_right_eye() ? buffer_frame.view_projection_unjittered_right : buffer_frame.view_projection_unjittered));
    o.clip_previous = mul(float4(previous_world, 1), get_view_projection_previous_unjittered());
    o.position  = clip;
    o.local     = c;
    o.color     = p.color.rgb;
    o.alpha     = p.color.a * fade_in;
    if (emitter.render_mode >= 2u)
    {
        // Airborne details dissolve near the lens, keeping cockpit/road views clear.
        float camera_distance = distance(p.position, get_camera_position());
        if (!leaf) o.alpha *= smoothstep(1.5, 5.0, camera_distance);
        if (!leaf) o.alpha *= 1.0 - smoothstep(0.85, 1.0, age_t);
        if (leaf)
        {
            // only a leaf brushing the lens fades, the far edge matches the spatial cull
            o.alpha *= smoothstep(0.3, 0.9, camera_distance);
            o.alpha *= emitter.render_mode == 5u ? 1.0 - smoothstep(50.0, 60.0, camera_distance) : 1.0 - smoothstep(140.0, 160.0, camera_distance);
        }
    }
    o.lin_depth = linearize_depth(clip_c.z / max(clip_c.w, 1e-6));
    if (leaf) o.lin_depth = linearize_depth(clip.z / max(clip.w, 1e-6));
    o.rot       = float2(cos(ang), sin(ang));
    o.use_tex   = use_texture;
    o.position_world = p.position;
    if (leaf) o.position_world = world;
    o.age_t     = age_t;
    o.render_params = float4((float)emitter.blend_mode, (float)emitter.lighting_mode, p.birth_effect.w, emitter.soft_depth_scale);
    if (leaf) o.render_params.w = 1000.0;
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
    surface.sheen_color            = 0.0f;
    surface.sheen_roughness        = 0.0f;
    surface.subsurface_scattering  = 0.0;
    surface.flake_weight           = 0.0;
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

    return rt_trace_visibility(origin, direction, t_max);
}
#endif

// a smoke quad covers a large part of the screen and puffs stack many deep, so a ray query per light
// per pixel multiplies into billions of traversals, only the primary light is allowed to trace and
// the local lights stay unshadowed, the atlas is not rendered with ray traced shadows
// translucency > 0 marks a solid blade (a leaf): its bent_normal is the geometric face, its normal
// carries the fold and veins, it gets a waxy specular lobe, and a light behind it shines through
float3 evaluate_particle_light(uint light_index, uint2 pixel, Surface surface, bool allow_ray_traced_shadow, float translucency)
{
    Light light;
    light.Build(light_index, surface);

    bool transmitted = translucency > 0.0 && dot(surface.bent_normal, light.to_pixel) > 0.0;
    Surface shadow_surface = surface;
    if (translucency > 0.0)
    {
        shadow_surface.normal = transmitted ? -surface.bent_normal : surface.bent_normal;
    }
    if (transmitted)
    {
        light.Build(light_index, shadow_surface);
    }

    if (light.has_shadows())
    {
        float shadow = 1.0;
    #ifdef RAY_TRACING_ENABLED
        if (is_ray_traced_shadows_enabled())
        {
            if (allow_ray_traced_shadow)
            {
                shadow = trace_particle_shadow_ray(light, shadow_surface);
            }
        }
        else
    #endif
        {
            shadow = compute_shadow(shadow_surface, light);
        }

        light.radiance *= shadow;
    }

    // light through a thin blade is filtered twice by its pigment, so it comes out deeper and warmer
    if (transmitted)
    {
        return surface.albedo * saturate(surface.albedo * 2.2) * light.radiance * translucency * INV_PI;
    }

    if (!any(light.radiance > 0.0))
    {
        return 0.0;
    }

    AngularInfo angular_info;
    angular_info.Build(light, surface);

    if (translucency > 0.0)
    {
        float3 specular = BRDF_Specular_Isotropic(surface, angular_info);
        return (BRDF_Diffuse(surface, angular_info) * surface.diffuse_energy + specular) * light.radiance;
    }

    return BRDF_Diffuse(surface, angular_info) * light.radiance;
}

float3 evaluate_particle_lighting(uint2 pixel, Surface surface, float translucency)
{
    // skylight, the flat eight percent term had no illuminant at all so the warm sun was the only
    // thing with a hue and smoke read brown, the divide by pi keeps it in the same units as the
    // diffuse lobe below
    // the floor is the night sky the clouds and skysphere use, a fixed one outshines a moonlit scene
    // by two orders of magnitude and auto exposure then shows every particle at full daylight
    float3 sky     = get_sky_fill_radiance() * particle_sky_fill / 3.14159265;
    float3 night   = night_sky_radiance(0.35) + night_airglow_rad * 0.8;
    float3 ambient = surface.albedo * (night + sky);

    // a leaf is an opaque surface, not a puff of scattering medium, so it takes the skylight the
    // ground around it takes, more from above than from the ground bounce below
    if (translucency > 0.0)
    {
        float hemisphere = surface.occlusion * (0.55 + 0.45 * surface.normal.y);
        ambient = surface.albedo * (night_sky_radiance(surface.normal.y) + night_airglow_rad + get_sky_fill_radiance() * leaf_sky_fill / 3.14159265) * hemisphere;
    }

    float3 result  = ambient;
    uint total_lights = buffer_frame.cluster_light_count;

    if (total_lights > 0u)
    {
        result += evaluate_particle_light(0u, pixel, surface, true, translucency);
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
                result += evaluate_particle_light(light_index, pixel, surface, false, translucency);
            }
        }
    }

    return max(result, ambient);
}

struct particle_output
{
    float4 color : SV_Target0;
    float4 velocity : SV_Target1;
    float4 reactivity : SV_Target2;
};

float leaf_hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float leaf_noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(leaf_hash(i), leaf_hash(i + float2(1, 0)), f.x), lerp(leaf_hash(i + float2(0, 1)), leaf_hash(i + float2(1, 1)), f.x), f.y);
}

particle_output main_ps(ps_input input)
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
    bool  textured = input.use_tex > 0.5 && input.emissive_shape.y < 0.5;
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
    float leaf_height = 0.0;
    bool leaf_blade   = false;
    uint nature = (uint)round(input.nature_shape.x);
    if (nature >= 2u)
    {
        float phase = input.flipbook.w * 6.2831853;
        float seconds = max(input.nature_shape.y, 0.0);
        float angle = phase + seconds * (0.7 + input.flipbook.w);
        float2 q = float2(input.local.x * cos(angle) - input.local.y * sin(angle),
                          input.local.x * sin(angle) + input.local.y * cos(angle));
        falloff = 1.0;
        if ((nature == 2u || nature == 5u))
        {
            // blade outline, each leaf with its own width, taper and serration, around a slightly bent midrib
            float seed   = input.flipbook.w;
            float center = 0.08 * sin(q.y * 3.0 + phase);
            float dx     = q.x - center;
            float along  = q.y / 0.78; // minus one at the stem end, one at the tip
            float taper  = lerp(1.1, 2.0, frac(seed * 7.13));
            float width  = lerp(0.24, 0.42, seed) * saturate(1.0 - pow(abs(along), taper));
            width       *= 1.0 + 0.07 * sin(atan2(dx, q.y) * lerp(14.0, 28.0, frac(seed * 5.31)));
            float edge   = max(abs(dx) - width, abs(q.y) - 0.78);
            float aa     = max(fwidth(edge), 0.008);
            float blade  = 1.0 - smoothstep(-aa, aa, edge);
            float stem   = (1.0 - smoothstep(0.009, 0.025, abs(dx))) * (1.0 - smoothstep(0.80, 0.93, abs(q.y)));
            alpha_mask   = max(blade, stem);
            float vein   = exp(-abs(dx) * 110.0);
            float ribs   = pow(saturate(cos((q.y - abs(dx) * 0.8) * 22.0)), 6.0);

            // height over the blade in quad units: the halves rise from a creased midrib, the blade
            // cups or arches, the tip and rim curl, and the side veins stand slightly proud
            leaf_height = abs(dx) * 0.28 + dx * dx * lerp(-0.4, 0.7, frac(seed * 11.1)) + along * along * 0.07 + ribs * 0.012 - vein * 0.015;
            leaf_blade  = true;

            // a dry leaf is never one flat colour: fresher at the stem, drier at the tip and rim,
            // with rot blotches and specks, a pale midrib, and a duller, paler underside
            float2 nq    = q * 2.5 + seed * 37.0;
            float blotch = leaf_noise(nq) * 0.65 + leaf_noise(nq * 2.9 + 5.0) * 0.35;
            float rim    = smoothstep(-0.10, 0.0, edge);
            float3 tint  = base_color * lerp(float3(1.10, 1.06, 0.80), float3(0.88, 0.80, 0.74), along * 0.5 + 0.5);
            tint         = lerp(tint, tint * float3(0.50, 0.38, 0.28), smoothstep(0.55, 0.85, blotch) * 0.85);
            tint         = lerp(tint, tint * float3(0.62, 0.50, 0.40), rim * 0.7);
            tint        *= 1.0 - 0.45 * pow(leaf_noise(q * 15.0 + seed * 91.0), 6.0);
            tint        *= 1.0 + vein * 0.18 - ribs * 0.06;
            if (input.nature_shape.y < -1.5)
            {
                tint = lerp(tint, dot(tint, float3(0.3, 0.59, 0.11)) * float3(1.05, 1.0, 0.9), 0.3) * 1.12;
            }
            base_color = tint;
        }
        else if (nature == 3u)
        {
            float2 tuft = q - float2(0, 0.12);
            float r = length(tuft);
            float a = atan2(tuft.y, tuft.x);
            float filament = pow(abs(cos(a * 7.0 + phase)), 22.0);
            float halo = exp(-r * r * 9.0);
            float stem = exp(-q.x * q.x * 1800.0) * (1.0 - smoothstep(0.25, 0.65, abs(q.y + 0.15)));
            alpha_mask = saturate(halo * (0.12 + 0.8 * filament) + stem * 0.65);
        }
        else if (nature == 4u)
        {
            float pulse = pow(0.5 + 0.5 * sin(seconds * 2.6 + phase), 3.0);
            alpha_mask = exp(-dot(q, q) * 18.0) * (0.12 + 0.88 * pulse);
        }
    }
    if (input.emissive_shape.y > 0.5)
    {
        // A hot grain has a narrow incandescent core, not a miniature smoke sprite.
        alpha_mask = exp(-input.local.y * input.local.y * 45.0) * saturate(1.0 - input.local.x * input.local.x);
    }
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
        // Keep the authored hot-core colour at birth; the orange texture should
        // contribute its detail without filtering all the blue out of a fresh jet.
        float brightness = dot(sample.rgb, float3(0.2126, 0.7152, 0.0722));
        float3 texture_color = input.emissive_shape.x > 0.5
            ? lerp(sample.rgb, brightness.xxx, 0.35 * (1.0 - smoothstep(0.1, 0.8, input.age_t))) : sample.rgb;
        base_color   *= texture_color;
        alpha_mask    = sample.a;
    }

    // the blade's relief bends the normal through screen derivatives (mikkelsen 2010), so a quad
    // needs no tangent frame and the fold survives whatever orientation the leaf tumbles into,
    // evaluated outside any branch so the derivatives stay defined
    float3 dpdx        = ddx(input.position_world);
    float3 dpdy        = ddy(input.position_world);
    float local_rate   = length(ddx(input.local)) + length(ddy(input.local));
    float relief       = leaf_height * (length(dpdx) + length(dpdy)) / max(local_rate, 1e-6);
    float relief_dx    = ddx(relief);
    float relief_dy    = ddy(relief);

    uint lighting_mode = (uint)round(input.render_params.y);
    float3 lit_color = base_color;
    if (lighting_mode == particle_lighting_lit)
    {
        Surface surface = build_particle_surface(input.position_world, base_color, uint2(pixel), input.local);
        if (dot(input.contact_normal, input.contact_normal) > 0.5)
        {
            surface.normal = input.contact_normal;
            surface.bent_normal = input.contact_normal;
        }
        float translucency = 0.0;
        if (leaf_blade)
        {
            float3 face = surface.normal;
            float3 r1   = cross(dpdy, face);
            float3 r2   = cross(face, dpdx);
            float det   = dot(dpdx, r1);
            float3 grad = sign(det) * (relief_dx * r1 + relief_dy * r2);
            surface.normal          = safe_normalize(abs(det) * face - grad, face);
            surface.bent_normal     = face;
            surface.roughness       = input.nature_shape.y < -1.5 ? 0.8 : 0.55; // waxy cuticle on top, matte below
            surface.roughness_alpha = surface.roughness * surface.roughness;
            surface.occlusion       = lerp(0.3, 1.0, saturate(input.render_params.z)); // open sky fraction, a covered leaf still gets the bounce
            translucency            = leaf_translucency;
        }
        lit_color = evaluate_particle_lighting(uint2(pixel), surface, translucency);
    }
    else if (lighting_mode == particle_lighting_emissive)
    {
        lit_color = base_color * max(input.render_params.z, 1.0);
        if (input.emissive_shape.x > 0.5)
            lit_color *= lerp(0.5, 0.09, smoothstep(0.15, 1.0, input.age_t));
    }

    float alpha = saturate(alpha_mask * input.alpha * soft_factor * falloff);
    if (alpha <= 0.0)
    {
        discard;
    }

    float camera_distance = distance(input.position_world, get_camera_position());
    float2 fog_resolution;
    tex_depth.GetDimensions(fog_resolution.x, fog_resolution.y);
    FogTransport fog = sample_fog_volume(render_uv_to_screen_uv(input.position.xy / fog_resolution), camera_distance);
    uint blend_mode = (uint)round(input.render_params.x);
    lit_color *= fog.transmittance;
    if (blend_mode != particle_blend_additive)
        lit_color += fog.scattering;
    particle_output result;
    float2 velocity = input.clip_current.xy / max(input.clip_current.w, 1e-5) - input.clip_previous.xy / max(input.clip_previous.w, 1e-5);
    result.color = float4(lit_color, alpha);
    result.velocity = float4(velocity, 0, alpha);
    result.reactivity = float4(1, 0, 0, alpha);
    if (blend_mode == particle_blend_additive)
    {
        result.color = float4(lit_color * alpha, 0);
        result.velocity = 0; // additive light cannot replace the opaque surface motion
        result.reactivity = float4(alpha, 0, 0, 0);
    }
    else if (blend_mode == particle_blend_premultiplied)
    {
        result.color.rgb *= alpha;
        result.velocity.xy *= alpha;
        result.reactivity.x = alpha;
    }
    return result;
}

#endif
