/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// The smoke fluid solver.
//
// Smoke is carried by air, and air around a car is anything but still: the tread drags a layer of it
// around the tire, the body shoves it aside and pulls a wake in behind, hot gas climbs through it and
// every one of those motions folds the others over. Particles pushed by a handful of analytic vortices
// cannot do that, each parcel only knows about the formulas and never about its neighbours, which is
// why a plume made of them reads as separate puffs. Here the air itself is solved on a grid as an
// incompressible fluid, so whatever stirs it stirs all the smoke standing in it at once.
//
// one step, in the order the kernels run:
//   advect      carries last frame's velocity and smoke along the flow, then adds heat lift, the
//               emitters and the drag of the surfaces the air touches
//   vorticity   measures how much the air is spinning
//   confine     hands back the small eddies the advection smeared out
//   divergence  measures where the air is being squeezed or pulled apart, solids included
//   pressure    relaxes the pressure that undoes it, run several times
//   project     removes the pressure gradient, which leaves a flow that goes around things

#include "common.hlsl"
#include "common_particle_fluid.hlsl"

Texture3D<float4> tex_fluid_aux      : register(t53); // vorticity kernels: xyz = curl, w = its length, pressure kernels: r = divergence, g = wall mask
Texture3D<float>  tex_fluid_pressure : register(t54);
[[vk::image_format("unknown")]] RWTexture3D<float4> tex_fluid_velocity_uav : register(u77);
[[vk::image_format("unknown")]] RWTexture3D<float4> tex_fluid_scalar_uav   : register(u78);
[[vk::image_format("unknown")]] RWTexture3D<float4> tex_fluid_aux_uav      : register(u79);
[[vk::image_format("unknown")]] RWTexture3D<float>  tex_fluid_pressure_uav : register(u80);
RWStructuredBuffer<ParticleFluidRecord> particle_fluid_scene : register(u81);

// keep in sync with the record layout of particle_fluid in ParticleFluid.h and Renderer::Pass_Particles_Fluid
static const uint fluid_body_offset   = 0u;  // two records per box
static const uint fluid_wheel_offset  = 16u;
static const uint fluid_source_offset = 32u;

// the share of the wind the air near the ground actually moves with, matches what the particles take
static const float fluid_wind_influence   = 0.35f;
// how fast stirred air comes back to rest, the eddies of a burnout die down over a few seconds
static const float fluid_settle_rate      = 0.22f;
// heat is stored as the lift it gives in metres per second squared, it mixes away as the plume entrains
// cold air and the grid already does that on its own, so this is only what radiates and conducts
static const float fluid_cooling_rate     = 1.1f;
// smoke thins as it spreads below what the grid can hold, this is the tail of that
static const float fluid_dissipation_rate = 0.16f;
// the emitters hand over the smoke their particles would have carried, a parcel thins out by fading over
// its life while the grid keeps what it is given, this is the share of it that stays visible
static const float fluid_source_gain      = 0.35f;
// seconds over which the age that drives the detail noise runs from zero to one
static const float fluid_age_span         = 6.0f;
// vorticity confinement, scales the force that spins existing eddies back up
static const float fluid_confinement      = 2.4f;
// thickness in cells of the layer of air a surface carries with it
static const float fluid_boundary_cells   = 1.6f;
// the gap between a tread and its arch in cells, narrower than the grid can hold air in and the tire would
// be sealed in, and how far past the faces of the tire the arch runs in metres
static const float fluid_arch_cells       = 1.3f;
static const float fluid_arch_reach       = 0.30f;
// how fast the air in that layer takes up the speed of the surface, per second at the surface itself,
// the layer is as thick as the grid makes it, many times what really clings to a tread, so the tread
// gets a rate that moves about as much air as a real one does rather than a no slip wall, which at this
// thickness is a pump that blows the smoke out of the arch and forward along the sill
static const float fluid_grip_ground      = 3.0f;
static const float fluid_grip_body        = 16.0f;
static const float fluid_grip_wheel       = 9.0f;
static const float fluid_speed_max        = 55.0f;
// wall mask, one bit per neighbour that is solid, and one for a cell that is solid itself
static const uint  fluid_mask_solid       = 64u;

static const int3 fluid_neighbours[6] =
{
    int3(-1, 0, 0), int3(1, 0, 0),
    int3(0, -1, 0), int3(0, 1, 0),
    int3(0, 0, -1), int3(0, 0, 1)
};

