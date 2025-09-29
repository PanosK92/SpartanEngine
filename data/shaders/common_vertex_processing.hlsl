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
    float4 position           : POSITION;
    float2 uv                 : TEXCOORD;
    float3 normal             : NORMAL;
    float3 tangent            : TANGENT;
    matrix instance_transform : INSTANCE_TRANSFORM;
};

// vertex buffer output
struct gbuffer_vertex
{
    float4 position          : SV_POSITION;
    float4 position_previous : POS_CLIP_PREVIOUS;
    float3 normal            : NORMAL_WORLD;
    float3 tangent           : TANGENT_WORLD;
    float4 uv_misc           : TEXCOORD; // xy = uv, z = height_percent, w = instance_id - packed together to reduced the interpolators (shader registers) the gpu needs to track
}; 

// remap a value from one range to another
float remap(float value, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

static float3 extract_position(matrix transform)
{
    return float3(transform._31, transform._32, transform._33);
}

// create a 3x3 rotation matrix using Rodrigues' rotation formula
static float3x3 rotation_matrix(float3 axis, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    float t = 1.0f - c;
    
    // normalize the axis to ensure proper rotation
    axis = normalize(axis);
    
    // rodrigues' rotation formula
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
   
    static void process_local_space(Surface surface, inout Vertex_PosUvNorTan input, inout gbuffer_vertex vertex, const float width_percent, uint instance_id)
    {
        if (!surface.is_grass_blade())
            return;
    
        const float3 up    = float3(0, 1, 0);
        const float3 right = float3(1, 0, 0);
    
        // replace flat normals with curved ones
        const float total_curvature = 80.0f * DEG_TO_RAD;
        float t                     = (width_percent - 0.5f) * 2.0f;
        float harsh_factor          = t * t * t;
        float curve_angle           = harsh_factor * (total_curvature / 2.0f);
        float3x3 curvature_rotation = rotation_matrix(up, curve_angle);
        input.normal                = mul(curvature_rotation, input.normal);
        input.tangent               = mul(curvature_rotation, input.tangent);
    
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
        float distance_to_camera = fast_length(position_world - buffer_frame.camera_position);
        if (surface.is_grass_blade() && distance_to_camera <= 300.0f)
        {
            const float wind_direction_scale      = 0.05f; // scale for wind direction noise (larger scale = broader patterns)
            const float wind_direction_variation  = PI / 4.0f * (0.5f + base_wind_magnitude / 2.0f); // scale variation width (e.g., wider swings at high mag)
            const float wind_strength_scale       = 0.25f; // scale for wind strength noise
            const float wind_strength_amplitude   = 2.0f;  // amplifies the wind strength noise
            const float min_wind_lean             = 0.25f; // minimum grass lean angle
            const float max_wind_lean             = 1.0f;  // maximum grass lean angle
            
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
            wind_lean_angle       = clamp(wind_lean_angle, 0.0f, PI); // cap at π to avoid bending below ground
            
            // wind direction vector and rotation axis
            float3 wind_dir      = float3(cos(wind_direction), 0, sin(wind_direction));
            float3 rotation_axis = normalize(cross(instance_up, -wind_dir));
            
            // apply wind bend based on height
            float total_height = 1.0f; // this can be passed from the cpu, but it's currently not needed
            float curve_angle  = (wind_lean_angle / total_height) * vertex.uv_misc.z;
            
            // rotate position, normal, and tangent around the axis
            float3x3 rotation    = rotation_matrix(rotation_axis, curve_angle);
            float3 base_position = position_world - position_local;
            float3 local_pos     = position_world - base_position;
            local_pos            = mul(rotation, local_pos);
            position_world       = base_position + local_pos;
            vertex.normal        = mul(rotation, vertex.normal);
            vertex.tangent       = mul(rotation, vertex.tangent);
        }
        
        if (surface.has_wind_animation() && !surface.is_grass_blade()) // grass has its own wind (now unified via buffer)
        {
            const float sway_extent       = 0.2f; // maximum sway amplitude
            const float noise_scale       = 0.1f; // scale of low-frequency noise
            const float flutter_intensity = 0.1f; // intensity of fluttering
        
            // base sinusoidal sway
            float phase_offset = float(instance_id) * 0.25f * PI; // unique phase per instance
            float base_wave    = sin(time * base_wind_magnitude + phase_offset);
        
            // add low-frequency perlin noise for smooth directional variation
            float low_freq_noise        = noise_perlin(time * noise_scale * (1.0f + base_wind_magnitude / 2.0f) + instance_id * 0.1f);
            float directional_variation = lerp(-0.5f, 0.5f, low_freq_noise); // smooth variation
        
            float3 perp_dir                = normalize(cross(base_wind_dir, instance_up));
            float3 adjusted_wind_direction = normalize(base_wind_dir + directional_variation * perp_dir);
        
            // add high-frequency flutter (localized and rapid movement)
            float flutter = sin(position_world.x * 10.0f + time * (5.0f * (1.0f + base_wind_magnitude))) * flutter_intensity;
        
            // combine all factors for sway
            float combined_wave = base_wave + flutter;
            float3 sway_offset  = adjusted_wind_direction * combined_wave * sway_extent * vertex.uv_misc.z * base_wind_magnitude;
        
            // apply the calculated sway to the vertex
            position_world += sway_offset;
        }
    
        if (surface.has_wind_animation() || surface.is_grass_blade())
        {
            // This appears to be camera-based bending, not wind-related (e.g., simulating displacement from player/camera proximity)
            // Kept as-is since it's separate from wind simulation
            float distance                    = length(float2(position_world.x - buffer_frame.camera_position.x, position_world.z - buffer_frame.camera_position.z));
            float bending_strength            = saturate(1.0f / (distance * distance + 1.0f));
            float2 direction_away_from_player = normalize(position_world.xz - buffer_frame.camera_position.xz);
            float3 bending_offset             = float3(direction_away_from_player * bending_strength * vertex.uv_misc.z, bending_strength * vertex.uv_misc.z * 0.5f);
        
            // adjust position: apply both horizontal and vertical bending
            position_world.xz += bending_offset.xz * 0.5f; // horizontal effect
            position_world.y  += bending_offset.y;         // vertical effect
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
    float width_percent       = (input.position.xyz.x + material.local_width * 0.5) / material.local_width;
    vertex.uv_misc.z          = (input.position.xyz.y - position_transform.y) / material.local_height;

    // process in local space
    vertex_processing::process_local_space(surface, input, vertex, width_percent, instance_id);
  
    // transform to world space
    transform                 = mul(input.instance_transform, transform); // identity for non-instanced
    matrix transform_previous = mul(input.instance_transform, pass_get_transform_previous());
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
