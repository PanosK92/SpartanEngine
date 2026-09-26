// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#ifndef COMMON_RAIN_H
#define COMMON_RAIN_H

// rain on surfaces, lagarde 2012 (water drop 1 to 3)
// what is under cover comes from a height grid of the topmost static surface around the camera,
// a surface at or above that height sees the sky, anything lower is under a roof, a tree trunk's
// collider or a bridge deck and stays dry
// exposed surfaces soak, porous ones darken, smooth hydrophobic ones bead, steep ones carry running
// rivulets and standing water rings with impacts

StructuredBuffer<float> rain_occlusion : register(t68);

static const float rain_height_exposed = -1.0e5f;

float rain_weather_rain()       { return buffer_frame.weather.x; }
float rain_weather_wetness()    { return buffer_frame.weather.y; }
float rain_weather_puddles()    { return buffer_frame.weather.w; }
float rain_authored_puddles()   { return buffer_frame.weather.z; }

uint rain_hash_u(uint2 v)
{
    // pcg2d, cheap and without the banding a sin hash gets at large world coordinates
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    return v.x ^ v.y;
}

float rain_hash(int2 cell, uint salt)
{
    return float(rain_hash_u(uint2(cell) + uint2(salt * 7919u, salt * 104729u)) & 0x00ffffffu) / 16777216.0f;
}

float2 rain_hash2(int2 cell, uint salt)
{
    uint h = rain_hash_u(uint2(cell) + uint2(salt * 7919u, salt * 104729u));
    return float2(h & 0xffffu, h >> 16u) / 65536.0f;
}

// height of the topmost static surface over a world cell, open sky when nothing is there or the grid does not reach
float rain_occluder_height(int2 cell)
{
    int2 origin = int2(round(buffer_frame.rain_occlusion.xy / buffer_frame.rain_occlusion.z));
    int2 local  = cell - origin;
    if (any(local < 0) || any(local >= RAIN_OCCLUSION_RESOLUTION))
        return rain_height_exposed;

    uint mask = RAIN_OCCLUSION_RESOLUTION - 1u;
    uint slot = (uint(cell.y) & mask) * RAIN_OCCLUSION_RESOLUTION + (uint(cell.x) & mask);
    return rain_occlusion[uint(buffer_frame.rain_occlusion.w) + slot];
}

// 1 where rain reaches a point, a point level with the topmost surface is that surface so it is exposed
float rain_exposure_at(float3 position, float tolerance)
{
    float cell_size = buffer_frame.rain_occlusion.z;
    float2 p        = position.xz / cell_size - 0.5f;
    int2 c          = int2(floor(p));
    float2 f        = p - floor(p);

    // the exposure of each tap is blended, not the heights, a roof edge must not average into a ramp
    float4 h = float4(
        rain_occluder_height(c),
        rain_occluder_height(c + int2(1, 0)),
        rain_occluder_height(c + int2(0, 1)),
        rain_occluder_height(c + int2(1, 1))
    );
    float4 e = saturate((position.y - h + tolerance * 2.0f) / tolerance);
    return lerp(lerp(e.x, e.y, f.x), lerp(e.z, e.w, f.x), f.y);
}

// the sides of things are wetted by what falls past them, so a wall looks up from the cell in front of it,
// and what faces the ground never sees a drop
float rain_exposure(float3 position, float3 geometric_normal)
{
    float up        = geometric_normal.y;
    float facing    = saturate(up * 2.5f + 1.0f);
    if (facing <= 0.0f)
        return 0.0f;

    float2 outward  = geometric_normal.xz;
    float3 probe    = position + float3(outward.x, 0.0f, outward.y) * 0.6f;
    // a slope departs from the height the ray hit at the cell centre, give it room
    float tolerance = 0.12f + 0.35f * (1.0f - saturate(up));
    return rain_exposure_at(probe, tolerance) * facing;
}

