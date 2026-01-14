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
#include "shadow_mapping.hlsl"
//============================

// shades ray traced reflection hits using the same lighting as the main scene
// reads from reflection g-buffer textures and outputs to reflections texture

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get output resolution
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    
    // early bounds check
    if (thread_id.x >= resolution_out.x || thread_id.y >= resolution_out.y)
        return;
    
    // read reflection g-buffer data
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
    
    // skip pixels marked as "no reflection needed" (roughness >= 0.9)
    if (hit_distance < 0.0f)
    {
        tex_uav[thread_id.xy] = float4(0, 0, 0, 0);
        return;
    }
    
    // miss (sky) - sample skysphere using stored direction
    if (hit_distance == 0.0f)
    {
        float3 ray_dir   = position; // direction stored in position for misses
        float2 sky_uv    = direction_sphere_uv(ray_dir);
        float3 sky_color = tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), sky_uv, 0).rgb;
        tex_uav[thread_id.xy] = float4(sky_color, 1.0f);
        return;
    }
    
    // hit - build minimal surface for lighting calculation
    // get material properties
    MaterialParameters mat = material_parameters[material_index];
    float metallic         = mat.metallness;
    float3 F0              = lerp(0.04f, albedo, metallic);
    
    // compute view direction from camera to hit point
    float3 camera_to_pixel        = position - buffer_frame.camera_position.xyz;
    float  camera_to_pixel_length = length(camera_to_pixel);
    camera_to_pixel               = normalize(camera_to_pixel);
    
    // initialize output accumulators
    float3 out_diffuse  = 0.0f;
    float3 out_specular = 0.0f;
    
    // process lights (same as light.hlsl but simplified)
    uint light_count = min(pass_get_f3_value().x, 4); // limit to first 4 lights for perf
    for (uint i = 0; i < light_count; i++)
    {
        LightParameters light_params = light_parameters[i];
        
        // extract light properties
        uint   light_flags     = light_params.flags;
        float3 light_color     = light_params.color.rgb;
        float3 light_position  = light_params.position.xyz;
        float  light_intensity = light_params.intensity;
        float3 light_forward   = light_params.direction.xyz;
        float  light_range     = light_params.range;
        float  light_angle     = light_params.angle;
        
        bool is_directional = (light_flags & uint(1U << 0)) != 0;
        bool is_point       = (light_flags & uint(1U << 1)) != 0;
        bool is_spot        = (light_flags & uint(1U << 2)) != 0;
        bool has_shadows    = (light_flags & uint(1U << 3)) != 0;
        
        // compute light direction
        float3 to_pixel = 0.0f;
        if (is_directional)
        {
            to_pixel = normalize(light_forward);
        }
        else
        {
            to_pixel = normalize(position - light_position);
        }
        
        // compute attenuation
        float attenuation = 1.0f;
        if (is_directional)
        {
            attenuation = saturate(dot(-light_forward, float3(0.0f, 1.0f, 0.0f)));
        }
        else if (is_point || is_spot)
        {
            float d                = length(position - light_position);
            float attenuation_dist = 1.0f / (d * d + 0.0001f);
            float distance_falloff = saturate(1.0f - d / light_range);
            distance_falloff      *= distance_falloff;
            attenuation            = attenuation_dist * distance_falloff;
            
            if (is_spot)
            {
                float cos_outer   = cos(light_angle);
                float cos_inner   = cos(light_angle * 0.9f);
                float scale       = 1.0f / max(0.001f, cos_inner - cos_outer);
                float offset      = -cos_outer * scale;
                float cd          = dot(to_pixel, light_forward);
                float atten_angle = saturate(cd * scale + offset);
                attenuation      *= atten_angle * atten_angle;
            }
        }
        
        // n dot l
        float3 l      = normalize(-to_pixel);
        float  n_dot_l = saturate(dot(normal, l));
        
        // skip if no contribution
        if (n_dot_l <= 0.0f || attenuation <= 0.0f)
            continue;
        
        // shadow (simplified - only directional light cascade 0)
        float shadow = 1.0f;
        if (has_shadows && is_directional)
        {
            // transform to light space (cascade 0)
            float4 clip_pos = mul(float4(position, 1.0f), light_params.transform[0]);
            float3 ndc      = clip_pos.xyz / clip_pos.w;
            float2 uv       = ndc_to_uv(ndc.xy);
            
            // check bounds
            float2 ndc_abs = abs(ndc.xy);
            if (max(ndc_abs.x, ndc_abs.y) <= 1.0f)
            {
                // simple shadow comparison (no soft shadows for reflections)
                float2 atlas_uv = light_params.atlas_offsets[0] + uv * light_params.atlas_scales[0];
                float depth_sample = tex5.SampleLevel(GET_SAMPLER(sampler_point_clamp), atlas_uv, 0).r;
                shadow = (ndc.z < depth_sample) ? 1.0f : 0.0f;
            }
        }
        
        // compute radiance
        float3 radiance = light_color * light_intensity * attenuation * n_dot_l * shadow;
        
        // brdf calculation (simplified)
        float3 v = normalize(-camera_to_pixel);
        float3 h = normalize(l + v);
        
        float n_dot_v = saturate(dot(normal, v));
        float n_dot_h = saturate(dot(normal, h));
        float l_dot_h = saturate(dot(l, h));
        float v_dot_h = saturate(dot(v, h));
        
        float roughness_alpha = roughness * roughness;
        float alpha2          = roughness_alpha * roughness_alpha;
        
        // diffuse (lambert)
        float3 diffuse_brdf = albedo * INV_PI * (1.0f - metallic);
        
        // specular (simplified ggx)
        float D = D_GGX(n_dot_h, alpha2);
        float G = V_SmithGGX(n_dot_v, n_dot_l, alpha2);
        float3 F = F_Schlick(F0, get_f90(), l_dot_h);
        float3 specular_brdf = D * G * F;
        
        // accumulate
        out_diffuse  += diffuse_brdf * radiance;
        out_specular += specular_brdf * radiance;
    }
    
    // ibl contribution (sample skysphere based on roughness)
    float mip_count   = pass_get_f3_value().y;
    float mip_level   = roughness * roughness * (mip_count - 1.0f);
    float2 sky_uv     = direction_sphere_uv(normal);
    float3 ibl_sample = tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), sky_uv, mip_level).rgb;
    
    // compute fresnel for ibl
    float3 v      = normalize(-camera_to_pixel);
    float  n_dot_v = saturate(dot(normal, v));
    float3 F_ibl   = F0 + (max(1.0f - roughness, F0) - F0) * pow(1.0f - n_dot_v, 5.0f);
    
    // ibl diffuse and specular
    float3 ibl_diffuse  = albedo * ibl_sample * (1.0f - metallic) * (1.0f - F_ibl) * 0.3f;
    float3 ibl_specular = ibl_sample * F_ibl * 0.2f;
    
    // combine all lighting
    float3 final_color = out_diffuse + out_specular + ibl_diffuse + ibl_specular;
    
    tex_uav[thread_id.xy] = validate_output(float4(final_color, 1.0f));
}
