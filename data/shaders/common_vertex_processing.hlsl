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

// shared in-shader vertex container
// holds the 24-byte cpu vertex plus per-instance fields populated from the geometry_instances buffer in gpu-driven paths
// this struct must NOT be used as a vertex shader input parameter, the cpu input layout only supplies the 4 base attributes
// for cpu-driven entry points use Vertex_PosUvNorTan_Cpu and call to_full_vertex to fill the instance fields with zeros
struct Vertex_PosUvNorTan
{
    float3 position;
    uint   uv_packed;
    uint   normal_packed;
    uint   tangent_packed;
    // full float, half-precision here quantizes world positions to a ~1m lattice past a few
    // hundred meters from the origin and that snaps grass blades onto a visible doll-hair grid
    float instance_position_x;
    float instance_position_y;
    float instance_position_z;
    uint instance_normal_oct;
    uint instance_yaw;
    uint instance_scale;
};

// matches the engine's input layout for RHI_Vertex_Type::PosUvNorTan, 24 bytes
// uv/normal/tangent are R32_Uint and decoded in shader, the instance fields are not part of the input layout
struct Vertex_PosUvNorTan_Cpu
{
    float3 position       : POSITION;
    uint   uv_packed      : TEXCOORD;
    uint   normal_packed  : NORMAL;
    uint   tangent_packed : TANGENT;
};

// expands a cpu input to the in-shader vertex with zeroed instance fields, identity-instance is detected by compose_instance_transform
Vertex_PosUvNorTan to_full_vertex(Vertex_PosUvNorTan_Cpu cpu_input)
{
    Vertex_PosUvNorTan v;
    v.position            = cpu_input.position;
    v.uv_packed           = cpu_input.uv_packed;
    v.normal_packed       = cpu_input.normal_packed;
    v.tangent_packed      = cpu_input.tangent_packed;
    v.instance_position_x = 0.0f;
    v.instance_position_y = 0.0f;
    v.instance_position_z = 0.0f;
    v.instance_normal_oct = 0u;
    v.instance_yaw        = 0u;
    v.instance_scale      = 0u;
    return v;
}

Vertex_PosUvNorTan pull_vertex(uint vertex_id, uint instance_id, uint instance_offset)
{
    PulledVertex pulled = geometry_vertices[vertex_id];

    // slot 0 of the global instance pool is seeded with an identity instance, non-instanced renderables pass instance_offset=0 and read it
    PackedInstance pi = geometry_instances[instance_offset + instance_id];
    uint pos_x_h      = pi.pos_xy & 0xFFFF;
    uint pos_y_h      = (pi.pos_xy >> 16) & 0xFFFF;
    uint pos_z_h      = pi.pos_z_norm & 0xFFFF;
    uint nrm_oct      = (pi.pos_z_norm >> 16) & 0xFFFF;
    uint yaw_p        = pi.yaw_scale & 0xFF;
    uint scale_p      = (pi.yaw_scale >> 8) & 0xFF;

    Vertex_PosUvNorTan v;
    v.position            = pulled.position;
    v.uv_packed           = pulled.uv;
    v.normal_packed       = pulled.normal;
    v.tangent_packed      = pulled.tangent;
    v.instance_position_x = f16tof32(pos_x_h);
    v.instance_position_y = f16tof32(pos_y_h);
    v.instance_position_z = f16tof32(pos_z_h);
    v.instance_normal_oct = nrm_oct;
    v.instance_yaw        = yaw_p;
    v.instance_scale      = scale_p;

    return v;
}

