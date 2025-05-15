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
    float3 position               : POS_WORLD;
    float3 position_previous      : POS_WORLD_PREVIOUS;
    float4 position_clip          : SV_POSITION;
    float4 position_clip_current  : POS_CLIP;
    float4 position_clip_previous : POS_CLIP_PREVIOUS;
    float3 normal                 : NORMAL_WORLD;
    float3 tangent                : TANGENT_WORLD;
    float2 uv                     : TEXCOORD;
    float height_percent          : HEIGHT_PERCENT;
    uint instance_id              : INSTANCE_ID;
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

static float3 rotate_around_axis(float3 axis, float angle, float3 v)
{
    float c = cos(angle);
    float s = sin(angle);
    float t = 1.0f - c;

    // normalize the axis to ensure proper rotation
    axis = normalize(axis);

    // rodrigues' rotation formula
    float3x3 rotation = float3x3(
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

    return mul(rotation, v);
}

struct vertex_processing
{
    struct vegetation
    {
        static float3 apply_wind(uint instance_id, float3 position_vertex, float height_percent, float3 wind, float time)
        {
            const float sway_extent       = 0.2f; // maximum sway amplitude
            const float sway_speed        = 2.0f; // sway frequency
            const float noise_scale       = 0.1f; // scale of low-frequency noise
            const float flutter_intensity = 0.1f; // intensity of fluttering
        
            // normalize wind direction and calculate magnitude
            float3 wind_direction = normalize(wind);
            float wind_magnitude  = length(wind);
        
            // base sinusoidal sway
            float phase_offset = float(instance_id) * 0.25f * PI; // unique phase per instance
            float base_wave    = sin(time * sway_speed + phase_offset);
        
            // add low-frequency perlin noise for smooth directional variation
            float low_freq_noise           = get_noise_perlin(time * noise_scale + instance_id * 0.1f);
            float directional_variation    = lerp(-0.5f, 0.5f, low_freq_noise); // smooth variation
            float3 adjusted_wind_direction = wind_direction + directional_variation;
        
            // add high-frequency flutter (localized and rapid movement)
            float flutter = sin(position_vertex.x * 10.0f + time * 15.0f) * flutter_intensity;
        
            // combine all factors for sway
            float combined_wave = base_wave + flutter;
            float3 sway_offset  = adjusted_wind_direction * combined_wave * sway_extent * height_percent * wind_magnitude;
        
            // apply the calculated sway to the vertex
            position_vertex += sway_offset;
        
            return position_vertex;
        }
        
        static float3 apply_player_bend(float3 position_vertex, float height_percent)
        {
            // calculate horizontal distance to player
            float distance = length(float2(position_vertex.x - buffer_frame.camera_position.x, position_vertex.z - buffer_frame.camera_position.z));
        
            // determine bending strength (inverse square law)
            float bending_strength = saturate(1.0f / (distance * distance + 1.0f));
        
            // direction away from player
            float2 direction_away_from_player = normalize(position_vertex.xz - buffer_frame.camera_position.xz);
        
            // apply rotational bending
            float3 bending_offset = float3(direction_away_from_player * bending_strength * height_percent, 
                                           bending_strength * height_percent * 0.5f);
        
            // adjust position: apply both horizontal and vertical bending
            position_vertex.xz += bending_offset.xz * 0.5f; // horizontal effect
            position_vertex.y  += bending_offset.y;         // vertical effect
        
            return position_vertex;
        }
    };
    
    struct water
    {
        static void compute_wave_offset(inout float3 position_vertex, inout float3 dp_dx, inout float3 dp_dz, float time, float amplitude, float wavelength, float frequency, float fade_factor)
        {
            const float2 direction = normalize(buffer_frame.wind.xz);
            
            // gerstner wave parameters
            float k = PI2 / wavelength; // wave number (2π / wavelength)
            float w = PI2 * frequency;  // angular frequency (2π * frequency in hz)
            
            // phase calculation
            float phase = dot(direction, position_vertex.xz) * k + time * w;
            float c     = cos(phase);
            float s     = sin(phase);

            // position offset
            position_vertex.x += amplitude * direction.x * c * fade_factor;
            position_vertex.z += amplitude * direction.y * c * fade_factor;
            position_vertex.y += amplitude * s * fade_factor;
            
            // accumulate wave direction
            {
                float kx = k * direction.x;
                float kz = k * direction.y;
                float A  = amplitude;

                // tangent
                dp_dx += float3(-A * direction.x * kx * s,  // dx/dx
                                 A * kx * c,                // dy/dx
                                -A * direction.y * kx * s); // dz/dx
            
                // bitangent
                dp_dz += float3(-A * direction.x * kz * s,  // dx/dz
                                 A * kz * c,                // dy/dz
                                -A * direction.y * kz * s); // dz/dz
            }
        }
        
        static void apply_wave(inout float3 position, inout float3 normal, inout float3 tangent, float time, float fade_factor)
        {
            // small waves: high-frequency small ripples (SI units)
            const float amplitude  = 0.1f;
            const float wavelength = 1.0f;
            const float frequency  = 1.0f; 
            
            float3 dp_dx = tangent;
            float3 dp_dz = normalize(cross(normal, tangent)); // bitangent
        
            // update position
            compute_wave_offset(position, dp_dx, dp_dz, time, amplitude, wavelength, frequency, fade_factor);
        
            // update normal
            tangent          = normalize(dp_dx);
            float3 bitangent = normalize(dp_dz);
            normal           = normalize(cross(bitangent, tangent));
        }
    };

    static void process_local_space(Surface surface, inout Vertex_PosUvNorTan input, inout gbuffer_vertex vertex, float width_percent, uint instance_id)
    {
        if (surface.is_grass_blade())
        {
            const float3 up    = float3(0, 1, 0);
            const float3 right = float3(1, 0, 0);

            // replace flat normals with curved ones
            const float total_curvature = 60.0f * DEG_TO_RAD;            // total angle from left to right
            float t                     = (width_percent - 0.5f) * 2.0f; // map [0, 1] to [-1, 1]
            float harsh_factor          = t * t * t;                     // cubic function for sharper transition
            float curve_angle           = harsh_factor * (total_curvature / 2.0f);
            input.normal                = rotate_around_axis(up, curve_angle, input.normal);
            input.tangent               = rotate_around_axis(up, curve_angle, input.tangent);

            // bend due to gravity
            float random_lean  = get_hash(instance_id) * 1.0f;
            curve_angle        = random_lean * vertex.height_percent;
            input.position.xyz = rotate_around_axis(right, curve_angle, input.position.xyz);
            input.normal       = rotate_around_axis(right, curve_angle, input.normal);
            input.tangent      = rotate_around_axis(right, curve_angle, input.tangent);

            // bend due to wind
            curve_angle        = get_noise_perlin((float)buffer_frame.time * 1.0f) * 0.2f;
            input.position.xyz = rotate_around_axis(right, curve_angle, input.position.xyz);
            input.normal       = rotate_around_axis(right, curve_angle, input.normal);
            input.tangent      = rotate_around_axis(right, curve_angle, input.tangent);
        }
    }

    static void process_world_space(Surface surface, inout float3 position_world, inout gbuffer_vertex vertex, float3 position_local, float4x4 transform, float width_percent, uint instance_id, float time_offset = 0.0f)
    {
        float time  = (float)buffer_frame.time + time_offset;
        float3 wind = buffer_frame.wind;

        // wind simulation
        if (surface.is_grass_blade())
        {
            const float wind_direction_scale      = 0.05f; // Scale for wind direction noise (larger scale = broader patterns)
            const float wind_direction_time_scale = 0.05f; // Speed of wind direction animation
            const float wind_strength_scale       = 0.25f; // Scale for wind strength noise
            const float wind_strength_time_scale  = 2.0f;  // Speed of wind strength animation (faster for more dynamic changes)
            const float wind_strength_amplitude   = 2.0f;  // Amplifies the wind strength noise
            const float min_wind_lean             = 0.25f; // Minimum grass lean angle
            const float max_wind_lean             = 1.0f;  // Maximum grass lean angle
            const float gust_scale                = 0.01f; // Scale for global gust noise (slower for broad gusts)
            
            // global wind strength modulation (simulates gusts and lulls)
            float global_wind_strength = get_noise_perlin(float2(time * gust_scale, 0.0f));
            global_wind_strength       = remap(global_wind_strength, -1.0f, 1.0f, 0.5f, 1.5f); // varies between 0.5x and 1.5x strength
            
            // 2D noise for wind direction
            float2 noise_pos_dir = position_world.xz * wind_direction_scale + float2(time * wind_direction_time_scale, 0.0f);
            float wind_direction = get_noise_perlin(noise_pos_dir);
            wind_direction       = remap(wind_direction, -1.0f, 1.0f, 0.0f, 6.2832f); // remap to [0, 2π]
            
            // 2D noise for wind strength
            float2 noise_pos_strength = position_world.xz * wind_strength_scale + float2(time * wind_strength_time_scale, 0.0f);
            float wind_strength_noise = get_noise_perlin(noise_pos_strength) * wind_strength_amplitude * global_wind_strength;
            
            // calculate wind lean angle with cubic easing for natural bending
            float wind_lean_angle = remap(wind_strength_noise, -1.0f, 1.0f, min_wind_lean, max_wind_lean);
            wind_lean_angle       = (wind_lean_angle * wind_lean_angle * wind_lean_angle); // cubic ease-in
            wind_lean_angle       = clamp(wind_lean_angle, 0.0f, PI); // cap at π to avoid bending below ground
            
            // wind direction vector and rotation axis
            float3 wind_dir      = float3(cos(wind_direction), 0, sin(wind_direction));
            float3 rotation_axis = normalize(cross(float3(0, 1, 0), wind_dir));
            
            // apply wind bend based on height
            float total_height = 1.0f; // this can be passed from the cpu, but it's currently not needed
            float curve_angle  = (wind_lean_angle / total_height) * vertex.height_percent;
            
            // rotate position, normal, and tangent around the axis
            float3 base_position = position_world - position_local; // base of the blade
            float3 local_pos     = position_world - base_position;
            local_pos            = rotate_around_axis(rotation_axis, curve_angle, local_pos);
            position_world       = base_position + local_pos;
            vertex.normal        = rotate_around_axis(rotation_axis, curve_angle, vertex.normal);
            vertex.tangent       = rotate_around_axis(rotation_axis, curve_angle, vertex.tangent);
        }
        
        if (surface.has_wind_animation() && !surface.is_grass_blade()) // grass has it's own wind (need to unify)
        {
            position_world = vegetation::apply_wind(instance_id, position_world, vertex.height_percent, wind, time);
        }

        if (surface.has_wind_animation() || surface.is_grass_blade())
        {
            position_world = vegetation::apply_player_bend(position_world, vertex.height_percent);
        }
    }
};

gbuffer_vertex transform_to_world_space(Vertex_PosUvNorTan input, uint instance_id, matrix transform)
{
    MaterialParameters material = GetMaterial();
    Surface surface;
    surface.flags = material.flags;

    // start building the vertex
    gbuffer_vertex vertex;
    vertex.instance_id = instance_id;
    
    // compute width and height percent, they represent the position of the vertex relative to the grass blade
    float3 position_transform = extract_position(transform); // bottom-left of the grass blade
    float width_percent       = (input.position.xyz.x) / GetMaterial().local_width;
    vertex.height_percent     = (input.position.xyz.y - position_transform.x) / GetMaterial().local_height;

    // vertex processing - local space
    vertex_processing::process_local_space(surface, input, vertex, width_percent, instance_id);

    // compute the final world transform
    bool is_instanced         = instance_id != 0;                   // not ideal as you can have instancing with instance_id = 0, however it's very performant branching due to predictability
    matrix transform_instance = input.instance_transform;           // identity for non-instanced
    transform                 = mul(transform, transform_instance);
    matrix full               = pass_get_transform_previous();
    matrix<float, 3, 3> temp  = (float3x3)full;                     // clip the last row as it has encoded data in the first two elements
    matrix transform_previous = matrix(                             // manually construct a matrix that can be multiplied with another matrix
        temp._m00, temp._m01, temp._m02, 0.0f,
        temp._m10, temp._m11, temp._m12, 0.0f,
        temp._m20, temp._m21, temp._m22, 0.0f,
        0.0f,      0.0f,      0.0f,      1.0f
    );
    transform_previous = is_instanced ? mul(transform_previous, transform_instance) : full;

    // transform to world space
    vertex.position          = mul(input.position, transform).xyz;
    vertex.position_previous = mul(input.position, transform_previous).xyz;
    vertex.normal            = normalize(mul(input.normal, (float3x3)transform));
    vertex.tangent           = normalize(mul(input.tangent, (float3x3)transform));

    // compute (world-space) uv
    float3 abs_normal = abs(vertex.normal); // absolute normal for weights
    float3 weights    = abs_normal / (abs_normal.x + abs_normal.y + abs_normal.z + 0.0001f); // normalize weights, avoid division by zero
    float2 uv_xy      = vertex.position.xy / material.tiling + material.offset;              // xy plane (walls facing Z)
    float2 uv_xz      = vertex.position.xz / material.tiling + material.offset;              // xz plane (floor/ceiling)
    float2 uv_yz      = vertex.position.yz / material.tiling + material.offset;              // yz plane (walls facing X)
    float2 world_uv   = uv_xy * weights.z + uv_xz * weights.y + uv_yz * weights.x;
    float2 mesh_uv    = float2(input.uv.x * material.tiling.x + material.offset.x, input.uv.y * material.tiling.y + material.offset.y);
    vertex.uv         = lerp(mesh_uv, world_uv, material.world_space_uv);

    // vertex processing - world space
    vertex_processing::process_world_space(surface, vertex.position, vertex, input.position.xyz, transform, width_percent, instance_id);
    vertex_processing::process_world_space(surface, vertex.position_previous, vertex, input.position.xyz, transform_previous, width_percent, instance_id, -buffer_frame.delta_time);
    
    return vertex;
}

gbuffer_vertex transform_to_clip_space(gbuffer_vertex vertex)
{
    vertex.position_clip          = mul(float4(vertex.position, 1.0f), buffer_frame.view_projection);
    vertex.position_clip_current  = vertex.position_clip;
    vertex.position_clip_previous = mul(float4(vertex.position_previous, 1.0f), buffer_frame.view_projection_previous);

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
    float edges[3] : SV_TessFactor;       // edge tessellation factors
    float inside   : SV_InsideTessFactor; // inside tessellation factor
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
    float3 avg_pos         = (input_patch[0].position + input_patch[1].position + input_patch[2].position) / 3.0f;
    float3 to_camera       = avg_pos - buffer_frame.camera_position;
    float distance_squared = dot(to_camera, to_camera);
    float tess_factor      = (distance_squared <= TESS_DISTANCE_SQUARED) ? TESS_FACTOR : 1.0f;

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
    vertex.position          = patch[0].position          * bary_coords.x + patch[1].position          * bary_coords.y + patch[2].position          * bary_coords.z;
    vertex.position_previous = patch[0].position_previous * bary_coords.x + patch[1].position_previous * bary_coords.y + patch[2].position_previous * bary_coords.z;
    vertex.normal            = normalize(patch[0].normal  * bary_coords.x + patch[1].normal            * bary_coords.y + patch[2].normal            * bary_coords.z);
    vertex.tangent           = normalize(patch[0].tangent * bary_coords.x + patch[1].tangent           * bary_coords.y + patch[2].tangent           * bary_coords.z);
    vertex.uv                = patch[0].uv                * bary_coords.x + patch[1].uv                * bary_coords.y + patch[2].uv                * bary_coords.z;
    vertex.height_percent    = patch[0].height_percent    * bary_coords.x + patch[1].height_percent    * bary_coords.y + patch[2].height_percent    * bary_coords.z; // pass through to avoid the compile optimizing out
    
    // calculate fade factor based on actual distance from camera
    float3 vec_to_vertex      = vertex.position.xyz - buffer_frame.camera_position;
    float distance_from_cam   = length(vec_to_vertex);
    const float fade_distance = 4.0f; // distance from the end at which tessellation starts to fade to 0
    float fade_factor         = saturate((TESS_DISTANCE - distance_from_cam) / fade_distance);

    // displace
    MaterialParameters material = GetMaterial(); Surface surface; surface.flags = material.flags;
    bool tessellated            = input.edges[0] > 1.0f || input.edges[1] > 1.0f || input.edges[2] > 1.0f || input.inside > 1.0f;
    if (tessellated)
    {
        if (surface.has_texture_height())
        {
            float height              = GET_TEXTURE(material_texture_index_packed).SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), vertex.uv, 0.0f).a * 0.04f;
            float3 displacement       = vertex.normal * height * material.height * fade_factor;
            vertex.position          += displacement;
            vertex.position_previous += displacement;
        }

        // for the terrain, add some perlin noise to make it look less flat
        if (surface.is_terrain())
        {
            float height              = get_noise_perlin(vertex.position.xz * 8.0f) * 0.1f;
            float3 displacement       = vertex.normal * height * fade_factor;
            vertex.position          += displacement;
            vertex.position_previous += displacement;
        }
    }

    // for the water, apply some wave patterns
    if (surface.is_water())
    {
        float time          = (float)buffer_frame.time;
        float time_previous = time - (float)buffer_frame.delta_time;
        
        float3 normal, tangent;
        vertex_processing::water::apply_wave(vertex.position_previous, normal,        tangent,        time_previous, fade_factor);
        vertex_processing::water::apply_wave(vertex.position,          vertex.normal, vertex.tangent, time,          fade_factor);
    }

    return transform_to_clip_space(vertex);
}
