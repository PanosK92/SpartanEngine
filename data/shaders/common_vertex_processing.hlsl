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
};

float4x4 compose_instance_transform(min16float instance_position_x, min16float instance_position_y, min16float instance_position_z, uint instance_normal_oct, uint instance_yaw, uint instance_scale)
{
    // compose position
    float3 instance_position = float3(instance_position_x, instance_position_y, instance_position_z);
    
    // check for identity
    if (!any(instance_position) && instance_normal_oct == 0 && instance_yaw == 0 && instance_scale == 0)
        return float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    
    // compose octahedral normal
    float x            = float(instance_normal_oct >> 8) / 255.0 * 2.0 - 1.0;
    float y            = float(instance_normal_oct & 0xFF) / 255.0 * 2.0 - 1.0;
    float3 n           = float3(x, y, 1.0 - abs(x) - abs(y));
    float mask         = step(0.0, n.z);
    float2 adjusted_xy = (float2(1.0, 1.0) - abs(n.yx)) * sign(n.xy);
    n.xy               = mask * n.xy + (1.0 - mask) * adjusted_xy;
    float3 normal      = normalize(n);
    
    // compose yaw and scale
    float yaw   = float(instance_yaw) / 255.0 * 6.28318530718; // pi_2
    float scale = exp2(lerp(-6.643856, 6.643856, float(instance_scale) / 255.0)); // log2(0.01) to log2(100)
    
    // compose quaternion
    float3 up           = float3(0, 1, 0);
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
    float cy        = cos(yaw * 0.5);
    float sy        = sin(yaw * 0.5);
    float4 quat_yaw = float4(0, sy, 0, cy);
    float4 q        = float4(
        quat.w * quat_yaw.x + quat.x * quat_yaw.w + quat.y * quat_yaw.z - quat.z * quat_yaw.y,
        quat.w * quat_yaw.y - quat.x * quat_yaw.z + quat.y * quat_yaw.w + quat.z * quat_yaw.x,
        quat.w * quat_yaw.z + quat.x * quat_yaw.y - quat.y * quat_yaw.x + quat.z * quat_yaw.w,
        quat.w * quat_yaw.w - quat.x * quat_yaw.x - quat.y * quat_yaw.y - quat.z * quat_yaw.z
    );
    
    // compose rotation matrix
    float xx = q.x * q.x;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float xw = q.x * q.w;
    float yy = q.y * q.y;
    float yz = q.y * q.z;
    float yw = q.y * q.w;
    float zz = q.z * q.z;
    float zw = q.z * q.w;
    float3x3 rotation = float3x3(
        1 - 2 * (yy + zz), 2 * (xy - zw), 2 * (xz + yw),
        2 * (xy + zw), 1 - 2 * (xx + zz), 2 * (yz - xw),
        2 * (xz - yw), 2 * (yz + xw), 1 - 2 * (xx + yy)
    );
    
    // compose final transform
    return float4x4(
        float4(rotation._11 * scale, rotation._12 * scale, rotation._13 * scale, 0),
        float4(rotation._21 * scale, rotation._22 * scale, rotation._23 * scale, 0),
        float4(rotation._31 * scale, rotation._32 * scale, rotation._33 * scale, 0),
        float4(instance_position, 1)
    );
}

