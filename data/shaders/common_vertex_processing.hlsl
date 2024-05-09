/*
Copyright(c) 2016-2024 Panos Karabelas

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
    float4 position           : POSITION0;
    float2 uv                 : TEXCOORD0;
    float3 normal             : NORMAL0;
    float3 tangent            : TANGENT0;
    matrix instance_transform : INSTANCE_TRANSFORM0;
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
    uint instance_id              : INSTANCE_ID;
    matrix transform              : TRANSFORM;
    matrix transform_previous     : TRANSFORM_PREVIOUS;
};

static float3 extract_position(matrix transform)
{
    return float3(transform._31, transform._32, transform._33);
}

struct vertex_processing
{
    struct vegetation
    {
        static float hash(float n)
        {
            return frac(sin(n) * 43758.5453f);
        }

        static float perlin_noise(float x)
        {
            float i = floor(x);
            float f = frac(x);
            f = f * f * (3.0 - 2.0 * f);

            return lerp(hash(i), hash(i + 1.0), f);
        }

        static float3 apply_wind(uint instance_id, float3 position_vertex, float3 animation_pivot, float time)
        {
            const float3 base_wind_direction    = float3(1, 0, 0);
            const float wind_vertex_sway_extent = 0.4f; // oscillation amplitude
            const float wind_vertex_sway_speed  = 4.0f; // oscillation frequency
        
            // base oscillation, a combination of two sine waves with a phase difference
            float phase_offset = float(instance_id) * PI_HALF;
            float phase1       = (time * wind_vertex_sway_speed) + position_vertex.x + phase_offset;
            
            // phase difference to ensure continuous motion
            float phase_diff = PI / 3.0f; // choosing a non-half-multiples of PI to avoid total cancellation
            float phase2     = phase1 + phase_diff;
            float base_wave1 = sin(phase1);
            float base_wave2 = sin(phase2);
            
            // perlin noise for low-frequency wind changes
            float low_freq_noise = perlin_noise(time * 0.1f);
            float wind_direction_factor = lerp(-1.0f, 1.0f, low_freq_noise);
            float3 wind_direction = base_wind_direction * wind_direction_factor;
            
            // high-frequency perlin noise for flutter
            float high_freq_noise = perlin_noise(position_vertex.x * 10.0f + time * 10.0f) - 0.5f;
            
            // combine all factors
            float combined_wave = (base_wave1 + base_wave2 + high_freq_noise) / 3.0f;
            
            // reduce sway at the bottom, increase at the top
            float sway_factor = saturate((position_vertex.y - animation_pivot.y) / GetMaterial().world_space_height);
            
            // calculate final offset
            float3 offset = wind_direction * combined_wave * wind_vertex_sway_extent * sway_factor;
            
            position_vertex.xyz += offset;
            
            return position_vertex;
        }

        static float3 apply_player_bend(float3 position_vertex, float3 animation_pivot)
        {
            // calculate horizontal distance to player
            float distance = length(float2(position_vertex.x - buffer_frame.camera_position.x, position_vertex.z - buffer_frame.camera_position.z));
        
            // determine bending strength (inverse square law)
            float bending_strength = saturate(1.0f / (distance * distance + 1.0f));
        
            // direction away from player
            float2 direction_away_from_player = normalize(position_vertex.xz - buffer_frame.camera_position.xz);
        
            // calculate height factor (more bending at the top)
            float height_factor = (position_vertex.y - animation_pivot.y) / GetMaterial().world_space_height;
            height_factor = saturate(height_factor);
        
            // apply rotational bending
            float3 bending_offset = float3(direction_away_from_player * bending_strength * height_factor, bending_strength * height_factor * 0.5f);
        
            // adjust position
            position_vertex.xz += bending_offset.xz * 0.5f; // horizontal effect
            float proposed_y_position = position_vertex.y + bending_offset.y * 1.0f; // vertical effect
        
            // ensure vegetation doesn't bend below the ground
            position_vertex.y = max(proposed_y_position, animation_pivot.y);
        
            return position_vertex;
        }
    };
    
    struct water
    {
        static float3 apply_wave(float3 position_vertex, float time)
        {
            static const float base_wave_height    = 0.2f;
            static const float base_wave_frequency = 20.0f;
            static const float base_wave_speed     = 0.3f;

            // interleave 4 waves to have a more complex wave pattern
            float3 offset = 0.0f;
            for (int i = 0; i < 4; i++)
            {
                // modulate base wave parameters based on index
                float wave_height    = base_wave_height * (0.75f + i * 0.1f);
                float wave_frequency = base_wave_frequency * (0.9f + i * 0.05f);
                float wave_speed     = base_wave_speed * (0.9f + i * 0.05f);
    
                // dynamically calculate wave direction based on index
                float angle           = 2.0f * 3.14159f * i / 4.0f;
                float2 wave_direction = float2(cos(angle), sin(angle));
    
                // gerstner wave equation
                float k = 2 * 3.14159 / wave_frequency;
                float w = sqrt(9.8f / k) * wave_speed;
    
                // phase and amplitude
                float phase = dot(wave_direction, position_vertex.xz) * k + time * w;
                float c     = cos(phase);
                float s     = sin(phase);
    
                // calculate new position for this wave and add to the offset
                offset.x += wave_height * wave_direction.x * c;
                offset.z += wave_height * wave_direction.y * c;
                offset.y += wave_height * s;
            }
    
            position_vertex.xz += offset.xz;
            position_vertex.y += offset.y;
    
            return position_vertex;
        }

        static float3 apply_ripple(float3 position_vertex, float time)
        {
            static const float ripple_speed                = 0.25f;
            static const float ripple_max_height           = 0.2f;
            static const float ripple_frequency            = 5.0f;
            static const float ripple_decay_rate           = 0.1f;
            static const float ripple_decay_after_movement = 2.0f; // time for ripples to decay after movement stops

            // calculate time since the player last moved
            float time_since_last_movement = time - buffer_frame.camera_last_movement_time;

            // check if the camera (player) is near the sea level (0.0f)
            if (abs(buffer_frame.camera_position.y) < 4.0f)
            {
                float distance = length(position_vertex.xz - buffer_frame.camera_position.xz);
                
                // the ripple phase should consider the distance from the player
                float ripple_phase = ripple_frequency * (time * ripple_speed - distance);

                // adjust the ripple height based on time since last movement
                float decay_factor  = max(1.0f - (time_since_last_movement / ripple_decay_after_movement), 0.0f);
                float ripple_height = ripple_max_height * sin(ripple_phase) * exp(-ripple_decay_rate * distance) * decay_factor;

                position_vertex.y += ripple_height;
            }

            return position_vertex;
        }
    };

    static float3 ambient_animation(Surface surface, float3 position, float3 animation_pivot, uint instance_id, float time)
    {
        if (surface.vertex_animate_wind())
        {
            position = vegetation::apply_wind(instance_id, position, animation_pivot, time);
            position = vegetation::apply_player_bend(position, animation_pivot);
        }
    
        if (surface.vertex_animate_water())
        {
            position = water::apply_wave(position, time);
            position = water::apply_ripple(position, time);
        }
    
        return position;
    }
};

struct sampling
{
    // hash function with simple hashing to generate a gradient
    static float2 hash(float2 p)
    {
        float3 p3 = frac(float3(p.xyx) * 0.1 + float3(p.y, p.x, p.x) * 0.3 + float3(p.y, p.x, p.y) * 0.3);
        p3 += dot(p3, p3.yzx + 19.19);
        return frac((p3.xx + p3.yz) * p3.zy);
    }

    // single function to generate Perlin noise and rotate UVs
    static float2 rotate_uv(float2 uv, float noise_amount, float max_rotation_deg)
    {
        float2 i        = floor(uv * noise_amount);
        float2 f        = frac(uv * noise_amount);
        float a         = dot(hash(i + float2(0.0, 0.0)), f - float2(0.0, 0.0));
        float b         = dot(hash(i + float2(1.0, 0.0)), f - float2(1.0, 0.0));
        float c         = dot(hash(i + float2(0.0, 1.0)), f - float2(0.0, 1.0));
        float d         = dot(hash(i + float2(1.0, 1.0)), f - float2(1.0, 1.0));
        float2 u        = f * f * (3.0 - 2.0 * f);
        float noise     = lerp(lerp(a, b, u.x), lerp(c, d, u.y), u.y);
        float angle     = noise * max_rotation_deg * DEG_TO_RAD;
        float cos_angle = cos(angle);
        float sin_angle = sin(angle);
        
        return float2(uv.x * cos_angle - uv.y * sin_angle, uv.x * sin_angle + uv.y * cos_angle);
    }

    static void uv_reduce_tiling(float2 uv, inout float2 uv1, inout float2 uv2)
    {
        const float max_rotation_degrees = 0.1f;
        
        uv1 = rotate_uv(uv, 0.1f, max_rotation_degrees);
        uv2 = rotate_uv(uv, 0.4f, max_rotation_degrees);
    }

    static void uv_interleave(const float2 uv, inout float2 uv_1, inout float2 uv_2)
    {
        const float2 direction_1 = float2(1.0, 0.5);
        const float2 direction_2 = float2(-0.5, 1.0);
        const float speed_1      = 0.2;
        const float speed_2      = 0.15;
        
        uv_1 = uv + (float)buffer_frame.time * speed_1 * direction_1;
        uv_2 = uv + (float)buffer_frame.time * speed_2 * direction_2;
    }

    static float apply_snow_level_variation(float3 position_world, float base_snow_level)
    {
        // define constants
        const float frequency = 0.3f;
        const float amplitude = 10.0f;
    
        // apply sine wave based on world position
        float sine_value = sin(position_world.x * frequency);
    
        // map sine value from [-1, 1] to [0, 1]
        sine_value = sine_value * 0.5 + 0.5;
    
        // apply height variation and add to base snow level
        return base_snow_level + sine_value * amplitude;
    }

    static float4 smart(Surface surface, inout gbuffer_vertex vertex, uint texture_index)
    {
        // parameters
        const uint texture_index_rock = texture_index + 1;
        const uint texture_index_sand = texture_index + 2;
        const uint texture_index_snow = texture_index + 3;
        const float sea_level         = 0.0f;
        const float sand_offset       = 4.0f;
        const float snow_level        = apply_snow_level_variation(vertex.position, 75.0f);
        const float snow_blend_speed  = 0.1f;
    
        // compute uvs first to minimize texture fetching
        float2 uv_1 = 0.0f;
        float2 uv_2 = 0.0f;
        uv_reduce_tiling(vertex.uv, uv_1, uv_2);

        // compute blend factors
        float slope             = saturate(pow(saturate(dot(vertex.normal, float3(0.0f, 1.0f, 0.0f)) - -0.25f), 24.0f));
        float distance_to_snow  = vertex.position.y - snow_level;
        float snow_blend_factor = saturate(1.0 - max(0.0, -distance_to_snow) * snow_blend_speed);
        float sand_blend_factor = saturate(vertex.position.y / sand_offset);
    
        // defer texture sampling
        float4 color, tex_flat, tex_slope, tex_sand, tex_snow;
        if (surface.vertex_animate_water())
        {
            float2 uv_interleaved_1, uv_interleaved_2;
            uv_interleave(vertex.uv, uv_interleaved_1, uv_interleaved_2);

            float3 sample_1 = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_interleaved_1).rgb;
            float3 sample_2 = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_interleaved_2).rgb;
            color            = float4(normalize(sample_1 + sample_2), 0.0f);
        }
       else if (surface.texture_slope_based())
        {
            float4 tex_flat_1  = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_1);
            float4 tex_flat_2  = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_2);
            float4 tex_slope_1 = GET_TEXTURE(texture_index_rock).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_1);
            float4 tex_slope_2 = GET_TEXTURE(texture_index_rock).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_2);
            float4 tex_sand_1  = GET_TEXTURE(texture_index_sand).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_1);
            float4 tex_sand_2  = GET_TEXTURE(texture_index_sand).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_2);
            float4 tex_snow_1  = GET_TEXTURE(texture_index_snow).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_1);
            float4 tex_snow_2  = GET_TEXTURE(texture_index_snow).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_2);
        
            // blend each texture pair
            float4 tex_flat  = lerp(tex_flat_1, tex_flat_2, 0.5f);
            float4 tex_slope = lerp(tex_slope_1, tex_slope_2, 0.5f);
            float4 tex_sand  = lerp(tex_sand_1, tex_sand_2, 0.5f);
            float4 tex_snow  = lerp(tex_snow_1, tex_snow_2, 0.5f);
        
            // determine where the sand should appear: only below a certain elevation
            float sand_blend_threshold = sea_level + sand_offset; // define a threshold above which no sand should appear
            float sand_factor          = saturate((vertex.position.y - sea_level) / (sand_blend_threshold - sea_level));
            sand_blend_factor          = 1.0f - sand_factor; // invert factor: 1 near sea level, 0 above the threshold

            // blend textures
            float4 terrain = lerp(tex_slope, tex_flat, slope);           // blend base terrain with slope
            terrain        = lerp(terrain, tex_sand, sand_blend_factor); // then blend in sand based on height
            color          = lerp(terrain, tex_snow, snow_blend_factor); // blend in the snow
        }
        else // default texture sampling
        {
            color = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv);
        }
    
        // final snow blend for vegetation
        if (surface.vertex_animate_wind())
        {
            color.rgb = lerp(color.rgb, 0.95f, snow_blend_factor);
        }
    
        return color;
    }
};

gbuffer_vertex transform_to_world_space(Vertex_PosUvNorTan input, uint instance_id, matrix transform)
{
    gbuffer_vertex vertex;

    // compute uv
    Material material = GetMaterial();
    vertex.uv = float2(input.uv.x * material.tiling.x + material.offset.x, input.uv.y * material.tiling.y + material.offset.y);

    // compute the final world transform
    bool is_instanced         = instance_id != 0; // not ideal as you can have instancing with instance_id = 0, however it's very performant branching due to predictability
    matrix transform_instance = is_instanced ? input.instance_transform : matrix_identity;
    transform                 = mul(transform, transform_instance);
#ifndef TRANSFORM_IGNORE_PREVIOUS_POSITION
    // clip the last row as it has encoded data in the first two elements
    matrix full              = pass_get_transform_previous();
    matrix<float, 3, 3> temp = (float3x3)full;
    // manually construt a matrix that can be multiplied with another matrix
    matrix transform_previous = matrix(
        temp._m00, temp._m01, temp._m02, 0.0f,
        temp._m10, temp._m11, temp._m12, 0.0f,
        temp._m20, temp._m21, temp._m22, 0.0f,
        0.0f,      0.0f,      0.0f,      1.0f
    );
    transform_previous = is_instanced ? mul(transform_previous, transform_instance) : full;
#endif

    // transform to world space
    vertex.position          = mul(input.position, transform).xyz;
#ifndef TRANSFORM_IGNORE_PREVIOUS_POSITION
    vertex.position_previous = mul(input.position, transform_previous).xyz;
#endif
#ifndef TRANSFORM_IGNORE_NORMALS
    vertex.normal            = normalize(mul(input.normal, (float3x3)transform));
    vertex.tangent           = normalize(mul(input.tangent, (float3x3)transform));
#endif

    // save some things into the vertex
    vertex.instance_id        = instance_id;
    vertex.transform          = transform;
#ifndef TRANSFORM_IGNORE_PREVIOUS_POSITION
    vertex.transform_previous = transform_previous;
#endif
    return vertex;
}

gbuffer_vertex transform_to_clip_space(gbuffer_vertex vertex)
{
    // get material and surface
    Material material = GetMaterial();
    Surface surface; surface.flags = material.flags;
    
     // apply ambient animation - done here so it can benefit from potentially tessellated surfaces
    vertex.position          = vertex_processing::ambient_animation(surface, vertex.position, extract_position(vertex.transform), vertex.instance_id, (float)buffer_frame.time);
#ifndef TRANSFORM_IGNORE_PREVIOUS_POSITION
    vertex.position_previous = vertex_processing::ambient_animation(surface, vertex.position_previous, extract_position(vertex.transform_previous), vertex.instance_id, (float)buffer_frame.time - buffer_frame.delta_time);
#endif
    
    vertex.position_clip          = mul(float4(vertex.position, 1.0f), buffer_frame.view_projection);
    vertex.position_clip_current  = vertex.position_clip;
#ifndef TRANSFORM_IGNORE_PREVIOUS_POSITION
    vertex.position_clip_previous = mul(float4(vertex.position_previous, 1.0f), buffer_frame.view_projection_previous);
#endif

    return vertex;
}

// tessellation

#define MAX_POINTS 3
#define MAX_TESS_FACTOR 64
#define TESS_END_DISTANCE 50.0f

struct HsConstantDataOutput
{
    float edges[3] : SV_TessFactor;
    float inside   : SV_InsideTessFactor;
};

HsConstantDataOutput patch_constant_function(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch)
{
    HsConstantDataOutput output;
    float subdivisions = 1.0f;

    // calculate camera to the patch center vector
    float3 patch_center    = (input_patch[0].position + input_patch[1].position + input_patch[2].position) / 3.0f;
    float3 camera_to_patch = patch_center - buffer_frame.camera_position;

    // determine face visibility
    float visibility = 0.0f;
    {
        // calculate the normal of the patch
        float3 tangent1    = input_patch[1].position - input_patch[0].position;
        float3 tangent2    = input_patch[2].position - input_patch[0].position;
        float3 face_normal = cross(tangent1, tangent2);

        // check if the patch is back-facing
        visibility = dot(normalize(face_normal), normalize(camera_to_patch));
    }

    if (visibility > 0) // no tessellation
    {
        subdivisions = 1.0f;
    }
    else // distance based tessellation
    {
        float distance_squared    = dot(camera_to_patch, camera_to_patch);
        float normalized_distance = min(distance_squared / (TESS_END_DISTANCE * TESS_END_DISTANCE), 1.0f);
        float drop_off_factor     = pow(2, -normalized_distance * 10);
        subdivisions              = MAX_TESS_FACTOR * drop_off_factor;
        subdivisions              = max(subdivisions, 1.0f);
    }

    // set uniform tessellation across all edges and inside
    output.edges[0] = subdivisions;
    output.edges[1] = subdivisions;
    output.edges[2] = subdivisions;
    output.inside   = subdivisions;

    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[patchconstantfunc("patch_constant_function")]
[outputcontrolpoints(MAX_POINTS)]
[maxtessfactor(MAX_TESS_FACTOR)]
gbuffer_vertex main_hs(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch, uint cp_id : SV_OutputControlPointID)
{
    return input_patch[cp_id];
}

[domain("tri")]
gbuffer_vertex main_ds(HsConstantDataOutput input, float3 bary_coords : SV_DomainLocation, const OutputPatch<gbuffer_vertex, 3> patch)
{
    gbuffer_vertex vertex;

    // interpolate
    {
        vertex.position          = patch[0].position          * bary_coords.x + patch[1].position          * bary_coords.y + patch[2].position          * bary_coords.z;
        vertex.position_previous = patch[0].position_previous * bary_coords.x + patch[1].position_previous * bary_coords.y + patch[2].position_previous * bary_coords.z;
        
        vertex.normal            = normalize(patch[0].normal  * bary_coords.x + patch[1].normal  * bary_coords.y + patch[2].normal * bary_coords.z);
        vertex.tangent           = normalize(patch[0].tangent * bary_coords.x + patch[1].tangent * bary_coords.y + patch[2].tangent  * bary_coords.z);
        
        vertex.uv                = patch[0].uv * bary_coords.x + patch[1].uv * bary_coords.y + patch[2].uv * bary_coords.z;
    }

    // displace
    Material material = GetMaterial(); Surface surface; surface.flags = material.flags;
    bool tessellated  = input.edges[0] > 1.0f || input.edges[1] > 1.0f || input.edges[2] > 1.0f || input.inside > 1.0f;
    if (surface.has_texture_height() && tessellated)
    {
        float height              = 1.0f - GET_TEXTURE(material_height).SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv, 0.0f).r;
        float strength            = GetMaterial().height * 0.1f;
        float3 tangent_1          = patch[1].position - patch[0].position;
        float3 tangent_2          = patch[2].position - patch[0].position;
        float3 normal_stable      = cross(tangent_1, tangent_2);
        float3 displacement       = -normal_stable * strength * height;
        vertex.position          += displacement;
        vertex.position_previous += displacement;
    }

    return transform_to_clip_space(vertex);
}
