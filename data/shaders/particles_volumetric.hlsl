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

#include "common.hlsl"
#include "fog.hlsl"
#include "shadow_mapping.hlsl"
#include "light_cluster.hlsl"

static const uint particle_render_billboard   = 0u;
static const uint particle_render_volumetric  = 1u;
static const uint volume_width                = 160u;
static const uint volume_height               = 90u;
static const uint volume_depth                = 128u;
static const uint volume_voxel_count          = volume_width * volume_height * volume_depth;
// depth is split exponentially the way the fog volume is, a linear split put the slices a metre and a
// half apart which is wider than an entire plume, so every particle in it landed in one slice and the
// march integrated a constant along the ray, which is what made a plume read as a flat screen space
// blob no matter how much structure the density field carried, exponential spacing spends the slices
// where the smoke actually is and lands them a fifth of a metre apart at the range a chase camera
// sits from the wheels
static const float volume_min_distance        = 0.5f;
static const float volume_max_distance        = 64.0f;
static const float volume_density_scale       = 2048.0f;
static const float volume_color_scale         = 2048.0f;
// a silhouette only reads when the dense parts go properly opaque while the eroded parts go properly
// clear, at a third the densest core reached about sixty percent opacity and the whole plume sat in a
// narrow band of grey, which looks like haze however much structure is in the density field
static const float volume_extinction_scale    = 1.40f;
static const float volume_shadow_min_light    = 0.12f;
// how much of the skylight the medium collects, raise it and smoke goes whiter and flatter, drop it
// and the warm beam takes over again
static const float volume_sky_fill            = 0.18f;
// marching toward the light is what puts a lit face and a shaded core on the plume, and that gradient
// is the main thing the eye reads as volume, a metre and a half covers one billow
static const uint  volume_shadow_taps         = 4u;
static const float volume_shadow_length       = 1.60f;
// a multiplier on the view ray extinction rather than an absolute density, the old three point two was
// there to make a field clamped at one cast a visible shadow at all, and with the clamp gone it would
// darken the medium three times harder than the same medium attenuates the camera, which is not a
// property any medium has
static const float volume_shadow_density      = 1.0f;
// the grid is 160x90 across the screen, a twelve pixel block per voxel at 1080p and trilinear
// filtering smears it wider, so nothing finer can ever live in it and a plume resolves as a smooth
// ball however many particles feed it, the grid carries the coarse envelope only and these drive the
// structure carved in per ray sample instead, in world space, at full pixel resolution
// cell size is the reciprocal of this, so the octaves land near thirty, thirteen and six centimetres,
// which is the scale of the wisps in a real burnout, at one and a half the first octave was two thirds of
// a metre and those cells were the round lumps, the march has to step finer than this or the detail just
// integrates back into flat haze
static const float volume_detail_frequency    = 3.2f;
static const float volume_detail_amount       = 0.55f;
static const float volume_detail_contrast     = 0.65f;
static const float volume_detail_drift        = 0.22f;
static const float3 volume_detail_drift_axis  = float3(0.0f, -1.0f, 0.0f);
// how far a parcel's noise phase travels over a full lifetime, a little over one cell of the base octave,
// which is enough that a young parcel and an old one never read the same pattern, and the axis is a
// diagonal so it never aligns with the drift and beats against it
static const float3 volume_detail_age_axis    = float3(0.62f, 0.31f, -0.72f);
static const float  volume_detail_age_span    = 1.30f;
// the density the erosion is scaled against, the field is no longer capped at one so the threshold it
// subtracts needs a reference to be a fraction of, this is roughly the density of the thin skirt around
// a plume, everything below it can be eroded away completely and the core keeps its full range
static const float volume_detail_reference    = 1.0f;
// how much of the reprojected grid survives into the next frame, high enough to average the five or so
// successive splat subsets it takes to cover the live set, low enough that a plume does not tow a ghost,
// this is the knob for that trade, and a voxel the camera swept across keeps far less because its history
// describes a different part of the world
static const float volume_history_keep        = 0.82f;
static const float volume_history_keep_moving = 0.40f;
// mie scattering off smoke is strongly forward, which is what puts the bright halo on a plume with the
// sun behind it, near isotropic leaves it an evenly grey lump
static const float volume_phase_directional   = 0.60f;
static const float volume_phase_local         = 0.45f;
// how the two lobes are split, the forward one carries most of it, and the back lobe is a little tighter
// than the forward one is wide
static const float volume_phase_forward_weight = 0.72f;
static const float volume_phase_back_ratio     = 0.55f;
// later light bounces, a medium lit by single scattering alone reads dark because most of the light
// that makes real smoke bright has bounced several times before it leaves
static const uint  volume_scatter_octaves     = 3u;
static const float volume_scatter_falloff     = 0.40f;
// held back now that the light march supplies a real shaded core, too much of this fills the shadow
// back in and flattens the plume out again
static const float volume_scatter_gain        = 0.35f;

