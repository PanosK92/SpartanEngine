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

//= includes =========
#include "common.hlsl"
#include "fog.hlsl"
//====================

static const uint  fog_width         = 160u;
static const uint  fog_height        = 90u;
static const uint  fog_depth         = 64u;
static const float fog_near          = 0.5f;
static const float fog_far           = 256.0f;
static const float fog_scale_height  = 50.0f;
static const float fog_sigma_scale   = 0.0012f;
static const float fog_ambient_scale = 1.0f;

float fog_slice_to_distance(float slice_01)
{
    slice_01 = saturate(slice_01);
    return fog_near * pow(fog_far / fog_near, slice_01);
}

float fog_distance_to_slice(float dist)
{
    dist = clamp(dist, fog_near, fog_far);
    return log(dist / fog_near) / log(fog_far / fog_near);
}

float3 fog_world_position(uint3 voxel)
{
    float2 uv  = (float2(voxel.xy) + 0.5f) / float2((float)fog_width, (float)fog_height);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 view_far = mul(float4(ndc, 1.0f, 1.0f), get_projection_inverted());
    float3 view_dir = normalize(view_far.xyz / view_far.w);
    float3 world_dir = normalize(mul(float4(view_dir, 0.0f), get_view_inverted()).xyz);
    float slice_01 = ((float)voxel.z + 0.5f) / (float)fog_depth;
    float distance_camera = fog_slice_to_distance(slice_01);
    return get_camera_position() + world_dir * distance_camera;
}

float fog_air_sigma(float world_height, float fog_intensity)
{
    float density = fog_intensity * fog_sigma_scale;
    return density * exp(-world_height / fog_scale_height);
}

float fog_ocean_sigma()
{
    return 0.05f * buffer_frame.ocean_turbidity;
}

bool fog_in_ocean(float3 world_pos)
{
    return buffer_frame.ocean_enabled > 0.5f
        && buffer_frame.ocean_turbidity > 0.0f
        && world_pos.y < buffer_frame.ocean_sea_level;
}

float3 fog_sun_illuminance(float3 sample_pos, float3 sun_dir)
{
    return cloud_sun_illuminance(sample_pos, sun_dir, tex, GET_SAMPLER(sampler_bilinear_clamp));
}

float3 fog_sky_ambient(float3 view_dir)
{
    float2 uv = direction_sphere_uv(normalize(float3(view_dir.x, max(view_dir.y, 0.0f), view_dir.z)));
    float3 sky = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uv, 7).rgb;
    return min(sky, 50.0f) * fog_ambient_scale;
}

#if defined(FOG_DENSITY)

[numthreads(8, 8, 4)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= fog_width || dispatch_thread_id.y >= fog_height || dispatch_thread_id.z >= fog_depth)
    {
        return;
    }

    float fog_intensity = pass_get_f3_value().y;
    float3 world_pos    = fog_world_position(dispatch_thread_id);
    float sigma         = 0.0f;

    if (fog_in_ocean(world_pos))
    {
        sigma = fog_ocean_sigma();
    }
    else if (fog_intensity > 0.0f)
    {
        sigma = fog_air_sigma(world_pos.y, fog_intensity);
    }

    tex3d_uav[dispatch_thread_id] = float4(0.0f, 0.0f, 0.0f, sigma);
}

#elif defined(FOG_LIGHT)

