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

#ifndef SPARTAN_RESTIR_SURFACE
#define SPARTAN_RESTIR_SURFACE
#include "common_road.hlsl"
#include "common_decals.hlsl"
#include "common_ray_hit.hlsl"

struct PathSurface
{
    float3 hit_position;
    float3 hit_normal;
    float3 geometric_normal;
    float3 albedo;
    float3 emission;
    float  roughness;
    float  metallic;
    bool   hit;
};

// Read payload fields at the call site so DXC's payload-access analysis sees them.
PathSurface reconstruct_path_surface(float ray_t, uint instance_index, uint primitive_index, uint barycentrics_packed, RayDesc ray);

PathSurface reconstruct_path_surface(float ray_t, uint instance_index, uint primitive_index, uint barycentrics_packed, RayDesc ray)
{
    PathSurface payload = (PathSurface)0;
    payload.hit = ray_t >= 0.0f;
    if (!payload.hit)
        return payload;

    GeometryInfo geo = geometry_infos[instance_index];

    uint material_index    = geo.material_index;
    MaterialParameters mat = material_parameters[material_index];

    uint index_base      = geo.index_offset + primitive_index * 3;
    uint i0 = geometry_indices[index_base + 0];
    uint i1 = geometry_indices[index_base + 1];
    uint i2 = geometry_indices[index_base + 2];

    PulledVertex pv0 = geometry_vertices[geo.vertex_offset + i0];
    PulledVertex pv1 = geometry_vertices[geo.vertex_offset + i1];
    PulledVertex pv2 = geometry_vertices[geo.vertex_offset + i2];

    float3 bary = unpack_hit_barycentrics(barycentrics_packed);

    float3 n0 = unpack_vertex_oct(pv0.normal);
    float3 n1 = unpack_vertex_oct(pv1.normal);
    float3 n2 = unpack_vertex_oct(pv2.normal);
    float3 t0 = unpack_vertex_oct(pv0.tangent);
    float3 t1 = unpack_vertex_oct(pv1.tangent);
    float3 t2 = unpack_vertex_oct(pv2.tangent);
    float2 uv0 = unpack_vertex_uv(pv0.uv);
    float2 uv1 = unpack_vertex_uv(pv1.uv);
    float2 uv2 = unpack_vertex_uv(pv2.uv);

    float3 normal_object  = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float3 tangent_object = normalize(t0 * bary.x + t1 * bary.y + t2 * bary.z);
    float2 texcoord       = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    float3x3 obj_to_world = float3x3(geo.object_to_world_0.xyz, geo.object_to_world_1.xyz, geo.object_to_world_2.xyz);
    float3x3 world_to_obj = float3x3(geo.world_to_object_0.xyz, geo.world_to_object_1.xyz, geo.world_to_object_2.xyz);
    float3 normal_world   = normalize(mul(normal_object, transpose(world_to_obj)));
    float3 tangent_world  = normalize(mul(tangent_object, obj_to_world));

    // full uv state is per-renderable, fetched from geometry_infos[InstanceIndex()]
    float3 hit_position = ray.Origin + ray.Direction * ray_t;
    if (mat.is_terrain())
    {
        // terrain maps planar world xz with tiling as repeats per meter, matches the raster path
        texcoord = hit_position.xz;
    }
    else if (geo.uv_world_space > 0.0f)
    {
        texcoord = compute_world_space_uv(hit_position, normal_world);
    }
    texcoord = texcoord * geo.uv_tiling + geo.uv_offset;
    if (!mat.is_terrain() && geo.uv_world_space > 0.0f)
        texcoord = lerp(texcoord, 1.0f - frac(texcoord) + floor(texcoord), step(0.5f, geo.uv_invert));

    if (geo.uv_rotation != 0.0f)
        texcoord = rotate_uv_90(texcoord, geo.uv_rotation);

    float dist      = ray_t;
    float mip_level = clamp(log2(max(dist * 0.5f, 1.0f)), 0.0f, 4.0f);

    // terrain, same layer weights as the raster path, one layer and no hex tiling because a
    // secondary bounce cannot resolve the detail, without this gi lit the world as if it were grass
    bool terrain_shaded = mat.is_terrain() && mat.terrain_layer_count > 0;
    TerrainSurface terrain = (TerrainSurface)0;
    if (terrain_shaded)
    {
        terrain = terrain_shade_lod(mat, hit_position, normal_world, texcoord, mip_level);
    }

    float3 albedo = mat.color.rgb;
    if (terrain_shaded)
    {
        albedo = terrain.albedo;
    }
    else if (mat.has_texture_albedo())
    {
        uint albedo_texture_index = get_material_texture_index(material_index, material_texture_index_albedo);
        float4 sampled = material_textures[albedo_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        if (mat.is_albedo_srgb())
        {
            sampled.rgb = srgb_to_linear(sampled.rgb);
        }
        albedo = sampled.rgb * mat.color.rgb;
    }
    albedo = saturate(albedo);

    float roughness = mat.roughness;
    float metallic  = mat.metalness;
    if (terrain_shaded)
    {
        roughness = terrain.roughness;
        metallic  = terrain.metalness;
    }
    else if (mat.has_texture_roughness() || mat.has_texture_metalness())
    {
        float4 packed = material_textures[get_material_texture_index(material_index, material_texture_index_packed)].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        roughness *= lerp(1.0f, packed.g, (float)mat.has_texture_roughness());
        metallic  *= lerp(1.0f, packed.b, (float)mat.has_texture_metalness());
    }
    road_weathering(mat.flags,hit_position,albedo,roughness,exp2(mip_level)*3.0/4096.0);
    roughness = max(roughness, 0.04f);

    float3x3 obj_to_world_3x3 = float3x3(geo.object_to_world_0.xyz, geo.object_to_world_1.xyz, geo.object_to_world_2.xyz);
    float3 edge1_world   = mul(pv1.position - pv0.position, obj_to_world_3x3);
    float3 edge2_world   = mul(pv2.position - pv0.position, obj_to_world_3x3);
    float3 geometric_normal = normalize(cross(edge1_world, edge2_world));

    if (dot(geometric_normal, ray.Direction) > 0.0f)
        geometric_normal = -geometric_normal;
    if (dot(normal_world, geometric_normal) < 0.0f)
        normal_world = -normal_world;

    float3 tangent_projected = tangent_world - geometric_normal * dot(tangent_world, geometric_normal);
    if (dot(tangent_projected, tangent_projected) > 1e-6f)
    {
        tangent_world = normalize(tangent_projected);
    }
    else
    {
        float3 fallback_bitangent;
        build_orthonormal_basis_fast(geometric_normal, tangent_world, fallback_bitangent);
    }

    if (!terrain_shaded && mat.has_texture_normal())
    {
        uint normal_texture_index = get_material_texture_index(material_index, material_texture_index_normal);
        float3 normal_sample = material_textures[normal_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).rgb;

        // Same two-channel normal decode and strength as the raster G-buffer.
        normal_sample = normalize(normal_sample * 2.0f - 1.0f);
        normal_sample.z = sqrt(max(0.0f, 1.0f - dot(normal_sample.xy, normal_sample.xy)));
        normal_sample.xy *= saturate(max(0.01f, mat.normal));

        float3 bitangent = normalize(cross(geometric_normal, tangent_world));
        float3x3 tbn     = float3x3(tangent_world, bitangent, geometric_normal);

        normal_world = normalize(mul(normal_sample, tbn));
        if (dot(normal_world, geometric_normal) < 0.0f)
            normal_world = -normal_world;
    }

    // emissive calibration mirrors g_buffer and light_composition, from_albedo overrides the texture path
    float3 emission = float3(0.0f, 0.0f, 0.0f);
    if (mat.has_texture_emissive())
    {
        uint emissive_texture_index = get_material_texture_index(material_index, material_texture_index_emission);
        float3 emissive_sample = material_textures[emissive_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).rgb;
        if (mat.is_emissive_srgb())
        {
            emissive_sample = srgb_to_linear(emissive_sample);
        }
        emission = emissive_sample * photometric_to_radiometric(RESTIR_EMISSIVE_NITS_TEXTURE);
    }
    if (mat.emissive_from_albedo())
    {
        // the nee pool holds authored emitters only, so zero them here while it is active to keep
        // the two strategies from double counting, texture emitters stay on this path because the
        // pool derives radiance from the flat material color and cannot evaluate their texture
        emission = is_emtri_pool_active()
            ? float3(0.0f, 0.0f, 0.0f)
            : albedo * mat.emissive_strength * photometric_to_radiometric(RESTIR_EMISSIVE_NITS_FROM_ALBEDO);
    }

    float decal_occlusion = 1.0f;
    float footprint = max(ray_t * 0.001f, 0.001f);
    apply_decals(uint2(geo.decal_offset, geo.decal_count), hit_position, geometric_normal,
        tangent_world * footprint, cross(geometric_normal, tangent_world) * footprint,
        albedo, normal_world, roughness, metallic, decal_occlusion, emission);
    payload.hit_position     = hit_position;
    payload.hit_normal       = normal_world;
    payload.geometric_normal = geometric_normal;
    payload.albedo           = albedo;
    payload.emission         = emission;
    payload.roughness        = roughness;
    payload.metallic         = metallic;
    return payload;
}

#endif