uint volume_index(uint3 voxel)
{
    return voxel.x + voxel.y * volume_width + voxel.z * volume_width * volume_height;
}

// slice_u runs zero to one across the grid, voxel z holds the range at (z + 0.5) / volume_depth, which
// is also its texture coordinate, so nothing here needs a half voxel correction
float volume_slice_to_distance(float slice_u)
{
    float k = volume_max_distance / volume_min_distance;
    return (pow(k, saturate(slice_u)) - 1.0f) / (k - 1.0f) * volume_max_distance;
}

float volume_distance_to_slice(float dist)
{
    float k = volume_max_distance / volume_min_distance;
    return saturate(log(max(dist, 0.0f) / volume_max_distance * (k - 1.0f) + 1.0f) / log(k));
}

// metres spanned by one slice at this range, the exponential split makes it a function of distance and
// both the splat and the march need it, the splat to turn a particle radius into slice space and the
// march to pick a step that is finer than the grid it is reading
float volume_slice_thickness(float dist)
{
    float k = volume_max_distance / volume_min_distance;
    return log(k) * (dist + volume_max_distance / (k - 1.0f)) / (float)volume_depth;
}

float henyey_greenstein(float cos_theta, float g)
{
    g = clamp(g, -0.9f, 0.9f);
    float g2 = g * g;
    float d  = max(1.0f + g2 - 2.0f * g * cos_theta, 0.0001f);
    return (1.0f - g2) / (4.0f * PI * d * sqrt(d));
}

// two lobes, not one
//
// a single forward lobe is only half of what mie scattering off a smoke droplet does. the forward peak is
// the bigger one and it is what lights a plume up when the sun is behind it, but there is a second peak
// straight back toward the light, and that is what puts the bright silver edge on a plume lit from the
// camera side and gives it the shape that tells you how big it is. with one lobe a plume viewed away from
// the sun has nothing at all and reads as a flat grey cutout
float volume_phase(float cos_theta, float g)
{
    return lerp(henyey_greenstein(cos_theta, -g * volume_phase_back_ratio),
                henyey_greenstein(cos_theta, g),
                volume_phase_forward_weight);
}

float volume_noise_hash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float volume_value_noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);

    float2 a = float2(0.0f, 1.0f);
    return lerp(lerp(lerp(volume_noise_hash(i + a.xxx), volume_noise_hash(i + a.yxx), f.x),
                     lerp(volume_noise_hash(i + a.xyx), volume_noise_hash(i + a.yyx), f.x), f.y),
                lerp(lerp(volume_noise_hash(i + a.xxy), volume_noise_hash(i + a.yxy), f.x),
                     lerp(volume_noise_hash(i + a.xyy), volume_noise_hash(i + a.yyy), f.x), f.y), f.z);
}

