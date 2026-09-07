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
#if defined(GRASS_INSTANCED)
#include "grass_body.hlsl"
#endif
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
    float4x4 instance_transform;
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
    v.instance_transform = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    return v;
}

// CPU mesh instances retain independent scales and a high precision quaternion. The compact
// procedural grass format remains separate. Rendering, depth and culling all use this decoder.
float4x4 compose_packed_instance(PackedInstance instance)
{
    float4 q = normalize(float4(
        (int(instance.rotation_xy << 16) >> 16), (int(instance.rotation_xy) >> 16),
        (int(instance.rotation_zw << 16) >> 16), (int(instance.rotation_zw) >> 16)) / 32767.0f);
    float xx = q.x * q.x, xy = q.x * q.y, xz = q.x * q.z, xw = q.x * q.w;
    float yy = q.y * q.y, yz = q.y * q.z, yw = q.y * q.w;
    float zz = q.z * q.z, zw = q.z * q.w;
    return float4x4(
        float4(float3(1.0f - 2.0f * (yy + zz), 2.0f * (xy + zw), 2.0f * (xz - yw)) * instance.scale_x, 0),
        float4(float3(2.0f * (xy - zw), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + xw)) * instance.scale_y, 0),
        float4(float3(2.0f * (xz + yw), 2.0f * (yz - xw), 1.0f - 2.0f * (xx + yy)) * instance.scale_z, 0),
        float4(instance.pos_x, instance.pos_y, instance.pos_z, 1));
}

Vertex_PosUvNorTan pull_vertex(uint vertex_id, uint instance_id, uint instance_offset)
{
    PulledVertex pulled = geometry_vertices[vertex_id];

    // slot 0 of the global instance pool is seeded with an identity instance, non-instanced renderables pass instance_offset=0 and read it
    PackedInstance pi = geometry_instances[instance_offset + instance_id];

    Vertex_PosUvNorTan v;
    v.position            = pulled.position;
    v.uv_packed           = pulled.uv;
    v.normal_packed       = pulled.normal;
    v.tangent_packed      = pulled.tangent;
    v.instance_transform = compose_packed_instance(pi);

    return v;
}

// gpu-driven path entry, populates _draw and the meshlet handle from the visible triangle list
// vertex_id is sv_vertexid for a non-instanced indirect draw, vertex_count = visible_triangle_count * 3
// f4_value.x carries the region base, 0 for the opaque half and the half capacity for the alpha half,
// both draws issue with first_vertex 0 so the base must be added here rather than relying on sv_vertexid (api dependent)
Vertex_PosUvNorTan pull_visible_triangle_vertex(uint vertex_id, out MeshletInstance mi_out)
{
    uint local_triangle = vertex_id / 3u;
    uint corner         = vertex_id - local_triangle * 3u;
    uint triangle_slot  = (uint)pass_get_f4_value().x + local_triangle;

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
    float4 uv_misc           : TEXCOORD;  // xy uv, z height percent, w instance data
    float width_percent      : TEXCOORD2; // temp, will remove
    nointerpolation uint material_index : TEXCOORD3; // for indirect draws, material index passed from vs
    nointerpolation uint view_id        : TEXCOORD4; // multiview eye index (0 = left, 1 = right)
    // per-renderable uv transform passed through for the pixel shader's world-space-uv path,
    // nointerpolation since these are constant per draw
    nointerpolation float4 uv_xform_ts  : TEXCOORD5; // xy = tiling, zw = offset
    nointerpolation float4 uv_xform_ir  : TEXCOORD6; // xy = invert, z = rotation, w = unused
    nointerpolation uint draw_flags    : TEXCOORD8;
    float2 ocean_world_xz               : TEXCOORD7; // undisplaced clipmap world xz, fft normal/foam are indexed in this domain
};

// The indirect path exports one draw index instead of repeating immutable UV
// transforms, material index and flags at every vertex. Pixel invocations fetch
// those fields from the same draw record already used by geometry processing.
struct gbuffer_indirect_vertex
{
    precise float4 position  : SV_POSITION;
    float4 position_previous : POS_CLIP_PREVIOUS;
    float3 normal            : NORMAL_WORLD;
    float3 tangent           : TANGENT_WORLD;
    float4 uv_misc           : TEXCOORD;
    float width_percent      : TEXCOORD2;
    nointerpolation uint draw_index : TEXCOORD3;
    nointerpolation uint view_id    : TEXCOORD4;
    float2 ocean_world_xz     : TEXCOORD7;
};

