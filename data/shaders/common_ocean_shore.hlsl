/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_COMMON_OCEAN_SHORE
#define SPARTAN_COMMON_OCEAN_SHORE

// breaking surf where the fft ocean meets the terrain
// the fft is periodic deep water and cannot know where the beach is, so a shoreline field built on the
// cpu gives every point its signed distance to the coast, the shoreward direction and the beach slope
// swell trains travel down that field, slow and shorten as the bed rises, steepen and lean forward,
// break once they are taller than the water is deep and run up the sand as a thin sheet
// ocean_shore::evaluate in Renderer_Passes_Ocean.cpp mirrors this for buoyancy, keep the two in sync

static const float ocean_shore_g          = 9.81f;
static const float ocean_shore_breaker    = 0.78f; // breaking height over depth
static const float ocean_shore_rest_depth = 0.3f;  // setup at the waterline, keeps the last wavelength finite
static const float ocean_shore_obliquity  = 0.35f; // share of the deep swell direction that survives refraction
static const float ocean_shore_swash_span = 0.72f; // fraction of a period the sheet spends on the sand
static const float ocean_shore_runup      = 0.55f; // vertical run-up over the local swell height

struct OceanShore
{
    float3 displacement; // xz leans the crest shoreward, y is the surface height over sea level
    float  floor_y;      // world y of the swash sheet, far below the bed where the sheet has not reached
    float  breaking;     // 0 unbroken swell, 1 a fully broken bore
    float  crest;        // profile, 1 on the crest, 0 in the trough
    float  face;         // 1 on the steep shoreward face
    float  foam;         // whitewater coverage before the lace shaping
    float  sheet;        // 1 inside the swash sheet
    float  wet;          // sand still wet from the last run-up
    float  film;         // a mirror of water still standing on the sand, the first seconds after the sheet drains
    float  height;       // local wave height, crest to trough
    float  steepness;    // 1 as the wave nears its breaking height, thin enough for light to cross the lip
    float2 direction;    // shoreward
    float2 lace_offset;  // carries the lace with the water
};