// where in the noise field a parcel of this age, at this point, reads its structure from
//
// the field used to be a function of world position and the clock alone, so it slid past the smoke like
// a projected texture and the eye read the slide rather than the smoke, which is the single thing that
// most makes a plume look like drifting fog. offsetting the lookup by the parcel's own age fixes the
// pattern in the parcel's frame instead, so a puff keeps its structure as it travels and two puffs of
// different ages never share one, and because a parcel's age advances as it lives, the structure inside
// it evolves on its own without the field having to move at all
//
// the clock term is kept but small, in a steady plume the age field stops changing in space, so with the
// age term alone a burnout that had reached equilibrium would sit there frozen like marble
float3 volume_detail_phase(float age, float time)
{
    return volume_detail_age_axis * age * volume_detail_age_span +
           volume_detail_drift_axis * time * volume_detail_drift;
}

// each octave scales the same phase offset with its own frequency, so the whole field shifts with the
// parcel coherently rather than the octaves sliding apart from one another
float volume_detail_noise(float3 position, float3 phase)
{
    float sum  = 0.0f;
    float amp  = 0.5f;
    float freq = 1.0f;

    [unroll]
    for (uint i = 0u; i < 3u; i++)
    {
        sum  += amp * volume_value_noise(position * freq + phase * freq);
        amp  *= 0.5f;
        freq *= 2.37f;
    }

    return sum * (1.0f / 0.875f);
}

// erosion subtracts from the envelope and rescales what survives, so a saturated core comes back out at
// full strength while the thin skirt around it drops to nothing, that cut edge is what reads as smoke,
// modulating the density instead would only dim the ball and leave it a ball
float volume_carve_density(float density, float3 position, float time, float age)
{
    float n = volume_detail_noise(position * volume_detail_frequency, volume_detail_phase(age, time));
    // subtractive in absolute density now that the field is not capped at one, the old form divided by
    // one minus the threshold and saturated, which put back the exact clip the resolve stopped applying,
    // so every gain in dynamic range died here
    float e = (1.0f - n) * volume_detail_amount * volume_detail_reference;
    float carved = max(density - e, 0.0f);

    // erosion alone only ever shows around the fringe because the core stands far above the threshold,
    // so the field needs a multiplicative term as well to break up the middle
    return carved * lerp(1.0f - volume_detail_contrast, 1.0f, n);
}

// the shadow ray only needs to know roughly how much medium stands in the way, so it runs the shaping
// octave alone, the finer ones would not survive a four tap integration anyway
float volume_carve_density_coarse(float density, float3 position, float time, float age)
{
    // the phase is in cells of the base octave, same convention as the full carve
    float3 p = position * volume_detail_frequency + volume_detail_phase(age, time);

    float n = volume_value_noise(p);
    float e = (1.0f - n) * volume_detail_amount * volume_detail_reference;

    return max(density - e, 0.0f);
}

float3 volume_world_position(uint3 voxel)
{
    float2 uv = (float2(voxel.xy) + 0.5f) / float2((float)volume_width, (float)volume_height);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 view_far = mul(float4(ndc, 1.0f, 1.0f), get_projection_inverted());
    float3 view_dir = normalize(view_far.xyz / view_far.w);
    float3 world_dir = normalize(mul(float4(view_dir, 0.0f), get_view_inverted()).xyz);
    float distance_camera = volume_slice_to_distance(((float)voxel.z + 0.5f) / (float)volume_depth);
    return get_camera_position() + world_dir * distance_camera;
}

Surface build_volume_surface(float3 position, float3 ray_direction, uint2 pixel, float2 uv)
{
    Surface surface;
    surface.flags                  = 0u;
    surface.albedo                 = 1.0f;
    surface.alpha                  = 1.0f;
    surface.roughness              = 1.0f;
    surface.roughness_alpha        = 1.0f;
    surface.metallic               = 0.0f;
    surface.clearcoat              = 0.0f;
    surface.clearcoat_roughness    = 0.0f;
    surface.anisotropic            = 0.0f;
    surface.anisotropic_rotation   = 0.0f;
    surface.sheen                  = 0.0f;
    surface.subsurface_scattering  = 0.0f;
    surface.occlusion              = 1.0f;
    surface.emissive               = 0.0f;
    surface.F0                     = 0.04f;
    surface.pos                    = pixel;
    surface.uv                     = uv;
    surface.depth                  = 0.0f;
    surface.position               = position;
    surface.normal                 = -ray_direction;
    surface.bent_normal            = surface.normal;
    surface.camera_to_pixel        = ray_direction;
    surface.camera_to_pixel_length = distance(position, get_camera_position());
    surface.diffuse_energy         = 1.0f;

    return surface;
}

