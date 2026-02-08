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

//= INCLUDES ======================
#include "common.hlsl"
#include "common_tessellation.hlsl"
//=================================

struct gbuffer
{
    float4 albedo   : SV_Target0;
    float4 normal   : SV_Target1;
    float4 material : SV_Target2;
    float2 velocity : SV_Target3;
};

//= CONSTANTS =====================
static const float3 vegetation_greener  = float3(0.05f, 0.4f, 0.03f);
static const float3 vegetation_yellower = float3(0.45f, 0.4f, 0.15f);
static const float3 vegetation_browner  = float3(0.3f, 0.15f, 0.08f);
static const float3 grass_base          = float3(0.03f, 0.055f, 0.02f);
static const float3 grass_tip           = float3(0.05f, 0.09f, 0.03f);
static const float3 grass_var1          = float3(0.04f, 0.065f, 0.02f);
static const float3 grass_var2          = float3(0.035f, 0.04f, 0.015f);
static const float3 flower_base         = float3(0.05f, 0.07f, 0.03f);
static const float3 flower_blue         = float3(0.529f, 0.808f, 0.922f);
static const float3 flower_red          = float3(0.8f, 0.2f, 0.2f);
static const float3 flower_yellow       = float3(0.9f, 0.8f, 0.1f);
static const float3 snow_color          = float3(0.95f, 0.95f, 0.95f);
//=================================

// rotate uv around center (0.5, 0.5) by angle
static float2 rotate_uv(float2 uv, float angle)
{
    float cos_a      = cos(angle);
    float sin_a      = sin(angle);
    float2 centered  = uv - 0.5f;
    return float2(centered.x * cos_a - centered.y * sin_a, centered.x * sin_a + centered.y * cos_a) + 0.5f;
}

static float4 sample_reduce_tiling(uint texture_index, float2 uv, float3 world_pos)
{
    int2 tile_coords  = int2(floor(world_pos.x), floor(world_pos.z));
    float angle       = floor(hash(tile_coords) * 4.0f) * PI_HALF;
    float2 final_uv   = frac(float2(tile_coords) + rotate_uv(frac(uv), angle));
    return GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), final_uv);
}

static float4 sample_texture(gbuffer_vertex vertex, uint texture_index, Surface surface, float3 world_pos)
{
    float4 color;
    
    if (surface.is_terrain())
    {
        color               = sample_reduce_tiling(texture_index, vertex.uv_misc.xy, world_pos);
        float4 tex_rock     = sample_reduce_tiling(texture_index + 1, vertex.uv_misc.xy, world_pos);
        float4 tex_sand     = sample_reduce_tiling(texture_index + 2, vertex.uv_misc.xy, world_pos);

        float surface_angle = acos(dot(vertex.normal, float3(0.0f, 1.0f, 0.0f)));
        float slope         = saturate((surface_angle - 50.0f * DEG_TO_RAD) * 5.0f);
        float sand_factor   = saturate((world_pos.y - sea_level) * 1.333f);

        color = lerp(lerp(tex_rock, color, 1.0f - slope), tex_sand, 1.0f - sand_factor);
    }
    else
    {
        color = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv_misc.xy);
    }
    
    return color;
}

// compute grass blade color with variation
float3 compute_grass_color(float height_percent, float variation)
{
    float t           = smoothstep(0.2f, 1.0f, height_percent);
    float3 grass_tint = lerp(grass_base, grass_tip, t);
    
    // branchless color variation
    float3 var_color = lerp(grass_tint, grass_var1, step(0.33f, variation));
    var_color        = lerp(var_color, grass_var2, step(0.66f, variation));
    return lerp(grass_tint, var_color, 0.045f); // 0.3 * 0.15 = 0.045
}

// compute flower color with cluster-based hue
float3 compute_flower_color(float height_percent, uint instance_id)
{
    uint cluster_id         = instance_id / 5000u;
    float cluster_variation = hash(cluster_id);
    
    // branchless hue selection
    float3 tip = lerp(flower_blue, flower_red, step(0.33f, cluster_variation));
    tip        = lerp(tip, flower_yellow, step(0.66f, cluster_variation));
    tip       *= 0.9f + 0.1f * hash(instance_id * 13u);
    
    return lerp(flower_base, tip, smoothstep(0.2f, 1.0f, height_percent));
}

