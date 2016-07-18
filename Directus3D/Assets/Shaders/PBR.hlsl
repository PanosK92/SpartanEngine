/*-----------------------------------------------------------------------------------------------
										[F - Fresnel]
Fresnel factor called F. It describes how light reflects and refracts at the 
intersection of two different media (most often in computer graphics, air and the shaded surface)
-----------------------------------------------------------------------------------------------*/
float3 Fresnel_Schlick(float HdV, float3 F0)
{
  return F0 + (1-F0) * pow( 1 - HdV, 5);
}

float3 Specular_F_Roughness(float3 specularColor, float a, float3 h, float3 v)
{
	// Sclick using roughness to attenuate fresnel.
    return (specularColor + (max(1.0f-a, specularColor) - specularColor) * pow((1 - saturate(dot(v, h))), 5));
}

/*-----------------------------------------------------------------------------------------------
								[G - Geometric shadowing]
		Geometry shadowing term G. Defines the shadowing from the microfacets
------------------------------------------------------------------------------------------------*/
float Geometric_Smith_Schlick_GGX(float a, float NdV, float NdL)
{
        // Smith schlick-GGX.
    float k = a * 0.5f;
    float GV = NdV / (NdV * (1 - k) + k);
    float GL = NdL / (NdL * (1 - k) + k);

    return GV * GL;
}

/*-----------------------------------------------------------------------------------------------
								[D - Normal distribution]

------------------------------------------------------------------------------------------------*/
float NormalDistribution_GGX(float a, float NdH)
{
    // Isotropic ggx.
    float a2 = a*a;
    float NdH2 = NdH * NdH;

    float denominator = NdH2 * (a2 - 1.0f) + 1.0f;
    denominator *= denominator;
    denominator *= PI;

    return a2 / denominator;
}

/*-----------------------------------------------------------------------------------------------
										[BRDF]
------------------------------------------------------------------------------------------------*/
float3 DiffuseBRDF(float3 cDiff)
{
	return cDiff / PI;
}

float3 ComputeLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness, float3 lightColor, float3 lightDir, float3 viewDir)
{
    // Compute some useful values.
    float NdL = saturate(dot(normal, lightDir));
    float NdV = saturate(dot(normal, viewDir));
    float3 h = normalize(lightDir + viewDir);
    float NdH = saturate(dot(normal, h));
    float VdH = saturate(dot(viewDir, h));
    float LdV = saturate(dot(lightDir, viewDir));
    float a = max(0.001f, roughness * roughness);

	// calculate deiffuse contribution
    float3 cDiff = DiffuseBRDF(albedoColor);
	
	// calculate specular contribution
    float F 			= Fresnel_Schlick(VdH, specularColor);
    float G 			= Geometric_Smith_Schlick_GGX(a, NdV, NdL);
    float D 			= NormalDistribution_GGX(a, NdH);
	
	float numerator 	= F * G * D;
	float denominator 	= 4.0f * NdL * NdV + 0.0001f;
    float3 cSpec 		= numerator / denominator;
	
	// return final color
    return lightColor * NdL * (cDiff * (1.0f - cSpec) + cSpec);
}

float3 BRDF
(
float3 albedo, 
float roughness, 
float metallic, 
float specular, 
float3 normal, 
float3 viewDir, 
float3 lightDir, 
float3 lightColor, 
float lightAttunation, 
float lightIntensity, 
float ambientLightIntensity, 
float3 environmentColor, 
float3 irradianceColor)
{
    float3 albedoColor = albedo - albedo * metallic;
    float3 specularColor = lerp(0.03f, albedo, metallic);
			
	float3 light 		= ComputeLight(albedoColor, specularColor, normal, roughness, lightColor, lightDir, viewDir);	
	float3 envFresnel 	= Specular_F_Roughness(specularColor, roughness * roughness, normal, viewDir);
	
	float3 finalLight		= light * lightAttunation * lightIntensity;
	float3 finalReflection 	= envFresnel * environmentColor * specular;
	float3 finalAlbedo		= albedoColor * irradianceColor * ambientLightIntensity;

	return finalLight + finalReflection + finalAlbedo;
}