// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#ifndef COMMON_ROAD_H
#define COMMON_ROAD_H

float road_hash(float2 p)
{
    float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    q += dot(q, q.yzx + 33.33f);
    return frac((q.x + q.y) * q.z);
}

float2 road_hash2(float2 p)
{
    float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    q += dot(q, q.yzx + 33.33f);
    return frac((q.xx + q.yz) * q.zy);
}

// The lattice wraps every 289 cells, the hash streaks at island coordinates times high frequencies.
float road_noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    i -= floor(i / 289.0f) * 289.0f;
    float2 j = i + 1.0f;
    j -= floor(j / 289.0f) * 289.0f;
    return lerp(lerp(road_hash(i), road_hash(float2(j.x, i.y)), f.x),
                lerp(road_hash(float2(i.x, j.y)), road_hash(j), f.x), f.y);
}

float road_noise_soft(float2 p)
{
    float2 r = float2(p.x * 0.8f - p.y * 0.6f, p.x * 0.6f + p.y * 0.8f);
    return road_noise(p) * 0.55f + road_noise(r * 1.73f + float2(31.0f, 7.0f)) * 0.45f;
}

float road_band(float d, float w, float footprint)
{
    float h = max(footprint, 1e-4f) * 0.5f;
    return saturate((min(w, d + h) - max(-w, d - h)) / (2.0f * h));
}

// Voronoi edge distance and unit direction pointing away from the crack fissure.
float road_cell_border(float2 p, out float2 away)
{
    float2 n = floor(p);
    float2 f = frac(p);
    n -= floor(n / 289.0f) * 289.0f;
    float2 nearest_cell = 0.0f;
    float2 nearest = 0.0f;
    float best = 8.0f;
    [unroll]
    for (int j = -1; j <= 1; j++)
    {
        [unroll]
        for (int i = -1; i <= 1; i++)
        {
            float2 g = float2(i, j);
            float2 cell = n + g;
            cell -= floor(cell / 289.0f) * 289.0f;
            float2 r = g + road_hash2(cell) * 0.8f + 0.1f - f;
            float d = dot(r, r);
            if (d < best)
            {
                best = d;
                nearest = r;
                nearest_cell = g;
            }
        }
    }

    best = 8.0f;
    away = float2(1.0f, 0.0f);
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 g = nearest_cell + float2(x, y);
            float2 cell = n + g;
            cell -= floor(cell / 289.0f) * 289.0f;
            float2 r = g + road_hash2(cell) * 0.8f + 0.1f - f;
            float2 delta = r - nearest;
            float d2 = dot(delta, delta);
            if (d2 > 1e-5f)
            {
                float2 dir = delta * rsqrt(d2);
                float d = dot(0.5f * (nearest + r), dir);
                if (d < best)
                {
                    best = d;
                    away = -dir;
                }
            }
        }
    }
    return best;
}

float road_blobs(float2 p, float density, float2 radius, float2 seed)
{
    float2 n = floor(p);
    float2 f = frac(p);
    n -= floor(n / 289.0f) * 289.0f;
    float best = 0.0f;
    [unroll]
    for (int j = -1; j <= 1; j++)
    {
        [unroll]
        for (int i = -1; i <= 1; i++)
        {
            float2 g = float2(i, j);
            float2 cell = n + g + seed;
            cell -= floor(cell / 289.0f) * 289.0f;
            if (road_hash(cell + float2(5.3f, 1.7f)) > density)
                continue;
            float2 c = g + road_hash2(cell) * 0.8f + 0.1f - f;
            float r = lerp(radius.x, radius.y, road_hash(cell + float2(0.7f, 9.1f)));
            float strength = lerp(0.35f, 1.0f, road_hash(cell + float2(3.9f, 4.4f)));
            best = max(best, (1.0f - smoothstep(0.45f, 1.0f, length(c) / r)) * strength);
        }
    }
    return best;
}

// =============================================================================
// AAA World-Space Road Surface Detail, Contact-Refinement Parallax & Imperfections
// =============================================================================
static const float ROAD_DETAIL_REPEAT_PRIMARY   = 1.05f; // metres per 4K scanned asphalt tile (~3900 px/m = 0.25 mm/texel)
static const float ROAD_DETAIL_REPEAT_SECONDARY = 2.71f; // irrational rotated secondary octave eliminates all tiling
static const float ROAD_POM_MAX_STEPS           = 26.0f;
static const float ROAD_POM_MIN_STEPS           = 10.0f;
static const uint  ROAD_POM_REFINE_STEPS        = 6;
static const float ROAD_POM_FADE_START          = 16.0f;
static const float ROAD_POM_FADE_END            = 32.0f;
static const float ROAD_POM_MIN_COSINE          = 0.16f;
static const float ROAD_POM_RELIEF_METRES       = 0.026f; // 2.6 cm effective relief across cracks, divots & crushed aggregate
static const uint  ROAD_POM_SHADOW_STEPS        = 5;
static const float ROAD_POM_SHADOW_HARDNESS     = 4.0f;

