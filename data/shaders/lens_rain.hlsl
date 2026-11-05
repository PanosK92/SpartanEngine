/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// the water on the front element of the camera (CarLens.h), seen from behind the glass
// every drop is a spherical cap meeting the glass at the coating's contact angle, the caps and the veins the running drops
// left add up to one height field of water, so drops that touch flow into one surface
// a ray from the camera crosses the flat glass into the water and leaves through the curved surface, snell bends it
// towards the cap's centre and past it, so a drop holds the scene upside down and minified, sky at its bottom, road at its top,
// near the contact line the surface is too steep for the ray to leave, it is reflected back inside and the rim goes dark
// the camera is focused on the car metres away, everything on the glass is spread over the circle of confusion

//= INCLUDES =====================
#include "common.hlsl"
#include "common_water_drops.hlsl"
//================================

RWStructuredBuffer<LensDrop> lens_drops : register(u93);
RWStructuredBuffer<uint> lens_state     : register(u94);

static const uint  lens_group_size   = THREAD_GROUP_COUNT_X * THREAD_GROUP_COUNT_Y;
static const float lens_water_index  = 1.33f;
static const float lens_tan_half_fov = 0.5f;   // a chase camera's vertical field, how far a turn of a ray moves it over the frame in lens units
static const float lens_sin_contact  = 0.906f; // a hydrophobic coating, the caps meet the glass at about 65 degrees
static const float lens_cos_contact  = 0.423f;
static const float lens_wobble       = 0.1f;   // how far a contact line strays from a circle, pinned on specks it never closes round
static const float lens_trapped      = 0.45f;  // of the light around a drop that the rim reflects back inside carries to the camera
static const float lens_field        = 0.8f;   // lens units of the scene a cap's radius sees, its image of a light sits this much closer to its centre
static const float lens_film_visible = 2.0e-6f; // metres of vein where the film is a continuous sheet

groupshared uint tile_drops[LENS_DROPS_MAX];
groupshared uint tile_count;
groupshared float2 light_position[LENS_LIGHTS_X * LENS_LIGHTS_Y];
groupshared float3 light_energy[LENS_LIGHTS_X * LENS_LIGHTS_Y];

float lens_half_height() { return max(buffer_frame.lens_rain.z, 1e-6f); }

// cubic b-spline weights, smooth in value and slope so the cells of the film never show in the light it bends
float4 film_weights(float f)
{
    float g = 1.0f - f;
    return float4(g * g * g, 3.0f * f * f * f - 6.0f * f * f + 4.0f, 3.0f * g * g * g - 6.0f * g * g + 4.0f, f * f * f) / 6.0f;
}

// depth of the water film the running drops left, metres, p in lens units
float film_depth(float2 p)
{
    float aspect = max(buffer_frame.lens_rain.w, 1e-3f);
    float2 g     = float2((p.x / aspect * 0.5f + 0.5f) * LENS_TRAIL_WIDTH, (p.y * 0.5f + 0.5f) * LENS_TRAIL_HEIGHT) - 0.5f;
    int2 c       = int2(floor(g));
    float2 f     = g - float2(c);
    float4 wx    = film_weights(f.x);
    float4 wy    = film_weights(f.y);
    float depth  = 0.0f;
    for (int y = 0; y < 4; y++)
    {
        int cy = c.y + y - 1;
        if (cy < 0 || cy >= LENS_TRAIL_HEIGHT)
            continue;

        float row = 0.0f;
        for (int x = 0; x < 4; x++)
        {
            int cx = c.x + x - 1;
            if (cx < 0 || cx >= LENS_TRAIL_WIDTH)
                continue;

            row += float(lens_state[LENS_STATE_TRAIL + uint(cy) * LENS_TRAIL_WIDTH + uint(cx)]) * wx[x];
        }
        depth += row * wy[y];
    }
    return depth * 1.0e-9f;
}

// lens units, the frame's height spans -1 to 1, y up
float2 to_lens(float2 pixel, float2 resolution)
{
    float2 ndc = pixel / resolution * 2.0f - 1.0f;
    return float2(ndc.x * resolution.x / resolution.y, -ndc.y);
}