uint ocean_shore_hash_u(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float ocean_shore_hash(int n)
{
    return (ocean_shore_hash_u(uint(n)) & 0x00ffffffu) / 16777216.0f;
}

float ocean_shore_hash(int2 cell, int salt)
{
    uint key = uint(cell.x) * 0x9e3779b1u ^ uint(cell.y) * 0x85ebca77u ^ uint(salt) * 0xc2b2ae3du;
    return (ocean_shore_hash_u(key) & 0x00ffffffu) / 16777216.0f;
}

float ocean_shore_noise(float2 p, int salt)
{
    float2 i = floor(p);
    float2 f = p - i;
    f        = f * f * (3.0f - 2.0f * f);
    int2 c   = int2(i);
    float a  = ocean_shore_hash(c, salt);
    float b  = ocean_shore_hash(c + int2(1, 0), salt);
    float d  = ocean_shore_hash(c + int2(0, 1), salt);
    float e  = ocean_shore_hash(c + int2(1, 1), salt);
    return lerp(lerp(a, b, f.x), lerp(d, e, f.x), f.y);
}

// sets of bigger waves roll in every seven or so, with a random wave on top
float ocean_shore_group(float n)
{
    float set = 0.5f + 0.5f * cos(n * (6.2831853f / 7.0f));
    return lerp(0.55f, 1.45f, set * 0.6f + ocean_shore_hash(int(n)) * 0.4f);
}

// seconds for a crest to travel from this distance to the waterline over a bed of the given slope
// shallow water speed sqrt(g h) with h = slope * distance + rest depth, capped at the deep water speed
float ocean_shore_travel_time(float distance, float slope, float c_deep)
{
    float s      = max(distance, 0.0f);
    float h0     = ocean_shore_rest_depth;
    float s_deep = max((c_deep * c_deep / ocean_shore_g - h0) / slope, 0.0f);
    float k      = 2.0f / (slope * sqrt(ocean_shore_g));
    return k * (sqrt(slope * min(s, s_deep) + h0) - sqrt(h0)) + max(s - s_deep, 0.0f) / c_deep;
}

// shoaled height, soft limited by the depth so a wave never stands taller than the breaker index allows
float ocean_shore_limit(float height, float depth)
{
    float limit = ocean_shore_breaker * max(depth, 0.0f);
    float ratio = height / max(limit, 1e-4f);
    return height * rsqrt(sqrt(1.0f + ratio * ratio * ratio * ratio));
}

float ocean_shore_time(float offset)
{
    return buffer_frame.ocean_shore_wave.z + offset;
}

// spacing is the distance between surface samples (mesh cell or pixel footprint), waves it cannot resolve flatten instead of aliasing
OceanShore ocean_shore_evaluate(float2 grid_xz, float time, float spacing = 0.0f)
{
    OceanShore o  = (OceanShore)0;
    o.floor_y     = -100000.0f;
    float4 wave   = buffer_frame.ocean_shore_wave;
    if (wave.w < 0.5f || buffer_frame.terrain_height_enabled < 0.5f)
        return o;

    float2 uv = (grid_xz - buffer_frame.ocean_shore_mapping.xy) * buffer_frame.ocean_shore_mapping.zw;
    if (any(uv <= 0.0f) || any(uv >= 1.0f))
        return o;

    float4 field   = tex_ocean_shore.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0.0f);
    float distance = field.x;
    float reach    = buffer_frame.ocean_shore_swell.z;
    if (distance > reach)
        return o;

    float bed_valid = 0.0f;
    float bed       = sample_ocean_terrain_height(grid_xz, bed_valid);
    if (bed_valid < 0.5f)
        return o;

    float2 swell = buffer_frame.ocean_shore_swell.xy;
    float2 dir   = field.yz;
    float len    = length(dir);
    dir          = len > 1e-3f ? dir / len : swell;
    float slope  = clamp(field.w, 0.01f, 0.3f);

    float period  = wave.y;
    float c_deep  = ocean_shore_g * period / 6.2831853f;
    float depth   = buffer_frame.ocean_sea_level - bed; // negative on the sand
    float wet_d   = max(depth, 0.0f);

    // exposed coasts take the swell, the lee side sees a fraction of it
    // the swell only turns shore parallel and builds once it feels the bed, deep water belongs to the fft
    float exposure = lerp(0.55f, 1.0f, saturate(dot(dir, swell) * 0.6f + 0.5f));
    float fade     = 1.0f - smoothstep(reach * 0.5f, reach, distance);
    float l_deep   = c_deep * period;
    float shoaling = 1.0f - smoothstep(0.03f * l_deep, 0.1f * l_deep, wet_d);
    float offshore = wave.x * exposure * fade * shoaling;

    // the deep swell direction survives partly, so crests arrive along the coast at different times and peel
    float psi    = -ocean_shore_obliquity * dot(grid_xz, swell) / (c_deep * period) + ocean_shore_noise(grid_xz / 240.0f, 7) * 0.6f;
    float travel = ocean_shore_travel_time(distance, slope, c_deep);
    float cycles = (travel + time) / period + psi;

    // location mean, independent of the individual wave so the profile stays continuous across troughs
    float shoal      = clamp(pow(8.0f / max(wet_d, 0.05f), 0.25f), 1.0f, 1.7f);
    float h_loc_raw  = offshore * lerp(0.7f, 1.3f, ocean_shore_noise(grid_xz / 90.0f, 3)) * shoal;
    float h_loc      = ocean_shore_limit(h_loc_raw, wet_d);
    float ratio_loc  = h_loc_raw / max(ocean_shore_breaker * wet_d, 1e-3f);
    float peak_loc   = lerp(1.0f, 2.8f, saturate(h_loc_raw / max(wet_d, 0.05f) * 1.1f));
    float mean_loc   = min(0.5f, rsqrt(3.14159265f * peak_loc));

    // the face steepens as the wave feels the bed, the front half of the cycle shrinks
    float asym       = min(saturate(ratio_loc * 0.6f) * 0.65f + smoothstep(0.9f, 1.4f, ratio_loc) * 0.2f, 0.85f);
    float half_front = 0.5f * (1.0f - asym);
    float shifted    = cycles + half_front;
    float n          = floor(shifted);
    float u          = shifted - n - half_front;                       // crest at 0, shoreward face negative
    float un         = u < 0.0f ? u / (1.0f - asym) : u / (1.0f + asym); // back to a symmetric -0.5..0.5

    float h_raw  = offshore * lerp(0.6f, 1.4f, ocean_shore_noise(grid_xz / 60.0f, int(n))) * ocean_shore_group(n) * shoal;
    float height = ocean_shore_limit(h_raw, wet_d);
    float ratio  = h_raw / max(ocean_shore_breaker * wet_d, 1e-3f);
    float breaking = smoothstep(0.75f, 1.25f, ratio);
    float peak   = lerp(lerp(1.0f, 2.8f, saturate(h_raw / max(wet_d, 0.05f) * 1.1f)), 1.5f, breaking);
    float crest  = pow(0.5f + 0.5f * cos(6.2831853f * un), peak);

    // crest pushed ahead of its face, strongest right as it trips, never folded past the face width
    float c_local   = min(sqrt(ocean_shore_g * (slope * max(distance, 0.0f) + ocean_shore_rest_depth)), c_deep);
    float face_span = half_front * c_local * period;
    float push      = min(height * 0.6f * smoothstep(0.6f, 1.1f, ratio), 0.35f * face_span);
    float crest3    = crest * crest * crest;
    float resolve   = 1.0f - smoothstep(c_local * period / 16.0f, c_local * period / 6.0f, spacing);

    o.displacement = float3(dir.x * push * crest3, height * crest - h_loc * mean_loc, dir.y * push * crest3) * resolve;
    o.breaking     = breaking;
    o.crest        = crest;
    o.face         = u < 0.0f ? smoothstep(-half_front, 0.0f, u) : 0.0f;
    o.height       = height;
    o.steepness    = smoothstep(0.35f, 0.9f, ratio);
    o.direction    = dir;

    // run-up, the bore reaches the waterline as its crest arrives and climbs the slope as a thin sheet
    // fast uprush, slower backwash, the sheet edge sits exactly where the front height meets the sand
    float shore_cycles = time / period + psi;
    float ns           = floor(shore_cycles);
    float x            = shore_cycles - ns;
    float swell_shore  = wave.x * exposure * lerp(0.6f, 1.4f, ocean_shore_noise(grid_xz / 60.0f, int(ns))) * ocean_shore_group(ns);
    // every swash runs up in lobes and tongues a few metres wide, a front that only traced the height of the
    // sand would cut straight lines along the terrain triangles
    float lobes        = 1.0f + (ocean_shore_noise(grid_xz / 4.0f, int(ns) * 3 + 11) - 0.5f) * 0.35f + (ocean_shore_noise(grid_xz / 1.6f, int(ns) * 3 + 12) - 0.5f) * 0.2f;
    float runup        = ocean_shore_runup * swell_shore * lobes;
    float y            = saturate(x / ocean_shore_swash_span);
    float front        = x < ocean_shore_swash_span ? runup * sin(3.14159265f * pow(y, 0.75f)) : -1.0f;
    float above        = -depth;
    float excess       = front - above;
    o.floor_y          = excess > -2.0f ? bed + clamp(excess * 0.2f, -0.4f, lerp(0.05f, 0.015f, y)) : -100000.0f;
    o.sheet            = saturate(excess / 0.02f) * saturate((above + 0.3f) / 0.3f);

    // seconds since the sheet drained off this sand
    float since = 0.0f;
    if (excess <= 0.0f && above > 0.0f)
    {
        float leave = runup > above ? pow(1.0f - asin(saturate(above / runup)) / 3.14159265f, 4.0f / 3.0f) * ocean_shore_swash_span : 0.0f;
        since       = runup > above ? (x > leave ? x - leave : x + 1.0f - leave) * period : 1000.0f;
    }

    // the sheet leaves a mirror of water that soaks into the sand within a couple of seconds, the pores
    // below stay saturated far longer, so the sand darkens at once and pales over tens of seconds
    // the bigger waves of a set reach higher than the rest, which keeps a band below their reach damp all
    // the time, its upper edge wanders with the swash instead of tracing the terrain triangles
    float runup_max = wave.x * exposure * ocean_shore_runup * 1.6f;
    float wander    = (ocean_shore_noise(grid_xz / 7.0f, 5) - 0.5f) * 0.45f + (ocean_shore_noise(grid_xz / 1.7f, 9) - 0.5f) * 0.15f;
    float damp      = 1.0f - smoothstep(0.1f, 1.0f, (above + wander * runup_max) / max(runup_max, 1e-3f));
    o.wet           = max(exp(-since / 20.0f), damp * 0.7f);
    o.film          = exp(-since / 1.6f);

    // whitewater, the roller rides the face, a trail decays behind the crest, the surf zone keeps a residue
    // a coast does not break all at once, sections trip early or late with the alongshore swell, so the
    // white water peels along the beach while the sections between stay glassy and green
    float water   = saturate(wet_d / 0.1f);
    float spill   = ocean_shore_noise(grid_xz / 9.0f, int(n) + 17);
    float section = ocean_shore_noise(grid_xz / 38.0f, int(n) + 41) * 0.7f + ocean_shore_noise(grid_xz / 13.0f, int(n) + 43) * 0.3f;
    float tripped = smoothstep(0.75f, 1.25f, ratio * lerp(0.7f, 1.3f, section));
    float roller  = water * tripped * lerp(0.45f, 1.0f, spill) * (u < 0.0f ? smoothstep(-half_front * lerp(0.15f, 0.8f, spill), 0.0f, u) : 1.0f - smoothstep(0.0f, 0.05f, u));
    float trail   = water * tripped * (u >= 0.0f ? exp(-u * 4.0f) : 0.0f) * 0.75f;
    float surf    = water * smoothstep(0.8f, 1.6f, ratio_loc) * 0.4f;
    // the bore front is a ragged rim of foam, thick where it carries a broken roller and thin between
    float rim     = lerp(0.25f, 1.0f, smoothstep(0.2f, 0.8f, ocean_shore_noise(grid_xz / 6.0f, int(ns) + 29)));
    float lip     = o.sheet * (1.0f - smoothstep(0.0f, 0.04f + 0.04f * rim, excess)) * rim * lerp(0.5f, 1.0f, ocean_shore_noise(grid_xz / 2.5f, int(ns) + 23));
    float swash   = o.sheet * lerp(0.6f, 0.15f, y);
    o.foam        = saturate(max(max(roller, trail), max(surf, max(lip, swash))));
    o.foam       *= saturate(offshore * 4.0f + o.sheet);

    // the lace travels with the water, up and down the slope with the sheet, shoreward through the surf zone
    // the surf drift runs along the uniform swell, an offset built from the wave count times sampled field values
    // grows to kilometres and turns the field's filtering steps into blocks of torn foam
    float sheet_travel = max(front, 0.0f) / slope;
    o.lace_offset      = o.sheet > 0.0f ? -dir * sheet_travel * 0.5f : -swell * time * 0.6f;

    return o;
}

