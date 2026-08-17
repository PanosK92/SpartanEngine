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

// terrain surface evaluator
//
// the layer set lives in the bindless material table as a contiguous block, the surface material
// points at its base and every layer carries its own procedural rule. per pixel this file scores
// all layers against the baked heightfield analysis, keeps the highest weighted few, samples them
// with hex tiling or biplanar projection, and resolves them with a height blend.
//
// note this header is pulled in by every shader through common.hlsl, so nothing in here may call
// ddx or ddy, derivatives are always passed in by the caller

// snow level fallback for non terrain surfaces, terrain reads its own level off the material
static const float sea_level  = 0.0f;
static const float snow_level = 400.0f;

// hex tiling, mikkelsen jcgt 2022
// soft blend, a high exponent paints a diamond waffle across mid distance hills
static const float terrain_hex_exponent          = 4.0f;
static const float terrain_hex_falloff           = 0.5f;
static const float terrain_hex_gain              = 0.5f;
static const float terrain_hex_rotation_strength = 0.55f;
// the two tiling scales share no harmonics, so their repeats never line up, this takes over from the
// hex lattice where that stops and is the only thing breaking the repeat in the far field
static const float terrain_macro_scale_ratio = 8.7f;
static const float terrain_macro_fade_start  = 120.0f;
static const float terrain_macro_fade_end    = 600.0f;

// noise_perlin already scales its input by 0.1, so a wavelength of w meters needs 10 / w
//
// the perlin texture is 256 texels with blobs about eight texels across, so every octave below
// actually lands at a thirty second of the wavelength written against it, correcting that makes
// the per layer breakup low frequency enough to bias whole hillsides toward one layer, so the
// scale stays as it is until the breakup amplitude is reworked to match
static const float terrain_noise_rcp_scale = 10.0f;

static const uint terrain_layer_max_shader = 8;
static const uint terrain_layer_pick_max   = 4;

// rule domains, an edge at or past one of these means the rule does not bound that side
// slope is measured from horizontal so it can never leave zero to ninety degrees, the ceiling sits a
// hair under ninety so a rule written as ninety reads as open regardless of float rounding
static const float terrain_slope_domain_min  = 0.0f;
static const float terrain_slope_domain_max  = 1.5698f; // 89.94 degrees
static const float terrain_height_domain_min = -99999.0f;
static const float terrain_height_domain_max = 99999.0f;

// ramp widths, wide enough that a 25 m cell still holds a real mix instead of a hard isoline
static const float terrain_slope_feather_max  = 0.35f; // about 20 degrees
static const float terrain_height_feather_max = 6.0f;

// a height rule is an altitude contour, so a feather measured in metres of altitude is not what you
// see, what you see is its width along the ground and that shrinks as the ground steepens, six metres
// of altitude is about sixty metres across on a gentle slope and one metre across on a cliff, which is
// the knife edge, scaling the feather by the local gradient holds this width along the surface instead
static const float terrain_blend_width_world = 14.0f;
static const float terrain_gradient_max      = 8.0f; // about 83 degrees

// how far the projection switch is smeared either side of the biplanar threshold, a hard switch draws
// a razor line straight across every slope that happens to sit at the crossover
static const float terrain_biplanar_threshold = 0.55f;
static const float terrain_biplanar_feather   = 0.12f;

// threshold wobble, large enough to turn a slope contour into a coastline
static const float terrain_slope_jitter  = 0.22f; // about 13 degrees
static const float terrain_height_jitter = 4.0f;

// past this range the per layer detail work, hex tiling variants and the third and fourth picks, stops
// paying for itself
static const float terrain_detail_distance = 140.0f;
static const float terrain_detail_fade     = 140.0f;
// the interface between two layers is a different thing to the detail inside one, a layer patch is tens
// to hundreds of metres across so its boundary stays resolvable all the way out, collapsing to a single
// pick at the detail range is what turns the mid field into a hard edged patchwork
static const float terrain_blend_distance = 1800.0f;
static const float terrain_blend_fade     = 1400.0f;
// the hex lattice has to outlive the layer blend by a long way, the repeat is most obvious at exactly
// the mid distances where it is still resolvable, and on one layer three taps is cheap
static const float terrain_hex_distance = 1200.0f;
static const float terrain_hex_fade     = 1000.0f;

// debug views, must match TerrainDebugView in TerrainLayer.h
static const uint terrain_debug_off        = 0;
static const uint terrain_debug_weights    = 1;
static const uint terrain_debug_dominant   = 2;
static const uint terrain_debug_curvature  = 3;
static const uint terrain_debug_flow       = 4;
static const uint terrain_debug_occlusion  = 5;
static const uint terrain_debug_insolation = 6;
static const uint terrain_debug_deposition = 7;
static const uint terrain_debug_wear       = 8;
static const uint terrain_debug_talus      = 9;
static const uint terrain_debug_wetness    = 10;

//= analysis =====================================================================================

struct TerrainAnalysis
{
    float curvature;   // -1 convex ridge, +1 concave gully
    float flow;        // 0 dry divide, 1 channel
    float occlusion;   // 0 buried, 1 open sky
    float deposition;  // 0 bare, 1 thick sediment
    float wear;        // 0 untouched, 1 scoured bedrock
    float insolation;  // 0 permanently shaded, 1 baked
    float height_norm; // 0 lowest point, 1 highest
    float talus;       // 0 no scree, 1 scree fan
};

TerrainAnalysis terrain_analysis_neutral()
{
    TerrainAnalysis a;
    a.curvature   = 0.0f;
    a.flow        = 0.25f;
    a.occlusion   = 0.85f;
    a.deposition  = 0.3f;
    a.wear        = 0.3f;
    a.insolation  = 0.5f;
    a.height_norm = 0.5f;
    a.talus       = 0.2f;
    return a;
}

// the maps are stored in normalized terrain xz, world_mapping turns a world position into that
TerrainAnalysis terrain_sample_analysis(MaterialParameters surface, float3 position_world, float lod)
{
    if (!surface.terrain_has_maps())
    {
        return terrain_analysis_neutral();
    }

    float2 normalized = (position_world.xz - surface.terrain_world_mapping.xy) * surface.terrain_world_mapping.zw;

    // sample 0 sits at the world minimum and sample n-1 at the maximum, so the maps span n-1 intervals
    // while the texture spans n, rebasing onto the texel centres is what keeps this agreeing with the
    // cpu prop mask, which indexes the same maps as normalized * (n - 1)
    uint map_w;
    uint map_h;
    tex_terrain_map_a.GetDimensions(map_w, map_h);
    float2 map_size = float2(map_w, map_h);
    float2 uv       = (normalized * (map_size - 1.0f) + 0.5f) / map_size;

    float4 map_a = tex_terrain_map_a.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, lod);
    float4 map_b = tex_terrain_map_b.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, lod);

    TerrainAnalysis a;
    a.curvature   = map_a.r * 2.0f - 1.0f;
    a.flow        = map_a.g;
    a.occlusion   = map_a.b;
    a.deposition  = map_a.a;
    a.wear        = map_b.r;
    a.insolation  = map_b.g;
    a.height_norm = map_b.b;
    a.talus       = map_b.a;
    return a;
}