// impacts on standing water, each cell holds one drop whose ring expands and fades before the cell reseeds
// returns the tangent plane slope of the ring field
float2 rain_ripples(float2 xz, float rain, float time)
{
    float2 slope = 0.0f;
    [unroll]
    for (uint layer = 0; layer < 3; layer++)
    {
        float cell_size = 0.23f + 0.07f * layer;
        float2 p        = xz / cell_size + float2(0.37f, 0.71f) * layer;
        int2 cell       = int2(floor(p));
        float2 f        = p - floor(p);

        float2 h        = rain_hash2(cell, layer + 1u);
        float period    = 0.6f + 0.35f * h.y;
        float cycle     = time / period + h.x;
        float phase     = frac(cycle);
        // light rain leaves most cells quiet on any one cycle
        float active    = step(rain_hash(cell + int2(int(floor(cycle)), 0), layer + 11u), rain * 0.85f + 0.1f);

        float2 centre   = 0.5f + (rain_hash2(cell + int2(int(floor(cycle)) * 31, 7), layer + 5u) - 0.5f) * 0.3f;
        float2 d        = f - centre;
        float r         = length(d);
        float radius    = phase * 0.4f;
        float x         = (r - radius) / 0.045f;
        // the wave packet trails its crest, and the whole ring dies away as it spreads
        float ring      = -x * exp(-x * x) * sin(x * 3.0f);
        float amplitude = (1.0f - phase) * (1.0f - phase) * active;
        slope          += (r > 1e-4f ? d / r : 0.0f) * ring * amplitude;
    }
    return slope;
}

// droplets, a grid of cells where each cell may hold one drop
// the grid lies on whichever of the object's axis planes the face looks along, so it never turns with the normal,
// and every offset inside a drop is lifted from that plane back onto the face before it is measured, so a drop
// stays round on the paint however the panel curves or tilts
// every drop leans with the specific force along the face, gravity and on the occupied car its g forces and airflow
// through a damped spring on the cpu, big drops deform the most as the bond number grows with radius squared
// on the occupied car every drop also belongs to a size bucket that lets go once the force beats its contact angle
// hysteresis, big drops first, then slides by the distance the cpu integrated for that bucket and leaves a thinning
// trail that pinches off into beads, gravity included, so drops on the sides and the nose run down
#define RAIN_DROP_BUCKETS 4
// metres, the cpu wraps every slide over this distance (drop_wrap in Weather.cpp), so the drop pattern repeats over it
static const float rain_drop_wrap = 24.0f;

struct RainDropForces
{
    float3 lean;       // object space specific force, in g
    bool vehicle;      // the occupied car, its drops slide
    float3x3 rotation; // rows are the object's world axes
};

// how far and how fast a bucket (largest first) has slid over faces whose normal runs along an object axis, object space
// the cpu tracks the travel per car axis plane, the object axis picks the plane it lines up with
void rain_drop_motion(RainDropForces forces, uint axis, uint bucket, out float3 slide, out float3 flow)
{
    slide = 0.0f;
    flow  = 0.0f;
    if (!forces.vehicle)
        return;

    float3 axis_world = axis == 0u ? forces.rotation[0] : (axis == 1u ? forces.rotation[1] : forces.rotation[2]);
    float3 match      = abs(float3(dot(axis_world, buffer_frame.rain_vehicle_axis[0].xyz), dot(axis_world, buffer_frame.rain_vehicle_axis[1].xyz), dot(axis_world, buffer_frame.rain_vehicle_axis[2].xyz)));
    uint plane        = match.x > match.y ? (match.x > match.z ? 0u : 2u) : (match.y > match.z ? 1u : 2u);
    slide             = mul(forces.rotation, buffer_frame.rain_vehicle_slide[plane * 4u + bucket].xyz);
    flow              = mul(forces.rotation, buffer_frame.rain_vehicle_flow[plane * 4u + bucket].xyz);
}

struct RainDrop
{
    bool   present;
    float2 centre; // cell units
    float  radius; // cell units
    float  seed;
};