bool fluid_in_grid(int3 cell)
{
    return all(cell >= 0) && all(cell < int3(fluid_resolution));
}

float3 fluid_cell_center(uint3 cell)
{
    return fluid_origin() + (float3(cell) + 0.5f) * fluid_cell_size();
}

float3 fluid_ambient_velocity()
{
    return buffer_frame.wind * fluid_wind_influence;
}

#if defined(FLUID_ADVECT)

struct FluidSolid
{
    float  distance; // metres to the nearest surface, negative inside it
    float3 velocity; // of that surface at this point
    float  grip;     // how fast the air touching it is brought up to its speed, per second
    bool   ground;
};

float fluid_box_distance(float3 local, float3 extents)
{
    float3 q = abs(local) - extents;
    return length(max(q, 0.0f)) + min(max(q.x, max(q.y, q.z)), 0.0f);
}

FluidSolid fluid_solid_at(float3 position)
{
    FluidSolid solid;
    float4 plane   = pass_float4(pass_particles_fluid::ground_plane);
    solid.distance = dot(plane.xyz, position) + plane.w;
    solid.velocity = 0.0f;
    solid.grip     = fluid_grip_ground;
    solid.ground   = true;

    // the wheels, and around each of them the arch the body leaves for it, which is the wheel grown by
    // the gap the tire turns in, long enough across to come out through the side of the car
    float arch        = 1e6f;
    float arch_gap    = max(0.10f, fluid_cell_size() * fluid_arch_cells);
    uint  wheel_count = pass_uint(pass_particles_fluid::wheel_count);
    [loop]
    for (uint w = 0u; w < wheel_count; w++)
    {
        ParticleFluidRecord wheel = particle_fluid_scene[fluid_wheel_offset + w];
        float3 rel    = position - wheel.a.xyz;
        float  along  = dot(rel, wheel.b.xyz);
        float3 radial = rel - wheel.b.xyz * along;
        float2 q      = float2(length(radial) - wheel.a.w, abs(along) - wheel.b.w);
        float  d      = length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f);
        if (d < solid.distance)
        {
            // the tread carries its own surface speed, the hub only the speed of the car
            solid.distance = d;
            solid.velocity = wheel.c.xyz + cross(wheel.b.xyz * wheel.c.w, radial);
            solid.grip     = fluid_grip_wheel;
            solid.ground   = false;
        }

        float2 q_arch = q - float2(arch_gap, fluid_arch_reach);
        arch          = min(arch, length(max(q_arch, 0.0f)) + min(max(q_arch.x, q_arch.y), 0.0f));
    }

    // the bodies, a box each with the arches hollowed out of it, a box without them would bury the tires
    // and one cut away along its whole length would be a duct for the treads to blow smoke forward through
    uint body_count = pass_uint(pass_particles_fluid::body_count);
    [loop]
    for (uint b = 0u; b < body_count; b++)
    {
        ParticleFluidRecord shape  = particle_fluid_scene[fluid_body_offset + b * 2u];
        float3 rel   = position - shape.a.xyz;
        float3 local = float3(dot(rel, shape.b.xyz), dot(rel, shape.c.xyz), dot(rel, shape.d.xyz));
        float  d     = max(fluid_box_distance(local, float3(shape.b.w, shape.c.w, shape.d.w)), -arch);
        if (d < solid.distance)
        {
            ParticleFluidRecord motion = particle_fluid_scene[fluid_body_offset + b * 2u + 1u];
            solid.distance = d;
            solid.velocity = motion.a.xyz + cross(motion.b.xyz, position - motion.c.xyz);
            solid.grip     = fluid_grip_body;
            solid.ground   = false;
        }
    }

    return solid;
}

float3 fluid_previous_uvw(float3 position)
{
    return (position - pass_float3(pass_particles_fluid::origin_previous)) / fluid_size();
}

// what stood at a point last frame, anything from beyond the grid arrives as clean ambient air
float3 fluid_previous_velocity(float3 position, float3 ambient)
{
    float3 uvw = fluid_previous_uvw(position);
    if (any(uvw < 0.0f) || any(uvw > 1.0f))
    {
        return ambient;
    }

    return tex_fluid_velocity.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uvw, 0.0f).xyz;
}

float4 fluid_previous_scalar(float3 position)
{
    float3 uvw = fluid_previous_uvw(position);
    if (any(uvw < 0.0f) || any(uvw > 1.0f))
    {
        return 0.0f;
    }

    return tex_fluid_scalar.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uvw, 0.0f);
}

