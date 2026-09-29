/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_COMMON_RAY_SURFACE
#define SPARTAN_COMMON_RAY_SURFACE

//= INCLUDES =====================
#include "common_road.hlsl"
#include "common_decals.hlsl"
#include "common_puddles.hlsl"
#include "common_ray_hit.hlsl"
#include "common_radiance_cache.hlsl"
#include "sky/clouds.hlsl"
//================================

// surfaces met by reflection, bounce and refraction rays, reconstructed from the hit triangle with
// the same material evaluation the g-buffer runs (terrain layers, road weathering, decals, puddles and
// rain), so a reflection shows the surface the camera would see

struct RaySurface
{
    float3 position;
    float  hit_distance;
    float3 normal;
    float3 geometric_normal;
    float3 albedo;
    float  roughness;
    float  metallic;
    float3 emission;
    uint   material_index;
    bool   hit;
};

// ray cones, akenine-moller 2021, a ray carries a cone of width and spread angle, at the hit its
// footprint over the texel density of the triangle picks the mip, each texture adds its own resolution
float2 ray_cone_propagate(float2 cone, float distance)
{
    return float2(cone.x + cone.y * distance, cone.y);
}

float ray_cone_mip(float lod, uint texture_index)
{
    uint width, height, levels;
    material_textures[NonUniformResourceIndex(texture_index)].GetDimensions(0, width, height, levels);
    return clamp(lod + 0.5f * log2(float(width) * float(height)), 0.0f, float(levels) - 1.0f);
}