// the drop a cell holds this epoch, the centre leaves room for the drop to stretch without crossing its cell
// the pattern repeats every period cells so a slide wrapping by exactly that many cells shows no jump
RainDrop rain_drop_in_cell(int2 cell, int period, uint salt, float density, float time, float lifetime)
{
    cell          = cell % period;
    cell         += period * int2(cell < 0);
    float2 h      = rain_hash2(cell, salt);
    float life    = lifetime * (1.0f + 1.5f * h.y);
    float epoch   = floor(time / life + h.x);
    int2 seed     = cell + int2(int(epoch) * 131, int(epoch) * 71);

    RainDrop drop;
    drop.present  = rain_hash(seed, salt + 2u) < density;
    drop.radius   = 0.14f + 0.16f * rain_hash(seed, salt + 4u);
    drop.centre   = 0.5f + (rain_hash2(seed, salt + 6u) - 0.5f) * (1.0f - 3.2f * drop.radius);
    drop.seed     = rain_hash(seed, salt + 8u);
    return drop;
}

float2 rain_plane(float3 p, uint axis)
{
    return axis == 0u ? p.yz : (axis == 1u ? p.xz : p.xy);
}

// an offset in the projection plane lifted onto the tangent plane of the face, axis is the dropped coordinate
float3 rain_lift(float2 delta, uint axis, float3 n)
{
    if (axis == 0u)
    {
        float nx = n.x >= 0.0f ? max(n.x, 0.3f) : min(n.x, -0.3f);
        return float3(-(n.y * delta.x + n.z * delta.y) / nx, delta.x, delta.y);
    }
    if (axis == 1u)
    {
        float ny = n.y >= 0.0f ? max(n.y, 0.3f) : min(n.y, -0.3f);
        return float3(delta.x, -(n.x * delta.x + n.z * delta.y) / ny, delta.y);
    }
    float nz = n.z >= 0.0f ? max(n.z, 0.3f) : min(n.z, -0.3f);
    return float3(delta.x, delta.y, -(n.x * delta.x + n.y * delta.y) / nz);
}

// a cap pulled by lean, o and lean are tangent vectors in drop radii, the footprint stretches downstream and the
// apex follows, keeps whichever drop covers the pixel the most, the rim is its contact line
// a real contact line snags on specks and scratches, so each drop gets its own lopsided outline, and some are two
// drops caught mid merge, e1 and e2 span the tangent plane so the outline is measured on the paint itself
void rain_drop_shade(float3 o, float3 lean, float3 e1, float3 e2, float seed, float resolved, inout float coverage, inout float3 slope, inout float rim)
{
    float lean_len = length(lean);
    float3 along_d = lean_len > 1e-4f ? lean / lean_len : 0.0f;
    lean_len       = min(lean_len, 1.5f);
    float along    = dot(o, along_d);
    float3 across  = o - along_d * along;
    along         /= 1.0f + 0.6f * lean_len * saturate(along * 0.5f + 0.5f);
    float3 q       = along_d * along + across;
    float x        = dot(q, e1);
    float y        = dot(q, e2);
    float r2       = x * x + y * y;
    if (r2 >= 2.4f)
        return;

    // the outline, an oval and a three lobed wobble at their own angles
    float r        = sqrt(max(r2, 1e-6f));
    float2 c2      = float2(x * x - y * y, 2.0f * x * y) / max(r2, 1e-6f);
    float2 c3      = float2(x * x * x - 3.0f * x * y * y, 3.0f * x * x * y - y * y * y) / max(r2 * r, 1e-6f);
    float2 h       = frac(seed * float2(13.7f, 29.3f));
    float a2       = 0.05f + 0.13f * h.x;
    float a3       = 0.03f + 0.08f * h.y;
    float p2       = seed * 12.566f;
    float p3       = h.x * 18.85f;
    float outline  = 1.0f + a2 * dot(c2, float2(cos(p2), sin(p2))) + a3 * dot(c3, float2(cos(p3), sin(p3)));
    float sdf      = r - outline;
    float3 grad    = q / r;

    // a quarter of the drops are merging with a smaller one, the neck between them fills in smoothly
    if (h.y < 0.25f)
    {
        float angle    = h.x * 6.2832f;
        float3 offset  = (e1 * cos(angle) + e2 * sin(angle)) * 0.75f;
        float3 q2      = q - offset;
        float r_small  = length(q2);
        float sdf2     = r_small - 0.5f;
        float blend    = saturate(0.5f + 0.5f * (sdf2 - sdf) / 0.3f);
        sdf            = lerp(sdf2, sdf, blend) - 0.3f * blend * (1.0f - blend);
        grad           = normalize(lerp(q2 / max(r_small, 1e-4f), grad, blend));
    }
    if (sdf >= 0.0f)
        return;

    float s     = saturate(1.0f + sdf);
    float s2    = s * s;
    float cover = (1.0f - smoothstep(0.85f, 1.0f, s2)) * resolved;
    if (cover <= coverage)
        return;

    float cap   = sqrt(1.0f - s2);
    float3 apex = grad * s - along_d * lean_len * 0.35f * (1.0f - s2);
    coverage    = cover;
    slope       = apex / max(cap, 0.2f) * 0.55f * resolved;
    rim         = max(rim, smoothstep(0.7f, 0.97f, s2) * resolved);
}

