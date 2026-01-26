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

//= INCLUDES =========
#include "common.hlsl"
//====================

// - the functions are shared between depth_prepass.hlsl, g_buffer.hlsl and depth_light.hlsl
// - this is because the calculations have to be exactly the same and therefore produce identical values over time (motion vectors) and space (depth pre-pass vs g-buffer)

// vertex buffer input
struct Vertex_PosUvNorTan
{
    float4 position                : POSITION;
    float2 uv                      : TEXCOORD;
    float3 normal                  : NORMAL;
    float3 tangent                 : TANGENT;
    min16float instance_position_x : INSTANCE_POSITION_X;
    min16float instance_position_y : INSTANCE_POSITION_Y;
    min16float instance_position_z : INSTANCE_POSITION_Z;
    uint instance_normal_oct       : INSTANCE_NORMAL_OCT;
    uint instance_yaw              : INSTANCE_YAW;
    uint instance_scale            : INSTANCE_SCALE;
};

// vertex buffer output
struct gbuffer_vertex
{
    float4 position          : SV_POSITION;
    float4 position_previous : POS_CLIP_PREVIOUS;
    float3 normal            : NORMAL_WORLD;
    float3 tangent           : TANGENT_WORLD;
    float4 uv_misc           : TEXCOORD;  // xy = uv, z = height_percent, w = instance_id - packed together to reduced the interpolators (shader registers) the gpu needs to track
    float width_percent      : TEXCOORD2; // temp, will remove
    //float2 tile_position     : POS_TILE;
};

float4x4 compose_instance_transform(min16float instance_position_x, min16float instance_position_y, min16float instance_position_z, uint instance_normal_oct, uint instance_yaw, uint instance_scale)
{
    // compose position
    float3 instance_position = float3(instance_position_x, instance_position_y, instance_position_z);
    
    // check for identity
    float pos_sq = dot(instance_position, instance_position);
    if (pos_sq < 1e-10 && instance_normal_oct == 0 && instance_yaw == 0 && instance_scale == 0)
        return float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    
    // compose octahedral normal
    static const float rcp_255 = 1.0 / 255.0;
    float x            = (float(instance_normal_oct >> 8) * rcp_255) * 2.0 - 1.0;
    float y            = (float(instance_normal_oct & 0xFF) * rcp_255) * 2.0 - 1.0;
    float3 n           = float3(x, y, 1.0 - abs(x) - abs(y));
    float mask         = step(0.0, n.z);
    float2 adjusted_xy = (float2(1.0, 1.0) - abs(n.yx)) * sign(n.xy);
    n.xy               = mask * n.xy + (1.0 - mask) * adjusted_xy;
    float3 normal      = normalize(n);
    
    // compose yaw and scale
    static const float pi_2 = 6.28318530718;
    static const float scale_min_log2 = -6.643856; // log2(0.01)
    static const float scale_max_log2 = 6.643856;  // log2(100)
    float yaw   = float(instance_yaw) * rcp_255 * pi_2;
    float scale = exp2(lerp(scale_min_log2, scale_max_log2, float(instance_scale) * rcp_255));
    
    // compose quaternion
    static const float3 up = float3(0, 1, 0);
    float up_dot_normal = dot(up, normal);
    float4 quat;
    if (abs(up_dot_normal) >= 0.999999)
    {
        quat = up_dot_normal > 0 ? float4(0, 0, 0, 1) : float4(1, 0, 0, 0);
    }
    else
    {
        float s = fast_sqrt(2.0 + 2.0 * up_dot_normal);
        quat    = float4(cross(up, normal) / s, s * 0.5);
    }
    float yaw_half = -yaw * 0.5;
    float cy        = cos(yaw_half);
    float sy        = sin(yaw_half);
    float4 quat_yaw = float4(0, sy, 0, cy);
    
    // quaternion multiplication
    float qx = quat.w * quat_yaw.x + quat.x * quat_yaw.w + quat.y * quat_yaw.z - quat.z * quat_yaw.y;
    float qy = quat.w * quat_yaw.y - quat.x * quat_yaw.z + quat.y * quat_yaw.w + quat.z * quat_yaw.x;
    float qz = quat.w * quat_yaw.z + quat.x * quat_yaw.y - quat.y * quat_yaw.x + quat.z * quat_yaw.w;
    float qw = quat.w * quat_yaw.w - quat.x * quat_yaw.x - quat.y * quat_yaw.y - quat.z * quat_yaw.z;
    
    // compose rotation matrix directly as 4x4 with scale applied
    float xx = qx * qx;
    float xy = qx * qy;
    float xz = qx * qz;
    float xw = qx * qw;
    float yy = qy * qy;
    float yz = qy * qz;
    float yw = qy * qw;
    float zz = qz * qz;
    float zw = qz * qw;
    
    // compose final transform directly (scale applied during matrix construction)
    return float4x4(
        float4((1.0 - 2.0 * (yy + zz)) * scale, 2.0 * (xy + zw) * scale, 2.0 * (xz - yw) * scale, 0.0),
        float4(2.0 * (xy - zw) * scale, (1.0 - 2.0 * (xx + zz)) * scale, 2.0 * (yz + xw) * scale, 0.0),
        float4(2.0 * (xz + yw) * scale, 2.0 * (yz - xw) * scale, (1.0 - 2.0 * (xx + yy)) * scale, 0.0),
        float4(instance_position, 1.0)
    );
}