// the shore surface only, fft excluded, for normals from finite differences in the fft grid
float3 ocean_shore_surface(float2 grid_xz, float time, float spacing = 0.0f)
{
    OceanShore o = ocean_shore_evaluate(grid_xz, time, spacing);
    float y      = max(buffer_frame.ocean_sea_level + o.displacement.y, o.floor_y);
    return float3(grid_xz.x + o.displacement.x, y, grid_xz.y + o.displacement.z);
}

// nearest and second nearest feature distance of a jittered grid, and a random value for the nearest cell
float3 ocean_shore_cells(float2 p, int salt)
{
    float2 i  = floor(p);
    float2 f  = p - i;
    float d1  = 8.0f;
    float d2  = 8.0f;
    float id  = 0.0f;
    [unroll] for (int y = -1; y <= 1; y++)
    {
        [unroll] for (int x = -1; x <= 1; x++)
        {
            int2 cell  = int2(i) + int2(x, y);
            float2 jit = 0.1f + 0.8f * float2(ocean_shore_hash(cell, salt), ocean_shore_hash(cell, salt + 1));
            float2 d   = float2(x, y) + jit - f;
            float dd   = dot(d, d);
            if (dd < d1)
            {
                d2 = d1;
                d1 = dd;
                id = ocean_shore_hash(cell, salt + 2);
            }
            else if (dd < d2)
            {
                d2 = dd;
            }
        }
    }
    return float3(sqrt(d1), sqrt(d2), id);
}