#if defined(VOLUME_CLEAR)

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint index = dispatch_thread_id.x;
    if (index >= volume_voxel_count)
    {
        return;
    }

    particle_volume_density[index]         = 0u;
    particle_volume_color[index * 2u + 0u] = 0u;
    particle_volume_color[index * 2u + 1u] = 0u;
}

#elif defined(VOLUME_SPLAT)

float volume_hash(uint3 value, uint seed)
{
    uint h = value.x * 73856093u ^ value.y * 19349663u ^ value.z * 83492791u ^ seed;
    h = (h ^ (h >> 16u)) * 2246822519u;
    h = (h ^ (h >> 13u)) * 3266489917u;
    h = h ^ (h >> 16u);
    return (float)(h & 0x00ffffffu) * (1.0f / 16777215.0f);
}

void splat_voxel(uint3 voxel, Particle particle, EmitterParams emitter, float falloff, float age)
{
    uint index = volume_index(voxel);
    float density = saturate(particle.color.a) * max(emitter.volume_density, 0.0f) * falloff;
    if (density <= 0.0001f)
    {
        return;
    }

    // the composite marches toward the light for a real shadow, and it applies to every volumetric
    // emitter, so darkening a voxel by its own density here as well only doubles it up and the medium
    // ends up flat, this carried a per particle guess at something the volume as a whole answers
    //
    // a scalar shade rather than three channels of colour, every volumetric effect authored here is a
    // neutral grey to within a couple of percent, so the two channels that buys are better spent on the
    // parcel age the march reads to give each parcel a noise phase of its own
    float shade = saturate(dot(particle.color.rgb, float3(0.2126f, 0.7152f, 0.0722f)));

    uint density_u = (uint)min(density * volume_density_scale, 65535.0f);
    uint shade_u   = (uint)min(shade * density * volume_color_scale, 65535.0f);
    uint age_u     = (uint)min(saturate(age) * density * volume_color_scale, 65535.0f);

    InterlockedAdd(particle_volume_density[index], density_u);
    InterlockedAdd(particle_volume_color[index * 2u + 0u], shade_u);
    InterlockedAdd(particle_volume_color[index * 2u + 1u], age_u);
}

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint index = dispatch_thread_id.x;
    EmitterParams root = particle_emitter[0];
    if (index >= root.max_particles || root.emitter_count == 0u)
    {
        return;
    }

    Particle particle = particle_buffer_a[index];
    if (particle.lifetime <= 0.0f || particle.max_lifetime <= 0.0f || particle.color.a <= 0.0f)
    {
        return;
    }

    uint emitter_index = min(particle.emitter_index, root.emitter_count - 1u);
    EmitterParams emitter = particle_emitter[emitter_index];
    if (emitter.render_mode != particle_render_volumetric)
    {
        return;
    }

    // one splat scatters into hundreds of voxels, so the cost grows without bound as the emitter fills
    // up, take an evenly spaced subset and scale it up to carry the density the skipped ones would have
    //
    // which subset is taken rotates with the frame, so across a handful of frames every particle gets
    // deposited, and the resolve accumulates those frames against a reprojected history, so the field the
    // march reads is built from the whole live set while the splat only ever pays for a fraction of it,
    // a fixed subset instead left the same few particles carrying several times their own density forever
    // and a plume made of a few fat smooth ellipsoids is what that looks like
    uint stride = max(emitter.volume_splat_stride, 1u);
    if (stride > 1u && (index % stride) != (emitter.frame % stride))
    {
        return;
    }

    float4 clip = mul(float4(particle.position, 1.0f), buffer_frame.view_projection);
    if (clip.w <= 0.0f)
    {
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f)
    {
        return;
    }

    float distance_camera = distance(particle.position, get_camera_position());
    if (distance_camera <= 0.01f || distance_camera >= volume_max_distance)
    {
        return;
    }

    // zero at birth, one about to die, deposited density weighted so the march can read how old the smoke
    // in a voxel is and give it a noise phase that belongs to it
    float particle_age = 1.0f - saturate(particle.lifetime / max(particle.max_lifetime, 0.0001f));

    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float3 center = float3(uv * float2(volume_width, volume_height), volume_distance_to_slice(distance_camera) * (float)volume_depth);
    uint particle_seed = asuint(particle.max_lifetime) ^ (asuint(particle.start_size) * 1664525u) ^ (emitter_index * 1013904223u);

    // the depth extent used to be a slice count derived from the far plane, which floored at one slice,
    // and a slice was a metre and a half, so a twenty centimetre puff was deposited three metres deep and
    // the plume lost every trace of depth structure, take the world radius through the slice thickness at
    // its own range instead and let it come out below a slice, the trilinear filter carries the remainder
    float radius_world = max(particle.size * 0.5f, 0.001f);
    float thickness    = max(volume_slice_thickness(distance_camera), 0.001f);
    float want_xy      = radius_world / max(distance_camera, 0.25f) * (float)volume_height * 3.1f;
    float want_x       = want_xy * lerp(0.72f, 1.08f, volume_hash(uint3(3u, 17u, 41u), particle_seed));
    float want_y       = want_xy * lerp(0.65f, 1.00f, volume_hash(uint3(5u, 23u, 59u), particle_seed));
    float want_d       = radius_world / thickness * lerp(0.78f, 1.16f, volume_hash(uint3(7u, 29u, 67u), particle_seed));

    float radius_x = clamp(want_x, 0.5f, 10.0f);
    float radius_y = clamp(want_y, 0.5f, 10.0f);
    float radius_d = clamp(want_d, 0.35f, 6.0f);

    // the footprint is clamped for cost at one end and by the grid floor at the other, and a clamped
    // footprint no longer matches the particle, a near puff was spread thinner than it is and a far one
    // thicker, so the plume changed density as the camera moved and no authored value could hold, rescale
    // the peak by the ratio of the extent it asked for to the one it got and the deposited mass stops
    // caring about either the range or the slice split, the cap keeps a distant particle collapsed into a
    // single voxel from coming back as a bright dot
    float mass_scale = (want_x * want_y * want_d) / max(radius_x * radius_y * radius_d, 0.0001f);
    mass_scale = min(mass_scale, 3.0f);

    int3 radius = int3((int)ceil(radius_x), (int)ceil(radius_y), (int)ceil(radius_d));
    int3 c = int3(floor(center));

    [loop]
    for (int z = -radius.z; z <= radius.z; z++)
    {
        [loop]
        for (int y = -radius.y; y <= radius.y; y++)
        {
            [loop]
            for (int x = -radius.x; x <= radius.x; x++)
            {
                int3 v = c + int3(x, y, z);
                if (any(v < 0) || v.x >= (int)volume_width || v.y >= (int)volume_height || v.z >= (int)volume_depth)
                {
                    continue;
                }

                float3 voxel_center = float3(v) + 0.5f;
                float3 delta = float3((voxel_center.x - center.x) / max(radius_x, 0.001f),
                                      (voxel_center.y - center.y) / max(radius_y, 0.001f),
                                      (voxel_center.z - center.z) / max(radius_d, 0.001f));
                float dist_sq = dot(delta, delta);
                if (dist_sq >= 1.0f)
                {
                    continue;
                }

                // the envelope stays smooth, the hash that used to break it up was keyed on the screen
                // space voxel index so it sat still in front of the camera while the smoke moved through
                // it, a fixed screen door at grid frequency rather than structure belonging to the plume
                float falloff = (1.0f - dist_sq) * (1.0f - dist_sq);
                splat_voxel((uint3)v, particle, emitter, falloff * (float)stride * mass_scale, particle_age);
            }
        }
    }
}