gbuffer_indirect_vertex pack_gbuffer_indirect(gbuffer_vertex vertex, uint draw_index)
{
    gbuffer_indirect_vertex packed;
    packed.position = vertex.position;
    packed.position_previous = vertex.position_previous;
    packed.normal = vertex.normal;
    packed.tangent = vertex.tangent;
    packed.uv_misc = vertex.uv_misc;
    packed.width_percent = vertex.width_percent;
    packed.draw_index = draw_index;
    packed.view_id = vertex.view_id;
    packed.ocean_world_xz = vertex.ocean_world_xz;
    return packed;
}

gbuffer_vertex unpack_gbuffer_indirect(gbuffer_indirect_vertex packed)
{
    _draw = indirect_draw_data[packed.draw_index];
    gbuffer_vertex vertex;
    vertex.position = packed.position;
    vertex.position_previous = packed.position_previous;
    vertex.normal = packed.normal;
    vertex.tangent = packed.tangent;
    vertex.uv_misc = packed.uv_misc;
    vertex.width_percent = packed.width_percent;
    vertex.material_index = _draw.material_index;
    vertex.view_id = packed.view_id;
    vertex.ocean_world_xz = packed.ocean_world_xz;
    vertex.uv_xform_ts = float4(_draw.uv_tiling, _draw.uv_offset);
    vertex.uv_xform_ir = float4(_draw.uv_invert, _draw.uv_rotation, _draw.uv_world_space);
    vertex.draw_flags = _draw.flags;
    return vertex;
}

// slim mesh-shader depth payload, opaque prepass uses position only via depth_mesh_position
struct depth_mesh_position
{
    precise float4 position : SV_POSITION;
};

struct depth_mesh_vertex
{
    precise float4 position             : SV_POSITION;
    float4 uv_misc                      : TEXCOORD;
    nointerpolation uint material_index : TEXCOORD3;
    nointerpolation uint view_id        : TEXCOORD4;
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

// Culling must use exactly the same transform as the visible and depth geometry.
float4x4 pull_instance_transform(uint instance_offset, uint instance_id)
{
    if (instance_offset == 0u)
        return float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    return compose_packed_instance(geometry_instances[instance_offset + instance_id]);
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
static const float wind_flow_uv_per_second  = 0.03f;
static const float wind_gust_uv_per_second  = 0.065f;
static const float wind_micro_uv_per_second = 0.12f;

// shared wind sample for grass, flowers, and trees
// reading from the once-per-frame baked wind_field texture: rg = flow vector, b = gust pressure, a = micro turbulence
struct wind_sample
{
    float3 bend_dir_world; // unit vector in world xz plane the surface should bend toward
    float  bend_strength;  // dimensionless bend amplitude, scales with wind magnitude and local gust pressure
    float  gust;           // raw gust pressure 0..1, useful for non-rotational displacement
    float  micro;          // signed high-frequency jitter, [-0.5, 0.5]
};

wind_sample evaluate_wind(
    float3 world_position,
    float time_offset
)
{
    float3 wind_world = buffer_frame.wind;
    float  wind_mag   = length(float2(wind_world.x, wind_world.z));
    float2 wind_dir   = wind_mag > 1e-4f ? float2(wind_world.x, wind_world.z) / wind_mag : float2(0.0f, 1.0f);

    float2 uv = world_position.xz * (1.0f / wind_world_period);
    float4 wf = tex_wind_field.SampleLevel(
        GET_SAMPLER(sampler_bilinear_wrap),
        uv,
        0
    );
    if (time_offset < 0.0f)
    {
        float history_delta = -time_offset;
        float2 flow_uv = uv +
            wind_dir *
            history_delta *
            wind_flow_uv_per_second;
        float2 gust_uv = uv +
            wind_dir *
            history_delta *
            wind_gust_uv_per_second;
        float2 micro_uv = uv -
            float2(0.31f, -0.27f) *
            history_delta *
            wind_micro_uv_per_second;

        wf.rg = tex_wind_field.SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap),
            flow_uv,
            0
        ).rg;
        wf.b = tex_wind_field.SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap),
            gust_uv,
            0
        ).b;
        wf.a = tex_wind_field.SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap),
            micro_uv,
            0
        ).a;
    }

    // bias the bend direction with the local flow vector so the field is not purely along the macro wind
    float2 dir_xz = wind_dir + wf.rg * 0.55f;
    float  dlen   = length(dir_xz);
    dir_xz        = dlen > 1e-4f ? dir_xz / dlen : wind_dir;

    wind_sample s;
    s.bend_dir_world = float3(dir_xz.x, 0.0f, dir_xz.y);
    float ambient_pressure = 0.055f;
    float gust_pressure = smoothstep(
        0.08f,
        0.55f,
        wf.b
    );
    float wind_response = saturate(wind_mag * 0.10f);
    s.bend_strength = wind_response * (
        ambient_pressure +
        gust_pressure * 1.35f
    );
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