// surf foam is a raft of bubbles, what reads as white are the walls where bubbles touch, what reads as holes
// are the big bubbles that burst, so the foam is a network of cell walls whose width follows the density
// dense foam thickens the walls until the cells close, thin foam keeps only threads that snap apart
// 1 on a wall, 0 in a hole, the rim is soft because the walls thin out towards the open water
float ocean_shore_lace(float2 p, float density, int salt)
{
    // a bubble raft is never a regular grid, two octaves of warp give cells of every shape and size
    p           += (float2(ocean_shore_noise(p * 0.31f, salt + 71), ocean_shore_noise(p * 0.31f + 5.3f, salt + 73)) - 0.5f) * 2.2f;
    p           += (float2(ocean_shore_noise(p * 1.3f, salt + 75), ocean_shore_noise(p * 1.3f + 2.9f, salt + 77)) - 0.5f) * 0.45f;
    float3 c     = ocean_shore_cells(p, salt + 11);
    float edge   = c.y - c.x;
    // some bubbles hold and stay closed, some burst into wide holes, a per cell value would jump right on the
    // walls it is meant to widen, so the hole size is a smooth field over the raft
    float burst  = lerp(0.4f, 1.6f, ocean_shore_noise(p * 0.55f, salt + 37));
    float wall   = lerp(0.03f, 0.8f, density * density) / burst;
    // a thin wall is a few bubbles across, its rim is diffuse and the water shows through it
    float soft   = 0.08f + 0.3f * wall;
    float net    = (1.0f - smoothstep(wall * 0.5f, wall + soft, edge)) * lerp(0.5f, 1.0f, saturate(wall * 4.0f));
    // thin threads break, a strand survives only where the foam around it is still dense enough
    float strand = ocean_shore_noise(p * 0.9f, salt + 79);
    return net * smoothstep(0.15f, 0.55f, strand + density * 0.8f);
}