#elif defined(VOLUME_RESOLVE)

[numthreads(8, 8, 4)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= volume_width || dispatch_thread_id.y >= volume_height || dispatch_thread_id.z >= volume_depth)
    {
        return;
    }

    uint index = volume_index(dispatch_thread_id);
    uint density_u = particle_volume_density[index];
    // unclamped, the old saturate capped the field at one, which held peak optical depth near one and a
    // half and left the densest core about a quarter transparent, so the medium could never go opaque, it
    // could only ever be haze, and the carved structure had no range to work in either, r16 float carries
    // four orders of magnitude past this
    float density = (float)density_u / volume_density_scale;
    float shade = 0.0f;
    float age   = 0.0f;
    if (density_u > 0u)
    {
        // both channels were accumulated density weighted, so dividing by the density recovers the mean
        float norm = (float)density_u * volume_color_scale / volume_density_scale;
        shade = (float)particle_volume_color[index * 2u + 0u] / norm;
        age   = (float)particle_volume_color[index * 2u + 1u] / norm;
    }

    // r shade, g mean parcel age, b spare, a density
    float4 current = float4(saturate(shade), saturate(age), 0.0f, density);

    // the splat pass only deposits an evenly spaced subset of the live particles and scales the survivors
    // up to carry the rest, so one frame of this grid is a sparse unbiased sample of the medium rather
    // than the medium, and a few fat smooth ellipsoids is exactly what a sparse sample of a plume looks
    // like, accumulating against a reprojected history averages successive subsets into a dense field for
    // the price of a texture read, which is what the rotating stride in the splat is there to feed
    bool reset_history = pass_get_f3_value().x > 0.5f;
    float4 result = current;
    if (!reset_history)
    {
        float3 world_position = volume_world_position(dispatch_thread_id);
        float4 prev_clip = mul(float4(world_position, 1.0f), get_view_projection_previous_unjittered());
        if (prev_clip.w > 0.0f)
        {
            float3 prev_ndc  = prev_clip.xyz / prev_clip.w;
            float2 prev_uv   = ndc_to_uv(prev_ndc.xy);
            float  prev_dist = length(world_position - buffer_frame.camera_position_previous);
            float  prev_w    = volume_distance_to_slice(prev_dist);
            if (is_valid_uv(prev_uv) && prev_w > 0.0f && prev_w < 1.0f)
            {
                float4 history = tex3d.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), float3(prev_uv, prev_w), 0.0f);

                // smoke is a medium that moves on its own, so history lags it and holding too much of it
                // drags a ghost behind the plume, and a voxel the camera swept across has history that
                // belongs to a different part of the world entirely, so both cut the weight
                float2 uv_delta = prev_uv - float2((float2(dispatch_thread_id.xy) + 0.5f) / float2((float)volume_width, (float)volume_height));
                float voxel_motion = length(uv_delta * float2((float)volume_width, (float)volume_height));
                float keep = lerp(volume_history_keep, volume_history_keep_moving, saturate(voxel_motion * 0.5f));

                result = lerp(current, history, keep);
            }
        }
    }

    tex3d_uav[dispatch_thread_id] = result;
}