//= weights ======================================================================================

// low frequency fbm used to perturb every rule threshold, this is the single change that stops
// layer boundaries from reading as altitude contour lines
float terrain_jitter(float3 position_world, float seed)
{
    float2 p = position_world.xz * terrain_noise_rcp_scale;
    float n  = noise_perlin(p * (1.0f / 700.0f) + seed * 0.4f) * 1.1f; // 700 m
    n       += noise_perlin(p * (1.0f / 250.0f) + seed);               // 250 m
    n       += noise_perlin(p * (1.0f / 77.0f)  + seed * 1.7f) * 0.5f; //  77 m
    n       += noise_perlin(p * (1.0f / 24.0f)  + seed * 2.9f) * 0.25f;//  24 m
    return n / 2.85f;
}

// soft band, one at the centre and zero outside
//
// an edge sitting on the domain boundary is open and contributes nothing, without this a layer whose
// slope floor is zero scores half weight on perfectly flat ground, because the ramp is centred on the
// edge and half of it falls outside the domain, that is the one place the fallback layer should win
//
// the feather also comes from a closed span only, an open ended range has no meaningful width and
// feathering across it fades the layer out over its own open end
//
// a closed span feathers across its own width at most, past that the band peaks below one, which is
// harmless because the picks are normalized against each other, only their ratio decides the surface
float terrain_band(float value, float low, float high, float feather_max, float domain_min, float domain_max)
{
    bool open_low  = low  <= domain_min;
    bool open_high = high >= domain_max;

    float span    = (open_low || open_high) ? (feather_max * 2.0f) : (high - low);
    float feather = max(min(span, feather_max), 1e-4f);

    float rise = open_low  ? 1.0f : smoothstep(low  - feather, low  + feather, value);
    float fall = open_high ? 1.0f : 1.0f - smoothstep(high - feather, high + feather, value);
    return rise * fall;
}

float terrain_snow_weight(
    MaterialParameters surface,
    TerrainAnalysis    analysis,
    float3             position_world,
    float3             normal_world,
    float              jitter
)
{
    if (surface.terrain_snow_amount <= 0.0f)
    {
        return 0.0f;
    }

    // the snow line wanders, a straight one across a mountain range is an instant tell
    float snow_line = surface.terrain_snow_level + jitter * 70.0f;
    float altitude  = position_world.y - snow_line;
    float weight    = saturate(altitude / 130.0f);

    // steep faces shed, the exponent is what keeps cliffs bare while ledges hold
    weight *= pow(saturate(normal_world.y), 2.5f);

    // packs into concavities and crevices rather than sitting evenly
    weight *= lerp(0.55f, 1.15f, analysis.curvature * 0.5f + 0.5f);
    weight *= lerp(0.65f, 1.1f, 1.0f - analysis.occlusion);

    // windward faces get scoured down to the rock
    float3 wind        = buffer_frame.wind;
    float  wind_length = length(wind);
    if (wind_length > 0.001f)
    {
        weight *= lerp(1.0f, 0.4f, saturate(dot(normal_world, wind / wind_length)));
    }

    return weight * surface.terrain_snow_amount;
}

float terrain_layer_weight(
    MaterialParameters layer,
    MaterialParameters surface,
    TerrainAnalysis    analysis,
    float3             position_world,
    float3             normal_world,
    float              slope_radians,
    float              jitter
)
{
    if (layer.terrain_layer_snow())
    {
        return terrain_snow_weight(surface, analysis, position_world, normal_world, jitter);
    }

    // the height band is measured against sea level for the layers that care about the shore
    float height = position_world.y - (layer.terrain_layer_below_sea() ? surface.terrain_sea_level : 0.0f);

    float slope_jittered  = slope_radians + jitter * terrain_slope_jitter;
    float height_jittered = height        + jitter * terrain_height_jitter;

    // altitude gained per metre travelled horizontally, the tangent of the slope, this is the factor
    // between a feather written in altitude and the width it actually covers on the ground
    float normal_y = max(normal_world.y, 0.125f);
    float gradient = min(sqrt(saturate(1.0f - normal_y * normal_y)) / normal_y, terrain_gradient_max);

    float height_feather = max(
        terrain_blend_width_world * gradient,
        terrain_height_feather_max * 0.25f
    );

    float weight = terrain_band(
        slope_jittered,
        layer.terrain_slope_range.x,
        layer.terrain_slope_range.y,
        terrain_slope_feather_max,
        terrain_slope_domain_min,
        terrain_slope_domain_max
    );
    weight *= terrain_band(
        height_jittered,
        layer.terrain_height_range.x,
        layer.terrain_height_range.y,
        height_feather,
        terrain_height_domain_min,
        terrain_height_domain_max
    );

    if (weight <= 0.0f)
    {
        return 0.0f;
    }

    // every influence is a signed push around neutral, summed then applied multiplicatively so a
    // layer that scores zero on the bands can never be resurrected by a strong analysis channel
    float push = 0.0f;
    push += layer.terrain_curvature_influence  * analysis.curvature;
    push += layer.terrain_flow_influence       * (analysis.flow       * 2.0f - 1.0f);
    push += layer.terrain_occlusion_influence  * (1.0f - analysis.occlusion * 2.0f);
    push += layer.terrain_insolation_influence * (analysis.insolation * 2.0f - 1.0f);
    push += layer.terrain_wear_influence       * (analysis.wear       * 2.0f - 1.0f);
    push += layer.terrain_deposition_influence * (analysis.deposition * 2.0f - 1.0f);
    push += layer.terrain_talus_influence      * (analysis.talus      * 2.0f - 1.0f);

    // the influences bias a layer, they must not decide it, an unclamped sum of seven signed channels
    // fed into exp2 lets a layer with strong influences outscore the slope and height rules by an order
    // of magnitude, which is how gravel ends up on flat ground it has no business being on
    return weight * exp2(clamp(push, -1.0f, 1.0f) * 1.25f) * layer.terrain_weight_bias;
}

struct TerrainLayerPick
{
    uint  index[terrain_layer_pick_max];  // bindless material index of the layer
    float weight[terrain_layer_pick_max]; // normalized across the picks
    uint  count;
};