struct RoadImperfections
{
    float  crack;       // open deep thermal/fatigue fissure
    float  tar_snake;   // raised glossy bitumen crack sealant overband
    float  grime;       // damp/dirty halo along crack lips
    float2 slope;       // world XZ height slope from fissures & divots
    float  repair;      // saw-cut relaid asphalt patch
    float  repair_seam; // tar-overbanded patch saw-cut seam
    float  divot;       // spalled shallow raveling chip-out / mini-pothole
    float  bleach;      // sun-bleached oxidized dry stone patches
    float  stain;       // damp mineral / oil / tire wear blotches
    float  oil;         // dark glossy oil drips & spills
    float  grit;        // loose millimeter limestone/basalt grit pebbles (signed)
    float  age;         // local pavement age (0 fresh, 1 weathered Mediterranean stone)
};

// Rotated 37 deg transform for dual-octave anti-tiling world-space sampling
float2 road_rot37(float2 p)
{
    return float2(p.x * 0.7986355f - p.y * 0.6018150f, p.x * 0.6018150f + p.y * 0.7986355f);
}

// Fast procedural multi-scale crushed aggregate heightfield (fallback & micro-chisel boost)
float road_procedural_aggregate(float2 xz)
{
    float2 p1 = xz * 58.0f; // ~1.7 cm coarse crushed granite/limestone chips
    float2 p2 = road_rot37(xz) * 142.0f; // ~7 mm medium mineral aggregate
    float a1 = road_noise(p1);
    float a2 = road_noise(p2 + float2(19.3f, 7.1f));
    float a3 = road_noise(xz * 310.0f + float2(5.7f, 23.4f));
    // Crisp stone tops separated by recessed bitumen voids
    float stones = smoothstep(0.24f, 0.78f, a1 * 0.52f + a2 * 0.34f + a3 * 0.14f);
    return stones;
}