#elif defined(VOLUME_COMPOSITE)

// the grid is indexed by screen position and camera distance, so stepping through it toward the light
// means projecting each step back to where it lands on screen
float3 volume_uv_from_world(float3 world_position)
{
    float4 clip = mul(float4(world_position, 1.0f), buffer_frame.view_projection);
    float2 ndc  = clip.xy / max(clip.w, 0.0001f);
    float2 uv   = ndc * float2(0.5f, -0.5f) + 0.5f;

    return float3(uv, volume_distance_to_slice(distance(world_position, get_camera_position())));
}

// how much of the beam survives the medium between this point and the light, without this every voxel
// is lit as if it were on the surface of the plume and the whole thing reads as an evenly bright cloud
// with no interior, which is the single biggest reason smoke fails to look like smoke
float volume_self_shadow(float3 position, float3 light_dir, float time)
{
    float step_length = volume_shadow_length / (float)volume_shadow_taps;
    float optical_depth = 0.0f;

    [unroll]
    for (uint i = 0u; i < volume_shadow_taps; i++)
    {
        float3 p = position + light_dir * (((float)i + 0.5f) * step_length);
        float3 uvw = volume_uv_from_world(p);
        // the grid carries the mean parcel age alongside the density, so each tap gets the noise phase
        // belonging to the smoke actually standing there rather than the phase of the sample we came from
        float4 s = tex3d.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uvw, 0);
        optical_depth += volume_carve_density_coarse(s.a, p, time, s.g) * step_length;
    }

    return exp(-optical_depth * volume_shadow_density * volume_extinction_scale);
}

