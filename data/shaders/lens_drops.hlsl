/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// the water on the front element of the camera that follows the car, one thread per drop
// the cpu only says what the glass feels (CarLens.h), every step runs here, in this order:
// CLEAR    empties the merge grid and the work half, wipes the glass when asked
// SIMULATE moves the live drops under gravity, the camera's acceleration and the air, writes the survivors to the work half
// SPAWN    adds this step's rain and tyre mist impacts to the work half
// INSERT   links every drop of the work half into the cell of the grid it sits in
// TARGET   every drop finds the biggest drop it touches, the one it will flow into
// MERGE    a drop gathers everything that flows into it, at the centre of mass and with the momentum, into the live half
// LIGHTS   finds the lights of the frame, what every drop focuses into a bright spot
// a drop whose target flows on into another waits a step, so a chain never loses water

#include "common.hlsl"
#include "common_water_drops.hlsl"

RWStructuredBuffer<LensDrop> lens_drops : register(u93);
RWStructuredBuffer<uint> lens_state     : register(u94);

static const uint  lens_none          = 0xffffffffu;
static const float lens_glass_margin  = 1.15f;   // the glass reaches past the frame, a drop there is not seen but can come back
static const float lens_hysteresis    = 0.025f;  // the hydrophobic coating of a front element
static const float lens_stagnation    = 1.5f;    // the head on air parts at the dome's centre and runs out over it at 3/2 the airspeed times the distance over the dome's radius, a sphere's stagnation flow
static const float lens_wake_shelter  = 0.25f;   // air coming from behind, the glass sits in the camera's own wake
static const float lens_mass_min      = 1.0e-8f; // kg, anything lighter dries without being seen
static const float lens_speck_size    = 2.0e-4f; // metres, the specks on the coating that snag a contact line
static const float lens_spray_diameter = 3.0e-4f;
static const float lens_light_excess  = 4.0f;    // a light is brighter than this many times the frame's mean
// art direction over the physics, the eye needs time to take a drop in: a fresh one sits while it wets in, then runs slower than it would
static const float lens_settle_hold   = 5.0f;    // extra pinning at impact, times the coating's own
static const float lens_settle_time   = 1.5f;    // seconds that extra hold fades over
static const float lens_slide_pace    = 0.4f;    // of the physical sliding speed

float lens_half_height() { return buffer_frame.lens_rain.z; }
float lens_aspect()      { return buffer_frame.lens_rain.w; }

bool lens_on_glass(float2 p)
{
    float hh = lens_half_height();
    return abs(p.x) <= hh * lens_aspect() * lens_glass_margin && abs(p.y) <= hh * lens_glass_margin;
}

// specks on the coating make every spot a little different, which is what makes a running drop zig-zag
float lens_pinning(float2 p)
{
    int2 c      = int2(floor(p / lens_speck_size));
    float speck = float(drop_hash(uint(c.x) * 2654435761u ^ uint(c.y) * 1013904223u) & 0xffff) / 65535.0f;
    return drop_pinning(lens_hysteresis) * (0.8f + 0.4f * speck);
}

// patchy wettability of the coating, 0 to 1, smooth over about half a millimetre
float lens_patchiness(float2 p)
{
    float2 g = p / 5.0e-4f;
    int2 c   = int2(floor(g));
    float2 f = g - float2(c);
    f        = f * f * (3.0f - 2.0f * f);
    float a  = float(drop_hash(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u) & 0xffff) / 65535.0f;
    float b  = float(drop_hash(uint(c.x + 1) * 73856093u ^ uint(c.y) * 19349663u) & 0xffff) / 65535.0f;
    float d  = float(drop_hash(uint(c.x) * 73856093u ^ uint(c.y + 1) * 19349663u) & 0xffff) / 65535.0f;
    float e  = float(drop_hash(uint(c.x + 1) * 73856093u ^ uint(c.y + 1) * 19349663u) & 0xffff) / 65535.0f;
    return lerp(lerp(a, b, f.x), lerp(d, e, f.x), f.y);
}