// score every layer, keep the highest weighted few and renormalize, the layers that lost their
// place hand their weight to the winners which is what makes a missing folder harmless
TerrainLayerPick terrain_pick_layers(
    MaterialParameters surface,
    TerrainAnalysis    analysis,
    float3             position_world,
    float3             normal_world,
    float              slope_radians
)
{
    TerrainLayerPick pick;
    pick.count = 0;

    [unroll]
    for (uint clear_index = 0; clear_index < terrain_layer_pick_max; clear_index++)
    {
        pick.index[clear_index]  = surface.terrain_layer_base;
        pick.weight[clear_index] = 0.0f;
    }

    uint layer_count = min(surface.terrain_layer_count, terrain_layer_max_shader);
    if (layer_count == 0)
    {
        return pick;
    }

    uint  keep   = clamp(surface.terrain_layer_quality(), 1u, terrain_layer_pick_max);
    float jitter = terrain_jitter(position_world, 0.0f);

    float scores[terrain_layer_max_shader];
    [loop]
    for (uint i = 0; i < layer_count; i++)
    {
        uint layer_index = surface.terrain_layer_base + i * surface.terrain_layer_stride;
        MaterialParameters layer = material_parameters[NonUniformResourceIndex(layer_index)];

        // zero-bias layers can never win, skip the slope/height/analysis work
        if (layer.terrain_weight_bias <= 0.0f && !layer.terrain_layer_snow())
        {
            scores[i] = 0.0f;
            continue;
        }

        scores[i] = terrain_layer_weight(
            layer,
            surface,
            analysis,
            position_world,
            normal_world,
            slope_radians,
            jitter
        );

        // per layer noise so the isoline grows fingers instead of tracing a slope contour
        if (scores[i] > 0.0f)
        {
            float breakup = terrain_jitter(position_world, 3.1f + (float)i * 2.3f);
            scores[i]    *= lerp(0.55f, 1.45f, saturate(breakup * 0.5f + 0.5f));
        }
    }

    // selection pass, keep is at most four so this is a handful of comparisons
    [loop]
    for (uint slot = 0; slot < keep; slot++)
    {
        float best_score = 0.0f;
        uint  best_layer = 0;
        bool  found      = false;

        [loop]
        for (uint candidate = 0; candidate < layer_count; candidate++)
        {
            if (scores[candidate] > best_score)
            {
                best_score = scores[candidate];
                best_layer = candidate;
                found      = true;
            }
        }

        if (!found)
        {
            break;
        }

        scores[best_layer]      = 0.0f; // consumed
        pick.index[pick.count]  = surface.terrain_layer_base + best_layer * surface.terrain_layer_stride;
        pick.weight[pick.count] = best_score;
        pick.count++;
    }

    // nothing scored, fall back to layer zero so the terrain is never untextured
    if (pick.count == 0)
    {
        pick.index[0]  = surface.terrain_layer_base;
        pick.weight[0] = 1.0f;
        pick.count     = 1;
        return pick;
    }

    // the selection is a rank cut, and a rank cut is a step, the layer that just lost its slot walks
    // off with the weight it held and the one that took it walks on with the same amount, which is
    // exactly the knife edge, subtracting the highest rejected score pins both sides of that swap to
    // zero so a layer fades in as it climbs the ranking instead of appearing at full strength
    float rejected = 0.0f;
    [loop]
    for (uint rest = 0; rest < layer_count; rest++)
    {
        rejected = max(rejected, scores[rest]); // the winners were zeroed as they were consumed
    }

    float total = 0.0f;
    [unroll]
    for (uint n = 0; n < terrain_layer_pick_max; n++)
    {
        pick.weight[n] = max(pick.weight[n] - rejected, 0.0f);
        total         += pick.weight[n];
    }

    // every kept layer tied the cut exactly, rare enough to just hand the pixel to the winner
    if (total <= 0.0f)
    {
        pick.weight[0] = 1.0f;
        total          = 1.0f;
    }

    float inverse_total = 1.0f / total;
    [unroll]
    for (uint m = 0; m < terrain_layer_pick_max; m++)
    {
        pick.weight[m] *= inverse_total;
    }

    return pick;
}

//= hex tiling ===================================================================================