// Root wind is identical for every vertex in one meshlet. Mesh shaders cache
// both time samples; vertex-pull and shadow paths use the same evaluator.
struct TreeWindState
{
    wind_sample wind;
    float2 phase_freq;
    float3 axis;
    float response;
    float trunk_drive;
};

TreeWindState evaluate_tree_wind(float4x4 transform, float time_offset)
{
    TreeWindState state;
    float3 root = transform[3].xyz;
    float3 up = normalize(transform[1].xyz);
    state.wind = evaluate_wind(root, time_offset);
    state.phase_freq = wind_instance_phase_freq(root);
    state.response = saturate(length(buffer_frame.wind.xz) * 0.10f);
    float3 raw_axis = cross(up, state.wind.bend_dir_world);
    float axis_length_sq = dot(raw_axis, raw_axis);
    state.axis = axis_length_sq > 1e-8f ? raw_axis * rsqrt(axis_length_sq) : normalize(transform[0].xyz);
    float slow_time = ((float)buffer_frame.time + time_offset) * (0.32f + state.phase_freq.y * 0.065f);
    float sway = sin(slow_time + state.phase_freq.x) * 0.55f
               + sin(slow_time * 1.37f + state.phase_freq.x * 2.17f) * 0.30f
               + sin(slow_time * 0.73f + state.phase_freq.x * 0.61f) * 0.15f;
    state.trunk_drive = state.response * (0.8f + 0.45f * sway + 0.7f * state.wind.gust) * DEG_TO_RAD;
    return state;
}

CachedTreeWind pack_tree_wind(TreeWindState current, TreeWindState previous)
{
    CachedTreeWind cached;
    cached.current_axis_drive = float4(current.axis, current.trunk_drive);
    cached.current_phase_gust_micro = float4(current.phase_freq, current.wind.gust, current.wind.micro);
    cached.previous_axis_drive = float4(previous.axis, previous.trunk_drive);
    cached.previous_phase_gust_micro = float4(previous.phase_freq, previous.wind.gust, previous.wind.micro);
    return cached;
}

TreeWindState unpack_tree_wind(float4 axis_drive, float4 phase_gust_micro)
{
    TreeWindState state = (TreeWindState)0;
    state.axis = axis_drive.xyz;
    state.trunk_drive = axis_drive.w;
    state.phase_freq = phase_gust_micro.xy;
    state.wind.gust = phase_gust_micro.z;
    state.wind.micro = phase_gust_micro.w;
    return state;
}

#ifdef CACHE_MESH_TREE_WIND
groupshared TreeWindState mesh_tree_wind_current;
groupshared TreeWindState mesh_tree_wind_previous;
void cache_mesh_tree_wind(DrawData draw, uint instance_index, uint cache_slot_plus_one)
{
    if ((material_parameters[draw.material_index].flags & (1u << 9)) != 0u)
    {
        if (cache_slot_plus_one != 0u)
        {
            CachedTreeWind cached = tree_wind_cache[cache_slot_plus_one - 1u];
            mesh_tree_wind_current = unpack_tree_wind(cached.current_axis_drive, cached.current_phase_gust_micro);
#ifndef CACHE_MESH_TREE_WIND_CURRENT_ONLY
            mesh_tree_wind_previous = unpack_tree_wind(cached.previous_axis_drive, cached.previous_phase_gust_micro);
#endif
            return;
        }
        float4x4 instance = pull_instance_transform(draw.instance_offset, instance_index);
        mesh_tree_wind_current = evaluate_tree_wind(mul(instance, draw.transform), 0.0f);
#ifndef CACHE_MESH_TREE_WIND_CURRENT_ONLY
        mesh_tree_wind_previous = evaluate_tree_wind(mul(instance, draw.transform_previous), -buffer_frame.delta_time);
#endif
    }
}
#endif


