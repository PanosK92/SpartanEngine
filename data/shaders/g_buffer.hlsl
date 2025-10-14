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

struct gbuffer
{
    float4 albedo   : SV_Target0;
    float4 normal   : SV_Target1;
    float4 material : SV_Target2;
    float2 velocity : SV_Target3;
};

// rotate UV around center (0.5, 0.5) by angle
float2 rotate_uv(float2 uv, float angle)
{
    float cos_a = cos(angle);
    float sin_a = sin(angle);
    float2 centered = uv - 0.5f;
    float2 rotated = float2(
        centered.x * cos_a - centered.y * sin_a,
        centered.x * sin_a + centered.y * cos_a
    );
    return rotated + 0.5f;
}

static float4 sample_reduce_tiling(uint texture_index, float2 uv, float3 world_pos)
{
    // get integer tile coordinates to hash from
    int2 tile_coords = int2(floor(world_pos.x), floor(world_pos.z));

    // get random rotation angle per tile in multiples of 90 degrees (0, 90, 180, 270)
    float rnd   = hash(tile_coords);
    float angle = floor(rnd * 4.0f) * PI_HALF;

    // rotate uv inside tile
    float2 tile_uv    = frac(uv);
    float2 rotated_uv = rotate_uv(tile_uv, angle);

    // recombine with integer tile coords to preserve tiling but rotated
    float2 final_uv = float2(tile_coords) + rotated_uv;

    // wrap final uv again to [0,1] so sample doesn't go out of bounds
    final_uv = frac(final_uv);

    // sample texture
    return GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), final_uv);
}

static float4 sample_texture(gbuffer_vertex vertex, uint texture_index, Surface surface, const float3 world_position)
{
    float4 color;

    if (surface.is_terrain())
    {
        // sample base color without tiling
        color           = sample_reduce_tiling(texture_index,     vertex.uv_misc.xy, world_position);
        float4 tex_rock = sample_reduce_tiling(texture_index + 1, vertex.uv_misc.xy, world_position);
        float4 tex_sand = sample_reduce_tiling(texture_index + 2, vertex.uv_misc.xy, world_position);

        const float sand_offset    = 0.75f;
        const float rock_angle     = 50.0f * DEG_TO_RAD; // start blending here
        const float rock_sharpness = 0.2f;               // radians — smaller = sharper

        float surface_angle = acos(dot(vertex.normal, float3(0, 1, 0)));
        float slope         = saturate((surface_angle - rock_angle) / rock_sharpness);
        float sand_factor   = saturate((world_position.y - sea_level) / sand_offset);

        float4 terrain = lerp(tex_rock, color, 1.0f - slope);
        color          = lerp(terrain, tex_sand, 1.0f - sand_factor);
    }
    else
    {
        // sample base color with tiling for non-terrain
        color = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv_misc.xy);
    }

    return color;
}

gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    float3 position_world          = 0.0f;
    float3 position_world_previous = 0.0f;
    gbuffer_vertex vertex          = transform_to_world_space(input, instance_id, buffer_pass.transform, position_world, position_world_previous);

    // transform world space position to clip space
    Surface surface;
    surface.flags = GetMaterial().flags;
    if (!surface.is_tessellated())
    {
        vertex = transform_to_clip_space(vertex, position_world, position_world_previous);
    }

    return vertex;
}

