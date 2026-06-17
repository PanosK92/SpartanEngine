/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
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

//= INCLUDES =================
#include "common.hlsl"
#include "brdf.hlsl"
//============================

// shades ray traced reflection hits with analytical lights and a sky visibility tested ibl term

// small shadow ray budget, the reflection ray already spreads noise and the denoiser handles the rest
static const uint k_shadow_spp = 4;
static const float2 k_halton_2_3[4] =
{
    float2(0.500000f, 0.333333f),
    float2(0.250000f, 0.666667f),
    float2(0.750000f, 0.111111f),
    float2(0.125000f, 0.444444f)
};

float reflections_spatial_hash_unit(float2 pixel_xy)
{
    return frac(52.9829189f * frac(pixel_xy.x * 0.06711056f + pixel_xy.y * 0.00583715f));
}

// karis 2014 analytic split sum environment brdf, avoids binding the brdf lut in this pass
float2 reflections_env_brdf_approx(float roughness, float n_dot_v)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r        = roughness * c0 + c1;
    float  a004     = min(r.x * r.x, exp2(-9.28f * n_dot_v)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

float2 reflections_concentric_disk(float2 u)
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

// inline ray traced visibility for any light type at a reflection hit point, returns 0..1
float reflections_trace_shadow(LightParameters light_p, float3 hit_position, float3 hit_normal, float2 pixel_xy)
{
    bool is_directional = (light_p.flags & uint(1U << 0)) != 0;
    bool is_area        = (light_p.flags & uint(1U << 6)) != 0;
    
    // self intersection bias
    float bias    = 0.005f;
    float3 origin = hit_position + hit_normal * bias;
    
    // stationary per pixel rotation breaks stratification banding
    float rot_angle = reflections_spatial_hash_unit(pixel_xy) * PI2;
    float cos_r     = cos(rot_angle);
    float sin_r     = sin(rot_angle);
    
    // tangent frame for jittering directional/point/spot light directions
    float3 to_light_center = light_p.position.xyz - origin;
    float  center_dist     = length(to_light_center);
    float3 light_dir_unit  = is_directional ? normalize(-light_p.direction.xyz) : (center_dist > 0.0001f ? to_light_center / center_dist : float3(0.0f, 1.0f, 0.0f));
    float3 up_axis         = abs(light_dir_unit.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent         = normalize(cross(up_axis, light_dir_unit));
    float3 bitangent       = cross(light_dir_unit, tangent);
    
    // area light basis built from the light's authored right vector
    float3 area_right    = normalize(light_p.direction_right.xyz);
    float3 area_up       = normalize(cross(light_p.direction.xyz, area_right));
    float  emitter_safety = 0.0f;
    if (is_area)
        emitter_safety = min(light_p.area_width, light_p.area_height) * 0.5f + 0.005f;
    
    float visibility_sum = 0.0f;
    float valid_samples  = 0.0f;
    
    [unroll]
    for (uint s = 0; s < k_shadow_spp; s++)
    {
        float2 u    = k_halton_2_3[s] * 2.0f - 1.0f;
        float2 disk = reflections_concentric_disk(u);
        float2 disk_r = float2(disk.x * cos_r - disk.y * sin_r,
                               disk.x * sin_r + disk.y * cos_r);
        
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
            // sample a deterministic point on the rectangle for soft area shadows
            float3 sample_point = light_p.position.xyz
                                + area_right * disk_r.x * (light_p.area_width  * 0.5f)
                                + area_up    * disk_r.y * (light_p.area_height * 0.5f);
            float3 to_light = sample_point - origin;
            float  dist     = length(to_light);
            if (dist < 0.0001f)
            {
                visibility_sum += 1.0f;
                valid_samples  += 1.0f;
                continue;
            }
            direction = to_light / dist;
            t_max     = max(dist - emitter_safety, bias);
        }
        else
        {
            // point/spot, jitter inside a small spherical source for soft penumbra
            if (center_dist < 0.0001f)
            {
                visibility_sum += 1.0f;
                valid_samples  += 1.0f;
                continue;
            }
            const float light_radius = 0.05f;
            float3 jittered_target   = light_p.position.xyz + (tangent * disk_r.x + bitangent * disk_r.y) * light_radius;
            float3 to_jit            = jittered_target - origin;
            float  dist              = length(to_jit);
            direction                = to_jit / dist;
            t_max                    = max(dist - bias * 2.0f, bias);
        }
        
        // back facing samples carry no light, ignore them entirely from the average
        if (dot(hit_normal, direction) <= 0.0f)
            continue;
        
        valid_samples += 1.0f;
        
        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = direction;
        ray.TMin      = 0.001f;
        ray.TMax      = max(t_max, 0.001f);
        
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
        query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
        query.Proceed();
        
        visibility_sum += query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
    }
    
    return valid_samples > 0.0f ? (visibility_sum / valid_samples) : 1.0f;
}

// single ray sky visibility test, gates the ibl terms so hit points inside enclosed
// or shadowed geometry stop receiving full sky lighting, trace_dir lets the caller test
// the diffuse hemisphere (normal) or the specular reflection direction independently
float reflections_trace_sky_visibility(float3 hit_position, float3 hit_normal, float3 trace_dir)
{
    float  bias   = 0.005f;
    float3 origin = hit_position + hit_normal * bias;
    
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = trace_dir;
    ray.TMin      = 0.001f;
    ray.TMax      = 100.0f;
    
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();
    
    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    
    if (thread_id.x >= resolution_out.x || thread_id.y >= resolution_out.y)
        return;
    
    // reflection g-buffer layout
    // tex  = position.xyz + hit_distance
    // tex2 = normal.xyz + material_index
    // tex3 = albedo.rgb + roughness
    float4 gbuffer_position = tex[thread_id.xy];
    float4 gbuffer_normal   = tex2[thread_id.xy];
    float4 gbuffer_albedo   = tex3[thread_id.xy];
    
    float  hit_distance   = gbuffer_position.w;
    float3 position       = gbuffer_position.xyz;
    float3 normal         = gbuffer_normal.xyz;
    uint   material_index = uint(gbuffer_normal.w);
    float3 albedo         = gbuffer_albedo.rgb;
    float  roughness      = gbuffer_albedo.a;
    
    // skip pixels marked as no reflection needed (roughness >= 0.9)
    if (hit_distance < 0.0f)
    {
        tex_uav[thread_id.xy] = float4(0, 0, 0, 0);
        return;
    }
    
    // miss returns sky color, prefiltered by source surface roughness so smooth metals get sharp sky
    if (hit_distance == 0.0f)
    {
        float source_roughness = tex_material[thread_id.xy].r;
        float mip_count        = pass_get_f3_value().y;
        float sky_mip          = source_roughness * source_roughness * (mip_count - 1.0f);
        float3 ray_dir         = position; // direction stored in position for misses
        float2 sky_uv          = direction_sphere_uv(ray_dir);
        float3 sky_color       = tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), sky_uv, sky_mip).rgb;
        tex_uav[thread_id.xy]  = float4(sky_color, 1.0f);
        return;
    }
    
    // hit, lookup material and build view direction at the hit
    MaterialParameters mat = material_parameters[material_index];
    float  metallic        = mat.metalness;
    float3 F0              = lerp(0.04f, albedo, metallic);
    
    // view direction at the hit points back toward the source pixel
    float2 uv_source     = (thread_id.xy + 0.5f) / resolution_out;
    float3 source_pos    = get_position(uv_source);
    float3 view_dir      = source_pos - position;
    float  view_dist     = length(view_dir);
    view_dir             = view_dist > 0.0001f ? view_dir / view_dist : -normal;
    float  n_dot_v       = saturate(dot(normal, view_dir));
    
    // accumulators for radiance leaving the hit point along view_dir
    float3 out_diffuse  = 0.0f;
    float3 out_specular = 0.0f;
    
    // process all active lights with inline ray traced visibility
    uint light_count = uint(pass_get_f3_value().x);
    for (uint i = 0; i < light_count; i++)
    {
        LightParameters light_p = light_parameters[i];
        
        bool is_directional = (light_p.flags & uint(1U << 0)) != 0;
        bool is_point       = (light_p.flags & uint(1U << 1)) != 0;
        bool is_spot        = (light_p.flags & uint(1U << 2)) != 0;
        bool is_area        = (light_p.flags & uint(1U << 6)) != 0;
        bool has_shadows    = (light_p.flags & uint(1U << 3)) != 0;
        
        // direction from hit toward the light (l) and attenuation
        float3 to_light    = float3(0.0f, 0.0f, 0.0f);
        float  attenuation = 0.0f;
        
        if (is_directional)
        {
            to_light    = normalize(-light_p.direction.xyz);
            attenuation = 1.0f;
        }
        else
        {
            float3 from_light = position - light_p.position.xyz;
            float  d          = length(from_light);
            float3 to_pixel   = d > 0.0001f ? from_light / d : float3(0.0f, 1.0f, 0.0f);
            to_light          = -to_pixel;
            
            // inverse square + range cutoff, matches Light::compute_attenuation_distance
            float att_dist  = 1.0f / (d * d + 0.0001f);
            float att_range = (light_p.range > 0.0f && d < light_p.range) ? 1.0f : 0.0f;
            attenuation     = att_dist * att_range;
            
            if (is_spot)
            {
                float cos_outer   = cos(light_p.angle);
                float cos_inner   = cos(light_p.angle * 0.9f);
                float scale       = 1.0f / max(0.0001f, cos_inner - cos_outer);
                float cd          = dot(to_pixel, light_p.direction.xyz);
                float atten_angle = saturate((cd - cos_outer) * scale);
                attenuation      *= atten_angle * atten_angle;
            }
            else if (is_area)
            {
                float emitter_area = 0.5f * max(light_p.area_width * light_p.area_height, 0.0001f);
                float emission_cos = saturate(dot(light_p.direction.xyz, to_pixel));
                attenuation       *= emission_cos * emitter_area;
            }
        }
        
        // n dot l at the hit
        float n_dot_l = saturate(dot(normal, to_light));
        if (n_dot_l <= 0.0f || attenuation <= 0.0f)
            continue;
        
        // inline ray traced shadow at the hit so all light types respect occlusion
        float shadow = 1.0f;
        if (has_shadows)
        {
            shadow = reflections_trace_shadow(light_p, position, normal, float2(thread_id.xy));
        }
        if (shadow <= 0.0f)
            continue;
        
        // radiance arriving at the hit from this light
        float3 radiance = light_p.color.rgb * light_p.intensity * attenuation * n_dot_l * shadow;
        
        // simple isotropic ggx + lambert evaluated for view_dir back toward the source
        float3 h        = normalize(to_light + view_dir);
        float  n_dot_h  = saturate(dot(normal, h));
        float  l_dot_h  = saturate(dot(to_light, h));
        
        float roughness_alpha = roughness * roughness;
        float alpha2          = roughness_alpha * roughness_alpha;
        
        float3 diffuse_brdf  = albedo * INV_PI * (1.0f - metallic);
        float  D             = D_GGX(n_dot_h, alpha2);
        float  G             = V_SmithGGX(n_dot_v, n_dot_l, alpha2);
        float3 F             = F_Schlick(F0, get_f90(), l_dot_h);
        float3 specular_brdf = D * G * F;
        
        out_diffuse  += diffuse_brdf  * radiance;
        out_specular += specular_brdf * radiance;
    }
    
    float mip_count = pass_get_f3_value().y;

    // sky based diffuse ibl, gated by a sky visibility ray so enclosed hits stay dark
    float  sky_visibility = reflections_trace_sky_visibility(position, normal, normal);
    float2 sky_uv         = direction_sphere_uv(normal);
    float3 ibl_sample     = tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), sky_uv, mip_count - 1.0f).rgb;
    float3 ibl_diffuse    = albedo * ibl_sample * (1.0f - metallic) * sky_visibility * 0.3f;

    // specular environment at the hit, without this reflective hit surfaces (metal car, glossy
    // black tiles) shade to near black inside the reflection since their diffuse is killed by
    // metalness or a dark albedo, this is the term that makes a reflected car look like a car
    // and not a silhouette, single bounce sky approximation gated by a visibility ray along the
    // hit reflection direction so enclosed geometry does not leak sky
    float3 reflect_dir    = reflect(-view_dir, normal);
    float  spec_mip       = roughness * roughness * (mip_count - 1.0f);
    float3 env_spec       = tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), direction_sphere_uv(reflect_dir), spec_mip).rgb;
    float2 env_brdf       = reflections_env_brdf_approx(roughness, n_dot_v);
    float  spec_vis       = reflections_trace_sky_visibility(position, normal, reflect_dir);
    float3 ibl_specular   = env_spec * (F0 * env_brdf.x + env_brdf.y) * spec_vis;

    // emissive at the hit, the showroom is lit almost entirely by emissive light strips, without
    // this the emitters never appear in reflections so reflective surfaces have nothing to show,
    // the hdr boost mirrors light_composition so a reflected strip is as bright as the real one
    float3 emission = 0.0f;
    if (mat.emissive_from_albedo())
    {
        emission = albedo * 250.0f;
    }
    else if (mat.has_texture_emissive())
    {
        emission = mat.color.rgb * 25.0f;
    }

    float3 final_color = out_diffuse + out_specular + ibl_diffuse + ibl_specular + emission;

    // rt owns the full primary specular lobe, restir contributes diffuse only so there is no overlap
    tex_uav[thread_id.xy] = validate_output(float4(final_color, 1.0f));
}