float3 extract_position(matrix transform)
{
    return float3(transform._31, transform._32, transform._33);
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
        float time                        = (float)buffer_frame.time + time_offset;
        float3 wind                       = buffer_frame.wind;
        float3 base_wind_dir              = normalize(wind + float3(1e-6f, 0.0f, 1e-6f));
        float base_wind_magnitude         = length(wind);
        float scaled_gust_scale           = 0.01f * (1.0f + base_wind_magnitude);        // base slow, +mag for quicker gust cycles
        float scaled_direction_time_scale = 0.05f * (1.0f + base_wind_magnitude / 2.0f); // milder scale for direction to avoid chaos
        float3 instance_up                = normalize(transform[1].xyz);
        
        // wind simulation
        if (surface.is_grass_blade() || surface.is_flower())
        {
            float time         = (float) buffer_frame.time + time_offset;
            float3 wind        = buffer_frame.wind;
            float3 wind_dir    = normalize(wind + float3(1e-6f, 0.0f, 1e-6f));
            float wind_mag     = length(wind);

            // base params
            const float base_scale = 0.025f;                   // broad patterns
            const float time_scale = 0.1f * (1.0f + wind_mag); // speed up with mag
            const float sway_amp   = wind_mag * 3.0f;          // stronger bends at high wind

            // layered noise for sway (primary broad + secondary gust)
            float2 uv   = position_world.xz * base_scale + wind_dir.xz * time * time_scale;
            float sway  = noise_perlin(uv) * 0.7f;                                           // broad layer
            sway       += noise_perlin(uv * 2.0f + float2(time * 0.5f, 0.0f)) * 0.3f;        // gust layer
            sway        = sway * sway_amp * (vertex.uv_misc.z / GetMaterial().local_height); // height-based, remap to 0-1

            // bend dir with slight noise variation
            float dir_var   = noise_perlin(position_world.xz * 0.01f + time * 0.05f) * (PI / 6.0f);
            float3 bend_dir = normalize(wind_dir + float3(sin(dir_var), 0.0f, cos(dir_var)));

            // rotate around axis
            float3 axis  = normalize(cross(instance_up, bend_dir));
            float angle  = sway * (60.0f * DEG_TO_RAD);
            float3x3 rot = rotation_matrix(axis, angle);

             // apply to pos/normal/tangent
            float3 base_pos = position_world - position_local;
            position_world  = base_pos + mul(rot, position_world - base_pos);
            vertex.normal   = mul(rot, vertex.normal);
            vertex.tangent  = mul(rot, vertex.tangent);
        }
        else if (surface.has_wind_animation()) // tree branch/leaf wind sway
        {
            const float horizontal_amplitude = 0.15f; // max horizontal sway
            const float vertical_amplitude   = 0.1f;  // max vertical bob
            const float sway_frequency       = 1.5f;  // base frequency for slow branch sway
            const float bob_frequency        = 3.0f;  // faster frequency for leaf-like up-down flutter
            const float noise_scale          = 0.08f; // low-frequency noise for gusts/lulls
            const float flutter_variation    = 0.5f;  // subtle per-vertex flutter intensity

            float time          = (float)buffer_frame.time + time_offset;
            float height_factor = vertex.uv_misc.z; // 0 at base (trunk-attached), 1 at tip (more sway)

            // unique phase per instance/vertex for natural variation
            float instance_phase = float(instance_id) * 0.3f;
            float vertex_phase   = position_world.x * 0.05f + position_world.z * 0.05f; // spatial offset

            // horizontal sway: Wind-driven, sine-based bending in XZ plane
            float wind_phase      = time * sway_frequency + instance_phase;
            float horizontal_wave = sin(wind_phase + vertex_phase) * 0.5f; // base sine (range -0.5 to 0.5)
    
            // modulate with low-freq Perlin for gusts (tied to wind magnitude)
            float gust_noise = noise_perlin(float2(time * noise_scale, float(instance_id) * 0.1f));
            horizontal_wave *= (0.7f + 0.3f * gust_noise) * base_wind_magnitude; // varies 0.7x-1.0x base, scaled by wind

            // apply horizontal displacement (projected onto world XZ, scaled by height for attachment)
            float3 horizontal_dir     = float3(base_wind_dir.x, 0.0f, base_wind_dir.z); // ignore Y for pure horizontal
            float3 horizontal_offset  = horizontal_dir * horizontal_wave * horizontal_amplitude * height_factor;
            position_world           += horizontal_offset;

            // vertical oscillation: independent bob, with subtle wind influence for realism
            float bob_phase     = time * bob_frequency + instance_phase * 1.2f + vertex_phase * 2.0f; // slightly offset phase
            float vertical_wave = sin(bob_phase) * 0.6f; // asymmetric sine for more "drop" than "rise"

            // add high-freq flutter noise (localized, rapid leaf movement)
            float flutter_noise = noise_perlin(float2(position_world.xz * 5.0f + time * 2.0f));
            vertical_wave += flutter_noise * flutter_variation;

            // tie subtle wind influence to vertical (e.g., stronger wind = more pronounced bob)
            vertical_wave *= (1.0f + 0.2f * base_wind_magnitude * abs(horizontal_wave)); // amplify based on horizontal intensity

            // apply vertical displacement (pure Y, scaled by height but less aggressively for grounded feel)
            float3 vertical_offset  = float3(0.0f, vertical_wave * vertical_amplitude * height_factor * 0.8f, 0.0f);
            position_world         += vertical_offset;
            float bend_amount       = length(horizontal_offset + vertical_offset) * 0.5f * height_factor;
            float3 bend_dir         = normalize(horizontal_offset + vertical_offset * 0.5f); // bias toward horizontal
            vertex.normal          += bend_dir * bend_amount;
            vertex.normal           = normalize(vertex.normal);
            vertex.tangent         += bend_dir * bend_amount * 0.5f;
            vertex.tangent          = normalize(vertex.tangent);
        }
    }
};

gbuffer_vertex transform_to_world_space(Vertex_PosUvNorTan input, uint instance_id, matrix transform, inout float3 position_world, inout float3 position_world_previous)
{
    MaterialParameters material = GetMaterial();
    Surface surface;
    surface.flags = material.flags;

    // start building the vertex
    gbuffer_vertex vertex;
    vertex.uv_misc.w         = instance_id;
    vertex.uv_misc.xy        = float2(input.uv.x * material.tiling.x + material.offset.x, input.uv.y * material.tiling.y + material.offset.y);
    vertex.position          = 0.0f; // set to silence validation errors (in case it's never set later)
    vertex.position_previous = 0.0f; // set to silence validation errors (in case it's never set later)
    
    // compute width and height percent, they represent the position of the vertex relative to the grass blade
    float3 position_transform = extract_position(transform); // bottom of the grass blade
    float width_percent       = saturate((input.position.x + material.local_width * 0.5f) / material.local_width);
    float height_percent      = saturate(input.position.y / material.local_height);
    vertex.uv_misc.z          = height_percent;
    vertex.width_percent      = width_percent;
    
    // transform to world space
    matrix instance           = compose_instance_transform(input.instance_position_x, input.instance_position_y, input.instance_position_z, input.instance_normal_oct, input.instance_yaw, input.instance_scale);
    transform                 = mul(instance, transform);
    matrix transform_previous = mul(instance, pass_get_transform_previous());
    float3 position           = mul(input.position, transform).xyz;
    float3 position_previous  = mul(input.position, transform_previous).xyz;
    vertex.normal             = normalize(mul(input.normal, (float3x3)transform));
    vertex.tangent            = normalize(mul(input.tangent, (float3x3)transform));

    // process in world space
    vertex_processing::process_world_space(surface, position, vertex, input.position.xyz, transform, instance_id, 0.0f);
    vertex_processing::process_world_space(surface, position_previous, vertex, input.position.xyz, transform_previous, instance_id, -buffer_frame.delta_time);

    // out and return
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