// integer bit mix, a multi kilometre terrain pushes lattice indices into the thousands, where the
// usual frac(sin(x) * large) hash has no entropy left in fp32 and every site gets the same offset
uint terrain_hash_bits(int2 vertex)
{
    uint h = asuint(vertex.x) * 0x9E3779B1u ^ asuint(vertex.y) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

// per site uv offset, the two halves of one mix are independent enough to use as a pair
float2 terrain_hash2(int2 vertex)
{
    uint h = terrain_hash_bits(vertex);
    return float2(h & 0xFFFFu, (h >> 16) & 0xFFFFu) * (1.0f / 65536.0f);
}

// hex lattice barycentrics, the three nearest lattice sites and their coverage
void terrain_hex_lattice(float2 uv, out float3 weights, out int2 vertex1, out int2 vertex2, out int2 vertex3)
{
    uv *= 3.4641016f; // 2 * sqrt(3)

    // must stay the exact inverse of terrain_hex_center's matrix, a transposed pair still produces a
    // lattice but the reconstructed centre drifts with distance and shears the texture into streaks
    const float2x2 grid_to_skewed = float2x2(1.0f, -0.57735027f, 0.0f, 1.15470054f);
    float2 skewed                 = mul(grid_to_skewed, uv);

    int2 base   = int2(floor(skewed));
    float3 temp = float3(frac(skewed), 0.0f);
    temp.z      = 1.0f - temp.x - temp.y;

    if (temp.z > 0.0f)
    {
        weights = float3(temp.z, temp.y, temp.x);
        vertex1 = base;
        vertex2 = base + int2(0, 1);
        vertex3 = base + int2(1, 0);
    }
    else
    {
        weights = float3(-temp.z, 1.0f - temp.y, 1.0f - temp.x);
        vertex1 = base + int2(1, 1);
        vertex2 = base + int2(1, 0);
        vertex3 = base + int2(0, 1);
    }
}

float2 terrain_hex_center(int2 vertex)
{
    const float2x2 inverse_skew = float2x2(1.0f, 0.5f, 0.0f, 1.0f / 1.15470054f);
    return mul(inverse_skew, float2(vertex)) / 3.4641016f;
}

float2x2 terrain_hex_rotation(int2 vertex, float strength)
{
    // hashed so the angle stays uniform however far out the site sits, deriving it from
    // vertex.x * vertex.y and folding with fmod quantizes as soon as the product outgrows fp32
    uint  h     = terrain_hash_bits(vertex + int2(0x51, 0x2D));
    float angle = (float(h >> 8) * (1.0f / 16777216.0f) * PI2 - PI) * strength;

    float c = cos(angle);
    float s = sin(angle);
    return float2x2(c, -s, s, c);
}

// contrast restore on the blend weights, mikkelsen's gain3, without it the three way blend is a
// visible soft wash wherever the lattice sites meet
float3 terrain_gain3(float3 x, float r)
{
    float  k = log(1.0f - r) / log(0.5f);
    float3 s = 2.0f * step(0.5f, x);
    float3 m = 2.0f * (1.0f - s);
    float3 v = 0.5f * s + 0.25f * m * pow(max(0.0f, s + x * m), k);
    return v / max(v.x + v.y + v.z, 1e-6f);
}

struct TerrainHexSetup
{
    float3   weights;
    float2   uv[3];
    float2   duvdx[3];
    float2   duvdy[3];
};

// the lattice, rotations and offsets are shared by every map of a layer, so resolve them once and
// reuse them for albedo, normal and packed
TerrainHexSetup terrain_hex_setup(float2 uv, float2 duvdx, float2 duvdy)
{
    float3 weights;
    int2 vertex1, vertex2, vertex3;
    terrain_hex_lattice(uv, weights, vertex1, vertex2, vertex3);

    float2x2 rotation1 = terrain_hex_rotation(vertex1, terrain_hex_rotation_strength);
    float2x2 rotation2 = terrain_hex_rotation(vertex2, terrain_hex_rotation_strength);
    float2x2 rotation3 = terrain_hex_rotation(vertex3, terrain_hex_rotation_strength);

    float2 center1 = terrain_hex_center(vertex1);
    float2 center2 = terrain_hex_center(vertex2);
    float2 center3 = terrain_hex_center(vertex3);

    TerrainHexSetup setup;
    setup.weights = weights;

    setup.uv[0] = mul(uv - center1, rotation1) + center1 + terrain_hash2(vertex1);
    setup.uv[1] = mul(uv - center2, rotation2) + center2 + terrain_hash2(vertex2);
    setup.uv[2] = mul(uv - center3, rotation3) + center3 + terrain_hash2(vertex3);

    setup.duvdx[0] = mul(duvdx, rotation1);
    setup.duvdx[1] = mul(duvdx, rotation2);
    setup.duvdx[2] = mul(duvdx, rotation3);

    setup.duvdy[0] = mul(duvdy, rotation1);
    setup.duvdy[1] = mul(duvdy, rotation2);
    setup.duvdy[2] = mul(duvdy, rotation3);

    return setup;
}

float4 terrain_hex_sample(TerrainHexSetup setup, uint texture_index, bool restore_contrast)
{
    float4 tap0 = material_textures[NonUniformResourceIndex(texture_index)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), setup.uv[0], setup.duvdx[0], setup.duvdy[0]);
    float4 tap1 = material_textures[NonUniformResourceIndex(texture_index)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), setup.uv[1], setup.duvdx[1], setup.duvdy[1]);
    float4 tap2 = material_textures[NonUniformResourceIndex(texture_index)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), setup.uv[2], setup.duvdx[2], setup.duvdy[2]);

    // luminance weighting keeps the brighter tap dominant instead of averaging detail away
    const float3 luma = float3(0.299f, 0.587f, 0.114f);
    float3 density    = float3(dot(tap0.rgb, luma), dot(tap1.rgb, luma), dot(tap2.rgb, luma));
    density           = lerp(1.0f, density, terrain_hex_falloff);

    float3 w = density * pow(setup.weights, terrain_hex_exponent);
    w       /= max(w.x + w.y + w.z, 1e-6f);

    // normals must stay a linear blend, gain3 on them would pinch the transition into a crease
    if (restore_contrast)
    {
        w = terrain_gain3(w, terrain_hex_gain);
    }

    return w.x * tap0 + w.y * tap1 + w.z * tap2;
}

//= biplanar =====================================================================================

struct TerrainBiplanarSetup
{
    int3   axis_major;
    int3   axis_median;
    float2 weights;
    float2 uv[2];
    float2 duvdx[2];
    float2 duvdy[2];
};

// quilez biplanar, only the two dominant axes are fetched, which is what removes the vertical
// smear on cliff faces without paying for a third projection nobody can see
TerrainBiplanarSetup terrain_biplanar_setup(float3 position, float3 normal, float3 dpdx, float3 dpdy, float sharpness)
{
    float3 n = abs(normal);

    int3 major = (n.x > n.y && n.x > n.z) ? int3(0, 1, 2) :
                 (n.y > n.z)              ? int3(1, 2, 0) :
                                            int3(2, 0, 1);
    int3 minor = (n.x < n.y && n.x < n.z) ? int3(0, 1, 2) :
                 (n.y < n.z)              ? int3(1, 2, 0) :
                                            int3(2, 0, 1);
    int3 median = 3 - minor - major;

    TerrainBiplanarSetup setup;
    setup.axis_major  = major;
    setup.axis_median = median;

    setup.uv[0]  = float2(position[major.y],  position[major.z]);
    setup.uv[1]  = float2(position[median.y], position[median.z]);
    setup.duvdx[0] = float2(dpdx[major.y],  dpdx[major.z]);
    setup.duvdx[1] = float2(dpdx[median.y], dpdx[median.z]);
    setup.duvdy[0] = float2(dpdy[major.y],  dpdy[major.z]);
    setup.duvdy[1] = float2(dpdy[median.y], dpdy[median.z]);

    // local support, the remap is what keeps the two weights from bleeding across the whole sphere
    float2 w      = float2(n[major.x], n[median.x]);
    w             = saturate((w - 0.5773f) / (1.0f - 0.5773f));
    w             = pow(w, sharpness / 8.0f);
    setup.weights = w / max(w.x + w.y, 1e-6f);

    return setup;
}

float4 terrain_biplanar_sample(TerrainBiplanarSetup setup, uint texture_index)
{
    float4 tap0 = material_textures[NonUniformResourceIndex(texture_index)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), setup.uv[0], setup.duvdx[0], setup.duvdy[0]);
    float4 tap1 = material_textures[NonUniformResourceIndex(texture_index)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), setup.uv[1], setup.duvdx[1], setup.duvdy[1]);
    return setup.weights.x * tap0 + setup.weights.y * tap1;
}

//= surface gradients ============================================================================

// a tangent space normal expressed as a height gradient, mikkelsen's surface gradient framework
// gradients from different projections and different layers can be summed directly, which is what
// lets planar and biplanar layers share one resolve at the end
float2 terrain_normal_to_gradient(float3 tangent_normal)
{
    // reconstruct z for the two channel bc5 normal maps the material system produces
    float3 n = tangent_normal;
    n.xy     = n.xy * 2.0f - 1.0f;
    n.z      = fast_sqrt(max(0.0f, 1.0f - dot(n.xy, n.xy)));
    return -n.xy / max(n.z, 1e-3f);
}