uint lens_grid_cell(float2 p)
{
    float2 l = p / lens_half_height();
    int x    = clamp(int(floor((l.x + LENS_GRID_CELL * LENS_GRID_WIDTH * 0.5f) / LENS_GRID_CELL)), 0, LENS_GRID_WIDTH - 1);
    int y    = clamp(int(floor((l.y + LENS_GRID_CELL * LENS_GRID_HEIGHT * 0.5f) / LENS_GRID_CELL)), 0, LENS_GRID_HEIGHT - 1);
    return uint(y) * LENS_GRID_WIDTH + uint(x);
}

void lens_append(LensDrop d)
{
    uint slot;
    InterlockedAdd(lens_state[LENS_STATE_COUNT_WORK], 1u, slot);
    if (slot >= LENS_DROPS_MAX)
        return;

    lens_drops[LENS_DROPS_MAX + slot] = d;
    uint previous;
    InterlockedMax(lens_state[LENS_STATE_RADIUS_MAX], asuint(drop_radius(d.mass)), previous);
}

uint lens_work_count()
{
    return min(lens_state[LENS_STATE_COUNT_WORK], uint(LENS_DROPS_MAX));
}

#if defined(CLEAR)
[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint i     = thread_id.x;
    bool wipe  = buffer_frame.lens_rain.x > 1.5f;
    if (i < LENS_GRID_WIDTH * LENS_GRID_HEIGHT)
    {
        lens_state[LENS_STATE_HEADS + i] = lens_none;
    }
    if (i == 0)
    {
        lens_state[LENS_STATE_COUNT_WORK] = 0;
        lens_state[LENS_STATE_RADIUS_MAX] = 0;
        if (wipe)
        {
            lens_state[LENS_STATE_COUNT] = 0;
        }
    }
}
#endif

#if defined(SIMULATE)
[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= min(lens_state[LENS_STATE_COUNT], uint(LENS_DROPS_MAX)))
        return;

    LensDrop d     = lens_drops[thread_id.x];
    float hh       = lens_half_height();
    float dt       = buffer_frame.lens_air.w;
    float3 air     = buffer_frame.lens_air.xyz;
    float3 push    = buffer_frame.lens_force.xyz; // in the camera's frame everything on the glass feels gravity minus the camera's own acceleration
    float clock    = buffer_frame.lens_force.w;
    float dryness  = buffer_frame.lens_spawn.w;
    float radius   = drop_radius(d.mass);

    // the air over the glass, what comes at it head on parts at the centre and runs out to the edges, the rest skims across
    float air_speed    = length(air);
    float head_on      = max(0.0f, -air.z);
    bool sheltered     = air_speed > 0.01f && air.z / air_speed > 0.2f;
    float2 flow        = (air.xy + d.position * (head_on * lens_stagnation / LENS_DOME_RADIUS)) * drop_boundary_layer * (sheltered ? lens_wake_shelter : 1.0f);

    // the glass is a dome, off its centre it tilts out, so the push along the view has a share along the glass:
    // speeding up presses the drops back and they run out to the edges, braking throws them forward and they gather in
    float2 dome  = -push.z * d.position / LENS_DOME_RADIUS;
    float2 along = (push.xy + dome) * d.mass + drop_drag(flow, radius);

    // pulled off the glass harder than surface tension holds it, it is thrown off
    if (push.z * d.mass > drop_hold_off(radius))
        return;

    // the contact line wets further where the coating is patchier, which pulls the drop sideways and makes it zig-zag
    float push_length = length(along);
    if (push_length > 1e-12f)
    {
        float2 side = float2(-along.y, along.x) / push_length;
        float tilt  = lens_patchiness(d.position + side * radius) - lens_patchiness(d.position - side * radius);
        along      += side * drop_pinning(lens_hysteresis) * radius * tilt;
    }

    float pin  = lens_pinning(d.position) * radius * (1.0f + lens_settle_hold * exp(-d.age / lens_settle_time));
    d.velocity = drop_slide(d.velocity, d.mass, along, pin, radius, dt);
    d.lean     = along / max(pin, 1e-9f);
    float lean = length(d.lean);
    if (lean > 2.0f)
    {
        d.lean *= 2.0f / lean;
    }
    d.mass -= drop_evaporation(radius, dryness, length(flow)) * dt;
    d.age  += dt;

    d.position += d.velocity * lens_slide_pace * dt;

    if (d.mass < lens_mass_min || !lens_on_glass(d.position))
        return;

    lens_append(d);
}
#endif