// one population of drops on one projection plane, slide and flow are how far and how fast it has moved along the face
void rain_drop_population(float2 uv, uint axis, float3 n, float cell_size, int period, uint salt, float density, float lifetime, float resolved,
    float3 lean, float3 slide, float3 flow, float time, inout float coverage, inout float3 slope, inout float rim)
{
    float3 flow_t   = flow - n * dot(flow, n);
    float speed     = length(flow_t);
    float3 dir_t    = speed > 1e-4f ? flow_t / speed : 0.0f;
    float2 dir_p    = rain_plane(dir_t, axis);
    float plane_len = length(dir_p);
    dir_p           = plane_len > 1e-4f ? dir_p / plane_len : 0.0f;
    // the trail is what the drop wetted over the last half second
    float trail_m   = min(speed * 0.5f, 2.5f * cell_size);
    float trail_c   = trail_m * plane_len / cell_size;
    float2 p        = (uv - rain_plane(slide, axis)) / cell_size;
    float3 moving   = lean + dir_t * saturate(speed / 0.1f) * 0.8f;
    float3 e1       = normalize(rain_lift(float2(1.0f, 0.0f), axis, n));
    float3 e2       = cross(n, e1);

    [unroll]
    for (uint j = 0; j < 3; j++)
    {
        // drops further along the flow may trail back over this pixel
        if (j > 0 && trail_c < float(j) - 0.5f)
            break;

        int2 cell     = int2(floor(p + dir_p * float(j)));
        RainDrop drop = rain_drop_in_cell(cell, period, salt, density, time, lifetime);
        if (!drop.present)
            continue;

        float radius = drop.radius * cell_size;
        float3 rel   = rain_lift((p - cell - drop.centre) * cell_size, axis, n);
        rain_drop_shade(rel / radius, moving * 1.5f, e1, e2, drop.seed, resolved, coverage, slope, rim);

        // the trail, a thin film left on the paint that thins toward the tail and pinches off into beads
        float t = dot(rel, -dir_t) / max(trail_m, 1e-5f);
        if (trail_m > 0.05f * cell_size && t > 0.0f && t < 1.0f)
        {
            float3 across = rel + dir_t * (t * trail_m);
            float width   = radius * lerp(0.45f, 0.12f, t);
            float beading = 0.6f + 0.4f * sin(t * trail_c * 9.0f + drop.seed * 6.2832f);
            float trail   = (1.0f - smoothstep(width * 0.5f, width, length(across))) * (1.0f - t) * (1.0f - t) * beading * 0.75f * resolved;
            if (trail > coverage)
            {
                coverage = trail;
                slope    = -across / width * 0.35f * resolved;
            }
        }
    }
}

// the two object axis planes a face is laid out on, near where they meet both carry half, so no seam shows
void rain_planes(float3 n, out uint major, out uint minor, out float share)
{
    float3 a = abs(n);
    major    = a.x > a.y ? (a.x > a.z ? 0u : 2u) : (a.y > a.z ? 1u : 2u);
    minor    = major == 0u ? (a.y > a.z ? 1u : 2u) : (major == 1u ? (a.x > a.z ? 0u : 2u) : (a.x > a.y ? 0u : 1u));
    share    = saturate((a[major] - a[minor]) / 0.12f);
}