// gradient for a planar world xz projection, u runs along world x and v along world z
float3 terrain_gradient_planar(float2 gradient)
{
    return float3(gradient.x, 0.0f, gradient.y);
}

// gradient for one of the biplanar projections, the sampled axes map straight onto world axes
float3 terrain_gradient_axis(float2 gradient, int3 axis)
{
    float3 world = 0.0f;
    world[axis.y] = gradient.x;
    world[axis.z] = gradient.y;
    return world;
}

// project the accumulated gradient onto the surface tangent plane and tilt the geometric normal
float3 terrain_resolve_normal(float3 geometric_normal, float3 gradient, float intensity)
{
    float3 surface_gradient = gradient - dot(gradient, geometric_normal) * geometric_normal;
    return normalize(geometric_normal - surface_gradient * intensity);
}

//= surface ======================================================================================

struct TerrainSurface
{
    float3 albedo;
    float3 normal;    // world space
    float  roughness;
    float  metalness;
    float  occlusion;
    float  height;    // resolved blend height, drives tessellation displacement
    float  wetness;
    float  pom_height_scale; // zero when the dominant layer does not want parallax
};

// one layer resolved into albedo, a world space gradient and orm
struct TerrainLayerSample
{
    float4 albedo;
    float3 gradient;
    float3 orm;    // occlusion, roughness, metalness
    float  height;
};

// one method's raw maps, albedo is still encoded, packed and gradient are ready to blend
struct TerrainMapFetch
{
    float4 albedo;
    float4 packed;
    float3 gradient;
};

TerrainMapFetch terrain_fetch_biplanar(
    uint               layer_index,
    MaterialParameters layer,
    float3             position_world,
    float3             dpdx,
    float3             dpdy,
    float3             normal_world
)
{
    TerrainMapFetch fetch;
    fetch.gradient = 0.0f;

    float scale                = layer.terrain_tiling_scale;
    TerrainBiplanarSetup setup = terrain_biplanar_setup(
        position_world * scale, normal_world, dpdx * scale, dpdy * scale, 8.0f
    );

    fetch.albedo = terrain_biplanar_sample(setup, layer_index + material_texture_index_albedo);
    fetch.packed = terrain_biplanar_sample(setup, layer_index + material_texture_index_packed);

    if (layer.has_texture_normal())
    {
        float4 normal0 = material_textures[NonUniformResourceIndex(layer_index + material_texture_index_normal)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), setup.uv[0], setup.duvdx[0], setup.duvdy[0]);
        float4 normal1 = material_textures[NonUniformResourceIndex(layer_index + material_texture_index_normal)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), setup.uv[1], setup.duvdx[1], setup.duvdy[1]);

        fetch.gradient  = terrain_gradient_axis(terrain_normal_to_gradient(normal0.xyz), setup.axis_major)  * setup.weights.x;
        fetch.gradient += terrain_gradient_axis(terrain_normal_to_gradient(normal1.xyz), setup.axis_median) * setup.weights.y;
        fetch.gradient *= layer.normal;
    }

    return fetch;
}

TerrainMapFetch terrain_fetch_hex(
    uint               layer_index,
    MaterialParameters layer,
    float2             uv,
    float2             duvdx,
    float2             duvdy
)
{
    TerrainMapFetch fetch;
    fetch.gradient = 0.0f;

    float2 layer_uv       = uv * layer.terrain_tiling_scale;
    TerrainHexSetup setup = terrain_hex_setup(layer_uv, duvdx * layer.terrain_tiling_scale, duvdy * layer.terrain_tiling_scale);

    fetch.albedo = terrain_hex_sample(setup, layer_index + material_texture_index_albedo, true);
    fetch.packed = terrain_hex_sample(setup, layer_index + material_texture_index_packed, false);

    if (layer.has_texture_normal())
    {
        float4 normal_sample = terrain_hex_sample(setup, layer_index + material_texture_index_normal, false);
        fetch.gradient       = terrain_gradient_planar(terrain_normal_to_gradient(normal_sample.xyz)) * layer.normal;
    }

    return fetch;
}

TerrainMapFetch terrain_fetch_planar(
    uint               layer_index,
    MaterialParameters layer,
    float2             uv,
    float2             duvdx,
    float2             duvdy
)
{
    TerrainMapFetch fetch;
    fetch.gradient = 0.0f;

    float2 layer_uv = uv * layer.terrain_tiling_scale;
    float2 layer_dx = duvdx * layer.terrain_tiling_scale;
    float2 layer_dy = duvdy * layer.terrain_tiling_scale;

    fetch.albedo = material_textures[NonUniformResourceIndex(layer_index + material_texture_index_albedo)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), layer_uv, layer_dx, layer_dy);
    fetch.packed = material_textures[NonUniformResourceIndex(layer_index + material_texture_index_packed)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), layer_uv, layer_dx, layer_dy);

    if (layer.has_texture_normal())
    {
        float4 normal_sample = material_textures[NonUniformResourceIndex(layer_index + material_texture_index_normal)].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), layer_uv, layer_dx, layer_dy);
        fetch.gradient       = terrain_gradient_planar(terrain_normal_to_gradient(normal_sample.xyz)) * layer.normal;
    }

    return fetch;
}

TerrainMapFetch terrain_lerp_fetch(TerrainMapFetch a, TerrainMapFetch b, float t)
{
    TerrainMapFetch fetch;
    fetch.albedo   = lerp(a.albedo, b.albedo, t);
    fetch.packed   = lerp(a.packed, b.packed, t);
    fetch.gradient = lerp(a.gradient, b.gradient, t);
    return fetch;
}

// the ground projection, planar at range and hex tiled up close, one entry point so the biplanar
// blend fades into the exact same thing the non biplanar path would have produced
TerrainMapFetch terrain_fetch_ground(
    uint               layer_index,
    MaterialParameters layer,
    float2             uv,
    float2             duvdx,
    float2             duvdy,
    float              hex_weight
)
{
    if (hex_weight >= 1.0f)
    {
        return terrain_fetch_hex(layer_index, layer, uv, duvdx, duvdy);
    }

    if (hex_weight <= 0.0f)
    {
        return terrain_fetch_planar(layer_index, layer, uv, duvdx, duvdy);
    }

    return terrain_lerp_fetch(
        terrain_fetch_planar(layer_index, layer, uv, duvdx, duvdy),
        terrain_fetch_hex(layer_index, layer, uv, duvdx, duvdy),
        hex_weight
    );
}

// 1 inside the range, 0 past it, linear across fade_width so the change is not pinched into a ring
float terrain_lod_weight(float distance_to_camera, float cutoff, float fade_width, float distance_offset)
{
    float start = max(cutoff - fade_width, 0.0f);
    float span  = max(cutoff - start, 1e-5f);
    return 1.0f - saturate((distance_to_camera + distance_offset - start) / span);
}