// Evaluates world-space imperfections (cracks, tar-snakes, repair patches, divots, oil, grit)
RoadImperfections road_eval_imperfections(float2 xz, float footprint)
{
    RoadImperfections w;
    w.crack       = 0.0f;
    w.tar_snake   = 0.0f;
    w.grime       = 0.0f;
    w.slope       = 0.0f;
    w.repair      = 0.0f;
    w.repair_seam = 0.0f;
    w.divot       = 0.0f;
    w.bleach      = 0.0f;
    w.stain       = 0.0f;
    w.oil         = 0.0f;
    w.grit        = 0.0f;

    float fp = max(footprint, 1e-4f);

    // Macro pavement age & distress zones (25-60 m wavelength, completely breaks 27 m atlas repeat)
    float age_n    = road_noise_soft(xz * 0.022f + float2(11.0f, 47.0f));
    float distress = smoothstep(0.26f, 0.76f, age_n * 0.65f + road_noise_soft(xz * 0.065f + float2(73.0f, 19.0f)) * 0.35f);
    w.age          = saturate(0.32f + 0.68f * age_n);

    // 1. Saw-cut rectangular asphalt repair patches (world-space cells with per-cell rotation)
    {
        float2 patch_cell_sz = float2(7.5f, 11.0f);
        float2 pc = floor(xz / patch_cell_sz);
        float2 pf = frac(xz / patch_cell_sz) - 0.5f;
        float2 pc_wrap = pc - floor(pc / 289.0f) * 289.0f;
        float pick = road_hash(pc_wrap + float2(41.7f, 13.3f));
        if (pick > 0.76f)
        {
            float2 jitter = (road_hash2(pc_wrap + float2(3.1f, 8.9f)) - 0.5f) * 0.28f;
            float2 half_ext = lerp(float2(0.14f, 0.12f), float2(0.34f, 0.36f), road_hash2(pc_wrap + float2(17.5f, 29.1f)));
            float2 q = abs((pf - jitter) * patch_cell_sz) - half_ext * patch_cell_sz;
            float sdf = max(q.x, q.y);
            // Positive inside patch
            float inside = saturate(-sdf / max(fp, 0.008f) + 0.5f);
            float seam   = road_band(abs(sdf), 0.026f, fp) * 0.85f + road_band(abs(sdf), 0.048f, fp) * 0.25f;
            w.repair      = inside;
            w.repair_seam = saturate(seam);
            w.age         = lerp(w.age, 0.16f + 0.18f * road_hash(pc_wrap + float2(9.0f, 5.0f)), inside);
            w.tar_snake   = max(w.tar_snake, w.repair_seam * 0.92f);
        }
    }

    // 2. Primary meandering thermal & structural cracks (domain-warped jagged fissures ~3.4 m, sparse & organic)
    float sealed_zone = smoothstep(0.56f, 0.72f, road_noise_soft(xz * 0.028f + float2(9.0f, 61.0f)));
    {
        float2 p = xz / 3.40f;
        p += (float2(road_noise_soft(p * 0.90f + float2(4.0f, 19.0f)), road_noise_soft(p * 0.90f + float2(37.0f, 11.0f))) - 0.5f) * 1.15f;
        p += (float2(road_noise(p * 4.8f + float2(2.0f, 8.0f)), road_noise(p * 4.8f + float2(61.0f, 3.0f))) - 0.5f) * 0.22f;
        p += (float2(road_noise(p * 15.0f + float2(11.0f, 5.0f)), road_noise(p * 15.0f + float2(3.0f, 29.0f))) - 0.5f) * 0.05f;
        float2 away;
        float d = road_cell_border(p, away) * 3.40f;
        // Modulate crack runs so cracks appear in realistic localized bursts, never uniform loops
        float burst = smoothstep(0.46f, 0.68f, road_noise_soft(xz * 0.15f + float2(17.0f, 53.0f))) * smoothstep(0.32f, 0.62f, road_noise(xz * 0.65f + float2(7.0f, 13.0f)));
        float presence = burst * smoothstep(0.22f, 0.68f, distress) * (1.0f - w.repair * 0.92f);
        if (presence > 0.01f)
        {
            float half_w = 0.0022f + 0.0030f * road_noise(xz * 7.3f);
            float is_sealed = saturate(sealed_zone + step(0.74f, road_noise(floor(p) + float2(5.0f, 13.0f))) * 0.65f);
            float seal_w = 0.010f + half_w * 2.4f;
            float seal = (road_band(d, seal_w, fp) * 0.80f + road_band(d, seal_w * 1.45f, fp) * 0.20f) * is_sealed * presence;
            float open_c = road_band(d, half_w, fp) * (1.0f - is_sealed * 0.90f) * presence;
            float halo = (road_band(d, 0.012f + half_w * 3.2f, fp) * 0.65f + road_band(d, 0.028f, fp) * 0.35f) * presence;
            float groove = open_c * saturate(half_w * 6.0f / fp);
            w.tar_snake = max(w.tar_snake, seal);
            w.crack     = max(w.crack, open_c);
            w.grime     = max(w.grime, halo * (1.0f - is_sealed * 0.55f));
            w.slope    += away * groove * 1.45f;
        }
    }

    // 3. Secondary alligator fatigue cracking in distressed patches (~0.26 m cell network)
    float fatigue_zone = smoothstep(0.58f, 0.78f, road_noise_soft(xz * 0.16f + float2(83.0f, 29.0f))) * smoothstep(0.45f, 0.85f, distress) * (1.0f - w.repair);
    if (fatigue_zone > 0.04f && fp < 0.045f)
    {
        float2 p = xz / 0.26f;
        p += (float2(road_noise(p * 0.7f), road_noise(p * 0.7f + float2(13.0f, 7.0f))) - 0.5f) * 0.65f;
        float2 away;
        float d = road_cell_border(p, away) * 0.26f;
        float opened = smoothstep(0.32f, 0.55f, road_noise(xz * 4.1f + float2(19.0f, 5.0f)));
        float half_w = 0.0018f + 0.0022f * road_noise(xz * 11.0f);
        float c = road_band(d, half_w, fp) * fatigue_zone * opened * 0.88f;
        w.crack  = max(w.crack, c);
        w.grime  = max(w.grime, road_band(d, 0.012f, fp) * fatigue_zone * opened * 0.55f);
        w.slope += away * c * saturate(half_w * 4.0f / fp) * 0.9f;
    }

    // 4. Spalled shallow mini-potholes / raveled stone divots
    {
        float2 dp = xz / 1.85f + (float2(road_noise(xz * 1.9f), road_noise(xz * 1.9f + float2(7.0f, 23.0f))) - 0.5f) * 0.28f;
        float d_raw = road_blobs(dp, 0.08f + 0.10f * distress, float2(0.10f, 0.26f), float2(27.0f, 63.0f));
        w.divot = smoothstep(0.18f, 0.68f, d_raw) * (1.0f - w.repair * 0.85f) * (1.0f - w.tar_snake);
    }

    // 5. Macro sun-bleaching, damp mineral stains, oil drips & loose grit pebbles
    float2 warp = (float2(road_noise_soft(xz * 0.21f + float2(5.0f, 17.0f)), road_noise_soft(xz * 0.21f + float2(41.0f, 3.0f))) - 0.5f) * 2.2f;
    float ragged = road_noise(xz * 2.1f) * 0.14f + road_noise(xz * 7.5f + float2(9.0f, 2.0f)) * 0.08f;
    w.bleach = smoothstep(0.56f, 0.74f, road_noise_soft((xz + warp) * 0.14f + float2(13.0f, 71.0f)) + ragged) * (1.0f - w.repair * 0.7f);
    w.stain  = smoothstep(0.58f, 0.75f, road_noise_soft((xz + warp) * 0.19f + float2(59.0f, 11.0f)) + ragged) * (1.0f - w.bleach * 0.7f);

    float2 ow = (float2(road_noise_soft(xz * 1.6f), road_noise_soft(xz * 1.6f + float2(11.0f, 5.0f))) - 0.5f) * 0.35f;
    float drips  = road_blobs(xz / 0.85f + ow, 0.09f, float2(0.10f, 0.26f), float2(17.0f, 3.0f)) * 0.55f;
    float spills = road_blobs(xz / 4.2f + ow * 0.3f, 0.025f, float2(0.08f, 0.18f), float2(61.0f, 29.0f)) * 0.65f;
    w.oil = saturate(max(drips, spills));

    // Fine millimeter mineral grit & loose gravel specks (subtle, realistic scale, never round white confetti)
    float grit_res = 1.0f - smoothstep(0.0018f, 0.0055f, fp);
    if (grit_res > 0.0f)
    {
        float density = 0.05f + 0.14f * w.divot;
        float light_g = road_blobs(xz / 0.0075f, density, float2(0.14f, 0.30f), float2(3.0f, 11.0f));
        float dark_g  = road_blobs(xz / 0.0065f, density * 0.9f, float2(0.16f, 0.32f), float2(37.0f, 5.0f));
        w.grit = (light_g * 0.45f - dark_g * 0.65f) * grit_res;
    }

    return w;
}