[numthreads(8, 8, 4)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= fluid_resolution))
    {
        return;
    }

    float  h        = fluid_cell_size();
    float  dt       = pass_float(pass_particles_fluid::delta_time);
    bool   history  = pass_bool(pass_particles_fluid::history_valid);
    float3 position = fluid_cell_center(id);
    float3 ambient  = fluid_ambient_velocity();

    FluidSolid solid = fluid_solid_at(position);
    if (solid.distance < 0.0f)
    {
        // the filter reads across the surface, a floor holding nothing would bleed the smoke lying on
        // it away one sample at a time, so the ground keeps a copy of what rests on top of it
        float4 resting = 0.0f;
        if (solid.ground && history)
        {
            float3 normal = pass_float4(pass_particles_fluid::ground_plane).xyz;
            resting = fluid_previous_scalar(position + normal * (floor(-solid.distance / h) + 1.0f) * h);
        }

        tex_fluid_velocity_uav[id] = float4(solid.velocity, 1.0f);
        tex_fluid_scalar_uav[id]   = resting;
        return;
    }

    // semi lagrangian, follow the flow back to where this parcel of air came from and take what was
    // there, a midpoint step so a parcel going around a tire is traced around it rather than through it
    float3 velocity = ambient;
    float4 scalar   = 0.0f;
    if (history)
    {
        float3 v0   = fluid_previous_velocity(position, ambient);
        float3 v1   = fluid_previous_velocity(position - v0 * dt * 0.5f, ambient);
        float3 from = position - v1 * dt;
        velocity    = fluid_previous_velocity(from, ambient);
        scalar      = max(fluid_previous_scalar(from), 0.0f);
    }

    // hot gas climbs, and gives the heat up as it goes
    velocity.y += scalar.g * dt;
    scalar.g   *= exp(-fluid_cooling_rate * dt);

    // the smoke ages, the age is stored density weighted so mixing old smoke with new averages it
    scalar.b += scalar.r * dt / fluid_age_span;
    scalar.rba *= exp(-fluid_dissipation_rate * dt);

    velocity = lerp(ambient, velocity, exp(-fluid_settle_rate * dt));

    // emitters
    uint source_count = pass_uint(pass_particles_fluid::source_count);
    [loop]
    for (uint s = 0u; s < source_count; s++)
    {
        ParticleFluidRecord source = particle_fluid_scene[fluid_source_offset + s];

        // the emitter travelled from d to a over the step, depositing along the whole segment keeps a
        // fast car from laying a row of separate blobs
        float3 start   = source.d.xyz;
        float3 travel  = source.a.xyz - start;
        float  length2 = dot(travel, travel);
        float  along   = length2 > 1e-8f ? saturate(dot(position - start, travel) / length2) : 1.0f;
        float3 delta   = position - (start + travel * along);
        float  radius  = source.a.w;
        float  d2      = dot(delta, delta) / (radius * radius);
        if (d2 > 9.0f)
        {
            continue;
        }

        // divided by the volume of the kernel, so what arrives is the same whatever its width
        float falloff = exp(-0.5f * d2);
        float volume  = 15.75f * radius * radius * radius + sqrt(length2) * 6.283f * radius * radius;
        float deposit = source.b.w * fluid_source_gain * dt * falloff / volume;

        scalar.r += deposit;
        scalar.a += deposit * source.c.y;
        scalar.g  = max(scalar.g, source.c.x * falloff);
        velocity  = lerp(velocity, source.b.xyz, saturate(falloff * source.c.z * dt));
    }

    // no slip, the air touching a surface moves with it, this is what winds a vortex around a spinning
    // tire and what makes a passing body tow its wake
    float layer = fluid_boundary_cells * h;
    if (solid.distance < layer)
    {
        float touch = 1.0f - solid.distance / layer;
        velocity    = lerp(velocity, solid.velocity, 1.0f - exp(-solid.grip * touch * touch * dt));
    }

    // the open faces let ambient air in and the flow out without a wall to bounce off
    float3 size  = fluid_size();
    float3 local = position - fluid_origin();
    float  edge  = min(min(local.x, size.x - local.x), min(local.z, size.z - local.z));
    edge         = min(edge, size.y - local.y);
    velocity     = lerp(velocity, ambient, (1.0f - saturate(edge / (4.0f * h))) * saturate(dt * 8.0f));

    float speed = length(velocity);
    if (speed > fluid_speed_max)
    {
        velocity *= fluid_speed_max / speed;
    }

    if (scalar.r < 0.0005f)
    {
        scalar.rba = 0.0f;
    }

    tex_fluid_velocity_uav[id] = float4(velocity, 0.0f);
    tex_fluid_scalar_uav[id]   = scalar;
}