TerrainLayerSample terrain_sample_layer(
    uint   layer_index,
    float2 uv,
    float2 duvdx,
    float2 duvdy,
    float3 position_world,
    float3 dpdx,
    float3 dpdy,
    float3 normal_world,
    float  biplanar_weight,
    float  hex_weight
)
{
    MaterialParameters layer = material_parameters[NonUniformResourceIndex(layer_index)];

    TerrainLayerSample result;
    result.gradient = 0.0f;
    float4 packed   = 1.0f;

    // biplanar is for cliff faces, y still dominates until about 45 degrees so a looser
    // threshold projects a side axis onto rolling ground and shears the uv into streaks
    //
    // the threshold is feathered, a bare comparison swaps the whole projection between two adjacent
    // pixels and the two projections agree on nothing, so it draws a seam along the iso line of the
    // normal, which is a contour across every slope that grazes the crossover
    float projection_fade = 1.0f - smoothstep(
        terrain_biplanar_threshold - terrain_biplanar_feather,
        terrain_biplanar_threshold + terrain_biplanar_feather,
        abs(normal_world.y)
    );
    biplanar_weight    *= projection_fade;
    bool want_biplanar  = layer.terrain_layer_biplanar() && biplanar_weight > 0.0f;

    TerrainMapFetch fetch;
    if (want_biplanar)
    {
        fetch = terrain_fetch_biplanar(layer_index, layer, position_world, dpdx, dpdy, normal_world);
        if (biplanar_weight < 1.0f)
        {
            fetch = terrain_lerp_fetch(
                terrain_fetch_ground(layer_index, layer, uv, duvdx, duvdy, hex_weight),
                fetch,
                biplanar_weight
            );
        }
    }
    else
    {
        fetch = terrain_fetch_ground(layer_index, layer, uv, duvdx, duvdy, hex_weight);
    }

    result.albedo   = fetch.albedo;
    result.gradient = fetch.gradient;
    packed          = fetch.packed;

    // bc compressed albedo carries srgb bits the format does not declare, decode it here or every
    // layer reads washed out against the rest of the scene
    if (layer.is_albedo_srgb())
    {
        result.albedo.rgb = srgb_to_linear(result.albedo.rgb);
    }
    result.albedo.rgb *= layer.color.rgb;

    // orm the same way the standard path resolves it, a missing map falls back to the scalar
    result.orm.r = layer.has_texture_occlusion() ? packed.r : 1.0f;
    result.orm.g = layer.roughness * (layer.has_texture_roughness() ? packed.g : 1.0f);
    result.orm.b = layer.metalness * (layer.has_texture_metalness() ? packed.b : 1.0f);

    // without a height map the albedo luminance is a workable stand in, darker crevices sit lower
    result.height = layer.has_texture_height() ? packed.a : luminance(result.albedo.rgb);

    return result;
}

// three octaves of perlin multiplied together, multiplying is what produces patchiness, adding
// just produces a soft grey wash that reads as fog
float terrain_macro_variation(float3 position_world)
{
    float2 p = position_world.xz * terrain_noise_rcp_scale;
    float a  = noise_perlin(p * (1.0f / 15.0f))  * 0.5f + 0.5f; //  15 m
    float b  = noise_perlin(p * (1.0f / 60.0f))  * 0.5f + 0.5f; //  60 m
    float c  = noise_perlin(p * (1.0f / 250.0f)) * 0.5f + 0.5f; // 250 m
    // three halves multiplied average to an eighth, scale back to a mean of one and bound the
    // tails, the raw product spikes hard enough to blow out albedo where all three octaves peak
    return clamp(a * b * c * 8.0f, 0.4f, 1.6f);
}