// Samples the high-resolution road heightfield at world XZ for Contact-Refinement POM.
// Combines Slot-1 4K scanned asphalt height (when bound) + procedural aggregate + cracks/divots/tar-snakes.
float road_sample_relief_height(
    uint   detail_packed_idx,
    float2 xz,
    float2 dxz_dx,
    float2 dxz_dy,
    float  footprint,
    float  macro_crack_bias
)
{
    float h_agg;
    if (detail_packed_idx > 0u)
    {
        float2 uv1  = xz / ROAD_DETAIL_REPEAT_PRIMARY;
        float2 dx1  = dxz_dx / ROAD_DETAIL_REPEAT_PRIMARY;
        float2 dy1  = dxz_dy / ROAD_DETAIL_REPEAT_PRIMARY;
        float  h1   = material_textures[NonUniformResourceIndex(detail_packed_idx)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv1, dx1, dy1).a;

        float2 xz2  = road_rot37(xz);
        float2 uv2  = xz2 / ROAD_DETAIL_REPEAT_SECONDARY;
        float2 dx2  = road_rot37(dxz_dx) / ROAD_DETAIL_REPEAT_SECONDARY;
        float2 dy2  = road_rot37(dxz_dy) / ROAD_DETAIL_REPEAT_SECONDARY;
        float  h2   = material_textures[NonUniformResourceIndex(detail_packed_idx)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv2, dx2, dy2).a;

        // Strong contrast-expanded scanned asphalt stone relief for crisp 3D POM pebbles
        h_agg = saturate((h1 * 0.76f + h2 * 0.24f - 0.20f) * 1.85f);
    }
    else
    {
        h_agg = road_procedural_aggregate(xz);
    }

    // Matching jagged crack/divot/tar-snake macro relief terms inside the POM march
    float2 p = xz / 3.40f;
    p += (float2(road_noise_soft(p * 0.90f + float2(4.0f, 19.0f)), road_noise_soft(p * 0.90f + float2(37.0f, 11.0f))) - 0.5f) * 1.15f;
    p += (float2(road_noise(p * 4.8f + float2(2.0f, 8.0f)), road_noise(p * 4.8f + float2(61.0f, 3.0f))) - 0.5f) * 0.22f;
    p += (float2(road_noise(p * 15.0f + float2(11.0f, 5.0f)), road_noise(p * 15.0f + float2(3.0f, 29.0f))) - 0.5f) * 0.05f;
    float2 dummy_away;
    float d_crack = road_cell_border(p, dummy_away) * 3.40f;
    float burst   = smoothstep(0.46f, 0.68f, road_noise_soft(xz * 0.15f + float2(17.0f, 53.0f))) * smoothstep(0.32f, 0.62f, road_noise(xz * 0.65f + float2(7.0f, 13.0f)));
    float c_zone  = burst * macro_crack_bias;
    float sealed  = smoothstep(0.56f, 0.72f, road_noise_soft(xz * 0.028f + float2(9.0f, 61.0f)));
    float open_c  = road_band(d_crack, 0.005f, footprint) * (1.0f - sealed * 0.90f) * c_zone;
    float tar_s   = road_band(d_crack, 0.016f, footprint) * sealed * c_zone;

    float2 dp = xz / 1.85f + (float2(road_noise(xz * 1.9f), road_noise(xz * 1.9f + float2(7.0f, 23.0f))) - 0.5f) * 0.28f;
    float divot = smoothstep(0.18f, 0.68f, road_blobs(dp, 0.12f, float2(0.10f, 0.26f), float2(27.0f, 63.0f))) * (1.0f - tar_s);

    // Base stone relief lives in 0.36..0.92; open cracks carve down to ~0.04; tar-snakes mound gently to ~0.95
    float h = 0.36f + h_agg * 0.56f;
    h -= open_c * 0.44f;
    h -= divot  * 0.28f;
    h  = lerp(h, 0.95f, saturate(tar_s * 0.80f));
    return saturate(h);
}