float2 to_uv(float2 lens_offset, float2 resolution)
{
    return float2(lens_offset.x * resolution.y / resolution.x, -lens_offset.y) * 0.5f;
}

// drop xy = centre, z = contact radius, both lens units, w = seed
// shape xy = the push along the glass over the pinning force, what deforms the cap, z = speed in lens units/s, w = age
void load_drop(uint i, out float4 drop, out float4 shape)
{
    LensDrop d = lens_drops[i];
    float hh   = lens_half_height();
    drop       = float4(d.position / hh, drop_radius(d.mass) / hh, d.seed);
    shape      = float4(d.lean, length(d.velocity) / hh, d.age);
}

// a cap leaning under the push is longer along it and a running drop draws its receding side out behind it, its leading edge stays round
float drop_stretch(float4 shape)
{
    return 1.0f + 0.25f * saturate(length(shape.xy) * 0.5f) + min(shape.z, 0.8f);
}

// how much the dome turns from the camera under a drop, 1 at its centre
float drop_squash(float2 centre)
{
    float tilt = saturate(length(centre) * lens_half_height() / LENS_DOME_RADIUS);
    return sqrt(max(1.0f - tilt * tilt, 0.05f));
}

// the offset from a drop's centre in the drop's own round frame
float2 drop_space(float2 offset, float2 centre, float squash, float4 shape)
{
    // off the dome's centre the glass faces away from the camera, a drop there is seen foreshortened along the radius
    float reach    = length(centre);
    float2 outward = reach > 1e-5f ? centre / reach : float2(1.0f, 0.0f);
    offset        += outward * dot(offset, outward) * (1.0f / squash - 1.0f);

    float lean    = length(shape.xy);
    float2 ahead  = lean > 1e-3f ? shape.xy / lean : float2(0.0f, -1.0f);
    float2 side   = float2(-ahead.y, ahead.x);
    float stretch = drop_stretch(shape);
    float along   = dot(offset, ahead);
    float across  = dot(offset, side);
    // the tail thins out as it trails away
    float tail    = saturate(-along / max(stretch, 1e-3f));
    along        /= along < 0.0f ? stretch : 1.0f + (stretch - 1.0f) * 0.1f;
    across       /= lerp(1.0f, 0.55f, tail * saturate(stretch - 1.0f));
    return float2(across, along);
}