float3x3 rotation_matrix(float3 axis, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    float t = 1.0f - c;

    axis = normalize(axis);

    return float3x3(
        t * axis.x * axis.x + c,
        t * axis.x * axis.y - s * axis.z,
        t * axis.x * axis.z + s * axis.y,
        
        t * axis.x * axis.y + s * axis.z,
        t * axis.y * axis.y + c,
        t * axis.y * axis.z - s * axis.x,
        
        t * axis.x * axis.z - s * axis.y,
        t * axis.y * axis.z + s * axis.x,
        t * axis.z * axis.z + c
    );
}

struct vertex_processing
{
    static void process_world_space(Surface surface, inout float3 position_world, inout gbuffer_vertex vertex, float3 position_local, float4x4 transform, uint instance_id, float time_offset)
    {
        float time                = (float)buffer_frame.time + time_offset;
        float3 wind               = buffer_frame.wind;
        float base_wind_magnitude = length(wind);
        float3 base_wind_dir      = normalize(wind + float3(1e-6f, 0.0f, 1e-6f));
        float3 instance_up        = normalize(transform[1].xyz);
        
        // camera-facing bias for grass (ghost of tsushima technique)
        // rotates blades partially toward camera when edge-on to maintain visual density
        if (surface.is_grass_blade())
        {
            const float camera_bias_strength = 0.7f; // 0 = no bias, 1 = full billboard
            
            // get camera direction in horizontal plane
            float3 to_camera    = buffer_frame.camera_position - position_world;
            float3 to_camera_xz = normalize(float3(to_camera.x, 0.0f, to_camera.z));
            
            // blade normal is the z-axis of the transform (perpendicular to blade surface)
            float3 blade_normal    = normalize(transform[2].xyz);
            float3 blade_normal_xz = normalize(float3(blade_normal.x, 0.0f, blade_normal.z) + float3(1e-6f, 0.0f, 1e-6f));
            
            // calculate how edge-on we are: 1 = facing camera, 0 = edge-on
            float facing_dot  = abs(dot(blade_normal_xz, to_camera_xz));
            float edge_factor = 1.0f - facing_dot; // higher when more edge-on
            
            // apply bias - stronger when more edge-on
            float bias_amount = edge_factor * edge_factor * camera_bias_strength; // squared for smooth falloff
            
            // calculate rotation to face camera
            float3 cross_result = cross(blade_normal_xz, to_camera_xz);
            float rotation_sign = sign(cross_result.y);
            float angle_to_camera = acos(saturate(facing_dot));
            float bias_angle = angle_to_camera * bias_amount * rotation_sign;
            
            // rotate around world up axis for horizontal rotation only
            float3x3 bias_rot = rotation_matrix(float3(0.0f, 1.0f, 0.0f), bias_angle);
            
            // apply rotation - use instance base position from transform
            float3 instance_pos = float3(transform[3].x, transform[3].y, transform[3].z);
            float3 offset       = position_world - instance_pos;
            position_world      = instance_pos + mul(bias_rot, offset);
            vertex.normal       = normalize(mul(bias_rot, vertex.normal));
            vertex.tangent      = normalize(mul(bias_rot, vertex.tangent));
        }
        
        // wind simulation
        if (surface.is_grass_blade() || surface.is_flower())
        {
            const float base_scale    = 0.032f;                              // spatial scale for noise sampling (higher = more waves)
            const float time_scale    = 0.06f * (1.0f + base_wind_magnitude); // animation speed scales with wind strength
            const float sway_amp      = base_wind_magnitude * 2.5f;          // maximum bend angle multiplier
            const float max_angle_deg = 75.0f;                               // maximum rotation angle to prevent ground intersection
            
            // height-based flexibility: stiffer at base, more flexible at tip
            MaterialParameters material = GetMaterial();
            float height_percent        = saturate(vertex.uv_misc.z / max(material.local_height, 0.001f));
            float height_factor         = pow(height_percent, 1.5f);
            
            // per-instance variation: subtle intensity multiplier (preserves wave patterns)
            float3 instance_pos  = float3(transform[3].x, transform[3].y, transform[3].z);
            float instance_var   = frac(dot(instance_pos.xz, float2(12.9898f, 78.233f))) * 0.3f + 0.85f; // 0.85 to 1.15
            
            // layered noise for natural sway: broad base pattern + faster gust layer
            float2 uv   = position_world.xz * base_scale + base_wind_dir.xz * time * time_scale;
            float sway  = noise_perlin(uv) * 0.7f;
            sway       += noise_perlin(uv * 1.5f + float2(time * 0.25f, 0.0f)) * 0.3f;
            
            // sharpen the wave transition for a more defined wave front
            sway = sign(sway) * pow(abs(sway), 0.55f);
            
            sway = sway * sway_amp * height_factor * instance_var;

            // wind direction variation for natural randomness
            float dir_var   = noise_perlin(position_world.xz * 0.016f + time * 0.025f) * (PI / 6.0f);
            float3 bend_dir = normalize(base_wind_dir + float3(sin(dir_var), 0.0f, cos(dir_var)));

            // calculate maximum allowed angle: ensure blade never goes below horizontal
            // angle between blade up direction and vertical must be less than max_angle_deg degrees
            static const float3 vertical      = float3(0.0f, 1.0f, 0.0f);
            float up_dot_vertical             = dot(instance_up, vertical);
            float current_angle_from_vertical = acos(saturate(up_dot_vertical));
            float max_allowed_angle           = (max_angle_deg * DEG_TO_RAD) - current_angle_from_vertical;
            max_allowed_angle                 = max(0.0f, max_allowed_angle);

            // rotate blade around axis perpendicular to wind direction
            // fallback to world right if instance_up is parallel to bend_dir
            float3 raw_axis       = cross(instance_up, bend_dir);
            float axis_length_sq  = dot(raw_axis, raw_axis);
            float3 axis           = axis_length_sq > 0.0001f ? raw_axis * rsqrt(axis_length_sq) : float3(1.0f, 0.0f, 0.0f);
            float angle           = sway * (50.0f * DEG_TO_RAD);
            
            // clamp angle to prevent ground intersection
            float angle_abs     = abs(angle);
            float clamped_angle = sign(angle) * min(angle_abs, max_allowed_angle);
            
            float3x3 rot = rotation_matrix(axis, clamped_angle);

            // apply rotation around instance base position
            float3 offset  = position_world - instance_pos;
            position_world = instance_pos + mul(rot, offset);
            vertex.normal  = normalize(mul(rot, vertex.normal));
            vertex.tangent = normalize(mul(rot, vertex.tangent));
        }
        else if (surface.has_wind_animation()) // tree branch/leaf wind sway
        {
            const float horizontal_amplitude = 0.15f; // maximum horizontal displacement
            const float vertical_amplitude   = 0.1f;  // maximum vertical displacement
            const float sway_frequency       = 1.5f;  // slow branch sway frequency
            const float bob_frequency        = 3.0f;  // faster leaf flutter frequency
            const float noise_scale          = 0.08f; // gust variation frequency
            const float flutter_variation    = 0.5f;  // leaf flutter intensity

            float height_factor = saturate(vertex.uv_misc.z); // 0 at trunk, 1 at branch tips

            // store original position before modifications for flutter calculation
            float2 position_original_xz = position_world.xz;

            // per-instance and per-vertex phase offsets for natural variation
            float instance_phase = float(instance_id) * 0.3f;
            float vertex_phase   = (position_world.x + position_world.z) * 0.05f;

            // horizontal sway: sine wave modulated by wind gusts
            float wind_phase    = time * sway_frequency + instance_phase;
            float horizontal_wave = sin(wind_phase + vertex_phase) * 0.5f;
    
            // apply gust modulation: low-frequency noise varies intensity 0.7x-1.0x
            float gust_noise = noise_perlin(float2(time * noise_scale, float(instance_id) * 0.1f));
            horizontal_wave *= (0.7f + 0.3f * gust_noise) * base_wind_magnitude;

            // apply horizontal displacement in wind direction, scaled by height
            float3 horizontal_dir    = normalize(float3(base_wind_dir.x, 0.0f, base_wind_dir.z) + float3(1e-6f, 0.0f, 1e-6f));
            float3 horizontal_offset = horizontal_dir * horizontal_wave * horizontal_amplitude * height_factor;
            position_world          += horizontal_offset;

            // vertical oscillation: independent up-down motion
            float bob_phase     = time * bob_frequency + instance_phase * 1.2f + vertex_phase * 2.0f;
            float vertical_wave = sin(bob_phase) * 0.6f;

            // add high-frequency flutter noise for leaf movement
            float flutter_noise = noise_perlin(position_original_xz * 5.0f + time * 2.0f);
            vertical_wave += flutter_noise * flutter_variation;

            // amplify vertical motion based on horizontal sway intensity
            vertical_wave *= (1.0f + 0.2f * base_wind_magnitude * abs(horizontal_wave));

            // apply vertical displacement, scaled by height
            float vertical_offset_y = vertical_wave * vertical_amplitude * height_factor * 0.8f;
            position_world.y       += vertical_offset_y;
            
            // update normals and tangents to reflect bending
            float3 vertical_offset = float3(0.0f, vertical_offset_y, 0.0f);
            float3 total_offset    = horizontal_offset + vertical_offset;
            
            // check squared length to prevent division by zero (NaN) when offset is near zero
            float offset_sq = dot(total_offset, total_offset);   
            if (offset_sq > 0.000001f)
            {
                float bend_amount = sqrt(offset_sq) * 0.5f * height_factor;
                float3 bend_dir   = total_offset * rsqrt(offset_sq); // optimized normalize
                
                vertex.normal    += bend_dir * bend_amount;
                vertex.normal     = normalize(vertex.normal);
                
                vertex.tangent   += bend_dir * bend_amount * 0.5f;
                vertex.tangent    = normalize(vertex.tangent);
            }
        }
    }
};