// smooth 1d value noise in -1 to 1, row picks an independent strand
float rain_noise(float x, int row, uint salt)
{
    float i = floor(x);
    float f = x - i;
    f       = f * f * (3.0f - 2.0f * f);
    return lerp(rain_hash(int2(int(i), row), salt), rain_hash(int2(int(i) + 1, row), salt), f) * 2.0f - 1.0f;
}

// running water, once a drop breaks loose it wets a path and the drops after it follow that path, so the water
// gathers into veins that wander, join and part, zigzag between the specks the contact line snags on, and slowly
// reshape, between runs a thin film marks each path and every so often a slug of water runs down it
// one set of paths laid along a fixed direction dir of the projection plane, columns run across it and neighbouring
// pairs drift together and apart, shift is how far the water has run along dir, in metres
void rain_vein_set(float2 uv, float2 dir, uint axis, float3 n, float density, float footprint, float shift, float time, uint salt, inout float coverage, inout float3 slope, inout float rim)
{
    const float column_w = 0.025f;
    float2 side    = float2(-dir.y, dir.x);
    float a        = dot(uv, side) / column_w;
    float b        = dot(uv, dir);
    int pair0      = int(floor(a * 0.5f));
    float3 side_t  = normalize(rain_lift(side, axis, n));
    float3 dir_t   = normalize(rain_lift(dir, axis, n));

    [unroll]
    for (uint k = 0; k < 2; k++)
    {
        // a path can lean out of its own pair, so the nearer neighbour pair is checked too
        int pair = k == 0 ? pair0 : (a - float(pair0) * 2.0f < 1.0f ? pair0 - 1 : pair0 + 1);
        float4 h = float4(rain_hash2(int2(pair, 3), salt), rain_hash2(int2(pair, 5), salt));

        // the pair wanders as one, its two paths drift apart and back and where they meet they run as a single vein,
        // the waves travel both ways at a crawl so the shape reshapes over minutes instead of sliding along
        float mid  = 1.0f + 0.3f * sin(b * (9.0f + 8.0f * h.x) + h.y * 6.2832f + time * 0.013f) + 0.15f * sin(b * (23.0f + 10.0f * h.z) + h.w * 6.2832f - time * 0.021f);
        float gap  = max(0.0f, 0.3f + 0.6f * sin(b * (6.0f + 6.0f * h.y) + h.x * 6.2832f + time * 0.017f) + 0.25f * sin(b * (15.0f + 7.0f * h.w) + h.z * 6.2832f - time * 0.011f));
        float zig  = 0.08f * rain_noise(b / 0.018f, pair, salt + 7u) + 0.04f * rain_noise(b / 0.007f, pair, salt + 9u);

        [unroll]
        for (uint v = 0; v < 2; v++)
        {
            int path     = pair * 2 + int(v);
            float2 hv    = rain_hash2(int2(path, 9), salt);
            float active = saturate((density - hv.x) / 0.15f);
            if (active <= 0.0f)
                continue;

            // wetted stretches, a run starts where a drop broke loose and ends where its head pinned again
            float run = sin(b * (2.5f + 2.0f * hv.y) + hv.x * 40.0f + time * 0.009f) + 0.6f * sin(b * (6.1f + 3.0f * hv.x) + hv.y * 17.0f);
            run       = smoothstep(-0.35f, 0.05f, run) * active;
            if (run <= 0.0f)
                continue;

            float centre = float(pair) * 2.0f + mid + (v == 0u ? -0.5f : 0.5f) * gap + zig;
            float x      = (a - centre) * column_w;
            float hw     = column_w * (0.1f + 0.08f * hv.y);

            // slugs ride the path, each path at its own pace, a round head with a tail thinning back into the film
            float pace   = 0.6f + 0.8f * frac(hv.x * 7.31f + hv.y * 3.17f);
            float period = 0.07f + 0.15f * hv.y;
            float e      = (1.0f - frac((b - shift * pace) / period + hv.x * 5.0f)) * period;
            float head_l = 2.6f * hw;
            float head_u = (e - 0.5f * head_l) / (0.5f * head_l);
            float head   = e < head_l ? 1.3f * hw * sqrt(saturate(1.0f - head_u * head_u)) : 0.0f;
            float tail   = e >= head_l ? 0.7f * hw * (1.0f - saturate((e - head_l) / 0.035f)) : 0.0f;
            // the film left behind beads up along the path
            float film   = 0.55f * hw * (0.85f + 0.3f * rain_noise(b / 0.005f, path, salt + 11u));
            float width  = max(max(head, tail), film);
            float thick  = head > film ? 1.0f : (tail > film ? 0.8f : 0.6f);

            // a vein narrower than a pixel spreads into a fainter, wider line instead of flickering
            float width_eff = max(width, footprint * 0.75f);
            float t         = x / width_eff;
            if (abs(t) >= 1.0f)
                continue;

            float fade  = width / width_eff;
            float cover = (1.0f - smoothstep(0.7f, 1.0f, t * t)) * run * fade * (head > film ? 1.0f : 0.85f);
            if (cover <= coverage)
                continue;

            float along = head > film ? -head_u * 0.4f : 0.0f;
            coverage    = cover;
            slope       = (side_t * (2.0f * t) + dir_t * along) * thick * run * fade;
            rim         = max(rim, smoothstep(0.45f, 0.95f, t * t) * cover);
        }
    }
}

