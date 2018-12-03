static const float tex_maxMip = 11.0f;

float3 GetSpecularDominantDir(float3 normal, float3 reflection, float roughness)
{
	const float smoothness = 1.0f - roughness;
	const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(normal, reflection, lerpFactor);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	float smoothness = 1.0 - roughness;
    return F0 + (max(float3(smoothness, smoothness, smoothness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
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

float3 ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, Texture2D tex_environment, Texture2D tex_lutIBL, SamplerState samplerLinear, float ambientTerm)
{
	float3 reflection 	= reflect(camera_to_pixel, normal);
	// From Sebastien Lagarde Moving Frostbite to PBR page 69
	reflection	= GetSpecularDominantDir(normal, reflection, material.roughness);

	float NdV 	= saturate(dot(-camera_to_pixel, normal));
	float3 F 	= FresnelSchlickRoughness(NdV, material.F0, material.roughness);

	float3 kS 	= F; 			// The energy of light that gets reflected
	float3 kD 	= 1.0f - kS;	// Remaining energy, light that gets refracted
	kD 			*= 1.0f - material.metallic;	

	// Diffuse
	float3 irradiance	= tex_environment.SampleLevel(samplerLinear, DirectionToSphereUV(normal), tex_maxMip).rgb;
	float3 cDiffuse		= irradiance * material.albedo;

	// Specular
	float mipSelect 		= material.alpha * tex_maxMip;
	float3 prefilteredColor	= tex_environment.SampleLevel(samplerLinear, DirectionToSphereUV(reflection), mipSelect).rgb;
	float2 envBRDF  		= tex_lutIBL.Sample(samplerLinear, float2(NdV, material.roughness)).xy;
	float3 cSpecular 		= prefilteredColor * (F * envBRDF.x + envBRDF.y);

	return (kD * cDiffuse + cSpecular) * ambientTerm; 
}