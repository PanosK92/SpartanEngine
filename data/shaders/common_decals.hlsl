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

// Receiver-scoped projection: no depth bias, floating polygons, UV seams or opposite-side bleed.
#ifndef SPARTAN_DECALS
#define SPARTAN_DECALS
StructuredBuffer<DecalParameters> decals : register(t66);

float decal_height(float2 p, float seed, float grass)
{
    float angle = atan2(p.y, p.x);
    float rim = 0.69f + 0.10f * sin(angle * 7.0f + seed) + 0.06f * sin(angle * 13.0f - seed);
    float blob = saturate((rim - length(p)) * 7.0f);
    float grain = 0.65f + 0.20f * sin(p.x * 43.0f + seed) * sin(p.y * 37.0f - seed);
    float fibers = pow(saturate(sin(p.x * 55.0f + sin(p.y * 9.0f + seed) * 2.0f)), 4.0f);
    return blob * lerp(grain, 0.35f + fibers * 0.65f, grass);
}

float apply_decals(uint2 range, float3 position, float3 geometric_normal, float3 dpdx, float3 dpdy,
    inout float3 albedo, inout float3 normal, inout float roughness, inout float metalness,
    inout float occlusion, inout float3 emission)
{
    float coverage_total = 0.0f;
    [loop] for (uint i = 0; i < min(range.y, (uint)decal_max_per_receiver); ++i)
    {
        DecalParameters d = decals[range.x + i];
        float3 q = mul(float4(position, 1.0f), d.world_to_decal).xyz;
        float3 gx = float3(d.world_to_decal[0][0], d.world_to_decal[1][0], d.world_to_decal[2][0]);
        float3 gy = float3(d.world_to_decal[0][1], d.world_to_decal[1][1], d.world_to_decal[2][1]);
        float3 gz = float3(d.world_to_decal[0][2], d.world_to_decal[1][2], d.world_to_decal[2][2]);
        float facing = smoothstep(0.15f, 0.65f, dot(geometric_normal, normalize(gz)));
        if (any(abs(q) > 1.0f) || facing == 0.0f) continue;
        // Evaluate footprint explicitly: derivatives inside divergent decal bounds are undefined.
        float footprint = max(length(float2(dot(dpdx, gx), dot(dpdx, gy))),
                              length(float2(dot(dpdy, gx), dot(dpdy, gy))));
        float h = decal_height(q.xy, d.surface.z, d.surface.w);
        float coverage = smoothstep(0.0f, max(0.08f, footprint * 5.0f), h);
        float alpha = saturate(d.color.a * coverage * facing * (1.0f - smoothstep(0.65f, 1.0f, abs(q.z))));
        const float e = 0.006f;
        float2 slope = float2(
            decal_height(q.xy + float2(e,0), d.surface.z, d.surface.w) - decal_height(q.xy - float2(e,0), d.surface.z, d.surface.w),
            decal_height(q.xy + float2(0,e), d.surface.z, d.surface.w) - decal_height(q.xy - float2(0,e), d.surface.z, d.surface.w)) / (2.0f * e);
        float3 gradient = (slope.x * gx + slope.y * gy) * d.surface.y;
        gradient -= geometric_normal * dot(gradient, geometric_normal);
        float3 deposit_color = d.color.rgb;
        float deposit_roughness = d.surface.x;
        if (d.source_material != 0xffffffffu)
        {
            MaterialParameters source = material_parameters[d.source_material];
            float2 uv = q.xy * 0.5f + (source.is_alpha_tested() ? float2(0.5f, 0.5f) : frac(d.surface.z * float2(0.37f, 0.73f)));
            float2 dx = float2(dot(dpdx, gx), dot(dpdx, gy)) * 0.5f;
            float2 dy = float2(dot(dpdy, gx), dot(dpdy, gy)) * 0.5f;
            if (source.has_texture_albedo())
            {
                float4 texel = material_textures[NonUniformResourceIndex(get_material_texture_index(d.source_material, material_texture_index_albedo))].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv, dx, dy);
                if (source.is_albedo_srgb()) texel.rgb = srgb_to_linear(texel.rgb);
                deposit_color = lerp(deposit_color, texel.rgb * source.color.rgb, 0.65f);
                // Authored alpha textures can supply arbitrary decal shapes (impacts, logos, etc.).
                if (source.is_alpha_tested())
                {
                    alpha = saturate(d.color.a * texel.a * source.color.a * facing * (1.0f - smoothstep(0.65f, 1.0f, abs(q.z))));
                    deposit_color = texel.rgb * source.color.rgb * d.color.rgb;
                }
            }
            if (source.has_texture_normal())
            {
                float2 xy = material_textures[NonUniformResourceIndex(get_material_texture_index(d.source_material, material_texture_index_normal))].SampleGrad(GET_SAMPLER(sampler_anisotropic_wrap), uv, dx, dy).xy * 2.0f - 1.0f;
                float3 grain = normalize(gx) * xy.x + normalize(gy) * xy.y;
                gradient -= (grain - geometric_normal * dot(grain, geometric_normal)) * 0.25f;
            }
        }
        coverage_total += (1.0f - coverage_total) * alpha;
        float detail_fade = 1.0f - smoothstep(0.03f, 0.15f, footprint);
        normal = normalize(normal - gradient * alpha * detail_fade);
        albedo = lerp(albedo, deposit_color * lerp(0.72f, 1.12f, h), alpha);
        roughness = lerp(roughness, deposit_roughness, alpha);
        metalness *= 1.0f - alpha;
        occlusion *= 1.0f - alpha * h * 0.12f;
        emission *= 1.0f - alpha;
    }
    return coverage_total;
}
#endif
