/*
Copyright(c) 2016-2020 Panos Karabelas

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
							 
//= TEXTURES ===========================================
Texture2D tex_albedo 					: register(t0);
Texture2D tex_normal 					: register(t1);
Texture2D tex_material 					: register(t2);
Texture2D tex_depth 					: register(t3);
Texture2D tex_ssao 						: register(t4);
Texture2D tex_ssr                       : register(t5);
Texture2D tex_frame                     : register(t6);
Texture2DArray light_directional_depth 	: register(t7);
Texture2DArray light_directional_color 	: register(t8);
TextureCube light_point_depth 			: register(t9);
TextureCube light_point_color 			: register(t10);
Texture2D light_spot_depth 				: register(t11);
Texture2D light_spot_color 				: register(t12);
//======================================================

//= INCLUDES =====================      
#include "BRDF.hlsl"              
#include "ShadowMapping.hlsl"
#include "VolumetricLighting.hlsl"
//================================

struct PixelOutputType
{
	float4 diffuse		: SV_Target0;
	float4 specular		: SV_Target1;
	float4 volumetric	: SV_Target2;
};

PixelOutputType mainPS(Pixel_PosUv input)
{
	PixelOutputType light_out;
    light_out.diffuse       = float4(0.0f, 0.0f, 0.0f, 1.0f);
	light_out.specular 		= float4(0.0f, 0.0f, 0.0f, 1.0f);
    light_out.volumetric    = float4(0.0f, 0.0f, 0.0f, 1.0f);

    float2 uv = input.uv;
    
    // Sample textures
    float4 albedo_sample    = tex_albedo.Sample(sampler_point_clamp, uv);
	float4 normal_sample 	= tex_normal.Sample(sampler_point_clamp, uv);
	float4 material_sample  = tex_material.Sample(sampler_point_clamp, uv);
	float depth_sample   	= tex_depth.Sample(sampler_point_clamp, uv).r;
	float ssao_sample 		= tex_ssao.Sample(sampler_point_clamp, uv).r;
    float2 sample_ssr       = tex_ssr.Sample(sampler_point_clamp, uv).xy;

	// Post-process samples	
	float3 normal	= normal_decode(normal_sample.xyz);
    float metallic 	= material_sample.g;
	float occlusion = normal_sample.a;
	occlusion 		= min(occlusion, ssao_sample);
    bool is_sky 	= material_sample.a == 0.0f;
    
	// Compute camera to pixel vector
    float3 position_world   = get_position_from_depth(depth_sample, uv);
    float3 camera_to_pixel  = normalize(position_world - g_camera_position.xyz);
	
    // Fill light struct
    Light light;
    light.color 	                = color.xyz;
    light.position 	                = position.xyz;
    light.intensity 			    = intensity_range_angle_bias.x;
    light.range 				    = intensity_range_angle_bias.y;
    light.angle 				    = intensity_range_angle_bias.z;
    light.bias					    = intensity_range_angle_bias.w;
    light.normal_bias 			    = normalBias_shadow_volumetric_contact.x;
    light.cast_shadows 		        = normalBias_shadow_volumetric_contact.y;
    light.cast_contact_shadows 	    = normalBias_shadow_volumetric_contact.z;
    light.cast_transparent_shadows  = color.w;
    light.is_volumetric 	        = normalBias_shadow_volumetric_contact.w;
    light.distance_to_pixel         = length(position_world - light.position);
    #if DIRECTIONAL
    light.array_size    = 4;
    light.direction	    = direction.xyz; 
    light.attenuation   = 1.0f;
    #elif POINT
    light.array_size    = 1;
    light.direction	    = normalize(position_world - light.position);
    light.attenuation   = saturate(1.0f - (light.distance_to_pixel / light.range)); light.attenuation *= light.attenuation;    
    #elif SPOT
    light.array_size    = 1;
    light.direction	    = normalize(position_world - light.position);
    float cutoffAngle   = 1.0f - light.angle;
    float theta         = dot(direction.xyz, light.direction);
    float epsilon       = cutoffAngle - cutoffAngle * 0.9f;
    light.attenuation   = saturate((theta - cutoffAngle) / epsilon); // attenuate when approaching the outer cone
    light.attenuation   *= saturate(1.0f - light.distance_to_pixel / light.range); light.attenuation *= light.attenuation;
    #endif
    light.intensity     *= light.attenuation;
    
    // Volumetric lighting (requires shadow maps)
    [branch]
    if (light.cast_shadows && light.is_volumetric)
    {
        light_out.volumetric.rgb = VolumetricLighting(light, position_world, uv);
    }
    
    // Ignore sky (but after we have allowed for the volumetric light to affect it)
    if (is_sky)
    {
        return light_out;
    }
    
    // Shadow 
    {
        float4 shadow = 1.0f;
        
        // Shadow mapping
        [branch]
        if (light.cast_shadows)
        {
            shadow = Shadow_Map(uv, normal, depth_sample, position_world, light, albedo_sample.a != 1.0f);
        }
        
        // Screen space shadows
        [branch]
        if (light.cast_contact_shadows)
        {
            shadow.a = min(shadow.a, ScreenSpaceShadows(light, position_world, uv)); 
        }
    
        // Occlusion from texture and ssao
        shadow.a = min(shadow.a, occlusion);
        
        // Modulate light intensity and color
        light.intensity  *= shadow.a;
        light.color     	*= shadow.rgb;
    }

    // Create material
    Material material;
	material.albedo		= albedo_sample.rgb;
    material.roughness  = material_sample.r;
    material.metallic   = material_sample.g;
    material.emissive   = material_sample.b;
    material.F0         = lerp(0.04f, material.albedo, material.metallic);

    // Reflectance equation
    [branch]
    if (light.intensity > 0.0f)
    {
        // Compute some stuff
        float3 l		= -light.direction;
        float3 v 		= -camera_to_pixel;
        float3 h 		= normalize(v + l);
        float v_dot_h 	= saturate(dot(v, h));
        float n_dot_v 	= saturate(dot(normal, v));
        float n_dot_l 	= saturate(dot(normal, l));
        float n_dot_h 	= saturate(dot(normal, h));
        float3 radiance	= light.color * light.intensity * n_dot_l;
    
        // BRDF components
        float3 F 			= 0.0f;
        float3 cDiffuse 	= BRDF_Diffuse(material, n_dot_v, n_dot_l, v_dot_h);	
        float3 cSpecular 	= BRDF_Specular(material, n_dot_v, n_dot_l, n_dot_h, v_dot_h, F);

        // SSR
        float3 light_reflection = 0.0f;
        [branch]
        if (g_ssr_enabled && sample_ssr.x != 0.0f && sample_ssr.y != 0.0f)
        {
            light_reflection = tex_frame.Sample(sampler_bilinear_clamp, sample_ssr.xy).rgb * F;
        }
    
        light_out.diffuse.rgb   = cDiffuse * radiance * energy_conservation(F, material.metallic);
        light_out.specular.rgb	= cSpecular * radiance + light_reflection;
    }

	return light_out;
}