// p, n and pull are object space, pull is what drives the water (gravity plus the car's lean) in g
// returns coverage and writes the object space slope of the water and its contact line
float rain_veins(float3 p, float3 n, float3 pull, float density, float footprint, float time, RainDropForces forces, out float3 slope, out float rim)
{
    slope          = 0.0f;
    rim            = 0.0f;
    float coverage = 0.0f;

    uint major, minor;
    float share;
    rain_planes(n, major, minor, share);

    [unroll]
    for (uint k = 0; k < 2; k++)
    {
        uint axis    = k == 0 ? major : minor;
        float weight = k == 0 ? 0.5f + 0.5f * share : 0.5f - 0.5f * share;
        if (weight <= 0.0f)
            continue;

        // the direction on the plane is the same across the whole face, so the paths never shear on a curved panel
        float2 pull_p = rain_plane(pull, axis);
        float pull_l  = length(pull_p);
        if (pull_l < 0.02f)
            continue;

        // how far the water has run, the car's integrated slide, anything else runs at a steady pace under gravity
        float2 travel = forces.vehicle ? 0.0f : pull_p / pull_l * (time * 0.12f * pull_l);
        if (forces.vehicle)
        {
            float3 slide, flow;
            rain_drop_motion(forces, axis, 1u, slide, flow);
            travel = rain_plane(slide, axis);
        }

        // fixed directions every 20 degrees, the paths of the two either side fade into each other as the pull turns,
        // old paths dry out and new ones form instead of the whole pattern swinging round
        const float step = 0.3491f;
        float angle      = atan2(pull_p.y, pull_p.x) / step;
        float bin        = floor(angle);
        float mix        = angle - bin;
        float2 uv        = rain_plane(p, axis);
        [unroll]
        for (uint j = 0; j < 2; j++)
        {
            float w = (j == 0 ? 1.0f - mix : mix) * weight * saturate(pull_l / 0.15f);
            if (w <= 0.02f)
                continue;

            float theta = (bin + float(j)) * step;
            float2 dir  = float2(cos(theta), sin(theta));
            // straight down wraps from +180 to -180 degrees, both ends must be the same set of paths
            uint salt   = 61u + uint(((int(bin) + int(j)) % 18 + 18) % 18) * 13u + axis * 131u;
            rain_vein_set(uv, dir, axis, n, density * w, footprint, dot(travel, dir), time, salt, coverage, slope, rim);
        }
    }
    return coverage;
}