// cone is the width in metres and the spread in radians at the ray origin
RaySurface ray_surface_reconstruct(float ray_t, uint instance_index, uint primitive_index, float3 bary, RayDesc ray, float2 cone)
{
    RaySurface surface = (RaySurface)0;
    surface.hit        = ray_t >= 0.0f;
    if (!surface.hit)
        return surface;

    GeometryInfo geo       = geometry_infos[instance_index];
    uint material_index    = geo.material_index;
    MaterialParameters mat = material_parameters[material_index];

    uint index_base = geo.index_offset + primitive_index * 3;
    uint i0         = geometry_indices[index_base + 0];
    uint i1         = geometry_indices[index_base + 1];
    uint i2         = geometry_indices[index_base + 2];

    PulledVertex v0 = geometry_vertices[geo.vertex_offset + i0];
    PulledVertex v1 = geometry_vertices[geo.vertex_offset + i1];
    PulledVertex v2 = geometry_vertices[geo.vertex_offset + i2];

    float2 uv0 = unpack_vertex_uv(v0.uv);
    float2 uv1 = unpack_vertex_uv(v1.uv);
    float2 uv2 = unpack_vertex_uv(v2.uv);

    float2 texcoord       = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    float3 normal_object  = normalize(unpack_vertex_oct(v0.normal) * bary.x + unpack_vertex_oct(v1.normal) * bary.y + unpack_vertex_oct(v2.normal) * bary.z);
    float3 tangent_object = normalize(unpack_vertex_oct(v0.tangent) * bary.x + unpack_vertex_oct(v1.tangent) * bary.y + unpack_vertex_oct(v2.tangent) * bary.z);

    float3x3 obj_to_world = float3x3(geo.object_to_world_0.xyz, geo.object_to_world_1.xyz, geo.object_to_world_2.xyz);
    float3x3 world_to_obj = float3x3(geo.world_to_object_0.xyz, geo.world_to_object_1.xyz, geo.world_to_object_2.xyz);
    float3 normal_world   = normalize(mul(normal_object, transpose(world_to_obj)));
    float3 tangent_world  = normalize(mul(tangent_object, obj_to_world));
    float3 edge1_world    = mul(v1.position - v0.position, obj_to_world);
    float3 edge2_world    = mul(v2.position - v0.position, obj_to_world);
    float3 face           = cross(edge1_world, edge2_world);
    float  face_length    = length(face);
    float3 geometric      = face_length > 1e-12f ? face / face_length : normal_world;
    if (dot(geometric, ray.Direction) > 0.0f)
        geometric = -geometric;
    if (dot(normal_world, geometric) < 0.0f)
        normal_world = -normal_world;

    float3 hit_pos  = ray.Origin + ray.Direction * ray_t;
    float2 uv_scale = abs(geo.uv_tiling);
    float  density; // texture area per world area
    if (mat.is_terrain())
    {
        // terrain maps planar world xz with tiling as repeats per meter, matches the raster path
        texcoord = hit_pos.xz * geo.uv_tiling + geo.uv_offset;
        density  = uv_scale.x * uv_scale.y;
    }
    else if (geo.uv_world_space > 0.0f)
    {
        float2 uv_world = compute_world_space_uv(hit_pos, normal_world);
        uv_world        = uv_world * geo.uv_tiling + geo.uv_offset;

        // branchless inversion
        float2 invert_mask = step(0.5f, geo.uv_invert);
        texcoord           = lerp(uv_world, 1.0f - frac(uv_world) + floor(uv_world), invert_mask);
        density            = uv_scale.x * uv_scale.y;
    }
    else
    {
        texcoord       = texcoord * geo.uv_tiling + geo.uv_offset;
        float2 duv1    = uv1 - uv0;
        float2 duv2    = uv2 - uv0;
        float uv_area  = abs(duv1.x * duv2.y - duv1.y * duv2.x) * uv_scale.x * uv_scale.y;
        density        = uv_area / max(face_length, 1e-12f);
    }

    if (geo.uv_rotation != 0.0f)
        texcoord = rotate_uv_90(texcoord, geo.uv_rotation);

    // footprint of the cone at the hit, widened as the surface turns away from the ray
    float footprint = max(ray_cone_propagate(cone, ray_t).x, 1e-4f);
    float cosine    = max(abs(dot(geometric, ray.Direction)), 0.1f);
    float cone_lod  = 0.5f * log2(max(density, 1e-12f)) + log2(footprint / cosine);

    // terrain layers resolve their own textures, 2k is what they are authored at
    bool terrain_shaded    = mat.is_terrain() && mat.terrain_layer_count > 0;
    TerrainSurface terrain = (TerrainSurface)0;
    if (terrain_shaded)
    {
        terrain = terrain_shade_lod(mat, hit_pos, normal_world, texcoord, clamp(cone_lod + 11.0f, 0.0f, 10.0f));
    }

    // normal mapping, same two channel decode and strength as the g-buffer
    if (!terrain_shaded && mat.has_texture_normal())
    {
        uint  normal_texture_index = get_material_texture_index(material_index, material_texture_index_normal);
        float normal_mip           = ray_cone_mip(cone_lod, normal_texture_index);
        float3 normal_sample       = material_textures[NonUniformResourceIndex(normal_texture_index)].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, normal_mip).xyz;
        // the texture's missing blue channel is zero, not a negative tangent space z, using it flips
        // the normal behind the wall and the visibility rays from the hit start outside the room
        normal_sample     = normalize(normal_sample * 2.0f - 1.0f);
        normal_sample.z   = sqrt(max(0.0f, 1.0f - dot(normal_sample.xy, normal_sample.xy)));
        normal_sample.xy *= saturate(max(0.01f, mat.normal));

        tangent_world          = normalize(tangent_world - normal_world * dot(tangent_world, normal_world));
        float3 bitangent_world = normalize(cross(normal_world, tangent_world));
        float3x3 tbn           = float3x3(tangent_world, bitangent_world, normal_world);
        normal_world           = normalize(mul(normal_sample, tbn));
    }
    else if (terrain_shaded)
    {
        normal_world = terrain.normal;
    }

    float albedo_mip = 0.0f;
    float3 albedo    = mat.color.rgb;
    if (terrain_shaded)
    {
        albedo = terrain.albedo;
    }
    else if (mat.has_texture_albedo())
    {
        uint albedo_texture_index = get_material_texture_index(material_index, material_texture_index_albedo);
        albedo_mip                = ray_cone_mip(cone_lod, albedo_texture_index);
        float4 sampled_albedo     = material_textures[NonUniformResourceIndex(albedo_texture_index)].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, albedo_mip);
        if (mat.is_albedo_srgb())
        {
            sampled_albedo.rgb = srgb_to_linear(sampled_albedo.rgb);
        }
        if (sampled_albedo.a > 0.01f)
        {
            albedo = sampled_albedo.rgb * mat.color.rgb;
        }
    }

    float roughness = mat.roughness;
    float metallic  = mat.metalness;
    if (terrain_shaded)
    {
        roughness = terrain.roughness;
        metallic  = terrain.metalness;
    }
    else if (mat.has_texture_roughness() || mat.has_texture_metalness())
    {
        uint packed_texture_index = get_material_texture_index(material_index, material_texture_index_packed);
        float4 packed = material_textures[NonUniformResourceIndex(packed_texture_index)].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, ray_cone_mip(cone_lod, packed_texture_index));
        roughness *= lerp(1.0f, packed.g, (float)mat.has_texture_roughness());
        metallic  *= lerp(1.0f, packed.b, (float)mat.has_texture_metalness());
    }

    // evaluate the same emitter at the ray hit as at a primary raster hit
    float3 emission = 0.0f;
    if (mat.emissive_from_albedo())
    {
        emission = albedo * mat.emissive_strength * photometric_to_radiometric(lighting_emissive_nits_from_albedo);
    }
    else if (mat.has_texture_emissive())
    {
        uint emission_texture_index = get_material_texture_index(material_index, material_texture_index_emission);
        float3 sampled_emission     = material_textures[NonUniformResourceIndex(emission_texture_index)].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, ray_cone_mip(cone_lod, emission_texture_index)).rgb;
        if (mat.is_emissive_srgb())
        {
            sampled_emission = srgb_to_linear(sampled_emission);
        }
        emission = sampled_emission * photometric_to_radiometric(lighting_emissive_nits_texture);
    }

    road_weathering(mat.flags, hit_pos, texcoord, .6, albedo, roughness, exp2(albedo_mip) * road_asphalt_repeat / 4096.0);
    float occlusion         = terrain_shaded ? terrain.occlusion : 1.0f;
    float3 decal_tangent    = normalize(tangent_world - geometric * dot(tangent_world, geometric));
    float3 decal_bitangent  = normalize(cross(geometric, decal_tangent));
    apply_decals(uint2(geo.decal_offset, geo.decal_count), hit_pos, geometric,
        decal_tangent * footprint, decal_bitangent * footprint, albedo, normal_world,
        roughness, metallic, occlusion, emission);

    // standing water and rain, the relief a ray hit can resolve is the terrain height, road ruts need raster uvs
    bool  is_road      = (mat.flags & (1u << 22)) != 0;
    bool  is_paint     = (mat.flags & (1u << 23)) != 0;
    float relief       = terrain_shaded ? (0.5f - terrain.height) * 0.05f - 0.035f : (1.0f - occlusion) * 0.08f;
    float rain_exposed = 0.0f;
    float puddle_water = ground_water_apply(terrain_shaded, is_road, is_paint, hit_pos, geometric, relief, footprint,
        rain_exposed, albedo, normal_world, roughness, metallic, occlusion);
    bool is_water = (mat.flags & (1u << 13)) != 0;
    if ((rain_exposed > 0.0f || puddle_water > 0.0f) && !is_water)
    {
        bool is_ground = terrain_shaded || is_road || is_paint;
        RainSurface rain;
        rain.vehicle          = false;
        rain.position         = hit_pos;
        rain.geometric_normal = geometric;
        rain.object_origin    = 0.0f;
        rain.object_rotation  = float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
        rain.porosity         = is_ground ? (terrain_shaded ? 0.88f : ground_water_porosity(terrain_shaded, is_paint)) : saturate((roughness - 0.3f) * 1.8f) * (1.0f - metallic);
        rain.water            = puddle_water;
        rain.footprint        = footprint;
        rain.exposure         = rain_exposed;
        rain.macro_height     = -1.0f;
        rain.detail           = false;
        rain_apply(rain, albedo, normal_world, roughness, metallic, false);
    }
    if (dot(normal_world, geometric) < 0.0f)
        normal_world = normalize(normal_world - geometric * dot(normal_world, geometric) * 1.01f);

    surface.position         = hit_pos;
    surface.hit_distance     = ray_t;
    surface.normal           = normal_world;
    surface.geometric_normal = geometric;
    surface.albedo           = saturate(albedo);
    surface.roughness        = max(roughness, 0.04f);
    surface.metallic         = saturate(metallic);
    surface.emission         = emission;
    surface.material_index   = material_index;
    return surface;
}