// gpu-driven path entry, populates _draw and the meshlet handle from the visible triangle list
// vertex_id is sv_vertexid for a non-instanced indirect draw, vertex_count = visible_triangle_count * 3
Vertex_PosUvNorTan pull_visible_triangle_vertex(uint vertex_id, out MeshletInstance mi_out)
{
    uint triangle_slot = vertex_id / 3u;
    uint corner        = vertex_id - triangle_slot * 3u;

    uint packed       = visible_triangles[triangle_slot];
    uint mi_idx       = VISIBLE_TRI_MI(packed);
    uint triangle_idx = VISIBLE_TRI_IDX(packed);

    mi_out = meshlet_instances[mi_idx];
    _draw  = indirect_draw_data[mi_out.draw_index];

    MeshletBounds mb      = meshlet_bounds[mi_out.meshlet_index];
    uint global_index_pos = _draw.lod_first_index + meshlet_decode_first_index(mb) + triangle_idx * 3u + corner;
    uint local_vertex_id  = geometry_indices[global_index_pos];
    uint global_vertex_id = local_vertex_id + _draw.lod_vertex_offset;

    return pull_vertex(global_vertex_id, mi_out.instance_index, _draw.instance_offset);
}

// vertex buffer output
struct gbuffer_vertex
{
    precise float4 position  : SV_POSITION;
    float4 position_previous : POS_CLIP_PREVIOUS;
    float3 normal            : NORMAL_WORLD;
    float3 tangent           : TANGENT_WORLD;
    float4 uv_misc           : TEXCOORD;  // xy = uv, z = height_percent, w = instance_id - packed together to reduced the interpolators (shader registers) the gpu needs to track
    float width_percent      : TEXCOORD2; // temp, will remove
    nointerpolation uint material_index : TEXCOORD3; // for indirect draws, material index passed from vs
    nointerpolation uint view_id        : TEXCOORD4; // multiview eye index (0 = left, 1 = right)
    // per-renderable uv transform passed through for the pixel shader's world-space-uv path,
    // nointerpolation since these are constant per draw
    nointerpolation float4 uv_xform_ts  : TEXCOORD5; // xy = tiling, zw = offset
    nointerpolation float4 uv_xform_ir  : TEXCOORD6; // xy = invert, z = rotation, w = unused
    float2 ocean_world_xz               : TEXCOORD7; // undisplaced clipmap world xz, fft normal/foam are indexed in this domain
};

float4x4 compose_instance_transform(float instance_position_x, float instance_position_y, float instance_position_z, uint instance_normal_oct, uint instance_yaw, uint instance_scale)
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