#elif defined(FLUID_VORTICITY)

float3 fluid_load_velocity(int3 cell)
{
    return tex_fluid_velocity.Load(int4(clamp(cell, 0, int3(fluid_resolution) - 1), 0)).xyz;
}

[numthreads(8, 8, 4)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= fluid_resolution))
    {
        return;
    }

    int3 cell = int3(id);
    float3 x0 = fluid_load_velocity(cell + fluid_neighbours[0]);
    float3 x1 = fluid_load_velocity(cell + fluid_neighbours[1]);
    float3 y0 = fluid_load_velocity(cell + fluid_neighbours[2]);
    float3 y1 = fluid_load_velocity(cell + fluid_neighbours[3]);
    float3 z0 = fluid_load_velocity(cell + fluid_neighbours[4]);
    float3 z1 = fluid_load_velocity(cell + fluid_neighbours[5]);

    float3 curl = float3((y1.z - y0.z) - (z1.y - z0.y),
                         (z1.x - z0.x) - (x1.z - x0.z),
                         (x1.y - x0.y) - (y1.x - y0.x)) * (0.5f / fluid_cell_size());

    tex_fluid_aux_uav[id] = float4(curl, length(curl));
}

#elif defined(FLUID_CONFINE)

float fluid_load_spin(int3 cell)
{
    return tex_fluid_aux.Load(int4(clamp(cell, 0, int3(fluid_resolution) - 1), 0)).w;
}

// the advection averages neighbouring cells every step, and what that erases first is the smallest
// eddies, exactly the ones that make smoke curl, the gradient of the spin points at the middle of an
// eddy and pushing the air around that middle winds it back up without inventing any motion that was
// not there
[numthreads(8, 8, 4)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= fluid_resolution))
    {
        return;
    }

    int3   cell     = int3(id);
    float4 velocity = tex_fluid_velocity.Load(int4(cell, 0));
    if (velocity.w < 0.5f)
    {
        float  h    = fluid_cell_size();
        float3 curl = tex_fluid_aux.Load(int4(cell, 0)).xyz;
        float3 gradient = float3(fluid_load_spin(cell + fluid_neighbours[1]) - fluid_load_spin(cell + fluid_neighbours[0]),
                                 fluid_load_spin(cell + fluid_neighbours[3]) - fluid_load_spin(cell + fluid_neighbours[2]),
                                 fluid_load_spin(cell + fluid_neighbours[5]) - fluid_load_spin(cell + fluid_neighbours[4]));
        float steepness = length(gradient);
        if (steepness > 1e-4f)
        {
            velocity.xyz += cross(gradient / steepness, curl) * fluid_confinement * h * pass_float(pass_particles_fluid::delta_time);
        }
    }

    tex_fluid_velocity_uav[id] = velocity;
}

#elif defined(FLUID_DIVERGENCE)

[numthreads(8, 8, 4)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= fluid_resolution))
    {
        return;
    }

    int3   cell   = int3(id);
    float  h      = fluid_cell_size();
    float4 centre = tex_fluid_velocity.Load(int4(cell, 0));
    if (centre.w > 0.5f)
    {
        tex_fluid_aux_uav[id]      = float4(0.0f, (float)fluid_mask_solid, 0.0f, 0.0f);
        tex_fluid_pressure_uav[id] = 0.0f;
        return;
    }

    // a solid neighbour contributes the speed of its own surface, so a body moving through the air
    // shows up as squeezing in front of it and suction behind, an open face contributes no change
    uint   mask = 0u;
    float3 v[6];
    [unroll]
    for (uint i = 0u; i < 6u; i++)
    {
        int3 neighbour = cell + fluid_neighbours[i];
        v[i] = centre.xyz;
        if (fluid_in_grid(neighbour))
        {
            float4 value = tex_fluid_velocity.Load(int4(neighbour, 0));
            v[i] = value.xyz;
            if (value.w > 0.5f)
            {
                mask |= 1u << i;
            }
        }
        else if (i == 2u)
        {
            // nothing leaves through the floor of the box
            v[i]  = 0.0f;
            mask |= 1u << i;
        }
    }

    float divergence = ((v[1].x - v[0].x) + (v[3].y - v[2].y) + (v[5].z - v[4].z)) * (0.5f / h);
    tex_fluid_aux_uav[id] = float4(divergence, (float)mask, 0.0f, 0.0f);

    // the pressure changes little from one frame to the next, starting the relaxation from last frame's
    // answer is worth several times the iterations it then runs, the grid scrolls in whole cells so the
    // old answer is an exact lookup
    float pressure = 0.0f;
    if (pass_bool(pass_particles_fluid::history_valid))
    {
        int3 previous = cell + int3(round((fluid_origin() - pass_float3(pass_particles_fluid::origin_previous)) / h));
        if (fluid_in_grid(previous))
        {
            pressure = tex_fluid_pressure.Load(int4(previous, 0));
        }
    }
    tex_fluid_pressure_uav[id] = pressure;
}