// resolve an already selected layer set, split out from terrain_shade so the g buffer can run its
// parallax march against the dominant layer before the surface is sampled
TerrainSurface terrain_evaluate(
    MaterialParameters surface,
    TerrainLayerPick   pick,
    TerrainAnalysis    analysis,
    float3             position_world,
    float3             geometric_normal,
    float2             uv,
    float2             duvdx,
    float2             duvdy,
    float3             dpdx,
    float3             dpdy,
    float              distance_to_camera
)
{
    TerrainSurface output;
    output.albedo           = 0.5f;
    output.normal           = geometric_normal;
    output.roughness        = 1.0f;
    output.metalness        = 0.0f;
    output.occlusion        = 1.0f;
    output.height           = 0.5f;
    output.wetness          = 0.0f;
    output.pom_height_scale = 0.0f;

    if (pick.count == 0)
    {
        return output;
    }

    // wobble each cutoff so the fade is not a geometric circle around the camera
    float lod_noise     = terrain_jitter(position_world, 4.2f);
    float detail_weight = terrain_lod_weight(distance_to_camera, terrain_detail_distance, terrain_detail_fade, lod_noise * terrain_detail_fade * 0.75f);
    float hex_weight    = terrain_lod_weight(distance_to_camera, terrain_hex_distance,    terrain_hex_fade,    lod_noise * terrain_hex_fade    * 0.75f);

    // two layers stay in play at every distance, dropping to one is the hard split
    uint count = pick.count;
    if (detail_weight <= 0.0f)
    {
        count = min(pick.count, 2u);
    }

    // the soft selection leaves a tail pick at exactly zero wherever it lost the cut cleanly, and the
    // picks come out sorted, so trailing zeros are dead weight, nine texture fetches for no
    // contribution, dropping them is what pays for keeping a third layer in play at all
    [loop]
    while (count > 1 && pick.weight[count - 1] <= 0.0f)
    {
        count--;
    }

    // sample the picks
    TerrainLayerSample samples[terrain_layer_pick_max];
    float porosities[terrain_layer_pick_max];
    float macros[terrain_layer_pick_max];

    [loop]
    for (uint s = 0; s < count; s++)
    {
        samples[s] = terrain_sample_layer(
            pick.index[s], uv, duvdx, duvdy, position_world, dpdx, dpdy, geometric_normal, detail_weight, hex_weight
        );

        MaterialParameters layer = material_parameters[NonUniformResourceIndex(pick.index[s])];
        porosities[s]            = layer.terrain_porosity;
        macros[s]                = layer.terrain_macro_strength;
    }

    // mix by pick weight, height only nips the interface up close
    // a hard height gate painted the sawtooth and flickered with taa
    //
    // both modifiers are multiplicative on purpose, anything added here would give a layer that enters
    // the pick set at zero weight a floor to appear at, which puts the rank cut step back
    float deposition_bias = analysis.deposition * 0.35f;
    float resolved[terrain_layer_pick_max];
    [loop]
    for (uint b = 0; b < count; b++)
    {
        MaterialParameters layer = material_parameters[NonUniformResourceIndex(pick.index[b])];
        float bias               = layer.terrain_deposition_influence > 0.0f ? deposition_bias : 0.0f;
        float contrast           = layer.terrain_blend_contrast * detail_weight;
        float height_mod         = lerp(1.0f, samples[b].height + 0.5f, contrast);
        resolved[b]              = pick.weight[b] * (1.0f + bias * 0.5f) * height_mod;
    }

    if (count > 2)
    {
        resolved[2] *= detail_weight;
    }
    if (count > 3)
    {
        resolved[3] *= detail_weight;
    }

    float weight_sum = 0.0f;
    [loop]
    for (uint n = 0; n < count; n++)
    {
        weight_sum += resolved[n];
    }

    float inverse_sum = 1.0f / max(weight_sum, 1e-6f);

    float4 albedo   = 0.0f;
    float3 gradient = 0.0f;
    float3 orm      = 0.0f;
    float  height   = 0.0f;
    float  porosity = 0.0f;
    float  macro    = 0.0f;

    [loop]
    for (uint r = 0; r < count; r++)
    {
        float w   = resolved[r] * inverse_sum;
        albedo   += samples[r].albedo * w;
        gradient += samples[r].gradient * w;
        orm      += samples[r].orm * w;
        height   += samples[r].height * w;
        porosity += porosities[r] * w;
        macro    += macros[r] * w;
    }

    // dual scale tiling on the dominant layer's albedo, a second copy at a non integer scale
    // ratio breaks the mid distance repeat that no amount of per tile randomization can hide
    float macro_fade = saturate(
        (distance_to_camera - terrain_macro_fade_start) /
        max(terrain_macro_fade_end - terrain_macro_fade_start, 1.0f)
    );
    if (macro_fade > 0.0f)
    {
        MaterialParameters dominant = material_parameters[NonUniformResourceIndex(pick.index[0])];
        float scale                 = dominant.terrain_tiling_scale / terrain_macro_scale_ratio;
        float4 far_albedo           = material_textures[NonUniformResourceIndex(pick.index[0] + material_texture_index_albedo)]
            .SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv * scale, duvdx * scale, duvdy * scale);

        if (dominant.is_albedo_srgb())
        {
            far_albedo.rgb = srgb_to_linear(far_albedo.rgb);
        }

        // keep luminance so the layer colour does not drift
        float3 mixed   = albedo.rgb * far_albedo.rgb * 2.0f;
        float  lum_src = max(luminance(albedo.rgb), 1e-4f);
        float  lum_dst = max(luminance(mixed), 1e-4f);
        mixed         *= lum_src / lum_dst;
        albedo.rgb     = lerp(albedo.rgb, mixed, macro_fade * 0.55f);
    }

    // large scale colour and roughness breakup
    float variation = lerp(1.0f, terrain_macro_variation(position_world), saturate(macro));
    albedo.rgb     *= lerp(1.0f, variation, 0.45f);
    orm.g          *= lerp(1.0f, 2.0f - variation, 0.25f);

    // wetness, lagarde 2013, water fills the pores which darkens the diffuse and smooths the
    // specular, puddles want concave, high flow and flat all at once
    // flow is the channel most prone to plateauing, and at one it darkens albedo to a fifth and
    // drops roughness to 0.06, so a saturated basin reads as a slab of dark glass rather than damp
    // ground, the artist control is left free to go all the way up
    float wet  = saturate(analysis.flow * 1.4f - 0.35f) * 0.7f;
    wet        = max(wet, surface.terrain_wetness);
    float pool = saturate(analysis.curvature) * saturate(analysis.flow * 1.6f - 0.4f) * pow(saturate(geometric_normal.y), 8.0f);
    wet        = saturate(max(wet, pool));

    albedo.rgb *= lerp(1.0f, lerp(1.0f, 0.2f, saturate(porosity)), wet);
    orm.g       = lerp(orm.g, 0.06f, wet * 0.9f);

    // wet ground is a plane, flatten the detail gradient in proportion
    gradient *= lerp(1.0f, 0.25f, pool);

    MaterialParameters dominant_layer = material_parameters[NonUniformResourceIndex(pick.index[0])];

    output.albedo           = albedo.rgb;
    output.normal           = terrain_resolve_normal(geometric_normal, gradient, 1.0f);
    output.occlusion        = orm.r;
    output.roughness        = saturate(orm.g);
    output.metalness        = saturate(orm.b);
    output.height           = height;
    output.wetness          = wet;
    output.pom_height_scale = dominant_layer.terrain_layer_pom() ? dominant_layer.height : 0.0f;

    // debug views, without these the rule set is untunable, you cannot see why a layer won
    uint debug = surface.terrain_debug_view();
    if (debug != terrain_debug_off)
    {
        float3 view = 0.0f;

        if (debug == terrain_debug_weights)
        {
            // the top three picks as rgb, a single flat colour means one layer owns the pixel
            view = float3(
                pick.count > 0 ? resolved[0] * inverse_sum : 0.0f,
                pick.count > 1 ? resolved[1] * inverse_sum : 0.0f,
                pick.count > 2 ? resolved[2] * inverse_sum : 0.0f
            );
        }
        else if (debug == terrain_debug_dominant)
        {
            uint slot = (pick.index[0] - surface.terrain_layer_base) / max(surface.terrain_layer_stride, 1u);
            view      = float3(hash(float2(slot, 0.5f)), hash(float2(slot, 1.5f)), hash(float2(slot, 2.5f)));
        }
        else if (debug == terrain_debug_curvature)  { view = float3(saturate(-analysis.curvature), saturate(analysis.curvature), 0.0f); }
        else if (debug == terrain_debug_flow)       { view = analysis.flow; }
        else if (debug == terrain_debug_occlusion)  { view = analysis.occlusion; }
        else if (debug == terrain_debug_insolation) { view = analysis.insolation; }
        else if (debug == terrain_debug_deposition) { view = analysis.deposition; }
        else if (debug == terrain_debug_wear)       { view = analysis.wear; }
        else if (debug == terrain_debug_talus)      { view = analysis.talus; }
        else if (debug == terrain_debug_wetness)    { view = float3(0.0f, 0.0f, wet); }

        output.albedo    = view;
        output.normal    = geometric_normal;
        output.roughness = 1.0f;
        output.metalness = 0.0f;
        output.occlusion = 1.0f;
    }

    return output;
}