// helper for non-vs paths (compute culling, etc.) that need the same per-instance world transform the vs builds
// the early-out on instance_offset == 0 keeps non-instanced draws out of the geometry_instances read entirely
// the body avoids the min16float param chain compose_instance_transform uses, that path is vs-specific
float4x4 pull_instance_transform(uint instance_offset, uint instance_id)
{
    if (instance_offset == 0u)
        return float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

    PackedInstance pi = geometry_instances[instance_offset + instance_id];
    uint pos_x_h      = pi.pos_xy & 0xFFFF;
    uint pos_y_h      = (pi.pos_xy >> 16) & 0xFFFF;
    uint pos_z_h      = pi.pos_z_norm & 0xFFFF;
    uint nrm_oct      = (pi.pos_z_norm >> 16) & 0xFFFF;
    uint yaw_p        = pi.yaw_scale & 0xFF;
    uint scale_p      = (pi.yaw_scale >> 8) & 0xFF;

    float3 instance_position = float3(f16tof32(pos_x_h), f16tof32(pos_y_h), f16tof32(pos_z_h));

    if (dot(instance_position, instance_position) < 1e-10f && nrm_oct == 0u && yaw_p == 0u && scale_p == 0u)
        return float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

    static const float rcp_255 = 1.0f / 255.0f;
    float x            = (float(nrm_oct >> 8) * rcp_255) * 2.0f - 1.0f;
    float y            = (float(nrm_oct & 0xFFu) * rcp_255) * 2.0f - 1.0f;
    float3 n           = float3(x, y, 1.0f - abs(x) - abs(y));
    float mask         = step(0.0f, n.z);
    float2 adjusted_xy = (float2(1.0f, 1.0f) - abs(n.yx)) * sign(n.xy);
    n.xy               = mask * n.xy + (1.0f - mask) * adjusted_xy;
    float3 normal      = normalize(n);

    static const float pi_2           = 6.28318530718f;
    static const float scale_min_log2 = -6.643856f; // log2(0.01)
    static const float scale_max_log2 =  6.643856f; // log2(100)
    float yaw   = float(yaw_p) * rcp_255 * pi_2;
    float scale = exp2(lerp(scale_min_log2, scale_max_log2, float(scale_p) * rcp_255));

    static const float3 up = float3(0, 1, 0);
    float up_dot_normal    = dot(up, normal);
    float4 quat;
    if (abs(up_dot_normal) >= 0.999999f)
    {
        quat = up_dot_normal > 0.0f ? float4(0, 0, 0, 1) : float4(1, 0, 0, 0);
    }
    else
    {
        float s = sqrt(2.0f + 2.0f * up_dot_normal);
        quat    = float4(cross(up, normal) / s, s * 0.5f);
    }
    float yaw_half  = -yaw * 0.5f;
    float cy        = cos(yaw_half);
    float sy        = sin(yaw_half);
    float4 quat_yaw = float4(0.0f, sy, 0.0f, cy);

    float qx = quat.w * quat_yaw.x + quat.x * quat_yaw.w + quat.y * quat_yaw.z - quat.z * quat_yaw.y;
    float qy = quat.w * quat_yaw.y - quat.x * quat_yaw.z + quat.y * quat_yaw.w + quat.z * quat_yaw.x;
    float qz = quat.w * quat_yaw.z + quat.x * quat_yaw.y - quat.y * quat_yaw.x + quat.z * quat_yaw.w;
    float qw = quat.w * quat_yaw.w - quat.x * quat_yaw.x - quat.y * quat_yaw.y - quat.z * quat_yaw.z;

    float xx = qx * qx;
    float xy = qx * qy;
    float xz = qx * qz;
    float xw = qx * qw;
    float yy = qy * qy;
    float yz = qy * qz;
    float yw = qy * qw;
    float zz = qz * qz;
    float zw = qz * qw;

    return float4x4(
        float4((1.0f - 2.0f * (yy + zz)) * scale, 2.0f * (xy + zw) * scale, 2.0f * (xz - yw) * scale, 0.0f),
        float4(2.0f * (xy - zw) * scale, (1.0f - 2.0f * (xx + zz)) * scale, 2.0f * (yz + xw) * scale, 0.0f),
        float4(2.0f * (xz + yw) * scale, 2.0f * (yz - xw) * scale, (1.0f - 2.0f * (xx + yy)) * scale, 0.0f),
        float4(instance_position, 1.0f)
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

// world-space tile period for the baked wind field, must match the artistic intent for gust scale
// every wind_world_period meters the texture wraps once, smaller values give smaller, more chaotic gusts
static const float wind_world_period = 80.0f;

// shared wind sample for grass, flowers, and trees
// reading from the once-per-frame baked wind_field texture: rg = flow vector, b = gust pressure, a = micro turbulence
struct wind_sample
{
    float3 bend_dir_world; // unit vector in world xz plane the surface should bend toward
    float  bend_strength;  // dimensionless bend amplitude, scales with wind magnitude and local gust pressure
    float  gust;           // raw gust pressure 0..1, useful for non-rotational displacement
    float  micro;          // signed high-frequency jitter, [-0.5, 0.5]
};

wind_sample evaluate_wind(float3 world_position)
{
    float3 wind_world = buffer_frame.wind;
    float  wind_mag   = length(float2(wind_world.x, wind_world.z));
    float2 wind_dir   = wind_mag > 1e-4f ? float2(wind_world.x, wind_world.z) / wind_mag : float2(0.0f, 1.0f);

    float2 uv     = world_position.xz * (1.0f / wind_world_period);
    float4 wf     = tex_wind_field.SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), uv, 0);

    // bias the bend direction with the local flow vector so the field is not purely along the macro wind
    float2 dir_xz = wind_dir + wf.rg * 0.55f;
    float  dlen   = length(dir_xz);
    dir_xz        = dlen > 1e-4f ? dir_xz / dlen : wind_dir;

    wind_sample s;
    s.bend_dir_world = float3(dir_xz.x, 0.0f, dir_xz.y);
    s.bend_strength  = (0.20f + 0.80f * wf.b) * wind_mag;
    s.gust           = wf.b;
    s.micro          = wf.a - 0.5f;
    return s;
}

