/*-----------------------------------------------------------------------------------------------
										[F - Fresnel]
Fresnel factor called F. F0 represents the base reflectivity of the surface.
-----------------------------------------------------------------------------------------------*/
float3 Fresnel_Schlick(float HdV, float3 F0)
{
  return F0 + (1.0f - F0) * pow(1.0f - HdV, 5.0f);
}

float3 Specular_F_Roughness(float3 F0, float a, float VdH)
{
	// Sclick using roughness to attenuate fresnel.
    return (F0 + (max(1.0f - a, F0) - F0) * pow((1.0f - VdH), 5.0f));
}

/*-----------------------------------------------------------------------------------------------
								[G - Geometric shadowing]
		Geometry shadowing term G. Defines the shadowing from the microfacets
------------------------------------------------------------------------------------------------*/
float Geometric_Smith_Schlick_GGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0f;

    float nom   = NdotV;
    float denom = NdotV * (1.0f - k) + k;

    return nom / denom;
}

/*-----------------------------------------------------------------------------------------------
								[D - Normal distribution]
------------------------------------------------------------------------------------------------*/
float Distribution_GGX(float a, float NdH)
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
										[DIFFUSE]
------------------------------------------------------------------------------------------------*/
float3 ComputeLightDiffuse(float3 albedo)
{
	return albedo / PI;
}


float3 PBR
(
float3 albedo, 
float roughness, 
float metallic, 
float userReflectivity, 
float3 normal, 
float3 viewDir, 
float3 lightDir, 
float3 lightColor, 
float lightIntensity, 
float ambientLightIntensity,
float3 envColor,
float3 irradianceColor)
{
	//= Compute some useful values ==============
    float NdL = saturate(dot(normal, lightDir));
    float NdV = saturate(dot(normal, viewDir));
    float3 h = normalize(lightDir + viewDir);
    float NdH = saturate(dot(normal, h));
    float VdH = saturate(dot(viewDir, h));
	float a = max(0.001f, roughness * roughness);
	//===========================================

	float3 albedoColor 		= albedo - albedo * metallic; // Diffuse Color
	float3 F0 				= lerp(0.03f, albedo, metallic); // F0
	
	// Diffuse ================================================
    float3 cDiff 			= ComputeLightDiffuse(albedoColor);
	//=========================================================
	
	// Specular =================================================================================
	// Cook-Torrance Specular (BRDF)
    float3 F 				= Fresnel_Schlick(VdH, F0); // Fresnel	
    float G 				= Geometric_Smith_Schlick_GGX(NdV, roughness); // Geometric shadowing	
    float D 				= Distribution_GGX(a, NdH); // Normal distribution
	float3 nominator 		= F * G * D;
	float denominator 		= 4.0f * NdL * NdV + 0.0001f;
    float3 cSpec 			= nominator / denominator;
	//===========================================================================================
	
	//= Reflectivity ==============================================
	float smoothness		= 1.0f - roughness;
	smoothness 				*= userReflectivity;
	float reflectivity		= min(1.0f, metallic + smoothness);
	float3 envFresnel 		= Specular_F_Roughness(F0, a, VdH);	
	float3 envSpecular 		= envFresnel * envColor * reflectivity;
	//=============================================================
	
	float3 finalLight		= lightColor * lightIntensity * NdL * (cDiff * (1.0f - cSpec) + cSpec);
	float3 finalReflection 	= envFresnel * envColor * reflectivity;
	float3 finalAlbedo 		= albedoColor * irradianceColor * ambientLightIntensity;
	
	return finalLight + finalReflection + finalAlbedo;
}