// inline closest hit for compute passes, cutouts count as solid like every other secondary ray here
RaySurface ray_surface_trace(RayDesc ray, float2 cone)
{
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return (RaySurface)0;

    float2 b = query.CommittedTriangleBarycentrics();
    return ray_surface_reconstruct(query.CommittedRayT(), query.CommittedInstanceIndex(), query.CommittedPrimitiveIndex(), float3(1.0f - b.x - b.y, b.x, b.y), ray, cone);
}

// karis 2014 analytic split sum environment brdf, avoids binding the brdf lut in ray passes
float2 ray_env_brdf(float roughness, float n_dot_v)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r        = roughness * c0 + c1;
    float  a004     = min(r.x * r.x, exp2(-9.28f * n_dot_v)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

float ray_spatial_hash_unit(float2 pixel_xy)
{
    return frac(52.9829189f * frac(pixel_xy.x * 0.06711056f + pixel_xy.y * 0.00583715f));
}

float2 ray_concentric_disk(float2 u)
{
    if (u.x == 0.0f && u.y == 0.0f)
        return float2(0.0f, 0.0f);

    float r;
    float theta;
    if (abs(u.x) > abs(u.y))
    {
        r     = u.x;
        theta = (PI * 0.25f) * (u.y / u.x);
    }
    else
    {
        r     = u.y;
        theta = (PI * 0.5f) - (PI * 0.25f) * (u.x / u.y);
    }
    return r * float2(cos(theta), sin(theta));
}

// one inline ray traced visibility sample for any light type at a ray hit, returns 0..1
float ray_trace_shadow(LightParameters light_p, float3 hit_position, float3 hit_normal, float2 pixel_xy)
{
    bool is_directional = (light_p.flags & uint(1U << 0)) != 0;
    bool is_area        = (light_p.flags & uint(1U << 6)) != 0;

    // self intersection bias
    float bias    = 0.005f;
    float3 origin = hit_position + hit_normal * bias;

    // per pixel rotation of the source sample, the denoiser integrates the penumbra over frames
    float rot_angle = frac(ray_spatial_hash_unit(pixel_xy) + (float)buffer_frame.frame * 0.618034f) * PI2;
    float2 u        = float2(0.5f, 0.333333f) * 2.0f - 1.0f;
    float2 disk     = ray_concentric_disk(u);
    float2 disk_r   = float2(disk.x * cos(rot_angle) - disk.y * sin(rot_angle), disk.x * sin(rot_angle) + disk.y * cos(rot_angle));

    float3 to_light_center = light_p.position.xyz - origin;
    float  center_dist     = length(to_light_center);
    float3 light_dir_unit  = is_directional ? normalize(-light_p.direction.xyz) : (center_dist > 0.0001f ? to_light_center / center_dist : float3(0.0f, 1.0f, 0.0f));
    float3 up_axis         = abs(light_dir_unit.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent         = normalize(cross(up_axis, light_dir_unit));
    float3 bitangent       = cross(light_dir_unit, tangent);

    float3 direction;
    float  t_max;
    if (is_directional)
    {
        // jittered cone around the sun direction matching trace_inline_shadow_ray
        const float angular_radius = 0.0093f;
        direction = normalize(light_dir_unit + (tangent * disk_r.x + bitangent * disk_r.y) * angular_radius);
        t_max     = 10000.0f;
    }
    else if (is_area)
    {
        // a point on the rectangle from the light's authored right vector
        float3 area_right   = normalize(light_p.direction_right.xyz);
        float3 area_up      = normalize(cross(light_p.direction.xyz, area_right));
        float  safety       = min(light_p.area_width, light_p.area_height) * 0.5f + 0.005f;
        float3 sample_point = light_p.position.xyz + area_right * disk_r.x * (light_p.area_width * 0.5f) + area_up * disk_r.y * (light_p.area_height * 0.5f);
        float3 to_light     = sample_point - origin;
        float  dist         = length(to_light);
        if (dist < 0.0001f)
            return 1.0f;
        direction = to_light / dist;
        t_max     = max(dist - safety, bias);
    }
    else
    {
        // point and spot, a small spherical source for a soft penumbra
        if (center_dist < 0.0001f)
            return 1.0f;
        const float light_radius = 0.05f;
        float3 to_jit            = light_p.position.xyz + (tangent * disk_r.x + bitangent * disk_r.y) * light_radius - origin;
        float  dist              = length(to_jit);
        direction                = to_jit / dist;
        t_max                    = max(dist - bias * 2.0f, bias);
    }

    // back facing samples carry no light
    if (dot(hit_normal, direction) <= 0.0f)
        return 1.0f;

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001f;
    ray.TMax      = max(t_max, 0.001f);

    // force opaque, alpha tested foliage is non opaque in the tlas and a single proceed would stall on it
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> query;
    // opaque and glass blockers, grass (0x04) casts only screen space shadows
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x03, ray);
    query.Proceed();
    float visibility = query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;

    // the clouds are not in the tlas, under an overcast sky the sun the raster lighting dims to a
    // glow would otherwise light every reflected surface at full strength (tex6 is the cloud shadow map)
    if (is_directional && visibility > 0.0f)
    {
        visibility *= cloud_shadow_sample(tex6, GET_SAMPLER(sampler_bilinear_clamp), hit_position, light_dir_unit, get_camera_position());
    }
    return visibility;
}

// single ray sky visibility test, 1 when the direction reaches the sky within 100 m
float ray_trace_sky_visibility(float3 hit_position, float3 hit_normal, float3 trace_dir)
{
    RayDesc ray;
    ray.Origin    = hit_position + hit_normal * 0.005f;
    ray.Direction = trace_dir;
    ray.TMin      = 0.001f;
    ray.TMax      = 100.0f;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x03, ray); // grass does not shadow the sky either
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
}