#elif defined(FLUID_PRESSURE)

[numthreads(8, 8, 4)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= fluid_resolution))
    {
        return;
    }

    int3   cell = int3(id);
    float  h    = fluid_cell_size();
    float2 aux  = tex_fluid_aux.Load(int4(cell, 0)).xy;
    uint   mask = (uint)(aux.y + 0.5f);
    if (mask >= fluid_mask_solid)
    {
        tex_fluid_pressure_uav[id] = 0.0f;
        return;
    }

    // a wall pushes back exactly as hard as it is pushed, so it reads as the cell's own pressure, and
    // the air beyond an open face is at rest
    float centre = tex_fluid_pressure.Load(int4(cell, 0));
    float sum    = 0.0f;
    [unroll]
    for (uint i = 0u; i < 6u; i++)
    {
        int3 neighbour = cell + fluid_neighbours[i];
        if ((mask & (1u << i)) != 0u)
        {
            sum += centre;
        }
        else if (fluid_in_grid(neighbour))
        {
            sum += tex_fluid_pressure.Load(int4(neighbour, 0));
        }
    }

    tex_fluid_pressure_uav[id] = (sum - h * h * aux.x) / 6.0f;
}

#elif defined(FLUID_PROJECT)

[numthreads(8, 8, 4)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= fluid_resolution))
    {
        return;
    }

    int3   cell     = int3(id);
    float  h        = fluid_cell_size();
    float4 velocity = tex_fluid_velocity.Load(int4(cell, 0));
    if (velocity.w > 0.5f)
    {
        tex_fluid_velocity_uav[id] = velocity;
        return;
    }

    uint  mask   = (uint)(tex_fluid_aux.Load(int4(cell, 0)).y + 0.5f);
    float centre = tex_fluid_pressure.Load(int4(cell, 0));
    float p[6];
    [unroll]
    for (uint i = 0u; i < 6u; i++)
    {
        int3 neighbour = cell + fluid_neighbours[i];
        p[i] = 0.0f;
        if ((mask & (1u << i)) != 0u)
        {
            p[i] = centre;
        }
        else if (fluid_in_grid(neighbour))
        {
            p[i] = tex_fluid_pressure.Load(int4(neighbour, 0));
        }
    }

    float3 flow = velocity.xyz - float3(p[1] - p[0], p[3] - p[2], p[5] - p[4]) * (0.5f / h);

    // the relaxation is never run to the end, so what is left could still carry air into a wall, and air
    // cannot go into a wall, it may leave one freely and the pressure is what then fills in behind it
    [unroll]
    for (uint axis = 0u; axis < 3u; axis++)
    {
        if ((mask & (1u << (axis * 2u))) != 0u)
        {
            int3 neighbour = cell + fluid_neighbours[axis * 2u];
            float wall = fluid_in_grid(neighbour) ? tex_fluid_velocity.Load(int4(neighbour, 0))[axis] : 0.0f;
            flow[axis] = max(flow[axis], wall);
        }
        if ((mask & (1u << (axis * 2u + 1u))) != 0u)
        {
            float wall = tex_fluid_velocity.Load(int4(cell + fluid_neighbours[axis * 2u + 1u], 0))[axis];
            flow[axis] = min(flow[axis], wall);
        }
    }

    tex_fluid_velocity_uav[id] = float4(flow, 0.0f);
}

#endif