// dense whitewater is a solid mass, thinning foam erodes into a net and then into scattered strands
// unresolved detail fades to its mean so the beach does not shimmer from a distance
// the result is coverage and opacity in one, the thin walls of a draining net let the water through
float ocean_shore_foam_shape(OceanShore o, float2 world_xz, float footprint)
{
    if (o.foam <= 0.0f)
        return 0.0f;
    // the foam is carried by the water and drawn out along the way the swell runs
    // the stretch axis must be the same everywhere, rotating world coordinates kilometres from the origin by the
    // sampled shore direction turns its filtering steps into jumps of the whole pattern
    float2 p       = world_xz + o.lace_offset;
    float2 along   = buffer_frame.ocean_shore_swell.xy;
    float2 q       = float2(dot(p, float2(-along.y, along.x)), dot(p, along) * 0.65f);
    float detail_a = 1.0f - smoothstep(0.15f, 0.45f, footprint / 1.2f);
    float detail_b = 1.0f - smoothstep(0.15f, 0.45f, footprint / 0.3f);
    float patches  = ocean_shore_noise(p / 4.0f, 61) * 0.6f + ocean_shore_noise(p / 1.3f, 53) * 0.4f;
    float density  = saturate(o.foam * 0.95f + (patches - 0.5f) * 0.8f);
    float net_a    = lerp(saturate(density * 1.1f), ocean_shore_lace(q / 1.2f, density, 0), detail_a);
    float net_b    = lerp(0.55f + 0.45f * density, ocean_shore_lace(q / 0.3f + 17.3f, saturate(density + 0.25f), 100), detail_b);
    // the small bubbles fill the gaps between the big ones, only a draining raft opens the fine holes all the way
    net_b          = lerp(1.0f, net_b, lerp(0.9f, 0.4f, smoothstep(0.3f, 0.9f, density)));
    float keep     = smoothstep(0.04f, 0.22f, density);
    // a solid mass of whitewater still clumps, it is never one flat sheet of white
    float clumps   = lerp(0.82f, 1.0f, ocean_shore_noise(q / 0.45f, 83));
    float solid    = smoothstep(0.5f, 0.9f, o.foam) * smoothstep(0.35f, 0.65f, patches + (o.foam - 0.5f) * 1.5f) * clumps;
    float opacity  = lerp(0.5f, 1.0f, smoothstep(0.1f, 0.7f, density));
    // up close the foam is grains of bubbles, each one a bright cap ringed by a darker meniscus
    float detail_c = 1.0f - smoothstep(0.15f, 0.45f, footprint / 0.035f);
    // kilometres from the origin a centimetre lattice runs out of float precision, wrap it on a whole number of cells
    float2 q_grain = q - floor(q / 35.0f) * 35.0f;
    float bubbles  = 0.5f;
    if (detail_c > 0.0f)
    {
        float3 coarse = ocean_shore_cells(q_grain / 0.05f, 91);
        float3 fine   = ocean_shore_cells(q_grain / 0.018f + 3.7f, 93);
        bubbles       = lerp(smoothstep(0.1f, 0.5f, coarse.x), smoothstep(0.1f, 0.5f, fine.x), step(0.45f, coarse.z));
    }
    float grain    = lerp(1.0f, lerp(1.0f, 0.86f, bubbles), detail_c);
    return saturate(max(net_a * net_b * keep * opacity, solid) * grain);
}

#endif