// radiance an analytic light delivers to a lambert surface at a hit, unshadowed
float3 ray_light_irradiance(uint light_index, float3 position, float3 normal)
{
    Surface hit_surface  = (Surface)0;
    hit_surface.position = position;
    hit_surface.normal   = normal;
    Light light;
    light.Build(light_index, hit_surface);
    float n_dot_l = saturate(dot(normal, -light.to_pixel));
    LightParameters light_p = light_parameters[light_index];
    return light_p.color.rgb * light_p.intensity * light.attenuation * n_dot_l;
}

// what a lambert surface in the open sees when the cache has nothing yet, the darkest sky mip
float3 ray_sky_ambient(Texture2D sky, float sky_mip_count, float3 normal)
{
    return sky.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), direction_sphere_uv(normal), sky_mip_count - 1.0f).rgb * 0.3f;
}

// radiance leaving a secondary hit toward the ray origin, the sun and one sampled local light with
// shadow rays, the diffuse bounce from the radiance cache and the sky specular it reflects, the
// cache's darkness relative to open sky stands in for how enclosed the surface is
float3 ray_surface_shade_secondary(RaySurface s, float3 view_dir, Texture2D sky, float sky_mip_count, float2 pixel, uint light_count)
{
    float3 diffuse  = s.albedo * (1.0f - s.metallic);
    float3 radiance = s.emission;

    // the sun, then one local light picked in proportion to what it delivers here, a uniform pick
    // scaled by the light count turns a lamp next to the hit into a blotch hundreds of times too bright
    if (light_count > 0u && (light_parameters[0].flags & 1u) != 0u)
    {
        float3 incoming = ray_light_irradiance(0u, s.position, s.normal);
        if (any(incoming > 0.0f))
        {
            incoming *= ray_trace_shadow(light_parameters[0], s.position, s.normal, pixel);
        }
        radiance += diffuse * INV_PI * incoming;
    }
    if (light_count > 1u && any(diffuse > 0.0f))
    {
        float  random   = frac(ray_spatial_hash_unit(pixel + 17.0f) + (float)buffer_frame.frame * 0.7548777f);
        float  total    = 0.0f;
        float  chosen_w = 0.0f;
        float3 chosen   = 0.0f;
        uint   index    = 0u;
        for (uint i = 1u; i < light_count; i++)
        {
            LightParameters light_p = light_parameters[i];
            bool is_directional     = (light_p.flags & 1u) != 0u;
            float3 to_light         = is_directional ? -light_p.direction : light_p.position - s.position;
            if (!is_directional && (light_p.range <= 0.0f || dot(to_light, to_light) > light_p.range * light_p.range))
                continue;
            if (dot(s.normal, to_light) <= 0.0f)
                continue;

            float3 incoming = ray_light_irradiance(i, s.position, s.normal);
            float  weight   = luminance(incoming);
            if (weight <= 0.0f)
                continue;

            // one uniform number drives the whole stream, rescaled into the part of it each step leaves
            total += weight;
            float p = weight / total;
            if (random < p)
            {
                chosen   = incoming;
                chosen_w = weight;
                index    = i;
                random   = random / p;
            }
            else
            {
                random = (random - p) / (1.0f - p);
            }
        }
        if (chosen_w > 0.0f)
        {
            LightParameters light_p = light_parameters[index];
            if ((light_p.flags & (1u << 3)) != 0u)
            {
                chosen *= ray_trace_shadow(light_p, s.position, s.normal, pixel);
            }
            radiance += diffuse * INV_PI * chosen * (total / chosen_w);
        }
    }

    float confidence;
    float3 cached     = radiance_cache_lookup(s.position, s.normal, 0.0f, confidence);
    float3 open_sky   = ray_sky_ambient(sky, sky_mip_count, s.normal);
    float3 indirect   = lerp(open_sky, cached, confidence);
    radiance         += diffuse * indirect;

    float  n_dot_v    = max(saturate(dot(s.normal, view_dir)), 0.04f);
    float3 F0         = lerp(0.04f, s.albedo, s.metallic);
    float2 env        = ray_env_brdf(s.roughness, n_dot_v);
    float3 reflected  = reflect(-view_dir, s.normal);
    float  horizon    = saturate(1.0f + dot(reflected, s.geometric_normal));
    float  openness   = lerp(1.0f, saturate(luminance(cached) / max(luminance(open_sky), 1e-4f)), confidence);
    float3 sky_spec   = sky.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), direction_sphere_uv(reflected), s.roughness * s.roughness * (sky_mip_count - 1.0f)).rgb;
    radiance         += sky_spec * (F0 * env.x + env.y) * horizon * horizon * openness;
    return radiance;
}

#endif