[numthreads(8, 8, 4)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= fog_width || dispatch_thread_id.y >= fog_height || dispatch_thread_id.z >= fog_depth)
    {
        return;
    }

    float4 cell = tex3d_uav[dispatch_thread_id];
    float sigma = cell.a;
    if (sigma <= 1e-6f)
    {
        tex3d_uav[dispatch_thread_id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 world_pos     = fog_world_position(dispatch_thread_id);
    float3 ray_direction = normalize(world_pos - get_camera_position());
    float2 uv            = (float2(dispatch_thread_id.xy) + 0.5f) / float2((float)fog_width, (float)fog_height);
    uint2  pixel         = dispatch_thread_id.xy;
    bool   in_water      = fog_in_ocean(world_pos);

    float3 lighting = fog_sky_ambient(ray_direction) * sigma;

    // sun
    if (buffer_frame.cluster_light_count > 0u)
    {
        Surface surface;
        surface.flags                  = 0u;
        surface.position               = world_pos;
        surface.normal                 = -ray_direction;
        surface.bent_normal            = surface.normal;
        surface.camera_to_pixel        = ray_direction;
        surface.camera_to_pixel_length = length(world_pos - get_camera_position());
        surface.uv                     = uv;
        surface.pos                    = pixel;

        Light light;
        light.Build(0u, surface);

        // sun always lights the medium, local lights stay behind the volumetric flag
        float3 light_dir;
        float  local_atten;
        compute_volumetric_light_sample(light, world_pos, light_dir, local_atten);

        if (local_atten > 0.0f && light.intensity > 0.0f)
        {
            float visibility = visible(world_pos, light, pixel);
            if (visibility > 0.0f)
            {
                visibility *= cloud_shadow_sample(tex5, GET_SAMPLER(sampler_bilinear_clamp), world_pos, light_dir, get_camera_position());
            }

            if (visibility > 0.0f)
            {
                float cos_theta = dot(ray_direction, light_dir);
                float phase     = henyey_greenstein_phase(cos_theta, 0.25f);
                float3 sun_L    = fog_sun_illuminance(world_pos, light_dir);
                float3 tint     = 1.0f;

                if (in_water)
                {
                    float sun_path = (buffer_frame.ocean_sea_level - world_pos.y) / max(light_dir.y, 0.05f);
                    tint = exp(-ocean_extinction * sun_path)
                         * (0.2f + 0.8f * get_ocean_caustic(world_pos.xz + light_dir.xz * sun_path, sun_path));
                    sun_L = light.intensity * light.color;
                }

                lighting += phase * visibility * local_atten * sigma * sun_L * tint;
            }
        }
    }

    // local volumetric lights
    uint volumetric_count = buffer_frame.volumetric_light_count;
    [loop]
    for (uint k = 0u; k < volumetric_count; k++)
    {
        uint light_index = volumetric_light_indices[k];
        if (light_index == 0u)
        {
            continue;
        }

        Surface surface;
        surface.flags                  = 0u;
        surface.position               = world_pos;
        surface.normal                 = -ray_direction;
        surface.bent_normal            = surface.normal;
        surface.camera_to_pixel        = ray_direction;
        surface.camera_to_pixel_length = length(world_pos - get_camera_position());
        surface.uv                     = uv;
        surface.pos                    = pixel;

        Light light;
        light.Build(light_index, surface);
        if (!light.is_volumetric())
        {
            continue;
        }

        float3 light_dir;
        float  local_atten;
        compute_volumetric_light_sample(light, world_pos, light_dir, local_atten);
        if (local_atten <= 0.0f)
        {
            continue;
        }

        float visibility = visible(world_pos, light, pixel);
        if (visibility <= 0.0f)
        {
            continue;
        }

        float cos_theta = dot(ray_direction, light_dir);
        float phase     = henyey_greenstein_phase(cos_theta, 0.6f);
        lighting += phase * visibility * local_atten * sigma * light.intensity * light.color;
    }

    tex3d_uav[dispatch_thread_id] = float4(lighting, sigma);
}

#elif defined(FOG_TEMPORAL)

[numthreads(8, 8, 4)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= fog_width || dispatch_thread_id.y >= fog_height || dispatch_thread_id.z >= fog_depth)
    {
        return;
    }

    float4 current = tex3d_uav[dispatch_thread_id];
    bool history_invalid = pass_get_f3_value().x > 0.5f;
    if (history_invalid)
    {
        tex3d_uav[dispatch_thread_id] = current;
        return;
    }

    float3 world_pos = fog_world_position(dispatch_thread_id);
    float4 prev_clip = mul(float4(world_pos, 1.0f), get_view_projection_previous());
    if (prev_clip.w <= 0.0f)
    {
        tex3d_uav[dispatch_thread_id] = current;
        return;
    }

    float3 prev_ndc = prev_clip.xyz / prev_clip.w;
    float2 prev_uv  = float2(prev_ndc.x * 0.5f + 0.5f, 0.5f - prev_ndc.y * 0.5f);
    if (!is_valid_uv(prev_uv))
    {
        tex3d_uav[dispatch_thread_id] = current;
        return;
    }

    float prev_dist = length(world_pos - buffer_frame.camera_position_previous);
    float prev_z    = fog_distance_to_slice(prev_dist);
    float3 hist_uvw = float3(prev_uv, prev_z);
    if (hist_uvw.z < 0.0f || hist_uvw.z > 1.0f)
    {
        tex3d_uav[dispatch_thread_id] = current;
        return;
    }

    float4 history = tex3d.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), hist_uvw, 0);

    float camera_delta = length(get_camera_position() - buffer_frame.camera_position_previous);
    float history_w    = lerp(0.85f, 0.35f, saturate(camera_delta * 0.25f));
    float4 blended     = lerp(current, history, history_w);
    // keep extinction from the current frame so density stays sharp under motion
    blended.a = current.a;

    tex3d_uav[dispatch_thread_id] = blended;
}

