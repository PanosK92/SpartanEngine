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
    float4 velocity : SV_Target3; // xy = ndc velocity, z = radial motion blur mask
};

// constants
static const float3 vegetation_greener  = float3(0.05f, 0.4f, 0.03f);
static const float3 vegetation_yellower = float3(0.45f, 0.4f, 0.15f);
static const float3 vegetation_browner  = float3(0.3f, 0.15f, 0.08f);
static const float3 grass_base          = float3(0.018f, 0.060f, 0.010f);
static const float3 grass_tip           = float3(0.055f, 0.180f, 0.025f);
static const float3 grass_var1          = float3(0.140f, 0.115f, 0.025f);
static const float3 grass_var2          = float3(0.015f, 0.045f, 0.008f);
static const float3 flower_base         = float3(0.05f, 0.07f, 0.03f);
static const float3 flower_blue         = float3(0.529f, 0.808f, 0.922f);
static const float3 flower_red          = float3(0.8f, 0.2f, 0.2f);
static const float3 flower_yellow       = float3(0.9f, 0.8f, 0.1f);
// parallax occlusion mapping
static const uint  POM_MAX_STEPS         = 40;
static const uint  POM_MIN_STEPS         = 12;
static const uint  POM_REFINE_ITERATIONS = 6;
static const float POM_FADE_START        = 25.0f;
static const float POM_FADE_END          = 50.0f;
static const float POM_HEIGHT_SCALE      = 0.04f;

static float4 sample_texture(gbuffer_vertex vertex, uint texture_index, Surface surface, float3 world_pos)
{
    return GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv_misc.xy);
}