// p and n are object space, returns coverage and writes the object space slope of the drop's cap
float rain_drops(float3 p, float3 n, float density, float footprint, float time, RainDropForces forces, out float3 slope, out float rim)
{
    slope          = 0.0f;
    rim            = 0.0f;
    float coverage = 0.0f;

    uint major, minor;
    float share;
    rain_planes(n, major, minor, share);
    float3 lean = forces.lean - n * dot(forces.lean, n);

    [unroll]
    for (uint k = 0; k < 2; k++)
    {
        uint axis    = k == 0 ? major : minor;
        float weight = k == 0 ? 0.5f + 0.5f * share : 0.5f - 0.5f * share;
        if (weight <= 0.0f)
            continue;

        float2 uv = rain_plane(p, axis);
        [unroll]
        for (uint layer = 0; layer < 3; layer++)
        {
            // a clinging 5 mm mist, then the 1.2 cm and 2.4 cm drops that carry most of the water
            float cell_size = layer == 0 ? 0.005f : (layer == 1 ? 0.012f : 0.024f);
            // a drop needs a few pixels across before it reads as a drop rather than noise
            float resolved  = 1.0f - saturate(footprint / cell_size * 4.0f - 1.0f);
            if (resolved <= 0.0f)
                continue;

            float deform        = layer == 0 ? 0.2f : (layer == 1 ? 0.3f : 0.6f);
            float layer_density = density * weight * (layer == 0 ? 0.55f : (layer == 1 ? 0.8f : 0.45f));
            float lifetime      = 3.0f + 4.0f * layer;
            float2 layer_uv     = uv + float2(0.53f, 0.19f) * (layer * cell_size);
            uint salt           = 21u + layer * 16u + axis * 97u;
            if (layer == 0)
            {
                // the mist holds on the hardest, it creeps at half the pace of the smallest bucket
                float3 slide, flow;
                rain_drop_motion(forces, axis, RAIN_DROP_BUCKETS - 1u, slide, flow);
                int period = int(round(rain_drop_wrap * 0.5f / cell_size));
                rain_drop_population(layer_uv, axis, n, cell_size, period, salt, layer_density, lifetime, resolved, lean * deform, slide * 0.5f, flow * 0.5f, time, coverage, slope, rim);
            }
            else
            {
                // two buckets per size, the 2.4 cm drops hold on the least
                uint bucket = layer == 2 ? 0u : 2u;
                int period  = int(round(rain_drop_wrap / cell_size));
                [unroll]
                for (uint b = 0; b < 2; b++)
                {
                    float3 slide, flow;
                    rain_drop_motion(forces, axis, bucket + b, slide, flow);
                    rain_drop_population(layer_uv, axis, n, cell_size, period, salt + b * 8u, layer_density * 0.5f, lifetime, resolved, lean * deform, slide, flow, time, coverage, slope, rim);
                }
            }
        }
    }
    return coverage;
}

struct RainSurface
{
    float3 position;         // world
    float3 geometric_normal; // world, normalized
    float3 object_origin;    // world position of the object's pivot, beads ride with it
    float3x3 object_rotation; // rows are the object's world axes, the drops are laid out in its frame and turn with it
    float  porosity;         // 0 sealed to 1 soaks up everything
    float  water;            // standing water already on this pixel from the puddles
    float  footprint;        // metres per pixel
    float  exposure;         // 0 under cover to 1 in the open
    bool   detail;           // beads and rivulets, only on props, not on the ground
    bool   vehicle;          // part of the occupied car, its drops answer the car's g forces and it carries its own wetness
};

