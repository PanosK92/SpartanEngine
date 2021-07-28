/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Common.hlsl"
//====================

struct Surface
{
    // Properties
    float3  albedo;
    float   alpha;
    float   roughness;
    float   metallic;
    float   clearcoat;
    float   clearcoat_roughness;
    float   anisotropic;
    float   anisotropic_rotation;
    float   sheen;
    float   sheen_tint;
    float3  occlusion;
    float3  emissive;
    float3  F0;
    int     id;
    float2  uv;
    float   depth;
    float3  position;
    float3  normal;
    float3  camera_to_pixel;
    float   camera_to_pixel_length;
    
    // Activision GTAO paper: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
    float3 multi_bounce_ao(float visibility, float3 albedo)
    {
        float3 a    = 2.0404 * albedo - 0.3324;
        float3 b    = -4.7951 * albedo + 0.6417;
        float3 c    = 2.7552 * albedo + 0.6903;
        float x     = visibility;
        return max(x, ((x * a + b) * x + c) * x);
    }
    
    void Build(uint2 position_screen, bool use_albedo = true)
    {
        // Sample render targets
        float4 sample_albedo    = tex_albedo[position_screen];
        float4 sample_normal    = tex_normal[position_screen];
        float4 sample_material  = tex_material[position_screen];
        float sample_depth      = tex_depth[position_screen].r;

        // Misc
        uv      = (position_screen + 0.5f) / g_resolution_rt;
        depth   = sample_depth;
        id      = unpack_float16_to_uint32(sample_normal.a);
        
        albedo      = use_albedo ? sample_albedo.rgb : 1.0f;
        alpha       = sample_albedo.a;
        roughness   = sample_material.r;
        metallic    = sample_material.g;
        emissive    = sample_material.b * sample_albedo.rgb * 10.0f;
        F0          = lerp(0.04f, albedo, metallic);
        
        clearcoat            = mat_clearcoat_clearcoatRough_aniso_anisoRot[id].x;
        clearcoat_roughness  = mat_clearcoat_clearcoatRough_aniso_anisoRot[id].y;
        anisotropic          = mat_clearcoat_clearcoatRough_aniso_anisoRot[id].z;
        anisotropic_rotation = mat_clearcoat_clearcoatRough_aniso_anisoRot[id].w;
        sheen                = mat_sheen_sheenTint_pad[id].x;
        sheen_tint           = mat_sheen_sheenTint_pad[id].y;

        // Occlusion + GI
        {
            // Determine what we can do
            bool do_ssao    = is_ssao_enabled() && !g_is_transparent_pass;
            bool do_ssao_gi = do_ssao && is_ssao_gi_enabled();
            
            // Sample ssao texture
            float4 ssao = do_ssao ? tex_ssao[position_screen] : float4(0.0f, 0.0f, 0.0f, 1.0f);

            // Combine ssao with material ao
            float visibility = min(sample_material.a, ssao.a);

            if (do_ssao_gi)
            {
                occlusion   = visibility;
                emissive    += ssao.rgb;
            }
            else
            {
                // If ssao gi is not enabled, approximate some light bouncing
                occlusion = multi_bounce_ao(visibility, sample_albedo.rgb);
            }
        }
        
        // Reconstruct position from depth
        float x             = uv.x * 2.0f - 1.0f;
        float y             = (1.0f - uv.y) * 2.0f - 1.0f;
        float4 pos_clip     = float4(x, y, depth, 1.0f);
        float4 pos_world    = mul(pos_clip, g_view_projection_inverted);
        position            = pos_world.xyz / pos_world.w;

        normal                  = sample_normal.xyz;
        camera_to_pixel         = position - g_camera_position.xyz;
        camera_to_pixel_length  = length(camera_to_pixel);
        camera_to_pixel         = normalize(camera_to_pixel);
    }

    bool is_sky()           { return id == 0; }
    bool is_transparent()   { return alpha != 1.0f; }
    bool is_opaque()        { return alpha == 1.0f; }
};

struct Light
{
    // Properties
    float3  color;
    float3  position;
    float   intensity;
    float3  to_pixel;
    float3  forward;
    float   distance_to_pixel;
    float   angle;
    float   bias;
    float   normal_bias;
    float   near;
    float   far;
    float3  radiance;
    float   n_dot_l;
    uint    array_size;

    // attenuation functions are derived from Frostbite
    // https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v2.pdf

    // Attenuation over distance
    float compute_attenuation_distance(const float3 surface_position)
    {
        float distance_to_pixel = length(surface_position - position);
        float attenuation       = saturate(1.0f - distance_to_pixel / far);
        return attenuation * attenuation;
    }

    // Attenuation over angle (approaching the outer cone)
    float compute_attenuation_angle()
    {
        float cos_outer         = cos(angle);
        float cos_inner         = cos(angle * 0.9f);
        float cos_outer_squared = cos_outer * cos_outer;
        float scale             = 1.0f / max(0.001f, cos_inner - cos_outer);
        float offset            = -cos_outer * scale;

        float cd            = dot(to_pixel, forward);
        float attenuation   = saturate(cd * scale + offset);
        return attenuation * attenuation;
    }

    // Final attenuation for all suported lights
    float compute_attenuation(const float3 surface_position)
    {
        float attenuation = 0.0f;
        
        #if DIRECTIONAL
        attenuation     = saturate(dot(-forward.xyz, float3(0.0f, 1.0f, 0.0f)));
        #elif POINT
        attenuation     = compute_attenuation_distance(surface_position);
        #elif SPOT
        attenuation     = compute_attenuation_distance(surface_position) * compute_attenuation_angle();
        #endif
    
        return attenuation;
    }
    
    float3 compute_direction(float3 light_position, Surface surface)
    {
        float3 direction = 0.0f;
        
        #if DIRECTIONAL
        direction   = normalize(forward.xyz);
        #elif POINT
        direction   = normalize(surface.position - light_position);
        #elif SPOT
        direction   = normalize(surface.position - light_position);
        #endif
    
        return direction;
    }
    
    void Build(Surface surface)
    {
        color               = cb_light_color.rgb;
        position            = cb_light_position.xyz;
        intensity           = cb_light_intensity_range_angle_bias.x;
        far                 = cb_light_intensity_range_angle_bias.y;
        angle               = cb_light_intensity_range_angle_bias.z;
        bias                = cb_light_intensity_range_angle_bias.w;
        forward             = cb_light_direction.xyz;
        normal_bias         = cb_light_normal_bias;
        near                = 0.1f;
        distance_to_pixel   = length(surface.position - position);
        to_pixel            = compute_direction(position, surface);
        n_dot_l             = saturate(dot(surface.normal, -to_pixel)); // Pre-compute n_dot_l since it's used in many places
        radiance            = color * intensity * compute_attenuation(surface.position) * surface.occlusion * n_dot_l;
        #if DIRECTIONAL
        array_size = 4;
        #else
        array_size = 1;
        #endif
    }
};