[earlydepthstencil]
gbuffer main_ps(gbuffer_vertex vertex, bool is_front_face : SV_IsFrontFace)
{
    // setup
    MaterialParameters material    = GetMaterial();
    float4 albedo                  = material.color;
    float3 normal                  = vertex.normal.xyz;
    float roughness                = material.roughness;
    float metalness                = material.metallness;
    float emission                 = 0.0f;
    float2 velocity                = 0.0f;
    float occlusion                = 1.0f;
    Surface surface; surface.flags = material.flags;

    // velocity
    float2 position_ndc           = uv_to_ndc(vertex.position.xy / (buffer_frame.resolution_render * buffer_frame.resolution_scale));
    float2 position_ndc_previous  = (vertex.position_previous.xy / vertex.position_previous.w);
    position_ndc                 -= buffer_frame.taa_jitter_current;
    position_ndc_previous        -= buffer_frame.taa_jitter_previous;
    velocity                      = position_ndc - position_ndc_previous;
    
    // world position
    float3 position_world = get_position(vertex.position.z, ndc_to_uv(position_ndc));

    // world space uv
    if (any(material.world_space_uv))
    {
        float3 abs_normal = abs(normal);

         // planar projection based on dominant axis
        float2 uv_x = position_world.yz;
        float2 uv_y = position_world.xz;
        float2 uv_z = position_world.xy;

        // base world-space UV (planar blend)
        float2 uv_world = uv_x * abs_normal.x + uv_y * abs_normal.y + uv_z * abs_normal.z;

        // apply tiling and offset
        uv_world = uv_world * material.tiling + material.offset;

        vertex.uv_misc.xy = uv_world;
    }

    // albedo
    {
        float4 albedo_sample = 1.0f;
        if (surface.has_texture_albedo())
        {
            albedo_sample      = sample_texture(vertex, material_texture_index_albedo, surface, position_world);
            albedo_sample.rgb  = srgb_to_linear(albedo_sample.rgb);
            albedo            *= albedo_sample;
        }

        // dynamic vegetation coloring (grass blades, trees, etc.)
        {
            // color variation based on instance id
            static const float3 vegetation_greener    = float3(0.05f, 0.4f, 0.03f);
            static const float3 vegetation_yellower   = float3(0.45f, 0.4f, 0.15f);
            static const float3 vegetation_browner    = float3(0.3f,  0.15f, 0.08f);
            const float vegetation_variation_strength = 0.15f;
            uint instance_id                          = vertex.uv_misc.w;
            float variation                           = hash(instance_id);

            if (surface.is_grass_blade())
            {
                // natural, darkish grass base and tip
                const float3 grass_base = float3(0.05f, 0.07f, 0.03f); // muted earthy green
                const float3 grass_tip  = float3(0.08f, 0.12f, 0.04f); // slightly brighter at tip

                float height_percent = vertex.uv_misc.z;
                float t              = smoothstep(0.2f, 1.0f, height_percent);
                float3 grass_tint    = lerp(grass_base, grass_tip, t);

                // subtle variation along blades
                float3 variation_color = grass_tint;
                variation_color        = lerp(variation_color, float3(0.07f, 0.08f, 0.03f), step(0.33f, variation));
                variation_color        = lerp(variation_color, float3(0.06f, 0.05f, 0.02f), step(0.66f, variation));

                // blend with low weight to prevent whitening
                grass_tint = lerp(grass_tint, variation_color, 0.3f * vegetation_variation_strength);

                // final blend onto albedo with moderate weight
                albedo.rgb = lerp(albedo.rgb, grass_tint, 1.0f);
            }
            else if (surface.is_flower())
            {
                // base and default tip colors
                const float3 flower_base = float3(0.05f, 0.07f, 0.03f);

                uint instance_id = vertex.uv_misc.w;
                const uint instances_per_cluster = 5000;
                uint cluster_id = instance_id / instances_per_cluster;

                // stable cluster variation
                float cluster_variation = hash(cluster_id);

                 // define the three main cluster colors
                const float3 color_blue = float3(0.529f, 0.808f, 0.922f);
                const float3 color_red = float3(0.8f, 0.2f, 0.2f);
                const float3 color_yellow = float3(0.9f, 0.8f, 0.1f);

                // branchless hue selection
                float3 flower_tip = color_blue;
                flower_tip = lerp(flower_tip, color_red, step(0.33f, cluster_variation));
                flower_tip = lerp(flower_tip, color_yellow, step(0.66f, cluster_variation));

                // local subtle variation
                float local_variation = hash(instance_id * 13u);
                flower_tip *= (0.9f + 0.1f * local_variation); // slight brightness variance

                // vertical gradient
                float height_percent = vertex.uv_misc.z;
                float t = smoothstep(0.2f, 1.0f, height_percent);
                float3 flower_tint = lerp(flower_base, flower_tip, t);

                // blend with original albedo
                albedo.rgb = lerp(albedo.rgb, flower_tint, 1.0f);
            }
            else // trees and other vegetation variation
            {
                float3 variation_color = vegetation_greener;
                variation_color        = lerp(variation_color, vegetation_yellower, step(0.25f, variation));
                variation_color        = lerp(variation_color, vegetation_browner, step(0.5f, variation));
        
                albedo.rgb = lerp(albedo.rgb, variation_color, vegetation_variation_strength * (float)surface.color_variation_from_instance());
            }
       
            // snow blending based on world-space height and normal
            float snow_blend_factor = get_snow_blend_factor(position_world, vertex.normal);
            albedo.rgb = lerp(albedo.rgb, float3(0.95f, 0.95f, 0.95f), snow_blend_factor);
        }
        
        // alpha testing happens in the depth pre-pass, so here any opaque pixel has an alpha of 1
        albedo.a = lerp(albedo.a, 1.0f, step(albedo_sample.a, 1.0f) * pass_is_opaque());
    }

    // emission
    if (material.emissive_from_albedo())
    {
        emission += luminance(albedo.rgb) * (float)material.emissive_from_albedo();
    }
    else if (surface.has_texture_emissive())
    {
        float3 emissive_color  = GET_TEXTURE(material_texture_index_emission).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv_misc.xy).rgb;
        albedo.rgb            += emissive_color;            // overwrite the albedo color
        emission               = luminance(emissive_color); // use the luminance later to boost it (no need to carry a float3 around)
    }
    
    // normal mapping
    if (surface.has_texture_normal())
    {
        // get tangent space normal and apply the user defined intensity, then transform it to world space
        float3 normal_sample  = sample_texture(vertex, material_texture_index_normal, surface, position_world).xyz;
        float3 tangent_normal = normalize(unpack(normal_sample));
    
        // reconstruct z-component as this can be a bc5 two channel normal map
        tangent_normal.z = fast_sqrt(max(0.0, 1.0 - tangent_normal.x * tangent_normal.x - tangent_normal.y * tangent_normal.y));
    
        // rotate normals to fake water waves/ripples
        float distance_fade = 1.0f;
        if (surface.is_water())
        {
            float2 direction = float2(1.0, 0.5);
            float speed      = 0.5f;
            float time       = (float)buffer_frame.time;
            float2 uv_offset = direction * time * speed;
            float2 noise_uv  = (vertex.uv_misc.xy + uv_offset); 
            float noise      = noise_perlin(noise_uv);
            float angle      = noise * PI2; 
    
            // rotate tangent normal.xy around Z-axis (tangent space)
            float cos_a       = cos(angle);
            float sin_a       = sin(angle);
            float2 rotated_xy = float2(
                tangent_normal.x * cos_a - tangent_normal.y * sin_a,
                tangent_normal.x * sin_a + tangent_normal.y * cos_a
            );
    
            // blend between original and rotated normals based on is_water (0 = original, 1 = rotated)
            tangent_normal.xy = rotated_xy;
            tangent_normal.z  = fast_sqrt(max(0.0, 1.0 - tangent_normal.x * tangent_normal.x - tangent_normal.y * tangent_normal.y));
    
            // flip if normal points down
            tangent_normal *= sign(tangent_normal.z);

            // fade normal texture beyond a certain distnace to avoid high frequency noise from lower mips
            float fade_start = 150.0f;
            float fade_end   = 300.0f;
            float distance   = fast_length(position_world - buffer_frame.camera_position);
            distance_fade    = saturate((fade_end - distance) / (fade_end - fade_start));
        }

        float normal_intensity     = saturate(max(0.01f, GetMaterial().normal)) * distance_fade;
        tangent_normal.xy         *= normal_intensity;
        float3x3 tangent_to_world  = make_tangent_to_world_matrix(vertex.normal, vertex.tangent);
        normal                     = normalize(mul(tangent_normal, tangent_to_world).xyz);
    }

    // apply curved normals for foliage
    if (surface.is_grass_blade() || surface.is_flower())
    {
        // compute curvature angle based on width percent
        const float total_curvature = 120.0f * DEG_TO_RAD;
        float t                     = (vertex.width_percent - 0.5f) * 2.0f; // [left, right] -> [-1, 1]
        float harsh_factor          = t;
        float curve_angle           = harsh_factor * (total_curvature / 2.0f); // += half total
        curve_angle                 = clamp(curve_angle, -PI * 0.5f, PI * 0.5f);
       
        // rotate around the blade up axis
        float3 rotation_axis        = normalize(cross(vertex.normal, vertex.tangent)); // up
        float3x3 curvature_rotation = rotation_matrix(rotation_axis, curve_angle);
        normal                      = normalize(mul(curvature_rotation, normal));
        vertex.tangent              = normalize(mul(curvature_rotation, vertex.tangent));

        // grass blade has no back-face, so flip the normals to make it appear like it does
        float face_sign  = is_front_face * 2.0f - 1.0f; // [back, front] -> [-1, 1]
        normal          *= face_sign;
        vertex.tangent  *= face_sign;
    }
    
    // occlusion, roughness, metalness, height sample
    {
        float4 packed_sample  = sample_texture(vertex, material_texture_index_packed, surface, position_world);
        occlusion             = lerp(occlusion, packed_sample.r, (float)material.has_texture_occlusion());
        roughness            *= lerp(1.0f,      packed_sample.g, (float)material.has_texture_roughness());
        metalness            *= lerp(1.0f,      packed_sample.b, (float)material.has_texture_metalness());
    }
    
    // specular anti-aliasing (toksvig-inspired with distance adaptation)
    if (surface.has_texture_normal())
    {
        static const float strength           = 1.0f;
        static const float max_roughness_gain = 0.02f;
        float roughness2                      = roughness * roughness;
        float3 dndu                           = ddx(normal);
        float3 dndv                           = ddy(normal);
        
        // compute variance and normalize by normal length
        float variance       = length(dndu) * length(dndu) + length(dndv) * length(dndv);
        float normal_length  = length(normal);
        variance            /= max(0.001f, normal_length * normal_length);
        
        // adapt strength based on camera distance
        float distance            = length(position_world - buffer_frame.camera_position);
        float adaptive_strength   = lerp(1.0f, 0.3f, saturate(distance / 10.0f));
        float kernel_roughness2   = min(variance * strength * adaptive_strength, max_roughness_gain);
        float filtered_roughness2 = saturate(roughness2 + kernel_roughness2);
        roughness                 = fast_sqrt(filtered_roughness2);
    }

    // write to g-buffer
    gbuffer g_buffer;
    g_buffer.albedo   = albedo;
    g_buffer.normal   = float4(normal, pass_get_material_index());
    g_buffer.material = float4(roughness, metalness, emission, occlusion);
    g_buffer.velocity = velocity;

    return g_buffer;
}