// Contact-Refinement Parallax Occlusion Mapping + Directional Relief Self-Shadowing in World XZ
float2 road_parallax_march(
    uint      detail_packed_idx,
    float2    xz,
    float2    dxz_dx,
    float2    dxz_dy,
    float3    view_ray,
    float3    light_ray,
    float     depth_metres,
    float     footprint,
    float2    screen_pos,
    out float hit_height,
    out float relief_shadow
)
{
    relief_shadow = 0.0f;
    hit_height    = 0.65f;

    float n_dot_v = saturate(view_ray.z);
    if (depth_metres <= 1e-5f || n_dot_v <= 0.02f)
    {
        hit_height = road_sample_relief_height(detail_packed_idx, xz, dxz_dx, dxz_dy, footprint, 0.7f);
        return xz;
    }

    float age_n = road_noise_soft(xz * 0.022f + float2(11.0f, 47.0f));
    float crack_bias = smoothstep(0.20f, 0.65f, age_n * 0.65f + road_noise_soft(xz * 0.065f + float2(73.0f, 19.0f)) * 0.35f);

    uint   num_steps  = (uint)lerp(ROAD_POM_MAX_STEPS, ROAD_POM_MIN_STEPS, n_dot_v);
    float  layer_step = 1.0f / (float)num_steps;
    float2 delta_xz   = view_ray.xy / max(view_ray.z, ROAD_POM_MIN_COSINE) * depth_metres / (float)num_steps;

    float dither = noise_interleaved_gradient(screen_pos);
    float2 prev_xz    = xz;
    float  prev_layer = 1.0f;
    float  prev_samp  = road_sample_relief_height(detail_packed_idx, prev_xz, dxz_dx, dxz_dy, footprint, crack_bias);

    float2 cur_xz    = xz - delta_xz * dither;
    float  cur_layer = 1.0f - layer_step * dither;
    float  cur_samp  = road_sample_relief_height(detail_packed_idx, cur_xz, dxz_dx, dxz_dy, footprint, crack_bias);

    bool hit = cur_samp >= cur_layer;
    for (uint s = 1; s < num_steps && !hit; s++)
    {
        prev_xz    = cur_xz;
        prev_layer = cur_layer;
        prev_samp  = cur_samp;
        cur_xz    -= delta_xz;
        cur_layer -= layer_step;
        cur_samp   = road_sample_relief_height(detail_packed_idx, cur_xz, dxz_dx, dxz_dy, footprint, crack_bias);
        hit        = cur_samp >= cur_layer;
    }

    // Contact refinement walk inside the straddled interval
    if (hit)
    {
        float2 fine_xz    = (cur_xz - prev_xz) / (float)ROAD_POM_REFINE_STEPS;
        float  fine_layer = (cur_layer - prev_layer) / (float)ROAD_POM_REFINE_STEPS;
        for (uint r = 1; r < ROAD_POM_REFINE_STEPS; r++)
        {
            float2 step_xz    = prev_xz + fine_xz * (float)r;
            float  step_layer = prev_layer + fine_layer * (float)r;
            float  step_samp  = road_sample_relief_height(detail_packed_idx, step_xz, dxz_dx, dxz_dy, footprint, crack_bias);
            if (step_samp >= step_layer)
            {
                cur_xz    = step_xz;
                cur_layer = step_layer;
                cur_samp  = step_samp;
                break;
            }
            prev_xz    = step_xz;
            prev_layer = step_layer;
            prev_samp  = step_samp;
        }
    }

    float prev_diff = prev_layer - prev_samp;
    float cur_diff  = cur_samp - cur_layer;
    float t         = (prev_diff + cur_diff) > 1e-5f ? saturate(prev_diff / (prev_diff + cur_diff)) : 0.5f;
    float2 hit_xz   = lerp(prev_xz, cur_xz, t);
    hit_height      = lerp(prev_samp, cur_samp, t);

    // Directional sun relief self-shadowing (stones, crack edges, divots cast crisp micro-shadows)
    if (light_ray.z > 0.02f)
    {
        float2 light_xz = light_ray.xy / max(light_ray.z, ROAD_POM_MIN_COSINE) * depth_metres * (1.0f - hit_height);
        float shadow = 0.0f;
        [unroll(ROAD_POM_SHADOW_STEPS)]
        for (uint i = 0; i < ROAD_POM_SHADOW_STEPS; i++)
        {
            float s_t    = ((float)i + 0.25f + 0.75f * dither) / (float)ROAD_POM_SHADOW_STEPS;
            float ray_h  = lerp(hit_height, 1.0f, s_t);
            float samp_h = road_sample_relief_height(detail_packed_idx, hit_xz + light_xz * s_t, dxz_dx, dxz_dy, footprint, crack_bias);
            shadow       = max(shadow, (samp_h - ray_h) / s_t);
        }
        relief_shadow = saturate(shadow * ROAD_POM_SHADOW_HARDNESS) * saturate(light_ray.z * 8.0f);
    }

    return hit_xz;
}