TerrainSurface terrain_shade(
    MaterialParameters surface,
    float3             position_world,
    float3             geometric_normal,
    float2             uv,
    float2             duvdx,
    float2             duvdy,
    float3             dpdx,
    float3             dpdy,
    float              distance_to_camera,
    float              analysis_lod
)
{
    TerrainAnalysis analysis = terrain_sample_analysis(surface, position_world, analysis_lod);
    float slope_radians      = acos(saturate(dot(geometric_normal, float3(0.0f, 1.0f, 0.0f))));
    TerrainLayerPick pick    = terrain_pick_layers(surface, analysis, position_world, geometric_normal, slope_radians);

    return terrain_evaluate(
        surface, pick, analysis, position_world, geometric_normal, uv, duvdx, duvdy, dpdx, dpdy, distance_to_camera
    );
}

// ray tracing variant, the same weights so gi and reflections agree with the raster image, but a
// single layer, no hex tiling and an explicit mip from the ray cone
TerrainSurface terrain_shade_lod(
    MaterialParameters surface,
    float3             position_world,
    float3             geometric_normal,
    float2             uv,
    float              lod
)
{
    TerrainSurface output;
    output.albedo           = 0.5f;
    output.normal           = geometric_normal;
    output.roughness        = 1.0f;
    output.metalness        = 0.0f;
    output.occlusion        = 1.0f;
    output.height           = 0.5f;
    output.wetness          = 0.0f;
    output.pom_height_scale = 0.0f;

    if (surface.terrain_layer_count == 0)
    {
        return output;
    }

    TerrainAnalysis analysis = terrain_sample_analysis(surface, position_world, min(lod, 4.0f));
    float slope_radians      = acos(saturate(dot(geometric_normal, float3(0.0f, 1.0f, 0.0f))));
    TerrainLayerPick pick    = terrain_pick_layers(surface, analysis, position_world, geometric_normal, slope_radians);

    MaterialParameters layer = material_parameters[NonUniformResourceIndex(pick.index[0])];
    float2 layer_uv          = uv * layer.terrain_tiling_scale;

    float4 albedo = material_textures[NonUniformResourceIndex(pick.index[0] + material_texture_index_albedo)].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), layer_uv, lod);
    float4 packed = material_textures[NonUniformResourceIndex(pick.index[0] + material_texture_index_packed)].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), layer_uv, lod);

    if (layer.is_albedo_srgb())
    {
        albedo.rgb = srgb_to_linear(albedo.rgb);
    }
    albedo.rgb *= layer.color.rgb;

    float variation = lerp(1.0f, terrain_macro_variation(position_world), saturate(layer.terrain_macro_strength));
    albedo.rgb     *= lerp(1.0f, variation, 0.45f);

    float wet   = max(saturate(analysis.flow * 1.4f - 0.35f) * 0.7f, surface.terrain_wetness);
    albedo.rgb *= lerp(1.0f, lerp(1.0f, 0.2f, saturate(layer.terrain_porosity)), wet);

    output.albedo    = albedo.rgb;
    output.occlusion = layer.has_texture_occlusion() ? packed.r : 1.0f;
    output.roughness = saturate(lerp(layer.roughness * (layer.has_texture_roughness() ? packed.g : 1.0f), 0.06f, wet * 0.9f));
    output.metalness = saturate(layer.metalness * (layer.has_texture_metalness() ? packed.b : 1.0f));
    output.height    = packed.a;
    output.wetness   = wet;

    return output;
}

//= tessellation =================================================================================

// meters of displacement at a layer height of one, the tessellated grid is far coarser than the
// texel grid so anything larger than this pops between lod rings
static const float terrain_displacement_scale = 0.5f;

// vertex displacement for tessellated terrain, driven by the dominant layer's own height map so
// the silhouette agrees with what the pixel shader ends up drawing on it
float terrain_displacement(MaterialParameters surface, float3 position_world, float3 normal_world, float2 uv)
{
    if (surface.terrain_layer_count == 0)
    {
        return 0.0f;
    }

    TerrainAnalysis analysis = terrain_sample_analysis(surface, position_world, 0.0f);
    float slope_radians      = acos(saturate(dot(normal_world, float3(0.0f, 1.0f, 0.0f))));
    TerrainLayerPick pick    = terrain_pick_layers(surface, analysis, position_world, normal_world, slope_radians);

    if (pick.count == 0)
    {
        return 0.0f;
    }

    // blend the top picks so a flipping winner cannot pop the silhouette
    float displacement = 0.0f;
    uint disp_count    = min(pick.count, 2u);
    [loop]
    for (uint d = 0; d < disp_count; d++)
    {
        MaterialParameters layer = material_parameters[NonUniformResourceIndex(pick.index[d])];
        if (!layer.has_texture_height())
        {
            continue;
        }

        float height = material_textures[NonUniformResourceIndex(pick.index[d] + material_texture_index_packed)]
            .SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), uv * layer.terrain_tiling_scale, 0.0f).a;
        displacement += (height - 0.5f) * layer.height * pick.weight[d];
    }

    return displacement * terrain_displacement_scale;
}

//= non terrain snow =============================================================================

// flat white overlay kept for everything that is not terrain, rocks, props and vegetation
static float get_snow_blend_factor(float3 position_world, float3 normal_world)
{
    const float snow_blend_speed = 0.06f; // transition sharpness
    const float noise_scale      = 0.2f;  // noise frequency for terrain/vegetation
    const float noise_strength   = 50.0f; // height variation for snow level
    const float slope_threshold  = 0.5f;  // ~45 degrees, snow diminishes
    const float slope_factor     = 0.3f;  // snow reduction on steep slopes
    const float wind_influence   = 0.7f;  // wind effect strength

    // calculate height-based snow distance
    float distance_to_snow = position_world.y - snow_level;

    // add perlin noise for organic snow level variation
    float2 noise_coords  = position_world.xz * noise_scale;
    float noise          = noise_perlin(noise_coords); // [0, 1]
    noise                = noise * 2.0f - 1.0f; // remap to [-1, 1]
    distance_to_snow    += noise * noise_strength; // perturb snow level

    // base snow blend factor
    float snow_blend_factor = saturate(1.0f - max(0.0f, -distance_to_snow) * snow_blend_speed);

    // modulate based on surface slope
    float slope_dot           = dot(normal_world, float3(0.0f, 1.0f, 0.0f)); // compare to world up
    float slope_factor_final  = lerp(slope_factor, 1.0f, smoothstep(slope_threshold, 1.0f, slope_dot));
    snow_blend_factor        *= slope_factor_final;

    // modulate based on wind direction
    float3 wind         = buffer_frame.wind;
    float wind_strength = length(wind);
    if (wind_strength > 0.0f)
    {
        float3 wind_dir      = wind / wind_strength; // normalize direction
        float wind_exposure  = dot(normal_world, wind_dir); // surface exposure to wind
        snow_blend_factor   *= lerp(1.0f - wind_influence, 1.0f, smoothstep(-1.0f, 1.0f, wind_exposure));
    }

    return snow_blend_factor;
}
