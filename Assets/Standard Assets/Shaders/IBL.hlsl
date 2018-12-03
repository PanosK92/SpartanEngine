static const float tex_maxMip = 11.0f;

float3 GetSpecularDominantDir(float3 normal, float3 reflection, float roughness)
{
	const float smoothness = 1.0f - roughness;
	const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(normal, reflection, lerpFactor);
}

// https://www.unrealengine.com/blog/physically-based-shading-on-mobile
float3 EnvBRDFApprox(float3 specColor, float roughness, float NdV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f );
    const float4 c1 = float4(1.0f, 0.0425f, 1.0f, -0.04f );
    float4 r 		= roughness * c0 + c1;
    float a004 		= min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB 		= float2(-1.04f, 1.04f) * a004 + r.zw;
    return specColor * AB.x + AB.y;
}

float3 ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, Texture2D tex_environment, SamplerState samplerLinear, float ambientTerm)
{
    float brightness    = clamp(ambientTerm, 0.0, 1.0);
	float3 reflection 	= reflect(camera_to_pixel, normal);
	reflection			= GetSpecularDominantDir(normal, reflection, material.roughness);
	float NdV 			= saturate(dot(camera_to_pixel, normal));

	float mipSelect 		= material.roughness * tex_maxMip;
	float3 cube_specular	= tex_environment.SampleLevel(samplerLinear, DirectionToSphereUV(reflection), mipSelect).rgb;
	float3 cube_diffuse  	= tex_environment.SampleLevel(samplerLinear, DirectionToSphereUV(normal), tex_maxMip).rgb;
	
	float3 env_specular = EnvBRDFApprox(material.color_specular, material.roughness, NdV);
	float3 env_diffuse 	= EnvBRDFApprox(material.color_diffuse, 1.0f, NdV);

	float3 cSpecular 	= cube_specular * env_specular;
	float3 cDiffuse 	= cube_diffuse * env_diffuse;
	
	return (cSpecular + cDiffuse) * brightness;
}