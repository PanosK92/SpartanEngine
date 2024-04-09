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

// - these functions are shared between depth_prepass.hlsl, g_buffer.hlsl and depth_light.hlsl
// - this is because the calculations have to be exactly the same and therefore produce identical values over time and space (depth values)

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
            f       = f * f * (3.0 - 2.0 * f);

            return lerp(hash(i), hash(i + 1.0), f);
        }

        static float3 apply_wind(uint instance_id, float3 position_vertex, float3 animation_pivot, float time)
        {
            const float3 base_wind_direction     = float3(1, 0, 0);
            const float  wind_vertex_sway_extent = 0.4f; // oscillation amplitude
            const float  wind_vertex_sway_speed  = 4.0f; // oscillation frequency
        
            // base oscillation, a combination of two sine waves with a phase difference
            float phase_offset = float(instance_id) * PI_HALF;
            float phase1       = (time * wind_vertex_sway_speed) + position_vertex.x + phase_offset;
            
            // phase difference to ensure continuous motion
            float phase_diff = PI / 3.0f; // choosing a non-half-multiples of PI to avoid total cancellation
            float phase2     = phase1 + phase_diff; 
            float base_wave1 = sin(phase1);
            float base_wave2 = sin(phase2);
            
            // perlin noise for low-frequency wind changes
            float low_freq_noise        = perlin_noise(time * 0.1f);
            float wind_direction_factor = lerp(-1.0f, 1.0f, low_freq_noise);
            float3 wind_direction       = base_wind_direction * wind_direction_factor;
            
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
                float angle = 2.0f * 3.14159f * i / 4.0f;
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
            position_vertex.y  += offset.y;
    
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
        if(surface.vertex_animate_wind())
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

gbuffer_vertex transform_to_world_space(Vertex_PosUvNorTan input, uint instance_id, matrix transform)
{
    gbuffer_vertex vertex;
    vertex.uv = input.uv;

    // compute the final world transform
    bool is_instanced         = instance_id != 0; // no ideal as you can have instancing with instance_id = 0, however it's very performant branching due to predictability
    matrix transform_instance = is_instanced ? input.instance_transform : matrix_identity;
    transform                 = mul(transform, transform_instance);
#ifndef TRANSFORM_IGNORE_PREVIOUS_POSITION
    matrix transform_previous = mul(pass_get_transform_previous(), transform_instance);
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

    // apply ambient animation
    Surface surface;
    surface.flags = GetMaterial().flags;
    float3 animation_pivot   = float3(transform._31, transform._32, transform._33); // position
    vertex.position          = vertex_processing::ambient_animation(surface, vertex.position, animation_pivot, instance_id, buffer_frame.time);
#ifndef TRANSFORM_IGNORE_PREVIOUS_POSITION
    animation_pivot          = float3(transform_previous._31, transform_previous._32, transform_previous._33);
    vertex.position_previous = vertex_processing::ambient_animation(surface, vertex.position_previous, animation_pivot, instance_id, buffer_frame.time - buffer_frame.delta_time);
#endif

    return vertex;
}

void transform_to_clip_space(inout gbuffer_vertex vertex)
{
    vertex.position_clip_current  = mul(float4(vertex.position, 1.0f), buffer_frame.view_projection);
    vertex.position_clip          = vertex.position_clip_current;
#ifndef TRANSFORM_IGNORE_PREVIOUS_POSITION
    vertex.position_clip_previous = mul(float4(vertex.position_previous, 1.0f), buffer_frame.view_projection_previous);
#endif
}

// tessellation

#define MAX_POINTS 3

struct HsConstantDataOutput
{
    float edges[3] : SV_TessFactor;
    float inside   : SV_InsideTessFactor;
};

HsConstantDataOutput patch_constant_function(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch)
{
    HsConstantDataOutput output;

    const float subdivisions = 4.0f;

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
[maxtessfactor(15)]
gbuffer_vertex main_hs(InputPatch<gbuffer_vertex, MAX_POINTS> input_patch, uint cp_id : SV_OutputControlPointID)
{
    return input_patch[cp_id];
}

[domain("tri")]
gbuffer_vertex main_ds(HsConstantDataOutput input, float3 bary_coords : SV_DomainLocation, const OutputPatch<gbuffer_vertex, 3> patch)
{
    gbuffer_vertex output;

    // interpolate position using barycentric coordinates
    output.position = patch[0].position * bary_coords.x +
                      patch[1].position * bary_coords.y +
                      patch[2].position * bary_coords.z;

    // interpolate normal using barycentric coordinates
    output.normal = normalize(patch[0].normal * bary_coords.x +
                              patch[1].normal * bary_coords.y +
                              patch[2].normal * bary_coords.z);

    // interpolate tangent using barycentric coordinates
    output.tangent = normalize(patch[0].tangent * bary_coords.x +
                               patch[1].tangent * bary_coords.y +
                               patch[2].tangent * bary_coords.z);

    // interpolate texture coordinates using barycentric coordinates
    output.uv = patch[0].uv * bary_coords.x +
                patch[1].uv * bary_coords.y +
                patch[2].uv * bary_coords.z;

    // apply displacement
    float displacement           = GET_TEXTURE(material_height).SampleLevel(GET_SAMPLER(sampler_anisotropic_wrap), output.uv, 0.0f).r;
    float displacement_strength  = GetMaterial().height;
    output.position             += output.normal * displacement * displacement_strength;

    transform_to_clip_space(output);

    return output;
}