gbuffer_vertex transform_to_world_space(Vertex_PosUvNorTan input, uint instance_id, matrix transform, inout float3 position_world, inout float3 position_world_previous)
{
    MaterialParameters material = GetMaterial();
    Surface surface;
    surface.flags = material.flags;

    gbuffer_vertex vertex;
    vertex.uv_misc.w = instance_id;
    
    // compute UV with tiling and offset
    float2 uv = input.uv * material.tiling + material.offset;
    
    // apply UV inversion: mirror along axis if enabled
    float2 invert_mask = step(0.5f, material.invert_uv);
    uv                 = lerp(uv, 2.0f * floor(uv) + 1.0f - uv, invert_mask);
    vertex.uv_misc.xy  = uv;
    
    // compute width and height percent for grass blade positioning
    float width_percent  = saturate((input.position.x + material.local_width * 0.5f) / material.local_width);
    float height_percent = saturate(input.position.y / material.local_height);
    vertex.uv_misc.z     = height_percent;
    vertex.width_percent = width_percent;
    
    // compose instance transform and apply to base transform
    matrix instance = compose_instance_transform(input.instance_position_x, input.instance_position_y, input.instance_position_z, input.instance_normal_oct, input.instance_yaw, input.instance_scale);
    transform = mul(instance, transform);
    matrix transform_previous = mul(instance, pass_get_transform_previous());
    
    // transform position to world space
    float3 position          = mul(input.position, transform).xyz;
    float3 position_previous = mul(input.position, transform_previous).xyz;
    
    // transform normal and tangent to world space (extract 3x3 rotation/scale matrix)
    vertex.normal  = normalize(mul(input.normal, (float3x3)transform));
    vertex.tangent = normalize(mul(input.tangent, (float3x3)transform));

    // apply wind animation and other world-space effects
    // note: we need to save and restore vertex.normal/tangent because process_world_space modifies them
    // the second call is only for computing position_previous, we don't want to double-transform normals
    vertex_processing::process_world_space(surface, position, vertex, input.position.xyz, transform, instance_id, 0.0f);
    
    // save the correctly transformed normals before computing previous position
    float3 saved_normal  = vertex.normal;
    float3 saved_tangent = vertex.tangent;
    
    // compute previous position (this will incorrectly modify vertex.normal/tangent, but we'll restore them)
    vertex_processing::process_world_space(surface, position_previous, vertex, input.position.xyz, transform_previous, instance_id, -buffer_frame.delta_time);
    
    // restore the correct normals from the current frame
    vertex.normal  = saved_normal;
    vertex.tangent = saved_tangent;

    position_world          = position;
    position_world_previous = position_previous;
    return vertex;
}

gbuffer_vertex transform_to_clip_space(gbuffer_vertex vertex, float3 position, float3 position_previous)
{
    vertex.position          = mul(float4(position, 1.0f), buffer_frame.view_projection);
    vertex.position_previous = mul(float4(position_previous, 1.0f), buffer_frame.view_projection_previous);
    
    return vertex;
}