// darkening, glaze, beads, rivulets and ripples, returns how much of the pixel is now water
float rain_apply(RainSurface s, inout float3 albedo, inout float3 normal, inout float roughness, inout float metalness, bool transparent)
{
    float wet  = s.exposure * rain_weather_wetness();
    float rain = rain_weather_rain() * s.exposure;
    if (s.vehicle)
    {
        wet = max(wet, buffer_frame.rain_vehicle_lean.w);
    }
    if (wet <= 0.0f && s.water <= 0.0f)
        return 0.0f;

    float time = (float)fmod(buffer_frame.time, 3600.0);
    float3 n   = s.geometric_normal;
    float up   = n.y;

    // soaked, pores fill with water so the diffuse darkens, a film tightens the specular lobe
    // a sealed surface carries a glossy film, soil and grass drink the water so they mostly darken and stay matte
    float dry_roughness = roughness;
    float porosity      = saturate(s.porosity);
    if (!transparent)
    {
        albedo *= lerp(1.0f, lerp(0.92f, 0.5f, porosity), wet);
    }
    float wet_roughness = lerp(0.05f, 0.62f, porosity * porosity);
    roughness = lerp(roughness, min(roughness, wet_roughness), wet * lerp(0.9f, 0.6f, saturate(-up * 2.0f + 1.0f) * (1.0f - saturate(up))));

    float water = 0.0f;

    // offset from the object's pivot, the drops and veins are laid out in its frame so they ride along as it drives and turns
    float3 d       = s.position - s.object_origin;

    // ripples on anything flat and wet enough to hold a film, strongest in the pools
    if (rain > 0.0f && up > 0.8f)
    {
        float resolved = 1.0f - saturate(s.footprint * 90.0f - 0.5f);
        float strength = (s.water + 0.2f * wet * (1.0f - s.water)) * resolved * smoothstep(0.8f, 0.95f, up);
        if (strength > 0.001f)
        {
            float2 slope = rain_ripples(s.position.xz, rain, time) * strength * 0.9f;
            normal       = normalize(normal + float3(slope.x, 0.0f, slope.y));
        }
    }

    if (s.detail && wet > 0.0f)
    {
        // smooth coatings shed water into beads, a rough porous surface just drinks it
        float smooth_surface = saturate(1.0f - dry_roughness * 1.8f) * (1.0f - saturate(s.porosity));

        // everything in the object's frame, gravity sags every drop, the occupied car adds its g forces, airflow and sliding
        float3x3 r = s.object_rotation;
        RainDropForces forces;
        forces.lean     = mul(r, float3(0.0f, -1.0f, 0.0f) + (s.vehicle ? buffer_frame.rain_vehicle_lean.xyz : 0.0f));
        forces.vehicle  = s.vehicle;
        forces.rotation = r;

        // beads, on anything not facing the ground
        if (s.footprint < 0.012f && smooth_surface > 0.0f)
        {
            // steep faces keep fewer, the rest have run off
            float density = wet * smooth_surface * lerp(0.45f, 1.0f, saturate(up));

            float3 slope;
            float rim;
            float coverage = rain_drops(mul(r, d), mul(r, n), density, s.footprint, time, forces, slope, rim);
            if (coverage > 0.0f)
            {
                float3 bead_normal = normalize(n + mul(slope, r));
                normal    = normalize(lerp(normal, bead_normal, coverage));
                roughness = lerp(roughness, 0.02f, coverage);
                metalness = lerp(metalness, metalness * 0.6f, coverage);
                if (!transparent)
                {
                    // the contact line refracts the paint out of view, a thin dark ring around each drop
                    albedo *= lerp(1.0f, 0.9f, coverage) * (1.0f - 0.35f * rim);
                }
                water = max(water, coverage);
            }
        }

        // veins, wherever the pull along the face is strong enough the water runs down wetted paths, on the car the
        // pull is gravity plus its g forces and the airflow, so at speed the veins rake back over the hood and glass
        float3 pull  = s.vehicle ? buffer_frame.rain_vehicle_vein.xyz : float3(0.0f, -1.0f, 0.0f);
        float along  = length(pull - n * dot(pull, n));
        float steep  = smoothstep(0.2f, 0.55f, along) * step(-0.3f, up);
        float vein_resolved = 1.0f - saturate(s.footprint * 180.0f - 1.0f);
        if (steep > 0.0f && vein_resolved > 0.0f)
        {
            float3 slope;
            float rim;
            float density  = wet * (0.5f + 0.4f * smooth_surface) * steep;
            float coverage = rain_veins(mul(r, d), mul(r, n), mul(r, pull), density, s.footprint, time, forces, slope, rim) * vein_resolved;
            if (coverage > 0.0f)
            {
                normal    = normalize(normal + mul(slope, r) * vein_resolved);
                roughness = lerp(roughness, 0.02f, coverage);
                if (!transparent)
                {
                    // the water lens darkens what is under it and its edge refracts the paint out of view
                    albedo *= lerp(1.0f, lerp(0.84f, 0.75f, saturate(s.porosity)), coverage) * (1.0f - 0.5f * rim * vein_resolved);
                }
                water = max(water, coverage);
            }
        }
    }

    return max(water, s.water);
}

#endif