// height of a cap over the glass, lens units, edge is how far inside its contact line q sits
float cap_height(float2 q, float radius, float4 wobble, out float edge)
{
    float d2   = dot(q, q);
    float d    = sqrt(d2);
    float2 dir = d > 1e-6f ? q / d : float2(1.0f, 0.0f);
    float c2   = dir.x * dir.x - dir.y * dir.y;
    float s2   = 2.0f * dir.x * dir.y;
    float c3   = dir.x * c2 - dir.y * s2;
    float s3   = dir.y * c2 + dir.x * s2;
    float contact = radius * (1.0f + lens_wobble * (dot(float2(s2, c2), wobble.xy) + dot(float2(s3, c3), wobble.zw)));
    edge          = contact - d;
    if (edge <= 0.0f)
        return 0.0f;

    float sphere = contact / lens_sin_contact;
    return sqrt(max(sphere * sphere - d2, 0.0f)) - sphere * lens_cos_contact;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    uint count = min(lens_state[LENS_STATE_COUNT], uint(LENS_DROPS_MAX));
    float blur = max(buffer_frame.lens_rain.y, 1e-4f);
    float hh   = lens_half_height();

    // the height field is read over the circle of confusion, its slope is the slope the blurred image of the water has
    float spacing = max(blur, 3.0f / resolution.y);

    // the drops that reach this tile
    if (group_index == 0)
    {
        tile_count = 0;
    }
    if (group_index < LENS_LIGHTS_X * LENS_LIGHTS_Y)
    {
        uint base                   = LENS_STATE_LIGHTS + group_index * LENS_LIGHT_STRIDE;
        light_position[group_index] = float2(asfloat(lens_state[base + 0]), asfloat(lens_state[base + 1]));
        light_energy[group_index]   = float3(asfloat(lens_state[base + 2]), asfloat(lens_state[base + 3]), asfloat(lens_state[base + 4]));
    }
    GroupMemoryBarrierWithGroupSync();

    float2 tile_pixel = float2(group_id.xy * uint2(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y));
    float2 corner_a   = to_lens(tile_pixel, resolution);
    float2 corner_b   = to_lens(tile_pixel + float2(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y), resolution);
    float2 tile_min   = min(corner_a, corner_b);
    float2 tile_max   = max(corner_a, corner_b);
    for (uint i = group_index; i < count; i += lens_group_size)
    {
        float4 drop;
        float4 shape;
        load_drop(i, drop, shape);
        float reach    = drop.z * drop_stretch(shape) * (1.0f + lens_wobble * 1.5f) + blur + spacing;
        float2 nearest = clamp(drop.xy, tile_min, tile_max);
        float2 gap     = nearest - drop.xy;
        if (dot(gap, gap) < reach * reach)
        {
            uint slot;
            InterlockedAdd(tile_count, 1u, slot);
            tile_drops[slot] = i;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (any(thread_id.xy >= uint2(resolution)))
        return;

    float4 scene = tex[thread_id.xy];
    float2 p     = to_lens(float2(thread_id.xy) + 0.5f, resolution);

    uint size_x;
    uint size_y;
    uint levels;
    tex.GetDimensions(0, size_x, size_y, levels);
    float level_max = float(levels) - 1.0f;

    // the water over this pixel and around it, centre, right, left, up, down
    float h[5]       = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float2 taps[5]   = { float2(0.0f, 0.0f), float2(spacing, 0.0f), float2(-spacing, 0.0f), float2(0.0f, spacing), float2(0.0f, -spacing) };
    float clear      = 1.0f; // how much of the circle of confusion sees past every drop
    float3 around    = 0.0f; // the light around the drops over this pixel, what their rims reflect back inside
    float around_w   = 0.0f;
    float3 sparkle   = 0.0f;
    for (uint k = 0; k < tile_count; k++)
    {
        float4 drop;
        float4 shape;
        load_drop(tile_drops[k], drop, shape);
        float2 offset = p - drop.xy;
        float reach   = drop.z * drop_stretch(shape) * (1.0f + lens_wobble * 1.5f) + blur + spacing;
        if (dot(offset, offset) > reach * reach)
            continue;

        float seed    = drop.w * 6.2831853f;
        float4 wobble = float4(0.6f * cos(seed * 3.0f), 0.6f * sin(seed * 3.0f), 0.3f * cos(seed * 7.0f), 0.3f * sin(seed * 7.0f));
        float squash  = drop_squash(drop.xy);
        float edge    = 0.0f;
        // drops that touch merge within a step, where two caps still overlap the taller one is the surface
        for (uint t = 0; t < 5; t++)
        {
            float edge_tap;
            h[t] = max(h[t], cap_height(drop_space(offset + taps[t], drop.xy, squash, shape), drop.z, wobble, edge_tap));
            edge = t == 0 ? edge_tap : edge;
        }

        // a drop smaller than the circle of confusion covers only its share of it, the fine mist hardly shows
        float cover = smoothstep(-blur, blur, edge) * saturate(drop.z * drop.z / (blur * blur));
        if (cover <= 0.0f)
            continue;
        clear *= 1.0f - cover;

        // a few drop widths of the scene around it light the drop up from inside, the red of a tail lamp next to it glows in its rim
        float2 drop_uv   = 0.5f + to_uv(drop.xy, resolution);
        float halo_level = clamp(log2(max(drop.z * resolution.y * 2.0f, 1.0f)), 0.0f, level_max);
        around          += tex.SampleLevel(samplers[sampler_bilinear_clamp], saturate(drop_uv), halo_level).rgb * cover;
        around_w        += cover;

        // the cap focuses every light in its field to a small spot, upside down on the far side of its centre
        float sigma = drop.z * 0.08f + blur;
        for (uint l = 0; l < LENS_LIGHTS_X * LENS_LIGHTS_Y; l++)
        {
            float3 energy = light_energy[l];
            if (all(energy <= 0.0f))
                continue;

            float2 field = light_position[l] - drop.xy;
            float seen   = 1.0f - smoothstep(lens_field * 0.7f, lens_field, length(field));
            float2 image = drop.xy - field * drop.z / lens_field;
            float2 away  = p - image;
            sparkle     += energy * (drop.z * drop.z / (3.14159265f * sigma * sigma)) * exp(-dot(away, away) / (sigma * sigma)) * seen * cover;
        }
    }

    // the veins are part of the same water, a film so thin it only bends the view a little across it, it never blurs it
    float film[5];
    film[0]          = film_depth(p) / hh;
    float film_cover = saturate(film[0] * hh / lens_film_visible);

    float wet = 1.0f - clear * (1.0f - film_cover);
    if (wet < 1e-3f)
    {
        tex_uav[thread_id.xy] = scene;
        return;
    }
    for (uint t = 1; t < 5; t++)
    {
        film[t] = film_depth(p + taps[t]) / hh;
    }

    // the surface of the water, its normal faces the scene
    float2 slope  = float2(h[1] - h[2], h[3] - h[4]) / (2.0f * spacing) + float2(film[1] - film[2], film[3] - film[4]) / (4.0f * spacing);
    float3 normal = normalize(float3(-slope, 1.0f));

    // the camera's ray crosses the flat glass into the water, then leaves through the curved surface, or is turned back inside
    float3 view  = normalize(float3(p * lens_tan_half_fov, 1.0f));
    float3 water = refract(view, float3(0.0f, 0.0f, -1.0f), 1.0f / lens_water_index);
    float3 leaving = refract(water, -normal, lens_water_index);
    bool escapes   = dot(leaving, leaving) > 0.5f;

    // schlick, from the side of the larger angle, all of it reflected past the critical angle
    float cos_out     = escapes ? saturate(dot(leaving, normal)) : 0.0f;
    float transmitted = escapes ? 1.0f - (0.02f + 0.98f * pow(1.0f - cos_out, 5.0f)) : 0.0f;

    // the cap shrinks the scene into itself, one pixel of it gathers this many of the scene's, the image it forms is small and upside down but sharp,
    // a little softer than its minification for the focus it shifts, the scene stays as sharp as it is through a flat film
    float curvature = abs(h[1] + h[2] + h[3] + h[4] - 4.0f * h[0]) / (spacing * spacing);
    float minified  = 1.0f + curvature * (lens_water_index - 1.0f) / (2.0f * lens_tan_half_fov);
    float level     = clamp(log2(minified) * 1.25f, 0.0f, level_max);

    // a few taps over a texel of that level, so a coarse mip reads as a soft blur and not as its texels
    float2 seen_at  = escapes ? leaving.xy / (max(leaving.z, 1e-3f) * lens_tan_half_fov) : p;
    float2 seen_uv  = 0.5f + to_uv(seen_at, resolution);
    float2 texel    = exp2(level) / resolution;
    float3 through  = 0.0f;
    for (uint s = 0; s < 4; s++)
    {
        float2 offset = float2(s & 1 ? 0.5f : -0.5f, s & 2 ? 0.5f : -0.5f) * texel * (level > 0.5f ? 1.0f : 0.0f);
        through      += tex.SampleLevel(samplers[sampler_bilinear_clamp], saturate(seen_uv + float2(offset.x - offset.y, offset.x + offset.y) * 0.7071f), level).rgb;
    }
    through *= 0.25f;

    float3 inside = around_w > 0.0f ? around / around_w : tex.SampleLevel(samplers[sampler_bilinear_clamp], saturate(0.5f + to_uv(p, resolution)), min(4.0f, level_max)).rgb;
    float3 water_color = through * transmitted + inside * lens_trapped * (1.0f - transmitted);
    water_color       += sparkle / wet;

    tex_uav[thread_id.xy] = float4(lerp(scene.rgb, water_color, wet), scene.a);
}