#elif defined(FOG_HISTORY_COPY)

[numthreads(8, 8, 4)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= fog_width || dispatch_thread_id.y >= fog_height || dispatch_thread_id.z >= fog_depth)
    {
        return;
    }

    tex3d_uav[dispatch_thread_id] = tex3d.Load(int4(dispatch_thread_id, 0));
}

#elif defined(FOG_INTEGRATE)

[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (dispatch_thread_id.x >= (uint)resolution_out.x || dispatch_thread_id.y >= (uint)resolution_out.y)
    {
        return;
    }

    uint2 pixel = dispatch_thread_id.xy;
    float2 uv   = (float2(pixel) + 0.5f) / resolution_out;

    float depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    // reverse-z clears far/sky to 0
    bool is_sky = depth_raw <= 1e-7f;
    float scene_distance = is_sky ? fog_far : min(linearize_depth(depth_raw), fog_far);
    if (scene_distance <= 0.05f)
    {
        tex_uav[pixel] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float3 ray_origin    = get_camera_position();
    float3 ray_direction = normalize(get_position(depth_raw, uv) - ray_origin);
    if (is_sky)
    {
        float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
        float4 view_far = mul(float4(ndc, 1.0f, 1.0f), get_projection_inverted());
        float3 view_dir = normalize(view_far.xyz / view_far.w);
        ray_direction   = normalize(mul(float4(view_dir, 0.0f), get_view_inverted()).xyz);
    }

    const uint step_count = fog_depth;
    float transmittance   = 1.0f;
    float3 inscatter      = 0.0f;
    float prev_distance   = 0.0f;

    [loop]
    for (uint i = 0u; i < step_count; i++)
    {
        float slice_01  = ((float)i + 0.5f) / (float)step_count;
        float distance_s = fog_slice_to_distance(slice_01);
        if (distance_s > scene_distance)
        {
            break;
        }

        float dt = max(distance_s - prev_distance, 0.0f);
        prev_distance = distance_s;
        if (dt <= 0.0f)
        {
            continue;
        }

        float3 volume_uv = float3(uv, fog_distance_to_slice(distance_s));
        float4 sample    = tex3d.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), volume_uv, 0);
        float sigma_t    = sample.a;
        float3 lighting  = sample.rgb;

        if (sigma_t > 1e-6f || any(lighting > 0.0f))
        {
            // lighting already includes sigma_s * phase * L
            inscatter     += transmittance * lighting * dt;
            transmittance *= exp(-sigma_t * dt);
            if (transmittance < 0.005f)
            {
                break;
            }
        }
    }

    tex_uav[pixel] = float4(inscatter, transmittance);
}

#endif