#if defined(SPAWN)
[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint rain  = uint(buffer_frame.lens_spawn.x);
    uint spray = uint(buffer_frame.lens_spawn.y);
    if (thread_id.x >= rain + spray)
        return;

    uint state = drop_hash(thread_id.x * 747796405u + buffer_frame.frame * 2891336453u);
    float hh   = lens_half_height();
    LensDrop d = (LensDrop)0;
    d.position.x = (drop_random(state) * 2.0f - 1.0f) * hh * lens_aspect() * lens_glass_margin;
    d.position.y = (drop_random(state) * 2.0f - 1.0f) * hh * lens_glass_margin;
    d.seed       = drop_random(state);
    // the rain splashes and keeps part of an impact, the mist arrives slowly enough to stay whole
    d.mass     = thread_id.x < rain ? drop_mass_sample(buffer_frame.lens_spawn.z, 0.005f, drop_random(state)) * drop_rain_retained : drop_mass_sample(lens_spray_diameter, 0.002f, drop_random(state));
    if (d.mass < lens_mass_min)
        return;

    lens_append(d);
}
#endif

#if defined(INSERT)
[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // the live half was read by the step, the merge refills it
    if (thread_id.x == 0)
    {
        lens_state[LENS_STATE_COUNT] = 0;
    }

    uint i = thread_id.x;
    if (i >= lens_work_count())
        return;

    uint previous;
    InterlockedExchange(lens_state[LENS_STATE_HEADS + lens_grid_cell(lens_drops[LENS_DROPS_MAX + i].position)], i, previous);
    lens_state[LENS_STATE_NEXT + i] = previous;
}
#endif

#if defined(TARGET) || defined(MERGE)
int lens_reach_cells(float reach)
{
    return clamp(int(ceil(reach / lens_half_height() / LENS_GRID_CELL)), 1, 4);
}
#endif

#if defined(TARGET)
[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint i = thread_id.x;
    if (i >= lens_work_count())
        return;

    LensDrop a   = lens_drops[LENS_DROPS_MAX + i];
    float ra     = drop_radius(a.mass);
    uint cell    = lens_grid_cell(a.position);
    int cx       = int(cell % LENS_GRID_WIDTH);
    int cy       = int(cell / LENS_GRID_WIDTH);
    int reach    = lens_reach_cells(ra + asfloat(lens_state[LENS_STATE_RADIUS_MAX]));
    uint best    = lens_none;
    float best_m = a.mass;
    for (int y = max(cy - reach, 0); y <= min(cy + reach, LENS_GRID_HEIGHT - 1); y++)
    {
        for (int x = max(cx - reach, 0); x <= min(cx + reach, LENS_GRID_WIDTH - 1); x++)
        {
            uint j = lens_state[LENS_STATE_HEADS + uint(y) * LENS_GRID_WIDTH + uint(x)];
            for (uint guard = 0; j != lens_none && guard < LENS_DROPS_MAX; guard++)
            {
                LensDrop b = lens_drops[LENS_DROPS_MAX + j];
                bool bigger = b.mass > best_m || (b.mass == best_m && j > (best == lens_none ? i : best));
                if (j != i && bigger)
                {
                    float2 gap  = b.position - a.position;
                    float touch = ra + drop_radius(b.mass);
                    if (dot(gap, gap) < touch * touch)
                    {
                        best   = j;
                        best_m = b.mass;
                    }
                }
                j = lens_state[LENS_STATE_NEXT + j];
            }
        }
    }
    lens_state[LENS_STATE_TARGET + i] = best;
}
#endif