float3 evaluate_volume_light(uint light_index, float3 sample_pos, float3 ray_direction, uint2 pixel, float2 uv, bool self_shadow, float time)
{
    Surface surface = build_volume_surface(sample_pos, ray_direction, pixel, uv);

    Light light;
    light.Build(light_index, surface);

    float3 light_dir;
    float local_atten;
    compute_volumetric_light_sample(light, sample_pos, light_dir, local_atten);
    if (local_atten <= 0.0f)
    {
        return 0.0f;
    }

    surface.normal = light_dir;
    surface.bent_normal = light_dir;

    float visibility = 1.0f;
    if (light.has_shadows())
    {
    #ifdef RAY_TRACING_ENABLED
        if (is_ray_traced_shadows_enabled() || is_restir_pt_enabled())
        {
            visibility = 1.0f;
        }
        else
    #endif
        {
            visibility = compute_shadow(surface, light);
        }
    }
    float phase      = volume_phase(dot(ray_direction, light_dir), light.is_directional() ? volume_phase_directional : volume_phase_local);

    if (self_shadow)
    {
        visibility *= volume_self_shadow(sample_pos, light_dir, time);
    }

    return light.color * light.intensity * local_atten * visibility * phase;
}

float3 evaluate_volume_lighting(float3 sample_pos, float3 ray_direction, uint2 pixel, float2 uv, float time)
{
    // skylight, the old fill was the extincted sun color at two and a half percent, so the only thing
    // lighting the medium was the warm beam and white smoke came out brown
    float3 lighting = get_sky_fill_radiance() * volume_sky_fill;
    uint total_lights = buffer_frame.cluster_light_count;

    if (total_lights > 0u)
    {
        // only the dominant light pays for a light march, it is the one that carries the shape and four
        // extra taps per local light would multiply the cost of the whole composite
        lighting += evaluate_volume_light(0u, sample_pos, ray_direction, pixel, uv, true, time);
    }

    if (total_lights > 1u)
    {
        float view_z = mul(float4(sample_pos, 1.0f), get_view()).z;
        uint3 cid    = cluster_id_from_screen(uv, view_z);
        uint flat_id = cluster_flat(cid);
        uint2 range  = cluster_light_grid[flat_id];

        // this runs once per ray march step per pixel, so an uncapped cluster multiplies the whole
        // composite by the local light count, the nearest few carry the look
        const uint max_local_lights = 4u;
        uint light_count = min(range.y, max_local_lights);

        [loop]
        for (uint k = 0u; k < light_count; k++)
        {
            uint light_index = cluster_light_indices[range.x + k];
            lighting += evaluate_volume_light(light_index, sample_pos, ray_direction, pixel, uv, false, time);
        }
    }

    return lighting;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_thread_id.xy;
    if (pixel.x >= (uint)buffer_frame.resolution_render.x || pixel.y >= (uint)buffer_frame.resolution_render.y)
    {
        return;
    }

    float2 uv = (float2(pixel) + 0.5f) / buffer_frame.resolution_render;
    float depth_raw = tex_depth.Load(int3(pixel, 0)).r;
    float scene_distance = min(linearize_depth(depth_raw), volume_max_distance);
    if (scene_distance <= 0.05f)
    {
        return;
    }

    float transmittance = 1.0f;
    float3 scattering = 0.0f;
    float3 scene_pos = get_position(depth_raw, uv);
    float3 ray_direction = normalize(scene_pos - get_camera_position());
    // the drift offset gets multiplied by the octave frequency, so an unwrapped clock runs out of
    // fractional precision after a while and the field starts to quantise, ten minutes is long enough
    // that the repeat never reads as a loop
    float time = fmod((float)buffer_frame.time, 600.0f);

    // the carved density carries structure finer than a step length, so evenly spaced samples land on
    // the same features every frame and read as depth banding, offsetting each ray by a fraction of a
    // step turns that into noise which taa resolves
    float jitter = noise_interleaved_gradient(float2(pixel));

    // a fixed fraction of the distance to the scene spends nearly every sample in empty air, the plume is
    // a few metres deep in a shot that is twenty five to the wall behind it, so the samples that actually
    // landed inside the smoke sat half a metre apart and nothing finer than that could resolve however
    // fine the noise got, step long through empty space and short once inside the medium instead
    const uint  max_steps     = 112u;
    const float enter_density = 0.002f;
    float coarse_step = max(scene_distance / 40.0f, 0.20f);

    float t = jitter * coarse_step;
    float3 lighting = 0.0f;
    uint light_phase = 0u;

    [loop]
    for (uint i = 0u; i < max_steps; i++)
    {
        if (t >= scene_distance || transmittance < 0.01f)
        {
            break;
        }

        float4 sample = tex3d.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), float3(uv, volume_distance_to_slice(t)), 0);
        if (sample.a <= enter_density)
        {
            t += coarse_step;
            light_phase = 0u;
            continue;
        }

        // half a slice, so the march always reads finer than the grid it is sampling and never steps
        // over a billow, the exponential split already widens the slices with range so this tracks it
        // and stops the near field from being oversampled at a fixed tenth of a metre
        float fine_step = clamp(volume_slice_thickness(t) * 0.5f, 0.05f, 0.5f);
        float3 sample_pos = get_camera_position() + ray_direction * t;
        float density = volume_carve_density(sample.a, sample_pos, time, sample.g);

        if (density > 0.0001f)
        {
            // the light march is the expensive part and the beam does not change much across a tenth of a
            // metre, so it is refreshed every third sample and reused in between
            if (light_phase == 0u)
            {
                lighting = evaluate_volume_lighting(sample_pos, ray_direction, pixel, uv, time);
            }
            light_phase = (light_phase + 1u) % 3u;

            float optical_depth = density * fine_step * volume_extinction_scale;

            // successive scattering orders, each dimmer and reaching deeper because light that has already
            // bounced is scattered out of the beam less readily, summing a few lifts the inside of the
            // plume out of shadow without a second light evaluation
            float3 in_scatter = 0.0f;
            float order_extinction = 1.0f;
            float order_gain = 1.0f;

            [unroll]
            for (uint o = 0u; o < volume_scatter_octaves; o++)
            {
                in_scatter       += lighting * order_gain * (1.0f - exp(-optical_depth * order_extinction));
                order_extinction *= volume_scatter_falloff;
                order_gain       *= volume_scatter_gain;
            }

            // the grid holds a scalar shade rather than three channels of colour, the smoke authored here
            // is neutral to within a couple of percent and the channels that buys pay for the age above
            scattering += transmittance * sample.r * in_scatter;
            transmittance *= exp(-optical_depth);
        }

        t += fine_step;
    }

    if (any(scattering > 0.0f) || transmittance < 0.999f)
    {
        float4 frame = tex_uav[pixel];
        frame.rgb = frame.rgb * transmittance + scattering;
        tex_uav[pixel] = frame;

        float4 bias = tex_uav2[pixel];
        bias.r = max(bias.r, saturate(1.0f - transmittance));
        tex_uav2[pixel] = bias;
    }
}

#endif