// Applies AAA micro-detail, scanned 4K aggregate, world-space imperfections, and chipped thermoplastic paint.
void road_apply_aaa_detail(
    uint         material_index,
    bool         is_paint,
    float2       road_xz,
    float2       dxz_dx,
    float2       dxz_dy,
    float3       geometric_normal,
    float3       view_dir,
    float3       light_dir,
    float        distance,
    float        footprint,
    float        hit_height,
    float        relief_shadow,
    inout float3 albedo,
    inout float3 normal,
    inout float  roughness,
    inout float  occlusion,
    inout float  height_map
)
{
    uint detail_albedo_idx = get_material_texture_index(material_index, material_texture_index_albedo + 1);
    uint detail_normal_idx = get_material_texture_index(material_index, material_texture_index_normal + 1);
    uint detail_packed_idx = get_material_texture_index(material_index, material_texture_index_packed + 1);

    float detail_fade = saturate((110.0f - distance) / 85.0f);
    float micro_fade  = saturate((42.0f - distance) / 28.0f);

    // 1. Sample 4K scanned micro-asphalt detail (dual-rotated world XZ for zero repetition!)
    float3 det_albedo = float3(0.085f, 0.084f, 0.082f);
    float3 det_normal_ws = geometric_normal;
    float  det_ao     = 1.0f;
    float  det_rough  = 0.84f;
    float  det_height = hit_height;

    if (detail_albedo_idx > 0u && detail_normal_idx > 0u && detail_packed_idx > 0u)
    {
        float2 uv1 = road_xz / ROAD_DETAIL_REPEAT_PRIMARY;
        float2 dx1 = dxz_dx / ROAD_DETAIL_REPEAT_PRIMARY;
        float2 dy1 = dxz_dy / ROAD_DETAIL_REPEAT_PRIMARY;

        float2 xz2 = road_rot37(road_xz);
        float2 uv2 = xz2 / ROAD_DETAIL_REPEAT_SECONDARY;
        float2 dx2 = road_rot37(dxz_dx) / ROAD_DETAIL_REPEAT_SECONDARY;
        float2 dy2 = road_rot37(dxz_dy) / ROAD_DETAIL_REPEAT_SECONDARY;

        float3 c1 = srgb_to_linear(material_textures[NonUniformResourceIndex(detail_albedo_idx)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv1, dx1, dy1).rgb);
        float3 c2 = srgb_to_linear(material_textures[NonUniformResourceIndex(detail_albedo_idx)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv2, dx2, dy2).rgb);
        det_albedo = c1 * 0.72f + c2 * 0.28f;

        float4 p1 = material_textures[NonUniformResourceIndex(detail_packed_idx)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv1, dx1, dy1);
        float4 p2 = material_textures[NonUniformResourceIndex(detail_packed_idx)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv2, dx2, dy2);
        float4 pk = p1 * 0.72f + p2 * 0.28f;
        det_ao     = pk.r;
        det_rough  = pk.g;
        det_height = saturate((pk.a - 0.18f) * 1.55f);

        float3 n1 = material_textures[NonUniformResourceIndex(detail_normal_idx)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv1, dx1, dy1).xyz * 2.0f - 1.0f;
        float3 n2 = material_textures[NonUniformResourceIndex(detail_normal_idx)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv2, dx2, dy2).xyz * 2.0f - 1.0f;
        // Rotate n2 back from 37 deg to world XZ
        float2 n2_rot = float2(n2.x * 0.7986355f + n2.y * 0.6018150f, -n2.x * 0.6018150f + n2.y * 0.7986355f);
        float2 n_xy = (n1.xy * 0.78f + n2_rot * 0.28f) * 2.35f * detail_fade;
        // OpenGL normal (+Y up in UV -> +Z in world XZ), build orthonormal tangent basis on road surface
        float3 t_x = normalize(float3(1.0f, 0.0f, 0.0f) - geometric_normal * geometric_normal.x);
        float3 t_z = normalize(cross(geometric_normal, t_x));
        det_normal_ws = normalize(geometric_normal + t_x * n_xy.x + t_z * n_xy.y);
    }
    else
    {
        det_height = road_procedural_aggregate(road_xz);
        float eps = max(footprint * 0.6f, 0.0025f);
        float hx = road_procedural_aggregate(road_xz + float2(eps, 0.0f)) - det_height;
        float hz = road_procedural_aggregate(road_xz + float2(0.0f, eps)) - det_height;
        float3 slope_agg = float3(hx, 0.0f, hz) / eps * (0.012f * detail_fade);
        slope_agg -= geometric_normal * dot(slope_agg, geometric_normal);
        det_normal_ws = normalize(geometric_normal - slope_agg);
        det_ao = lerp(0.76f, 1.0f, det_height);
    }

    // 2. Detect baked white road markings in the macro atlas OR explicit paint layer
    float macro_lum    = dot(albedo, float3(0.2126f, 0.7152f, 0.0722f));
    float macro_chroma = max(albedo.r, max(albedo.g, albedo.b)) - min(albedo.r, min(albedo.g, albedo.b));
    float baked_paint  = smoothstep(0.24f, 0.42f, macro_lum) * (1.0f - smoothstep(0.06f, 0.16f, macro_chroma));
    float paint_raw    = is_paint ? 1.0f : baked_paint;

    // 3. Evaluate world-space imperfections (cracks, tar-snakes, patches, divots, oil, grit)
    RoadImperfections w = road_eval_imperfections(road_xz, footprint);

    // 4. Rebuild crisp crushed-aggregate asphalt albedo from macro tone + high-res stone variation
    float det_lum = max(dot(det_albedo, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f);
    // Individual exposed limestone/granite stone chips vs dark bitumen binder voids
    float stone_mask = saturate(smoothstep(0.38f, 0.76f, det_height) * 0.70f + smoothstep(0.012f, 0.036f, det_lum) * 0.45f);
    float fine_grain = road_noise(road_xz * 95.0f + float2(7.0f, 19.0f));
    float stone_tint = lerp(0.78f, 1.38f, stone_mask) * lerp(0.90f, 1.12f, fine_grain);

    // Preserve macro atlas wheel-path contrast while infusing crisp micro-stone contrast & world-space age breakup
    float3 asphalt_col = albedo;
    if (paint_raw < 0.99f)
    {
        float3 base_asphalt = is_paint ? float3(0.080f, 0.079f, 0.077f) : albedo;
        // Rich bitumen-void vs exposed crushed limestone/basalt aggregate contrast
        float3 stone_speck = lerp(base_asphalt * 0.44f, base_asphalt * 1.65f + float3(0.028f, 0.027f, 0.025f), stone_mask);
        // Subtle warm/cool mineral chip hue variation (limestone, feldspar, dark basalt)
        float chip_hue = road_noise(road_xz * 145.0f + float2(43.0f, 11.0f));
        stone_speck *= lerp(float3(0.93f, 0.95f, 1.03f), float3(1.07f, 1.02f, 0.93f), chip_hue);
        asphalt_col = lerp(base_asphalt, stone_speck, 0.85f * detail_fade + 0.20f);

        // Saw-cut relaid repair patches: slightly darker fresher finer-grain asphalt
        asphalt_col = lerp(asphalt_col, asphalt_col * float3(0.55f, 0.56f, 0.58f), w.repair * 0.78f);

        // Spalled divots: exposed pale-dusty coarse crushed limestone gravel in the depression
        asphalt_col = lerp(asphalt_col, lerp(asphalt_col * 0.72f, float3(0.145f, 0.138f, 0.125f), stone_mask), w.divot * 0.65f);

        // Sun-bleached dry patches vs damp dark stains (completely eliminates 27 m tiling!)
        float a_lum = dot(asphalt_col, float3(0.2126f, 0.7152f, 0.0722f));
        asphalt_col = lerp(asphalt_col, lerp(asphalt_col, a_lum, 0.4f) * 1.24f + float3(0.008f, 0.007f, 0.006f), w.bleach * 0.50f);
        asphalt_col *= lerp(1.0f, float3(0.72f, 0.71f, 0.69f), w.stain * 0.68f);
    }

    // 5. Crisp weathered thermoplastic road paint (chipped edges + stone peaks poking through!)
    float paint_mask = 0.0f;
    if (paint_raw > 0.02f)
    {
        // Sharpen soft blurry atlas paint border against high-res stone relief so edges look physically painted!
        float edge_wear = (det_height - 0.5f) * 0.24f + (road_noise(road_xz * 34.0f) - 0.5f) * 0.16f;
        paint_mask = smoothstep(0.18f, 0.62f, paint_raw + edge_wear);
        // Tire abrasion wears paint off the tallest stone peaks first, leaving paint in crevices
        float peak_chip = smoothstep(0.68f, 0.95f, det_height) * smoothstep(0.32f, 0.75f, road_noise_soft(road_xz * 2.4f + float2(17.0f, 9.0f))) * lerp(0.25f, 0.85f, w.age);
        paint_mask *= (1.0f - peak_chip * 0.68f) * (1.0f - w.repair * 0.80f);

        float3 paint_col = lerp(float3(0.72f, 0.71f, 0.67f), float3(0.54f, 0.52f, 0.47f), w.age * 0.65f);
        // Grime collects in deep asphalt voids beneath the thermoplastic paint
        float void_grime = saturate((0.35f - det_height) * 2.5f);
        paint_col = lerp(paint_col, float3(0.08f, 0.08f, 0.08f), void_grime * 0.55f);

        // Glass-bead micro-sparkle on sunlit thermoplastic lines
        float2 bead_cell = floor(road_xz * 240.0f);
        float bead = step(0.89f, road_hash(bead_cell)) * micro_fade * (1.0f - w.age * 0.45f);
        paint_col *= 1.0f + bead * 0.35f;

        albedo = lerp(asphalt_col, paint_col, paint_mask);
    }
    else
    {
        albedo = asphalt_col;
    }

    // 6. Roughness: sharp contrast between rough weathered stone, smooth wheel polish, glossy tar-snakes & oil
    float agg_rough = lerp(0.88f, 0.62f, stone_mask * 0.65f) * lerp(0.85f, 1.12f, det_rough);
    roughness = lerp(roughness, saturate(agg_rough), 0.72f * detail_fade + 0.18f);
    roughness = lerp(roughness, 0.60f, w.repair * 0.55f);
    roughness = lerp(roughness, 0.94f, w.divot * 0.70f);
    roughness = lerp(roughness, 0.91f, w.bleach * 0.45f);
    roughness = lerp(roughness, 0.64f, w.stain * 0.35f);
    if (paint_mask > 0.01f)
    {
        roughness = lerp(roughness, lerp(0.46f, 0.66f, w.age), paint_mask);
    }

    // Subtle quartz/feldspar aggregate micro-sparkle when catching raking direct sunlight
    if (micro_fade > 0.01f && paint_mask < 0.5f)
    {
        float2 spark_cell = floor(road_xz * 190.0f);
        float  spark_id   = road_hash(spark_cell);
        if (spark_id > 0.91f)
        {
            float3 h_dir = normalize(view_dir + light_dir);
            float  spark = pow(saturate(dot(det_normal_ws, h_dir)), 28.0f) * micro_fade * stone_mask * (1.0f - w.tar_snake);
            roughness = lerp(roughness, 0.14f, spark * 0.85f);
            albedo   += float3(0.06f, 0.058f, 0.054f) * spark;
        }
    }

    // 7. Apply damage on top of BOTH asphalt & paint (cracks, tar-snakes, oil, loose grit)
    // Oil stains
    albedo    = lerp(albedo, albedo * float3(0.32f, 0.30f, 0.28f), w.oil * 0.75f);
    roughness = lerp(roughness, 0.36f, w.oil * 0.65f);

    // Crack grime halo
    albedo    *= 1.0f - 0.30f * w.grime;
    roughness  = lerp(roughness, 0.95f, w.grime * 0.35f);

    // Glossy dark bitumen tar-snakes & repair overbands (iconic racing-game road sheen!)
    float3 bitumen = lerp(float3(0.022f, 0.022f, 0.024f), float3(0.042f, 0.040f, 0.038f), w.age * 0.6f);
    albedo    = lerp(albedo, bitumen, saturate(w.tar_snake * 0.92f));
    roughness = lerp(roughness, lerp(0.22f, 0.36f, w.age), saturate(w.tar_snake * 0.92f));

    // Deep open cracks hold dark shadow & trapped grit
    albedo    = lerp(albedo, albedo * 0.18f, w.crack);
    roughness = lerp(roughness, 0.98f, w.crack);

    // Fine mineral dust/grit flecks
    albedo    = lerp(albedo, float3(0.20f, 0.19f, 0.175f), saturate(w.grit) * 0.55f);
    albedo    = lerp(albedo, float3(0.03f, 0.03f, 0.032f), saturate(-w.grit) * 0.55f);
    roughness = lerp(roughness, 0.88f, abs(w.grit) * 0.4f);

    // 8. Normal composition: blend macro road normal + crisp 4K scanned aggregate normal + crack/divot slopes
    float3 combined_normal = normalize(normal + (det_normal_ws - geometric_normal) * (1.0f - paint_mask * 0.42f));
    // Tar-snakes & oil smooth over the underlying rough stone texture
    float smooth_fill = saturate(w.tar_snake * 0.82f + w.oil * 0.35f + w.repair * 0.15f);
    combined_normal = normalize(lerp(combined_normal, geometric_normal, smooth_fill));
    // Crack groove wall tilt
    float3 crack_slope = float3(w.slope.x, 0.0f, w.slope.y);
    crack_slope -= geometric_normal * dot(crack_slope, geometric_normal);
    normal = normalize(combined_normal - crack_slope);

    // 9. Occlusion & final height_map for puddles/rain
    float agg_ao = lerp(1.0f, det_ao, 0.80f * detail_fade);
    occlusion *= agg_ao;
    occlusion *= 1.0f - 0.68f * w.crack - 0.28f * w.divot - 0.14f * saturate(-w.grit);
    occlusion *= 1.0f - relief_shadow * 0.70f;
    occlusion  = saturate(occlusion);

    height_map = saturate(lerp(height_map, hit_height, 0.80f));
    roughness  = clamp(roughness, 0.18f, 0.98f);
}

#endif