// compute grass blade color with variation
float3 compute_grass_color(float height_percent, float variation)
{
    float t           = smoothstep(0.2f, 1.0f, height_percent);
    float3 grass_tint = lerp(grass_base, grass_tip, t);
    
    // branchless color variation
    float3 var_color = lerp(grass_tint, grass_var1, step(0.33f, variation));
    var_color        = lerp(var_color, grass_var2, step(0.66f, variation));
    return lerp(grass_tint, var_color, 0.18f);
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
gbuffer_vertex main_vs(uint vertex_id : SV_VertexID, uint view_id : SV_ViewID)
{
    MeshletInstance mi;
    Vertex_PosUvNorTan input = pull_visible_triangle_vertex(vertex_id, mi);
    uint instance_id         = mi.instance_index;
#elif defined(GRASS_INSTANCED)
gbuffer_vertex main_vs(Vertex_PosUvNorTan_Cpu cpu_input, uint instance_id : SV_InstanceID, uint view_id : SV_ViewID)
{
    Vertex_PosUvNorTan input = to_full_vertex(cpu_input);
    // pull the per-instance transform from the dedicated procedural grass buffer
    // lod_base in values[0].z lets the same vs handle all three lod rings
    uint slot        = instance_id + (uint)buffer_pass.values[0].z;
    GrassInstance gi = grass_instances[slot];
    input.instance_position_x = gi.pos_x;
    input.instance_position_y = gi.pos_y;
    input.instance_position_z = gi.pos_z;
    input.instance_normal_oct = (gi.normal_yaw_scale >> 16) & 0xFFFFu;
    input.instance_yaw        = (gi.normal_yaw_scale >> 8)  & 0xFFu;
    input.instance_scale      =  gi.normal_yaw_scale        & 0xFFu;
    // synthesize an identity per-renderable draw data, the instance carries the world transform
    _draw                    = (DrawData)0;
    _draw.transform          = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    _draw.transform_previous = _draw.transform;
    _draw.material_index     = buffer_pass.material_index;
    _draw.uv_tiling          = float2(1.0f, 1.0f);

    // a solid detail instance, a stone chip, has a planar uv that covers the whole material, so every
    // one of them would carry an identical copy of the texture. give each a small random patch of it
    // instead and a field of chips reads as many different pieces of the same stone
    float uv_patch = buffer_pass.values[0].x;
    if (uv_patch > 0.0f)
    {
        uint h = (asuint(gi.pos_x) * 73856093u) ^
                 (asuint(gi.pos_z) * 19349663u) ^
                 (gi.normal_yaw_scale * 83492791u);
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;

        _draw.uv_tiling = float2(uv_patch, uv_patch);
        _draw.uv_offset = float2(
            float(h & 0xFFFFu),
            float((h >> 16) & 0xFFFFu)
        ) * (1.0f / 65535.0f) * (1.0f - uv_patch);
    }
#else
gbuffer_vertex main_vs(Vertex_PosUvNorTan_Cpu cpu_input, uint instance_id : SV_InstanceID, uint view_id : SV_ViewID)
{
    Vertex_PosUvNorTan input = to_full_vertex(cpu_input);
    _draw = draw_data[buffer_pass.draw_index];
#endif

    float3 position_world          = 0.0f;
    float3 position_world_previous = 0.0f;
    gbuffer_vertex vertex          = transform_to_world_space(input, instance_id, _draw.transform, position_world, position_world_previous);
    vertex.material_index          = _draw.material_index;
    return transform_to_clip_space(vertex, position_world, position_world_previous, view_id);
}

gbuffer main_ps(gbuffer_vertex vertex, bool is_front_face : SV_IsFrontFace)
{
    // restore material index from vertex output (works for both indirect and cpu-driven draws)
    pass_load_draw_data_from_vertex(vertex.material_index);

    // material setup
    MaterialParameters material = GetMaterial();
    Surface surface;
    surface.flags               = material.flags;

    // two sided transparents (glass, water) render with cull none so the back face of
    // the shell is rasterized when the camera is on the other side, the geometric
    // normal still points outward in that case which makes the gbuffer normal face
    // away from the viewer, fresnel collapses to grazing in the refraction pass and
    // the surface ends up looking opaque/mirror like instead of transparent, flipping
    // both normal and tangent here keeps the shading frame oriented towards the
    // camera regardless of which face is hit
    if (pass_is_transparent() && !is_front_face)
    {
        vertex.normal  = -vertex.normal;
        vertex.tangent = -vertex.tangent;
    }

    float4 albedo   = material.color;
    float3 normal   = vertex.normal.xyz;
    float roughness = material.roughness;
    float metalness = material.metalness;
    float occlusion = 1.0f;
    float emission  = 0.0f;

    // velocity computation
    float2 position_ndc          = uv_to_ndc(vertex.position.xy / get_render_resolution_active());
    float2 position_ndc_previous = vertex.position_previous.xy / vertex.position_previous.w;
    float2 position_ndc_jittered = position_ndc;
    position_ndc                -= buffer_frame.taa_jitter_current;
    position_ndc_previous       -= buffer_frame.taa_jitter_previous;
    float2 velocity              = position_ndc - position_ndc_previous;
    
    // world position and distance
    // in multiview the g-buffer is drawn in a single call for both eyes, so buffer_pass.eye_index
    // is static and cannot be used to pick the right eye's inverse vp. drive the per-fragment
    // eye from the interpolated SV_ViewID (vertex.view_id) instead.
    float3 position_world  = get_position_for_view(vertex.position.z, ndc_to_uv(position_ndc_jittered), vertex.view_id);
    float3 camera_to_pixel = position_world - get_camera_position_for_view(vertex.view_id);
    float distance         = fast_sqrt(dot(camera_to_pixel, camera_to_pixel));

    // taken at top level, the terrain paths below sit behind branches where a derivative is undefined
    float3 dpdx_world = ddx(position_world);
    float3 dpdy_world = ddy(position_world);

    // world space uv transformation
    // the full uv state is per-renderable, forwarded by the vs through uv_xform_ts/uv_xform_ir
    float  uv_world_space = vertex.uv_xform_ir.w;
    if (uv_world_space > 0.0f)
    {
        float2 uv_tiling   = vertex.uv_xform_ts.xy;
        float2 uv_offset   = vertex.uv_xform_ts.zw;
        float2 uv_invert   = vertex.uv_xform_ir.xy;
        float  uv_rotation = vertex.uv_xform_ir.z;

        float2 uv_world = compute_world_space_uv(position_world, normal);
        uv_world        = uv_world * uv_tiling + uv_offset;

        // branchless inversion
        float2 invert_mask = step(0.5f, uv_invert);
        uv_world           = lerp(uv_world, 1.0f - frac(uv_world) + floor(uv_world), invert_mask);

        if (uv_rotation != 0.0f)
            uv_world = rotate_uv_90(uv_world, uv_rotation);

        vertex.uv_misc.xy = uv_world;
    }

    // terrain
    // one evaluator produces albedo, normal and orm from the same layer weights, the old path
    // blended three maps three separate times with the weights recomputed for each
    bool terrain_shaded = false;
    if (surface.is_terrain() && material.terrain_layer_count > 0)
    {
        float3 dpdx = dpdx_world;
        float3 dpdy = dpdy_world;

        // planar world xz, tiling is repeats per meter, the ray tracing passes derive the same uv
        // from the hit position so raster and gi cannot drift apart
        float2 tiling = vertex.uv_xform_ts.xy;
        float2 uv     = position_world.xz * tiling + vertex.uv_xform_ts.zw;
        float2 duvdx  = dpdx.xz * tiling;
        float2 duvdy  = dpdy.xz * tiling;
        vertex.uv_misc.xy = uv;

        // which layer owns a pixel must not depend on where the camera stands, a distance driven mip
        // feeds blurrier analysis into the scores the further out you are, so the boundaries crawl and
        // the ranking flips as you move toward a hillside, the maps are already low frequency
        float analysis_lod = 0.0f;

        TerrainAnalysis analysis = terrain_sample_analysis(material, position_world, analysis_lod);
        float slope_radians      = acos(saturate(dot(vertex.normal, float3(0.0f, 1.0f, 0.0f))));
        TerrainLayerPick pick    = terrain_pick_layers(material, analysis, position_world, vertex.normal, slope_radians);

        TerrainSurface terrain = terrain_evaluate(
            material, pick, analysis, position_world, vertex.normal, uv, duvdx, duvdy, dpdx, dpdy, distance
        );

        albedo.rgb     *= terrain.albedo;
        normal          = terrain.normal;
        roughness       = terrain.roughness;
        metalness       = terrain.metalness;
        occlusion       = terrain.occlusion;
        terrain_shaded  = true;
    }

    // parallax occlusion mapping, gated on height texture
    // uses offset limiting (no v.z divide), a grazing fade to kill warp at glancing angles,
    // and an analytical sub-step intersection that removes residual contour stepping
    if (surface.has_texture_height() && !surface.is_terrain() && !surface.is_grass_blade() && !surface.is_flower() && !surface.is_water())
    {
        float3x3 world_to_tangent = make_world_to_tangent_matrix(vertex.normal, vertex.tangent);
        float3 v_tangent          = normalize(mul(-camera_to_pixel, world_to_tangent));

        float distance_fade = saturate((POM_FADE_END - distance) / (POM_FADE_END - POM_FADE_START));
        float n_dot_v       = saturate(v_tangent.z);
        float grazing_fade  = smoothstep(0.1f, 0.4f, n_dot_v);
        float fade          = distance_fade * grazing_fade;

        if (fade > 0.0f)
        {
            float max_disp  = material.height * POM_HEIGHT_SCALE * fade;
            uint  num_steps = (uint)lerp(POM_MAX_STEPS, POM_MIN_STEPS, n_dot_v);

            float2 dx = ddx(vertex.uv_misc.xy);
            float2 dy = ddy(vertex.uv_misc.xy);

            // offset limited delta_uv, total shift across the march is bounded by max_disp regardless of view angle
            float2 delta_uv  = v_tangent.xy * max_disp / num_steps;
            float  layer_h   = 1.0f / num_steps;
            float2 cur_uv    = vertex.uv_misc.xy;
            float  cur_layer = 1.0f;
            float  cur_samp  = GET_TEXTURE(material_texture_index_packed).SampleGrad(GET_SAMPLER(sampler_bilinear_wrap), cur_uv, dx, dy).a;

            // track the previous straddling sample so we can solve the exact intersection at the end
            float2 prev_uv    = cur_uv;
            float  prev_layer = cur_layer;
            float  prev_samp  = cur_samp;

            // shift the layer grid by a fraction of a step, taa averages the moving slices away
            if (is_taa_enabled())
            {
                float dither = noise_interleaved_gradient(vertex.position.xy, false);
                cur_uv    -= delta_uv * dither;
                cur_layer -= layer_h * dither;
                cur_samp   = GET_TEXTURE(material_texture_index_packed).SampleGrad(GET_SAMPLER(sampler_bilinear_wrap), cur_uv, dx, dy).a;
            }

            // steep parallax linear search
            [loop]
            while (cur_layer > cur_samp && cur_layer > 0.0f)
            {
                prev_uv    = cur_uv;
                prev_layer = cur_layer;
                prev_samp  = cur_samp;

                cur_uv    -= delta_uv;
                cur_layer -= layer_h;
                cur_samp   = GET_TEXTURE(material_texture_index_packed).SampleGrad(GET_SAMPLER(sampler_bilinear_wrap), cur_uv, dx, dy).a;
            }

            // binary search refinement, narrows the bracket while preserving the above/below invariant
            [unroll(POM_REFINE_ITERATIONS)]
            for (uint i = 0; i < POM_REFINE_ITERATIONS; ++i)
            {
                float2 mid_uv    = (cur_uv + prev_uv)       * 0.5f;
                float  mid_layer = (cur_layer + prev_layer) * 0.5f;
                float  mid_samp  = GET_TEXTURE(material_texture_index_packed).SampleGrad(GET_SAMPLER(sampler_bilinear_wrap), mid_uv, dx, dy).a;
                bool   above     = mid_layer > mid_samp;

                cur_uv     = above ? mid_uv     : cur_uv;
                cur_layer  = above ? mid_layer  : cur_layer;
                cur_samp   = above ? mid_samp   : cur_samp;
                prev_uv    = above ? prev_uv    : mid_uv;
                prev_layer = above ? prev_layer : mid_layer;
                prev_samp  = above ? prev_samp  : mid_samp;
            }

            // analytical intersection between the ray and the linear segment connecting the two samples,
            // this is what eliminates the staircase that pure binary search leaves behind
            float h_above_prev = max(prev_layer - prev_samp, 0.0f);
            float h_below_cur  = max(cur_samp   - cur_layer, 0.0f);
            float t            = h_above_prev / max(h_above_prev + h_below_cur, 1e-5f);
            vertex.uv_misc.xy  = lerp(prev_uv, cur_uv, t);
        }
    }

    // albedo sampling
    float4 albedo_sample = 1.0f;
    if (!terrain_shaded && surface.has_texture_albedo())
    {
        albedo_sample     = sample_texture(vertex, material_texture_index_albedo, surface, position_world);
        if (material.is_albedo_srgb())
        {
            albedo_sample.rgb = srgb_to_linear(albedo_sample.rgb);
        }
        albedo           *= albedo_sample;
    }

    // vegetation coloring
    if (surface.is_grass_blade())
    {
        float height_percent = vertex.uv_misc.z;
        float variation      = vertex.uv_misc.w;
        albedo.rgb           = compute_grass_color(
            height_percent,
            variation
        );
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

    // alpha: opaque pass forces alpha to 1 for non-transparent pixels
    albedo.a = lerp(albedo.a, 1.0f, step(albedo_sample.a, 1.0f) * pass_is_opaque());
    if (surface.is_skid_mark())
    {
        // stain mask is texture alpha times vertex fade, color.a is only the transparent pass ticket
        albedo.a = saturate(albedo_sample.a) * saturate(vertex.uv_misc.z);
    }

    // emission
    if (surface.has_texture_emissive())
    {
        float3 emissive = GET_TEXTURE(material_texture_index_emission).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv_misc.xy).rgb;
        if (material.is_emissive_srgb())
        {
            emissive = srgb_to_linear(emissive);
        }
        albedo.rgb     += emissive;
        emission        = luminance(emissive);
    }
    // emissive_from_albedo strength is authored 0-1, composition maps 1.0 to the calibrated nits
    if (material.emissive_from_albedo())
        emission = material.emissive_strength;
    
    // normal mapping
    float distance_fade = 1.0f;
    if (!terrain_shaded && surface.has_texture_normal())
    {
        float3 normal_sample  = sample_texture(vertex, material_texture_index_normal, surface, position_world).xyz;
        float3 tangent_normal = normalize(unpack(normal_sample));
    
        // reconstruct z for bc5 two-channel normal maps
        tangent_normal.z = fast_sqrt(max(0.0f, 1.0f - dot(tangent_normal.xy, tangent_normal.xy)));

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
    if (
        !terrain_shaded &&
        (
            material.has_texture_occlusion() ||
            material.has_texture_roughness() ||
            material.has_texture_metalness()
        )
    )
    {
        float4 packed = sample_texture(vertex, material_texture_index_packed, surface, position_world);
        occlusion     = lerp(occlusion, packed.r, (float)material.has_texture_occlusion());
        roughness    *= lerp(1.0f, packed.g, (float)material.has_texture_roughness());
        metalness    *= lerp(1.0f, packed.b, (float)material.has_texture_metalness());
    }

    // fft ocean shading, normal from the displaced surface so lighting follows the swell
    if (surface.is_water() && buffer_frame.ocean_enabled > 0.5f)
    {
        float foam = 0.0f;
        sample_ocean_surface(vertex.ocean_world_xz, distance, normal, foam);

        // distance hides the sub-texel slope variance, lift roughness with distance so far water reads as a glitter sheet, not a sharp mirror
        float distance_fade = saturate(distance / 600.0f);
        roughness           = lerp(roughness, 0.35f, distance_fade * distance_fade);

        // foam reads as whitewater in the transparency pass, keep its roughness moderate here so its reflection stays bright enough to light the foam, a full rough surface collapses the reflection to black
        roughness  = lerp(roughness, 0.5f, foam);
    }

    if (material.flake_strength > 0.0f)
    {
        float flake_scale = max(material.flake_scale, 1.0f);
        float2 flake_cell = floor(vertex.uv_misc.xy * flake_scale);
        float flake_hash  = hash(flake_cell + floor(position_world.xz * 0.25f));
        float sparkle     = pow(saturate(flake_hash), 48.0f) * saturate(material.flake_strength);
        float3 flake_tint = lerp(albedo.rgb, float3(1.0f, 1.0f, 1.0f), 0.35f);
        albedo.rgb       += sparkle * flake_tint * 0.35f;
        roughness         = lerp(roughness, roughness * 0.65f, sparkle);
        metalness         = saturate(metalness + sparkle * 0.12f);
    }
    
    // terrain blending, the ground creeps up over whatever sinks into it so a prop reads as bedded in
    // rather than parked on top, grass and flowers are excluded because their whole height sits inside
    // the band and they would turn into ground
    bool terrain_blendable =
        !terrain_shaded           &&
        pass_is_opaque()          &&
        !surface.is_grass_blade() &&
        !surface.is_flower()      &&
        !surface.is_water()       &&
        !surface.is_skid_mark();

    if (terrain_blendable)
    {
        float band = buffer_frame.terrain_blend_height * material.terrain_blend;

        TerrainBlend blend = terrain_blend_evaluate(
            position_world, normal, dpdx_world, dpdy_world, distance, band, material.terrain_blend_sharpness
        );

        // the normal rides its own wider weight, that bend is what turns the contact into a rounded
        // fillet instead of two surfaces meeting at a corner
        if (blend.weight_normal > 0.0f)
        {
            normal = terrain_blend_normal_arc(normal, blend.normal, blend.weight_normal);
        }

        if (blend.weight > 0.0f)
        {
            albedo.rgb = lerp(albedo.rgb, blend.albedo, blend.weight);
            roughness  = lerp(roughness, blend.roughness, blend.weight);
            metalness  = lerp(metalness, blend.metalness, blend.weight);
            occlusion  = lerp(occlusion, blend.occlusion, blend.weight);
            emission   = lerp(emission, 0.0f, blend.weight);
        }
    }

    // geometric specular antialiasing, yamada 2018, the screen space normal variance is folded
    // into the ggx width so sub pixel detail rolls off into roughness instead of shimmering
    // water has analytic normals rather than a normal texture so it is admitted explicitly
    if (surface.has_texture_normal() || surface.is_water() || terrain_shaded)
    {
        const float SPECULAR_AA_SIGMA2 = 0.25f;
        const float SPECULAR_AA_KAPPA  = 0.18f;

        float3 dndu = ddx(normal);
        float3 dndv = ddy(normal);

        float variance  = SPECULAR_AA_SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
        float kernel    = min(2.0f * variance, SPECULAR_AA_KAPPA);
        roughness       = fast_sqrt(saturate(roughness * roughness + kernel));
    }

    // output
    gbuffer g_buffer;
    g_buffer.albedo   = albedo;
    g_buffer.normal   = float4(normal, pass_get_material_index());
    g_buffer.material = float4(roughness, metalness, emission, occlusion);
    g_buffer.velocity = float4(velocity, material.is_motion_blur_radial() ? 1.0f : 0.0f, surface.is_skid_mark() ? 1.0f : 0.0f);
    return g_buffer;
}