#if defined(MERGE)
[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint i = thread_id.x;
    if (i >= lens_work_count())
        return;

    LensDrop a  = lens_drops[LENS_DROPS_MAX + i];
    uint target = lens_state[LENS_STATE_TARGET + i];
    if (target != lens_none)
    {
        // it flows into its target this step, unless the target itself flows on, then it waits for the next
        if (lens_state[LENS_STATE_TARGET + target] == lens_none)
            return;
    }
    else
    {
        // everything that flows into it is smaller, so it touches within twice its own radius
        float mass    = a.mass;
        float2 centre = a.position * a.mass;
        float2 moment = a.velocity * a.mass;
        float age     = a.age;
        uint cell     = lens_grid_cell(a.position);
        int cx        = int(cell % LENS_GRID_WIDTH);
        int cy        = int(cell / LENS_GRID_WIDTH);
        int reach     = lens_reach_cells(2.0f * drop_radius(a.mass));
        for (int y = max(cy - reach, 0); y <= min(cy + reach, LENS_GRID_HEIGHT - 1); y++)
        {
            for (int x = max(cx - reach, 0); x <= min(cx + reach, LENS_GRID_WIDTH - 1); x++)
            {
                uint j = lens_state[LENS_STATE_HEADS + uint(y) * LENS_GRID_WIDTH + uint(x)];
                for (uint guard = 0; j != lens_none && guard < LENS_DROPS_MAX; guard++)
                {
                    if (lens_state[LENS_STATE_TARGET + j] == i)
                    {
                        LensDrop b = lens_drops[LENS_DROPS_MAX + j];
                        mass      += b.mass;
                        centre    += b.position * b.mass;
                        moment    += b.velocity * b.mass;
                        age        = max(age, b.age);
                    }
                    j = lens_state[LENS_STATE_NEXT + j];
                }
            }
        }
        a.position = centre / mass;
        a.velocity = moment / mass;
        a.mass     = mass;
        a.age      = age;
    }

    uint slot;
    InterlockedAdd(lens_state[LENS_STATE_COUNT], 1u, slot);
    if (slot < LENS_DROPS_MAX)
    {
        lens_drops[slot] = a;
    }
}
#endif

#if defined(LIGHTS)
// one thread per region of the frame, the light there is what stands out of the mean, its energy is radiance times the area it covers in lens units
[numthreads(LENS_LIGHTS_X * LENS_LIGHTS_Y, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint region = thread_id.x;
    uint width;
    uint height;
    uint levels;
    tex.GetDimensions(0, width, height, levels);
    uint level = min(uint(max(ceil(log2(float(width) / 128.0f)), 0.0f)), levels - 1);
    uint level_width;
    uint level_height;
    uint level_levels;
    tex.GetDimensions(level, level_width, level_height, level_levels);

    float mean      = luminance(tex.Load(int3(0, 0, levels - 1)).rgb);
    float aspect    = float(width) / float(height);
    float texel     = 2.0f / float(level_height);
    uint2 size      = uint2((level_width + LENS_LIGHTS_X - 1) / LENS_LIGHTS_X, (level_height + LENS_LIGHTS_Y - 1) / LENS_LIGHTS_Y);
    uint2 start     = uint2(region % LENS_LIGHTS_X, region / LENS_LIGHTS_X) * size;
    float3 energy   = 0.0f;
    float2 centre   = 0.0f;
    float weight    = 0.0f;
    for (uint y = start.y; y < min(start.y + size.y, level_height); y++)
    {
        for (uint x = start.x; x < min(start.x + size.x, level_width); x++)
        {
            float3 color = tex.Load(int3(x, y, level)).rgb;
            float lum    = luminance(color);
            float excess = max(lum - mean * lens_light_excess, 0.0f);
            if (excess <= 0.0f)
                continue;

            float2 uv  = (float2(x, y) + 0.5f) / float2(level_width, level_height);
            float2 p   = float2((uv.x * 2.0f - 1.0f) * aspect, 1.0f - uv.y * 2.0f);
            energy    += color * (excess / lum) * texel * texel;
            centre    += p * excess;
            weight    += excess;
        }
    }

    uint base = LENS_STATE_LIGHTS + region * LENS_LIGHT_STRIDE;
    float2 position = weight > 0.0f ? centre / weight : 0.0f;
    lens_state[base + 0] = asuint(position.x);
    lens_state[base + 1] = asuint(position.y);
    lens_state[base + 2] = asuint(energy.r);
    lens_state[base + 3] = asuint(energy.g);
    lens_state[base + 4] = asuint(energy.b);
}
#endif