struct vertex_processing
{
    static void process_world_space(Surface surface, inout float3 position_world, inout gbuffer_vertex vertex, float3 position_local, float4x4 transform, uint instance_id, float time_offset)
    {
        float  time          = (float)buffer_frame.time + time_offset;
        float3 instance_up   = normalize(transform[1].xyz);
        float3 instance_pos  = float3(transform[3].x, transform[3].y, transform[3].z);

        float3 grass_press = 0.0f;
#ifdef GRASS_INSTANCED
        if (surface.is_grass_blade())
        {
            float4 mapping = time_offset < 0.0f ? buffer_pass.values[2] : buffer_pass.values[1];
            float2 uv = (instance_pos.xz - mapping.xy) * mapping.z;
            if (mapping.w > 0.5f && all(uv > 0.0f) && all(uv < 1.0f))
            {
                float4 pressed;
                if (time_offset < 0.0f)
                    pressed = tex2.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, 0);
                else
                    pressed = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, 0);
                // Premultiplied height stays correct when filtering against empty texels.
                float height_weight = pressed.z > 0.0001f ?
                    1.0f - smoothstep(0.25f, 0.65f, abs(pressed.w / pressed.z - instance_pos.y)) : 0.0f;
                grass_press = pressed.xyz * height_weight;
            }
        }
