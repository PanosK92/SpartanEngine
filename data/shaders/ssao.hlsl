/*
Copyright(c) 2015-2025 Panos Karabelas

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
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// spartan engine take on GTAO + Bent Normals + Visibility Bitmask
// inspired from XeGTAO:                    https://github.com/GameTechDev/XeGTAO
// enchanced with visibility bitmasks from: https://cdrinmatane.github.io/posts/ssaovb-code/

// constants
static const float g_ao_radius    = 1.5f;
static const float g_ao_intensity = 1.0f;
static const uint g_directions    = 3;
static const uint g_steps         = 3;
static const uint g_sector_count  = 32;
static const float g_thickness    = 1.0f;
static const float g_offsets[]    = { 0.0f, 0.5f, 0.25f, 0.75f };
static const float g_rotations[]  = { 0.1666f, 0.8333f, 0.5f, 0.6666f, 0.3333f, 0.0f };

float3x3 rotation_from_to(float3 from, float3 to)
{
    from = normalize(from);
    to = normalize(to);
    float cos_theta = dot(from, to);
    if (cos_theta > 0.9999f)
    {
        return float3x3(1,0,0, 0,1,0, 0,0,1);
    }
    if (cos_theta < -0.9999f)
    {
        return float3x3(-1,0,0, 0,-1,0, 0,0,-1);
    }
    float3 axis = normalize(cross(from, to));
    float sin_theta = sqrt(1.0f - cos_theta * cos_theta);
    float omc = 1.0f - cos_theta;
    float x = axis.x, y = axis.y, z = axis.z;
    float3x3 mat;
    mat._11 = cos_theta + x * x * omc;
    mat._12 = x * y * omc - z * sin_theta;
    mat._13 = x * z * omc + y * sin_theta;
    mat._21 = y * x * omc + z * sin_theta;
    mat._22 = cos_theta + y * y * omc;
    mat._23 = y * z * omc - x * sin_theta;
    mat._31 = z * x * omc - y * sin_theta;
    mat._32 = z * y * omc + x * sin_theta;
    mat._33 = cos_theta + z * z * omc;
    return mat;
}

float compute_slice_visibility(float horizon_cos0, float horizon_cos1, float cos_norm, float n, out float h0, out float h1)
{
    h0          = -fast_acos(horizon_cos1);
    h1          = fast_acos(horizon_cos0);
    h0          = n + clamp(h0 - n, -PI_HALF, PI_HALF);
    h1          = n + clamp(h1 - n, -PI_HALF, PI_HALF);
    float iarc0 = (cos_norm + 2.0f * h0 * sin(n) - cos(2.0f * h0 - n)) * 0.25f;
    float iarc1 = (cos_norm + 2.0f * h1 * sin(n) - cos(2.0f * h1 - n)) * 0.25f;
    return iarc0 + iarc1;
}

float3 compute_slice_bent_normal(float cos_phi, float sin_phi, float h0, float h1, float n)
{
    float t0 = (6.0f * sin(h0 - n) - sin(3.0f * h0 - n) + 6.0f * sin(h1 - n) - sin(3.0f * h1 - n) + 16.0f * sin(n) - 3.0f * (sin(h0 + n) + sin(h1 + n))) / 12.0f;
    float t1 = (-cos(3.0f * h0 - n) - cos(3.0f * h1 - n) + 8.0f * cos(n) - 3.0f * (cos(h0 + n) + cos(h1 + n))) / 12.0f;
    return float3(cos_phi * t0, sin_phi * t0, -t1); // local bent normal
}

uint update_sectors(float minHorizon, float maxHorizon, uint globalOccludedbitmask)
{
    uint startHorizonInt        = uint(minHorizon * g_sector_count);
    float angleHorizon          = (maxHorizon - minHorizon) * g_sector_count;
    uint angleHorizonInt        = uint(ceil(angleHorizon));
    uint angleHorizonbitmask    = angleHorizonInt > 0 ? (0xFFFFFFFFu >> (g_sector_count - angleHorizonInt)) : 0u;
    uint currentOccludedbitmask = angleHorizonbitmask << startHorizonInt;
    return globalOccludedbitmask | currentOccludedbitmask;
}

float2 fast_acos2(float2 x)
{
   return (-0.69813170*x*x - 0.87266463)*x + 1.57079633;
}

float2 get_front_back_horizons(float samplingDirection, float3 deltaPos, float3 view_vec, float n)
{
    samplingDirection       = -samplingDirection; // flip to align with left-handed view space and Y-down
    float3 deltaPosBackface = deltaPos - view_vec * g_thickness;
    float2 frontBackHorizon = float2(dot(normalize(deltaPos), view_vec), dot(normalize(deltaPosBackface), view_vec));
    frontBackHorizon        = fast_acos2(frontBackHorizon);
    frontBackHorizon        = saturate((samplingDirection * -frontBackHorizon - n + PI_HALF) / PI);
    frontBackHorizon        = samplingDirection >= 0.0f ? frontBackHorizon.yx : frontBackHorizon.xy;
    
    return frontBackHorizon;
}

float3 compute_slice_normal_from_bitmask(uint bitmask, float3 ortho_dir_vec, float3 view_vec)
{
    float3 slice_normal = float3(0,0,0);

    for (uint sector = 0; sector < g_sector_count; sector++)
    {
        bool occluded = (bitmask & (1u << sector)) != 0u;
        if (!occluded)
        {
            float angle = (float(sector) + 0.5f) / float(g_sector_count) * PI; // sector center
            float cos_a = cos(angle);
            float sin_a = sin(angle);

            // sector vector in slice plane
            float3 dir_in_plane = cos_a * ortho_dir_vec + sin_a * cross(ortho_dir_vec, view_vec);

            slice_normal += dir_in_plane;
        }
    }

    return normalize(slice_normal);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);

    // compute pixel vectors
    uint2 pos                    = thread_id.xy;
    const float2 origin_uv       = (pos + 0.5f) / resolution_out;
    const float3 origin_position = get_position_view_space(origin_uv);
    const float3 origin_normal   = get_normal_view_space(origin_uv);

    Surface surface;
    surface.Build(pos, resolution_out, true, false);

    // setup
    const float noise_gradient_temporal           = noise_interleaved_gradient(pos);
    const float offset_spatial                    = 0.25 * (float)((pos.y - pos.x) & 3);
    const float offset_temporal                   = g_offsets[buffer_frame.frame % 4];
    const float offset_rotation_temporal          = g_rotations[buffer_frame.frame % 6];
    const float ray_offset                        = frac(offset_spatial + offset_temporal) + (hash(origin_uv) * 2.0f - 1.0f) * 0.25f;
    const float2 texel_size                       = 1.0f / resolution_out;
    const float3 view_vec                         = normalize(-origin_position);
    const float falloff_range                     = 0.6f * g_ao_radius;
    const float falloff_from                      = g_ao_radius - falloff_range;
    float falloff_mul                             = -1.0f / falloff_range;
    float falloff_add                             = falloff_from / falloff_range + 1.0f;
    float3 pos_right                              = get_position_view_space(origin_uv + float2(texel_size.x, 0));
    float pixel_dir_rb_viewspace_size_at_center_z = length(pos_right - origin_position);
    float screenspace_radius                      = g_ao_radius / pixel_dir_rb_viewspace_size_at_center_z;
    const float pixel_too_close_threshold         = 1.3f;
    const float min_s                             = pixel_too_close_threshold / screenspace_radius;
    const float noise_slice                       = noise_gradient_temporal + offset_rotation_temporal;
    const float noise_sample                      = ray_offset;
    const float3x3 rot_mat                        = rotation_from_to(float3(0.0f, 0.0f, -1.0f), view_vec);
    
    // loop over slice directions to accumulate visibility
    float visibility   = 0.0f;
    float3 bent_normal = 0.0f;
    [unroll]
    for (uint slice = 0; slice < g_directions; slice++)
    {
        // slice setup: compute rotated direction (omega), project normal onto slice plane, compute bent normal angle (n)
        float slice_k                     = (float(slice) + noise_slice) / float(g_directions);
        float phi                         = slice_k * PI;
        float cos_phi                     = cos(phi);
        float sin_phi                     = sin(phi);
        float2 omega                      = float2(cos_phi, -sin_phi) * screenspace_radius;
        const float3 direction_vec        = float3(cos_phi, sin_phi, 0.0f);
        const float3 ortho_direction_vec  = direction_vec - dot(direction_vec, view_vec) * view_vec;
        const float3 axis_vec             = normalize(cross(ortho_direction_vec, view_vec));
        float3 projected_normal_vec       = origin_normal - axis_vec * dot(origin_normal, axis_vec);
        float projected_normal_vec_length = length(projected_normal_vec);
        float cos_norm                    = saturate(dot(projected_normal_vec, view_vec) / projected_normal_vec_length);
        float sign_norm                   = sign(dot(ortho_direction_vec, projected_normal_vec));
        float n                           = sign_norm * fast_acos(cos_norm);

        // find horizon angles
        float low_horizon_cos0  = cos(n + PI_HALF);
        float low_horizon_cos1  = cos(n - PI_HALF);
        float horizon_cos0      = low_horizon_cos0;
        float horizon_cos1      = low_horizon_cos1;
        uint occlusion_bitmask = 0u;
        [unroll]
        for (uint step = 0; step < g_steps; step++)
        {
            float step_base_noise  = float(slice + step * g_steps) * 0.6180339887498948482f;
            float step_noise       = frac(noise_sample + step_base_noise);
            float s                = (step + step_noise) / float(g_steps);
            s                     += min_s;

            float2 sample_offset       = s * omega;
            float sample_offset_length = length(sample_offset);
            sample_offset              = round(sample_offset) * texel_size;

            float2 sample_screen_pos0  = origin_uv + sample_offset;
            float3 sample_pos0         = get_position_view_space(sample_screen_pos0);
            float3 sample_delta0       = sample_pos0 - origin_position;
            float sample_dist0         = length(sample_delta0);
            float3 sample_horizon_vec0 = sample_delta0 / sample_dist0;

            float2 sample_screen_pos1  = origin_uv - sample_offset;
            float3 sample_pos1         = get_position_view_space(sample_screen_pos1);
            float3 sample_delta1       = sample_pos1 - origin_position;
            float sample_dist1         = length(sample_delta1);
            float3 sample_horizon_vec1 = sample_delta1 / sample_dist1;

            float weight0 = saturate(sample_dist0 * falloff_mul + falloff_add);
            float weight1 = saturate(sample_dist1 * falloff_mul + falloff_add);

            float shc0 = dot(sample_horizon_vec0, view_vec);
            float shc1 = dot(sample_horizon_vec1, view_vec);
            shc0       = lerp(low_horizon_cos0, shc0, weight0);
            shc1       = lerp(low_horizon_cos1, shc1, weight1);

            horizon_cos0 = max(horizon_cos0, shc0);
            horizon_cos1 = max(horizon_cos1, shc1);

            // visibility bitmask for sample0 (positive dir along +omega)
            float2 fbh0       = get_front_back_horizons(1.0f, sample_delta0, view_vec, n);
            occlusion_bitmask = update_sectors(fbh0.x, fbh0.y, occlusion_bitmask);
            
            // visibility bitmask for sample1 (negative dir along -omega)
            float2 fbh1       = get_front_back_horizons(-1.0f, sample_delta1, view_vec, n);
            occlusion_bitmask = update_sectors(fbh1.x, fbh1.y, occlusion_bitmask);
        }

       // compute slice visibility using bitmask (bits correspond to indivudal slice sectors)
       float local_visibility  = (1.0f - float(countbits(occlusion_bitmask)) / float(g_sector_count));
       visibility             += local_visibility;

       // compute bent normal
       float h0, h1;
       compute_slice_visibility(horizon_cos0, horizon_cos1, cos_norm, n, h0, h1);
       float slice_angle    = (h0 + h1) * 0.5f - n;
       float3 bent_normal_l = normalize(
            cos(slice_angle) * projected_normal_vec + 
            sin(slice_angle) * cross(axis_vec, projected_normal_vec) +
            (1.0 - cos(slice_angle)) * dot(axis_vec, projected_normal_vec) * axis_vec
        );
       bent_normal += mul(bent_normal_l, rot_mat);
    }

    // normalize visibility
    visibility /= float(g_directions);
    visibility  = pow(visibility, g_ao_intensity);

    // project bent normal to world space and normalize
    bent_normal = normalize(view_to_world(normalize(bent_normal), false));

    // when input textures are black/empty, NaNs can appear, in that case, zero out the result
    bent_normal *= 1.0f - (float) any(isnan(bent_normal));
    visibility  *= 1.0f - (float) isnan(visibility);

    tex_uav[thread_id.xy] = float4(bent_normal, visibility);
}