#ifdef INDIRECT_DRAW
gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint draw_id : DRAW_INDEX)
{
    // read per-draw data from the compacted draw data buffer (written by the cull shader)
    DrawData dd                      = indirect_draw_data_out[draw_id];
    _indirect_transform_previous     = dd.transform_previous;
    _indirect_material_index         = dd.material_index;

    float3 position_world            = 0.0f;
    float3 position_world_previous   = 0.0f;
    gbuffer_vertex vertex            = transform_to_world_space(input, instance_id, dd.transform, position_world, position_world_previous);
    vertex.material_index            = dd.material_index;
    return transform_to_clip_space(vertex, position_world, position_world_previous);
}
#else
gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    float3 position_world          = 0.0f;
    float3 position_world_previous = 0.0f;
    gbuffer_vertex vertex          = transform_to_world_space(input, instance_id, buffer_pass.transform, position_world, position_world_previous);
    vertex.material_index          = pass_get_material_index();
    return transform_to_clip_space(vertex, position_world, position_world_previous);
}
#endif

gbuffer main_ps(gbuffer_vertex vertex, bool is_front_face : SV_IsFrontFace)
{
#ifdef INDIRECT_DRAW
    _indirect_material_index = vertex.material_index;
#endif

    // material setup
    MaterialParameters material = GetMaterial();
    Surface surface;
    surface.flags               = material.flags;
    
    float4 albedo   = material.color;
    float3 normal   = vertex.normal.xyz;
    float roughness = material.roughness;
    float metalness = material.metallness;
    float occlusion = 1.0f;
    float emission  = 0.0f;

    // velocity computation
    float2 position_ndc          = uv_to_ndc(vertex.position.xy / (buffer_frame.resolution_render * buffer_frame.resolution_scale));
    float2 position_ndc_previous = vertex.position_previous.xy / vertex.position_previous.w;
    float2 position_ndc_jittered = position_ndc;
    position_ndc                -= buffer_frame.taa_jitter_current;
    position_ndc_previous       -= buffer_frame.taa_jitter_previous;
    float2 velocity              = position_ndc - position_ndc_previous;
    
    // world position and distance
    float3 position_world  = get_position(vertex.position.z, ndc_to_uv(position_ndc_jittered));
    float3 camera_to_pixel = position_world - buffer_frame.camera_position;
    float distance         = fast_sqrt(dot(camera_to_pixel, camera_to_pixel));

    // world space uv transformation
    if (any(material.world_space_uv))
    {
        float3 abs_normal = abs(normal);
        float2 uv_world   = position_world.yz * abs_normal.x + 
                            position_world.xz * abs_normal.y + 
                            position_world.xy * abs_normal.z;
        uv_world          = uv_world * material.tiling + material.offset;
        
        // branchless inversion
        float2 invert_mask = step(0.5f, material.invert_uv);
        vertex.uv_misc.xy  = lerp(uv_world, 1.0f - frac(uv_world) + floor(uv_world), invert_mask);
    }

    // albedo sampling
    float4 albedo_sample = 1.0f;
    if (surface.has_texture_albedo())
    {
        albedo_sample     = sample_texture(vertex, material_texture_index_albedo, surface, position_world);
        albedo_sample.rgb = srgb_to_linear(albedo_sample.rgb);
        albedo           *= albedo_sample;
    }

    // vegetation coloring
    if (surface.is_grass_blade())
    {
        uint instance_id     = vertex.uv_misc.w;
        float height_percent = vertex.uv_misc.z;
        albedo.rgb           = compute_grass_color(height_percent, hash(instance_id));
    }
    else if (surface.is_flower())
    {
        uint instance_id     = vertex.uv_misc.w;
        float height_percent = vertex.uv_misc.z;
        albedo.rgb           = compute_flower_color(height_percent, instance_id);
    }
    else if (surface.color_variation_from_instance())
    {
        float variation       = hash((uint)vertex.uv_misc.w);
        float3 variation_tint = lerp(vegetation_greener, vegetation_yellower, step(0.25f, variation));
        variation_tint        = lerp(variation_tint, vegetation_browner, step(0.5f, variation));
        albedo.rgb            = lerp(albedo.rgb, variation_tint, 0.15f);
    }

    // snow blending (applies to all surfaces)
    float snow_factor = get_snow_blend_factor(position_world, vertex.normal);
    albedo.rgb        = lerp(albedo.rgb, snow_color, snow_factor);

    // alpha: opaque pass forces alpha to 1 for non-transparent pixels
    albedo.a = lerp(albedo.a, 1.0f, step(albedo_sample.a, 1.0f) * pass_is_opaque());

    // emission
    if (surface.has_texture_emissive())
    {
        float3 emissive = GET_TEXTURE(material_texture_index_emission).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv_misc.xy).rgb;
        albedo.rgb     += emissive;
        emission        = luminance(emissive);
    }
    emission += luminance(albedo.rgb) * (float)material.emissive_from_albedo();
    
    // normal mapping
    float distance_fade = 1.0f;
    if (surface.has_texture_normal())
    {
        float3 normal_sample  = sample_texture(vertex, material_texture_index_normal, surface, position_world).xyz;
        float3 tangent_normal = normalize(unpack(normal_sample));
    
        // reconstruct z for bc5 two-channel normal maps
        tangent_normal.z = fast_sqrt(max(0.0f, 1.0f - dot(tangent_normal.xy, tangent_normal.xy)));
    
        // rotate normals to fake water waves/ripples
        if (surface.is_water())
        {
            static const float2 direction = float2(1.0f, 0.5f);
            static const float speed      = 0.5f;
            float time                    = (float)buffer_frame.time;
            float2 uv_offset              = direction * time * speed;
            float2 noise_uv               = vertex.uv_misc.xy + uv_offset;
            float noise                   = noise_perlin(noise_uv);
            float angle                   = noise * PI2;

            // rotate tangent normal.xy around z-axis (tangent space)
            float cos_a = cos(angle);
            float sin_a = sin(angle);
            float2 rotated_xy = float2(
                tangent_normal.x * cos_a - tangent_normal.y * sin_a,
                tangent_normal.x * sin_a + tangent_normal.y * cos_a
            );

            tangent_normal.xy    = rotated_xy;
            float2 tangent_xy_sq = tangent_normal.xy * tangent_normal.xy;
            tangent_normal.z     = fast_sqrt(max(0.0f, 1.0f - tangent_xy_sq.x - tangent_xy_sq.y));

            // flip if normal points down
            tangent_normal *= sign(tangent_normal.z);

            // fade normal texture beyond a certain distance to avoid high frequency noise from lower mips
            static const float fade_start = 200.0f;
            static const float fade_end   = 500.0f;
            static const float fade_range = fade_end - fade_start;
            distance_fade                 = saturate((fade_end - distance) / fade_range);
        }

        float normal_intensity     = saturate(max(0.01f, material.normal)) * distance_fade;
        tangent_normal.xy         *= normal_intensity;
        float3x3 tangent_to_world  = make_tangent_to_world_matrix(vertex.normal, vertex.tangent);
        normal                     = normalize(mul(tangent_normal, tangent_to_world).xyz);
    }

    // foliage curved normals
    if (surface.is_grass_blade() || surface.is_flower())
    {
        float curve_angle = clamp((vertex.width_percent - 0.5f) * 120.0f * DEG_TO_RAD, -PI * 0.5f, PI * 0.5f);
        
        float3 rotation_axis        = normalize(cross(vertex.normal, vertex.tangent));
        float3x3 curvature_rotation = rotation_matrix(rotation_axis, curve_angle);
        normal                      = normalize(mul(curvature_rotation, normal));
        vertex.tangent              = normalize(mul(curvature_rotation, vertex.tangent));

        // flip for back-faces (grass has no back-face geometry)
        float face_sign  = is_front_face * 2.0f - 1.0f;
        normal          *= face_sign;
        vertex.tangent  *= face_sign;
    }
    
    // packed material texture (occlusion, roughness, metalness)
    {
        float4 packed = sample_texture(vertex, material_texture_index_packed, surface, position_world);
        occlusion     = lerp(occlusion, packed.r, (float)material.has_texture_occlusion());
        roughness    *= lerp(1.0f, packed.g, (float)material.has_texture_roughness());
        metalness    *= lerp(1.0f, packed.b, (float)material.has_texture_metalness());
    }
    
    // specular anti-aliasing
    if (surface.has_texture_normal())
    {
        float3 dndu = ddx(normal);
        float3 dndv = ddy(normal);
        
        float variance        = (dot(dndu, dndu) + dot(dndv, dndv)) / max(0.001f, dot(normal, normal));
        float adaptive        = lerp(1.0f, 0.3f, saturate(distance * 0.1f));
        float roughness2      = roughness * roughness;
        roughness             = fast_sqrt(saturate(roughness2 + min(variance * adaptive, 0.02f)));
    }

    // output
    gbuffer g_buffer;
    g_buffer.albedo   = albedo;
    g_buffer.normal   = float4(normal, pass_get_material_index());
    g_buffer.material = float4(roughness, metalness, emission, occlusion);
    g_buffer.velocity = velocity;
    return g_buffer;
}