#endif

        // fft ocean, displace the camera-centered clipmap by the summed cascade displacement
        if (surface.is_water() && buffer_frame.ocean_enabled > 0.5f)
        {
            float2 world_xz = position_world.xz;
            float depth     = get_ocean_water_depth(world_xz);
            float3 disp     = 0.0f;
            uint cascades   = buffer_frame.ocean_cascade_count;
            [loop] for (uint c = 0; c < cascades; ++c)
            {
                float L     = buffer_frame.ocean_cascade_length[c];
                float2 uv   = world_xz / L;
                float scale = ocean_cascade_depth_scale(depth, L);
                float3 displacement;
                if (time_offset < 0.0f)
                {
                    displacement = tex_ocean_displacement_previous.SampleLevel(
                        GET_SAMPLER(sampler_bilinear_wrap),
                        float3(uv, (float)c),
                        0.0f
                    ).xyz;
                }
                else
                {
                    displacement = tex_ocean_displacement.SampleLevel(
                        GET_SAMPLER(sampler_bilinear_wrap),
                        float3(uv, (float)c),
                        0.0f
                    ).xyz;
                }
                disp += displacement * scale;
            }

            // river ribbons sit above the sea, keep the fft as ripples not open ocean swell
            float above = position_world.y - buffer_frame.ocean_sea_level;
            disp *= lerp(1.0f, 0.22f, saturate(above / 1.5f));

            position_world += disp;
            return;
        }

        // Broadside attraction: follow small view changes, then smoothly release toward
        // edge-on at wider angles. The root supplies one rotation for the entire blade.
        if (surface.is_grass_blade())
        {
            const float camera_bias_strength = 0.9f;

            float3 camera_position = time_offset < 0.0f ?
                buffer_frame.camera_position_previous :
                buffer_frame.camera_position;
            float3 to_camera = camera_position - instance_pos;
            float3 view_planar = to_camera - instance_up * dot(to_camera, instance_up);
            float view_length_sq = dot(view_planar, view_planar);
            view_planar *= rsqrt(max(view_length_sq, 1e-8f));

            float3 blade_normal = transform[2].xyz;
            blade_normal -= instance_up * dot(blade_normal, instance_up);
            blade_normal *= rsqrt(max(dot(blade_normal, blade_normal), 1e-8f));

            float facing_cos = clamp(dot(blade_normal, view_planar), -1.0f, 1.0f);
            float facing_sin = dot(cross(blade_normal, view_planar), instance_up);
            // bias = 0.45 * sin(2 * view_angle), bounded to about 26 degrees. Keeping
            // the signed cosine makes both faces agree, with no flip at the blade edge.
            // A 45-degree view stays about 19 degrees off broadside; a 90-degree view
            // still sees the edge. Fade near overhead where the viewing azimuth vanishes.
            float overhead_fade = saturate(view_length_sq / max(dot(to_camera, to_camera) * 0.04f, 1e-8f));
            float bias_angle = camera_bias_strength * facing_sin * facing_cos * overhead_fade * (1.0f - grass_press.z);

            float3x3 bias_rot = rotation_matrix(instance_up, bias_angle);

            float3 offset  = position_world - instance_pos;
            position_world = instance_pos + mul(bias_rot, offset);
            vertex.normal  = normalize(mul(bias_rot, vertex.normal));
            vertex.tangent = normalize(mul(bias_rot, vertex.tangent));
        }

        // grass and flower wind
        if (surface.is_grass_blade() || surface.is_flower())
        {
            wind_sample ws = evaluate_wind(
                instance_pos,
                time_offset
            );

            // height fraction along the blade, base = 0, tip = 1
            float h          = saturate(vertex.uv_misc.z);
            float h_cantilever = pow(h, 1.5f); // stiffer at the base than a linear taper

            // retain subtle blade variation without breaking gust coherence
            float2 inst          = wind_instance_phase_freq(instance_pos);
            float  instance_phase = inst.x;
            float  nat_freq       = inst.y;
            float  spring         = sin(
                time * nat_freq +
                instance_phase
            ) * 0.03f;
            float wind_response = saturate(
                length(buffer_frame.wind.xz) *
                0.10f
            );
            float ambient_wobble = (
                sin(
                    time * 0.80f +
                    instance_phase
                ) * 1.60f +
                sin(
                    time * 0.53f +
                    instance_phase * 1.70f
                ) * 0.70f
            ) * DEG_TO_RAD * wind_response;

            // peak bend angle of 55 deg
            float bend_amp     = ws.bend_strength * (1.0f + spring);
            float micro_jitter = ws.micro *
                0.035f *
                ws.bend_strength;
            float angle        = (
                bend_amp * (55.0f * DEG_TO_RAD) +
                ambient_wobble +
                micro_jitter
            ) * h_cantilever;

            // never let the blade rotate below horizontal
            static const float3 vertical              = float3(0.0f, 1.0f, 0.0f);
            float current_angle_from_vertical         = acos(saturate(dot(instance_up, vertical)));
            float max_allowed_angle                   = max(0.0f, (75.0f * DEG_TO_RAD) - current_angle_from_vertical);
            angle                                     = sign(angle) * min(abs(angle), max_allowed_angle);

            // rotation axis perpendicular to bend direction in the horizontal plane
            float3 raw_axis      = cross(instance_up, ws.bend_dir_world);
            float  axis_length_sq = dot(raw_axis, raw_axis);
            float3 axis           = axis_length_sq > 0.0001f ? raw_axis * rsqrt(axis_length_sq) : float3(1.0f, 0.0f, 0.0f);

            if (surface.is_grass_blade())
            {
                // Gravity gives every blade a gentle resting arch, even with no wind.
                // Hash the root, not the transient instance slot, so neither repopulation
                // nor camera motion changes its shape. Keep the lower stem relatively stiff.
                float rest_heading = hash(instance_pos.xz + float2(17.3f, 91.7f)) * PI2;
                float rest_angle = lerp(10.0f, 22.0f, hash(instance_pos.zx + float2(73.1f, 5.9f))) * DEG_TO_RAD * h_cantilever;
                // Let the resting arch deepen and relax in the breeze. Keep its direction
                // stable, and retain the exact resting pose at zero wind. Shared gusts drive
                // the strength while each blade's phase/frequency keeps the flex from marching
                // in unison. 'time' includes the previous-frame offset for motion vectors.
                float rest_flex = sin(time * (1.3f + nat_freq * 0.2f) + instance_phase) * 0.65f +
                                  sin(time * 0.93f + instance_phase * 1.7f) * 0.35f;
                rest_angle *= 1.0f + rest_flex * wind_response * (0.10f + 0.14f * ws.gust);
                float3 rest_direction = float3(cos(rest_heading), 0.0f, sin(rest_heading));
                float3 rest_axis = cross(instance_up, rest_direction);
                rest_axis *= rsqrt(max(dot(rest_axis, rest_axis), 1e-8f));
                float3 rotation_vector = axis * angle + rest_axis * rest_angle;
                float rotation_length = length(rotation_vector);
                axis = rotation_length > 1e-6f ? rotation_vector / rotation_length : instance_up;
                // Wind adds to the resting curve, while the existing slope limit keeps it
                // above the ground. Tire pressure below replaces this relaxed pose entirely.
                angle = min(rotation_length, max_allowed_angle);
            }

            if (grass_press.z > 0.0001f)
            {
                float3 direction = float3(grass_press.x, 0.0f, grass_press.y);
                direction -= instance_up * dot(direction, instance_up);
                float3 press_axis = cross(instance_up, direction);
                press_axis *= rsqrt(max(dot(press_axis, press_axis), 0.000001f));
                // A short curved root, with the rest of the blade laid almost flat on the
                // terrain plane. Fade wind out as pressure takes over so tire marks stay put.
                float press_angle = (86.0f * DEG_TO_RAD) * pow(h, 0.35f);
                float3 rotation_vector = axis * angle * (1.0f - grass_press.z) +
                    press_axis * press_angle * grass_press.z;
                angle = length(rotation_vector);
                axis = angle > 1e-6f ? rotation_vector / angle : instance_up;
            }

            // each vertex rotates by angle * h^1.5, so the base stays put and the tip sweeps the full angle
            // produces the cantilever silhouette without per-vertex pivot bookkeeping
            float3x3 rot   = rotation_matrix(axis, angle);
            float3   offset = position_world - instance_pos;
            position_world  = instance_pos + mul(rot, offset);
            vertex.normal   = normalize(mul(rot, vertex.normal));
            vertex.tangent  = normalize(mul(rot, vertex.tangent));
#if defined(GRASS_INSTANCED)
            if (surface.is_grass_blade() && buffer_pass.values[3].x > 0.5f)
            {
                MaterialParameters grass_material = GetMaterial();
                float blade_reach = grass_material.local_height * length(transform[1].xyz)
                    + grass_material.local_width * length(transform[0].xyz);
                bend_grass_around_body(grass_body_load(time_offset < 0.0f ? 1 : 0), instance_pos, blade_reach,
                    position_world, vertex.normal, vertex.tangent);
            }
#endif
        }
        else if (surface.has_wind_animation())
        {
            float response = saturate(length(buffer_frame.wind.xz) * 0.10f);
            if (response <= 0.0f)
                return;

            // All submeshes share the root sample and physical height: bark and foliage
            // must agree even when their material bounds differ. No per-vertex gusts.
#ifdef CACHE_MESH_TREE_WIND
            TreeWindState state;
            if (time_offset < 0.0f) state = mesh_tree_wind_previous;
            else state = mesh_tree_wind_current;
#else
            TreeWindState state = evaluate_tree_wind(transform, time_offset);
#endif
            wind_sample ws = state.wind;
            float2 inst = state.phase_freq;
            float3 axis = state.axis;
            float3 offset = position_world - instance_pos;
            float height = max(0.0f, dot(offset, instance_up));
            float flexible = height / (height + 8.0f);
            float root_weight = smoothstep(0.0f, 1.5f, height);
            float trunk_angle = state.trunk_drive * flexible * root_weight;
            float3x3 trunk_rotation = rotation_matrix(axis, trunk_angle);

            // Smooth spatial branch modes avoid hard sectors or random per-vertex
            // phases, which tear leaf cards. Outer branches flex more than the core.
            float3 radial = offset - instance_up * dot(offset, instance_up);
            float radius = length(radial);
            float branch_weight = smoothstep(0.3f, 3.0f, radius) * root_weight;
            float branch_phase = inst.x + dot(offset, float3(0.37f, 0.19f, 0.29f));
            float branch_wave = sin(time * (1.15f + inst.y * 0.14f) + branch_phase) * 0.65f
                              + sin(time * (1.83f + inst.y * 0.11f) + branch_phase * 0.83f) * 0.35f;
            float branch_angle = response * branch_weight
                               * (0.35f + 0.65f * ws.gust + branch_wave * 0.45f)
                               * DEG_TO_RAD;
            float3x3 branch_rotation = rotation_matrix(axis, branch_angle);
            float3 branch_pivot = instance_up * dot(offset, instance_up);
            offset = branch_pivot + mul(branch_rotation, offset - branch_pivot);
            vertex.normal = mul(branch_rotation, vertex.normal);
            vertex.tangent = mul(branch_rotation, vertex.tangent);

            if (surface.has_texture_alpha_mask())
            {
                // Centimetre-scale detail, never the old wind-independent canopy bob.
                // Fade high frequencies continuously with distance to avoid shimmer.
                // Use the corresponding camera for the motion-vector evaluation.
                float3 camera = time_offset < 0.0f ? buffer_frame.camera_position_previous : buffer_frame.camera_position;
                float detail = 1.0f - smoothstep(25.0f, 90.0f, length(camera - instance_pos));
                float flutter = sin(time * 8.3f + branch_phase * 2.0f)
                              * sin(time * 3.7f + branch_phase);
                float flutter_angle = response * branch_weight * detail
                                    * (0.35f + ws.gust * 0.65f)
                                    * (flutter + ws.micro * 0.3f) * 0.018f / max(radius, 1.0f);
                float3x3 leaf_rotation = rotation_matrix(axis, flutter_angle);
                offset = branch_pivot + mul(leaf_rotation, offset - branch_pivot);
                vertex.normal = mul(leaf_rotation, vertex.normal);
                vertex.tangent = mul(leaf_rotation, vertex.tangent);
            }

            position_world = instance_pos + mul(trunk_rotation, offset);
            vertex.normal = normalize(mul(trunk_rotation, vertex.normal));
            vertex.tangent = normalize(mul(trunk_rotation, vertex.tangent));
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
    float3 input_tangent;
    if (surface.is_skid_mark())
    {
        // tangent uint carries the ribbon fade, rebuild a lighting tangent from the normal
        float3 t = cross(input_normal, float3(0.0f, 0.0f, 1.0f));
        if (dot(t, t) < 0.001f)
        {
            t = cross(input_normal, float3(1.0f, 0.0f, 0.0f));
        }
        input_tangent = normalize(t);
    }
    else
    {
        input_tangent = unpack_vertex_oct(input.tangent_packed);
    }

    // uv state now lives on the per-renderable draw data, so multiple renderables can share a material
    float2 uv_tiling      = _draw.uv_tiling;
    float2 uv_offset      = _draw.uv_offset;
    float2 uv_invert      = _draw.uv_invert;
    float  uv_rotation    = _draw.uv_rotation;
    float  uv_world_space = _draw.uv_world_space;

    // forward to the pixel shader, ir.w carries the world_space_uv flag
    vertex.draw_flags = _draw.flags;
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
    float height_percent = saturate(input.position.y / max(material.local_height, 1e-4f));
    vertex.uv_misc.z     = surface.is_skid_mark() ? saturate(unpack_vertex_uv(input.tangent_packed).x) : height_percent;
    vertex.width_percent = width_percent;
    
    // compose instance transform and apply to base transform
    matrix instance = input.instance_transform;
    transform = mul(instance, transform);
    matrix transform_previous = mul(instance, pass_get_transform_previous());

    if (surface.is_grass_blade())
    {
        // Related colours in small tufts, with enough per-blade variation to avoid
        // identical coloured blocks. Root-based hashes remain stable across frames/LODs.
        float2 grass_cell = floor(transform[3].xz * 2.0f);
        float blade_variation = hash(transform[3].xz + float2(31.7f, 83.1f));
        vertex.uv_misc.w = lerp(hash(grass_cell), blade_variation, 0.35f);
    }
    
    // transform position to world space
    float4 position_local    = float4(input.position, 1.0f);
    float3 position          = mul(position_local, transform).xyz;
    float3 position_previous = mul(position_local, transform_previous).xyz;

    // clipmap recentering is not water motion
    if (surface.is_water())
    {
        position_previous.xz = position.xz;
    }

    // terrain maps planar world xz with tiling as repeats per meter, the half precision vertex uv quantizes under heavy tiling and collapses into stripes of repeated texels
    if (surface.is_terrain())
    {
        vertex.uv_misc.xy = position.xz * uv_tiling;
    }
    
    // transform normal and tangent to world space (extract 3x3 rotation/scale matrix)
    // Inverse transpose via cofactors handles nonuniform scale and parent shear.
    float3x3 cofactor = float3x3(cross(transform[1].xyz, transform[2].xyz),
        cross(transform[2].xyz, transform[0].xyz), cross(transform[0].xyz, transform[1].xyz));
    float orientation = dot(transform[0].xyz, cofactor[0]) < 0.0f ? -1.0f : 1.0f;
    vertex.normal = normalize(mul(input_normal, cofactor) * orientation);
    float3 tangent = mul(input_tangent, (float3x3)transform);
    vertex.tangent = normalize(tangent - vertex.normal * dot(tangent, vertex.normal));

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