// per-instance phase + natural frequency, derived from world-space base position
// keeps blades from moving in lockstep, deterministic so motion vectors stay correct
float2 wind_instance_phase_freq(float3 instance_pos)
{
    float h = frac(sin(dot(instance_pos, float3(12.9898f, 78.233f, 37.719f))) * 43758.5453f);
    return float2(h * PI2, 2.0f + frac(h * 17.13f) * 1.5f);
}

struct vertex_processing
{
    static void process_world_space(Surface surface, inout float3 position_world, inout gbuffer_vertex vertex, float3 position_local, float4x4 transform, uint instance_id, float time_offset)
    {
        float  time          = (float)buffer_frame.time + time_offset;
        float3 instance_up   = normalize(transform[1].xyz);
        float3 instance_pos  = float3(transform[3].x, transform[3].y, transform[3].z);

        // fft ocean, displace the camera-centered clipmap by the summed cascade displacement
        if (surface.is_water() && buffer_frame.ocean_enabled > 0.5f)
        {
            float2 world_xz = position_world.xz;
            float3 disp     = 0.0f;
            uint cascades   = buffer_frame.ocean_cascade_count;
            [loop] for (uint c = 0; c < cascades; ++c)
            {
                float L   = buffer_frame.ocean_cascade_length[c];
                float2 uv = world_xz / L;
                disp     += tex_ocean_displacement.SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), float3(uv, (float)c), 0.0f).xyz;
            }
            position_world += disp;
            return;
        }

        // camera-facing bias for grass (ghost of tsushima technique)
        // rotates blades partially toward camera when edge-on to maintain visual density
        if (surface.is_grass_blade())
        {
            const float camera_bias_strength = 0.7f;

            float3 to_camera    = buffer_frame.camera_position - position_world;
            float3 to_camera_xz = normalize(float3(to_camera.x, 0.0f, to_camera.z));

            float3 blade_normal    = normalize(transform[2].xyz);
            float3 blade_normal_xz = normalize(float3(blade_normal.x, 0.0f, blade_normal.z) + float3(1e-6f, 0.0f, 1e-6f));

            float facing_dot  = abs(dot(blade_normal_xz, to_camera_xz));
            float edge_factor = 1.0f - facing_dot;
            float bias_amount = edge_factor * edge_factor * camera_bias_strength;

            float3 cross_result   = cross(blade_normal_xz, to_camera_xz);
            float  rotation_sign  = sign(cross_result.y);
            float  angle_to_camera = acos(saturate(facing_dot));
            float  bias_angle     = angle_to_camera * bias_amount * rotation_sign;

            float3x3 bias_rot = rotation_matrix(float3(0.0f, 1.0f, 0.0f), bias_angle);

            float3 offset  = position_world - instance_pos;
            position_world = instance_pos + mul(bias_rot, offset);
            vertex.normal  = normalize(mul(bias_rot, vertex.normal));
            vertex.tangent = normalize(mul(bias_rot, vertex.tangent));
        }

        // grass and flower wind: cantilever bend with per-instance spring response
        if (surface.is_grass_blade() || surface.is_flower())
        {
            wind_sample ws = evaluate_wind(position_world);

            // height fraction along the blade, base = 0, tip = 1
            float h          = saturate(vertex.uv_misc.z);
            float h_cantilever = pow(h, 1.5f); // stiffer at the base than a linear taper

            // per-instance natural frequency and phase, two oscillation modes for soft overshoot
            float2 inst          = wind_instance_phase_freq(instance_pos);
            float  instance_phase = inst.x;
            float  nat_freq       = inst.y;
            float  spring         = sin(time * nat_freq + instance_phase) * 0.18f
                                  + sin(time * nat_freq * 2.3f + instance_phase * 1.7f) * 0.07f;

            // peak bend angle of 60 deg, modulated by the per-instance spring and a subtle micro jitter
            float bend_amp     = ws.bend_strength * (1.0f + spring * 0.45f);
            float micro_jitter = ws.micro * 0.10f;
            float angle        = (bend_amp * (60.0f * DEG_TO_RAD) + micro_jitter) * h_cantilever;

            // never let the blade rotate below horizontal
            static const float3 vertical              = float3(0.0f, 1.0f, 0.0f);
            float current_angle_from_vertical         = acos(saturate(dot(instance_up, vertical)));
            float max_allowed_angle                   = max(0.0f, (75.0f * DEG_TO_RAD) - current_angle_from_vertical);
            angle                                     = sign(angle) * min(abs(angle), max_allowed_angle);

            // rotation axis perpendicular to bend direction in the horizontal plane
            float3 raw_axis      = cross(instance_up, ws.bend_dir_world);
            float  axis_length_sq = dot(raw_axis, raw_axis);
            float3 axis           = axis_length_sq > 0.0001f ? raw_axis * rsqrt(axis_length_sq) : float3(1.0f, 0.0f, 0.0f);

            // each vertex rotates by angle * h^1.5, so the base stays put and the tip sweeps the full angle
            // produces the cantilever silhouette without per-vertex pivot bookkeeping
            float3x3 rot   = rotation_matrix(axis, angle);
            float3   offset = position_world - instance_pos;
            position_world  = instance_pos + mul(rot, offset);
            vertex.normal   = normalize(mul(rot, vertex.normal));
            vertex.tangent  = normalize(mul(rot, vertex.tangent));
        }
        else if (surface.has_wind_animation()) // tree branch / leaf wind sway, fed by the same wind field
        {
            const float branch_amplitude = 0.18f;
            const float leaf_amplitude   = 0.10f;
            const float flutter_strength = 0.40f;

            wind_sample ws = evaluate_wind(position_world);

            float h = saturate(vertex.uv_misc.z); // 0 at trunk, 1 at branch tips

            // per-instance phase and frequency, branches sway slower than grass blades
            float2 inst          = wind_instance_phase_freq(instance_pos);
            float  instance_phase = inst.x;
            float  branch_freq    = inst.y * 0.55f;

            // gusts modulate the branch sway, sin gives it a soft heartbeat-like response
            float gust_pulse        = sin(time * branch_freq + instance_phase) * 0.5f + 0.5f;
            float horizontal_wave   = ws.bend_strength * (0.65f + 0.35f * gust_pulse);
            float3 horizontal_offset = ws.bend_dir_world * horizontal_wave * branch_amplitude * h;
            position_world          += horizontal_offset;

            // leaf flutter, faster than the branch sway, driven by the micro channel for spatial variation
            float flutter_freq      = 3.5f + inst.y;
            float vertical_wave     = sin(time * flutter_freq + instance_phase * 1.2f) * 0.6f
                                    + ws.micro * 2.0f * flutter_strength;
            float vertical_offset_y = vertical_wave * leaf_amplitude * h;
            position_world.y       += vertical_offset_y;

            // bend the normals and tangents to roughly match the displacement, scaled by height for canopy detail
            float3 vertical_offset = float3(0.0f, vertical_offset_y, 0.0f);
            float3 total_offset    = horizontal_offset + vertical_offset;

            float offset_sq = dot(total_offset, total_offset);
            if (offset_sq > 0.000001f)
            {
                float  bend_amount = sqrt(offset_sq) * 0.5f * h;
                float3 bend_dir    = total_offset * rsqrt(offset_sq);

                vertex.normal  = normalize(vertex.normal  + bend_dir * bend_amount);
                vertex.tangent = normalize(vertex.tangent + bend_dir * bend_amount * 0.5f);
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

    // decode packed vertex attributes
    float2 input_uv      = unpack_vertex_uv(input.uv_packed);
    float3 input_normal  = unpack_vertex_oct(input.normal_packed);
    float3 input_tangent = unpack_vertex_oct(input.tangent_packed);

    // uv state now lives on the per-renderable draw data, so multiple renderables can share a material
    float2 uv_tiling      = _draw.uv_tiling;
    float2 uv_offset      = _draw.uv_offset;
    float2 uv_invert      = _draw.uv_invert;
    float  uv_rotation    = _draw.uv_rotation;
    float  uv_world_space = _draw.uv_world_space;

    // forward to the pixel shader, ir.w carries the world_space_uv flag
    vertex.uv_xform_ts = float4(uv_tiling, uv_offset);
    vertex.uv_xform_ir = float4(uv_invert, uv_rotation, uv_world_space);

    // compute uv with tiling and offset
    float2 uv = input_uv * uv_tiling + uv_offset;

    // apply uv inversion, mirror along axis if enabled
    float2 invert_mask = step(0.5f, uv_invert);
    uv                 = lerp(uv, 2.0f * floor(uv) + 1.0f - uv, invert_mask);

    // apply 90 degree rotation increments
    if (uv_rotation != 0.0f)
        uv = rotate_uv_90(uv, uv_rotation);

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
    float4 position_local    = float4(input.position, 1.0f);
    float3 position          = mul(position_local, transform).xyz;
    float3 position_previous = mul(position_local, transform_previous).xyz;
    
    // transform normal and tangent to world space (extract 3x3 rotation/scale matrix)
    vertex.normal  = normalize(mul(input_normal,  (float3x3)transform));
    vertex.tangent = normalize(mul(input_tangent, (float3x3)transform));

    // capture the undisplaced world xz before the ocean displacement shifts it, the fft normal and foam are indexed in this domain
    vertex.ocean_world_xz = position.xz;

    // apply wind animation and other world-space effects
    // note: we need to save and restore vertex.normal/tangent because process_world_space modifies them
    // the second call is only for computing position_previous, we don't want to double-transform normals
    vertex_processing::process_world_space(surface, position, vertex, input.position, transform, instance_id, 0.0f);
    
    // save the correctly transformed normals before computing previous position
    float3 saved_normal  = vertex.normal;
    float3 saved_tangent = vertex.tangent;
    
    // compute previous position (this will incorrectly modify vertex.normal/tangent, but we'll restore them)
    vertex_processing::process_world_space(surface, position_previous, vertex, input.position, transform_previous, instance_id, -buffer_frame.delta_time);
    
    // restore the correct normals from the current frame
    vertex.normal  = saved_normal;
    vertex.tangent = saved_tangent;

    position_world          = position;
    position_world_previous = position_previous;
    return vertex;
}

gbuffer_vertex transform_to_clip_space(gbuffer_vertex vertex, float3 position, float3 position_previous, uint view_id = 0)
{
    vertex.view_id = view_id;

    // select per-eye matrices when rendering in multiview stereo
    matrix vp      = (buffer_frame.is_multiview && view_id == 1) ? buffer_frame.view_projection_right           : buffer_frame.view_projection;
    matrix vp_prev = (buffer_frame.is_multiview && view_id == 1) ? buffer_frame.view_projection_previous_right  : buffer_frame.view_projection_previous;

    vertex.position          = mul(float4(position, 1.0f), vp);
    vertex.position_previous = mul(float4(position_previous, 1.0f), vp_prev);
    
    return vertex;
}
