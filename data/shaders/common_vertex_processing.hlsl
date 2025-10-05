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
    float4 position          : POSITION;
    float2 uv                : TEXCOORD;
    float3 normal            : NORMAL;
    float3 tangent           : TANGENT;
    float3 instance_position : INSTANCE_POSITION;
    float4 instance_rotation : INSTANCE_ROTATION;
    float instance_scale     : INSTANCE_SCALE;
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

// remap a value from one range to another
float remap(float value, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
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

float4x4 instance_to_matrix(float3 instance_position, float4 instance_rotation, float instance_scale, matrix entity_transform)
{
    // quaternion to rotation matrix
    float xx = instance_rotation.x * instance_rotation.x;
    float xy = instance_rotation.x * instance_rotation.y;
    float xz = instance_rotation.x * instance_rotation.z;
    float xw = instance_rotation.x * instance_rotation.w;
    float yy = instance_rotation.y * instance_rotation.y;
    float yz = instance_rotation.y * instance_rotation.z;
    float yw = instance_rotation.y * instance_rotation.w;
    float zz = instance_rotation.z * instance_rotation.z;
    float zw = instance_rotation.z * instance_rotation.w;

    float3x3 rotation = float3x3(
        1 - 2 * (yy + zz), 2 * (xy - zw), 2 * (xz + yw),
        2 * (xy + zw), 1 - 2 * (xx + zz), 2 * (yz - xw),
        2 * (xz - yw), 2 * (yz + xw), 1 - 2 * (xx + yy)
    );

    // scale, rotation, translation
    float4x4 transform = float4x4(
        float4(rotation._11 * instance_scale, rotation._12 * instance_scale, rotation._13 * instance_scale, 0),
        float4(rotation._21 * instance_scale, rotation._22 * instance_scale, rotation._23 * instance_scale, 0),
        float4(rotation._31 * instance_scale, rotation._32 * instance_scale, rotation._33 * instance_scale, 0),
        float4(instance_position, 1)
    );
    return mul(transform, entity_transform);
}

struct vertex_processing
{
   
    static void process_local_space(Surface surface, inout Vertex_PosUvNorTan input, inout gbuffer_vertex vertex, const float width_percent, uint instance_id)
    {
        if (!surface.is_grass_blade())
            return;
    
        const float3 up    = float3(0, 1, 0);
        const float3 right = float3(1, 0, 0);
    
        // bending due to gravity and a subtle breeze (proper wind simulation is done in world space)
        float random_lean      = hash(instance_id);
        float gravity_angle    = random_lean * vertex.uv_misc.z;
        float wind_angle       = noise_perlin(float(buffer_frame.time * 0.5f) + input.position.x * 0.05f + input.position.z * 0.05f + float(instance_id) * 0.17f) * 0.2f;
        float3x3 bend_rotation = rotation_matrix(right, gravity_angle + wind_angle);
        input.position.xyz     = mul(bend_rotation, input.position.xyz);
        input.normal           = mul(bend_rotation, input.normal);
        input.tangent          = mul(bend_rotation, input.tangent);
    }
 
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
        if (surface.is_grass_blade())
        {
            const float wind_direction_scale      = 0.05f;                                           // scale for wind direction noise (larger scale = broader patterns)
            const float wind_direction_variation  = PI / 4.0f * (0.5f + base_wind_magnitude / 2.0f); // scale variation width (e.g., wider swings at high mag)
            const float wind_strength_scale       = 0.25f;                                           // scale for wind strength noise
            const float wind_strength_amplitude   = 2.0f;                                            // amplifies the wind strength noise
            const float min_wind_lean             = 0.25f;                                           // minimum grass lean angle
            const float max_wind_lean             = 1.0f;                                            // maximum grass lean angle
            
            // global wind strength modulation (simulates gusts and lulls)
            float global_wind_strength = noise_perlin(float2(time * scaled_gust_scale, 0.0f));
            global_wind_strength       = remap(global_wind_strength, -1.0f, 1.0f, 0.5f, 1.5f); // varies between 0.5x and 1.5x strength
            
            // base wind direction from buffer, with noise variation
            float base_wind_angle    = atan2(base_wind_dir.z, base_wind_dir.x);
            float2 noise_pos_dir     = position_world.xz * wind_direction_scale + float2(time * scaled_direction_time_scale, 0.0f);
            float wind_direction_var = noise_perlin(noise_pos_dir);
            float wind_direction     = base_wind_angle + remap(wind_direction_var, -1.0f, 1.0f, -wind_direction_variation, wind_direction_variation);
            
            // 2D noise for wind strength
            float2 noise_pos_strength = position_world.xz * wind_strength_scale + float2(time * base_wind_magnitude, 0.0f);
            float wind_strength_noise = noise_perlin(noise_pos_strength) * wind_strength_amplitude * global_wind_strength;
            
            // calculate wind lean angle with cubic easing for natural bending
            float wind_lean_angle = remap(wind_strength_noise, -1.0f, 1.0f, min_wind_lean, max_wind_lean);
            wind_lean_angle       = (wind_lean_angle * wind_lean_angle * wind_lean_angle); // cubic ease-in
            wind_lean_angle       = clamp(wind_lean_angle, 0.0f, PI);                      // cap at π to avoid bending below ground
            
            // wind direction vector and rotation axis
            float3 wind_dir      = float3(cos(wind_direction), 0, sin(wind_direction));
            float3 rotation_axis = normalize(cross(instance_up, -wind_dir));
            
            // apply wind bend based on height
            float total_height = GetMaterial().local_height;
            float curve_angle  = (wind_lean_angle / total_height) * vertex.uv_misc.z; // taller parts bend more
            
            // rotate position, normal, and tangent around the axis
            float3x3 rotation    = rotation_matrix(rotation_axis, curve_angle);
            float3 base_position = position_world - position_local;
            float3 local_pos     = position_world - base_position;
            local_pos            = mul(rotation, local_pos);
            position_world       = base_position + local_pos;
            vertex.normal        = mul(rotation, vertex.normal);
            vertex.tangent       = mul(rotation, vertex.tangent);
        }
        else if (surface.has_wind_animation()) // realistic tree branch/leaf wind sway
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
    
    // process in local space
    vertex_processing::process_local_space(surface, input, vertex, width_percent, instance_id);
  
    // transform to world space
    transform                 = instance_to_matrix(input.instance_position, input.instance_rotation, input.instance_scale, transform);
    matrix transform_previous = instance_to_matrix(input.instance_position, input.instance_rotation, input.instance_scale, pass_get_transform_previous());
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

// tessellation

#define MAX_POINTS 3
#define TESS_FACTOR 64
#define TESS_DISTANCE 32.0f
#define TESS_DISTANCE_SQUARED (TESS_DISTANCE * TESS_DISTANCE)

// hull shader constant data
struct HsConstantDataOutput
{
    float edges[3] : SV_TessFactor;     // edge tessellation factors
    float inside : SV_InsideTessFactor; // inside tessellation factor
};

// hull shader (control point phase) - pass-through
[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant_function")]
[outputcontrolpoints(MAX_POINTS)]
[maxtessfactor(TESS_FACTOR)]
gbuffer_vertex main_hs(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch, uint cp_id : SV_OutputControlPointID)
{
    return input_patch[cp_id]; // pass through unchanged
}

// hull shader (patch constant function) - dynamic tessellation
HsConstantDataOutput patch_constant_function(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch, uint patch_id : SV_PrimitiveID)
{
    HsConstantDataOutput output;
   
    // calculate distance from camera to triangle center
    float3 avg_pos = 0.0f;
    for (int i = 0; i < 3; i++)
    {
        float clip_w      = input_patch[i].position.w;
        float2 ndc        = input_patch[i].position.xy / clip_w;
        float depth       = input_patch[i].position.z / clip_w;
        float2 screen_uv  = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        avg_pos          += get_position(depth, screen_uv);
    }
    avg_pos                /= 3.0f;
    float3 to_camera        = avg_pos - buffer_frame.camera_position;
    float distance_squared  = dot(to_camera, to_camera);
    float tess_factor       = (distance_squared <= TESS_DISTANCE_SQUARED) ? TESS_FACTOR : 1.0f;
    
    // set tessellation factors
    output.edges[0] = tess_factor;
    output.edges[1] = tess_factor;
    output.edges[2] = tess_factor;
    output.inside   = tess_factor;
    return output;
}

// domain shader
[domain("tri")]
gbuffer_vertex main_ds(HsConstantDataOutput input, float3 bary_coords : SV_DomainLocation, const OutputPatch<gbuffer_vertex, 3> patch)
{
    gbuffer_vertex vertex;
    
    // interpolate vertex attributes
    vertex.position          = patch[0].position * bary_coords.x + patch[1].position * bary_coords.y + patch[2].position * bary_coords.z;
    vertex.position_previous = patch[0].position_previous * bary_coords.x + patch[1].position_previous * bary_coords.y + patch[2].position_previous * bary_coords.z;
    vertex.normal            = normalize(patch[0].normal * bary_coords.x + patch[1].normal * bary_coords.y + patch[2].normal * bary_coords.z);
    vertex.tangent           = normalize(patch[0].tangent * bary_coords.x + patch[1].tangent * bary_coords.y + patch[2].tangent * bary_coords.z);
    vertex.uv_misc.xy        = patch[0].uv_misc.xy * bary_coords.x + patch[1].uv_misc.xy * bary_coords.y + patch[2].uv_misc.xy * bary_coords.z;
    vertex.uv_misc.z         = patch[0].uv_misc.z * bary_coords.x + patch[1].uv_misc.z * bary_coords.y + patch[2].uv_misc.z * bary_coords.z; // pass through to avoid the compile optimizing out
   
    // recon position and position_previous from interpolated clip
    float clip_w              = vertex.position.w;
    float clip_previous_w     = vertex.position_previous.w;
    float2 ndc_current        = vertex.position.xy / clip_w;
    float2 ndc_previous       = vertex.position_previous.xy / clip_previous_w;
    float depth_current       = vertex.position.z / clip_w;
    float depth_previous      = vertex.position_previous.z / clip_previous_w;
    float2 screen_uv_current  = float2(ndc_current.x * 0.5f + 0.5f, 0.5f - ndc_current.y * 0.5f);
    float2 screen_uv_previous = float2(ndc_previous.x * 0.5f + 0.5f, 0.5f - ndc_previous.y * 0.5f);
    float3 position           = get_position(depth_current, screen_uv_current);
    float3 position_previous  = 0.0f;// get_position_previous(depth_previous, screen_uv_previous);

    // calculate fade factor based on actual distance from camera
    float3 vec_to_vertex      = position - buffer_frame.camera_position;
    float distance_from_cam   = length(vec_to_vertex);
    const float fade_distance = 4.0f; // distance from the end at which tessellation starts to fade to 0
    float fade_factor         = saturate((TESS_DISTANCE - distance_from_cam) / fade_distance);
    
    // displace
    MaterialParameters material = GetMaterial();
    Surface surface;
    surface.flags = material.flags;
    bool tessellated = input.edges[0] > 1.0f || input.edges[1] > 1.0f || input.edges[2] > 1.0f || input.inside > 1.0f;
    if (tessellated)
    {
        if (surface.has_texture_height())
        {
            float height         = GET_TEXTURE(material_texture_index_packed).SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), vertex.uv_misc.xy, 0.0f).a * 0.04f;
            float3 displacement  = vertex.normal * height * material.height * fade_factor;
            position            += displacement;
            position_previous   += displacement;
        }
        
        // for the terrain, add some perlin noise to make it look less flat
        if (surface.is_terrain())
        {
            float height         = noise_perlin(position.xz * 8.0f) * 0.1f;
            float3 displacement  = vertex.normal * height * fade_factor;
            position            += displacement;
            position_previous   += displacement;
        }
    }
    
    // recompute clips from new position
    vertex.position          = mul(float4(position, 1.0f), buffer_frame.view_projection);
    vertex.position_previous = mul(float4(position_previous, 1.0f), buffer_frame.view_projection_previous);
    
    return vertex;
}
