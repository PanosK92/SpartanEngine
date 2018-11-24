//= F - Fresnel ============================================================
float3 F_FresnelSchlick(float HdV, float3 F0)
{
	return F0 + (1.0f - F0) * pow(1.0f - HdV, 5.0f);
}

//= G - Geometric shadowing ================================================
float G_SmithSchlickGGX(float NdotV, float NdotL, float a)
{
    float k = a * 0.5f;
    float GV = NdotV / (NdotV * (1.0f - k) + k);
    float GL = NdotL / (NdotL * (1.0f - k) + k);

    return GV * GL;
}

//= D - Normal distribution ================================================
float D_GGX(float a, float NdotH)
{
    // Isotropic ggx.
    float a2 = a*a;
    float NdotH2 = NdotH * NdotH;

    float denominator = NdotH2 * (a2 - 1.0f) + 1.0f;
    denominator *= denominator;
    denominator *= PI;

    return a2 / denominator;
}

//= BRDF - DIFFUSE ==========================================================
// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
float3 Diffuse_OrenNayar( float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
	float a 	= Roughness * Roughness;
	float s 	= a;					// / ( 1.29 + 0.5 * a );
	float s2 	= s * s;
	float VoL 	= 2 * VoH * VoH - 1; 	// double angle identity
	float Cosri = VoL - NoV * NoL;
	float C1 	= 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 	= 0.45 * s2 / (s2 + 0.09) * Cosri * ( Cosri >= 0 ? rcp( max( NoL, NoV + 0.0001f ) ) : 1 );
	return DiffuseColor / PI * ( C1 + C2 ) * ( 1 + Roughness * 0.5 );
}

//============================================================================
float3 BRDF(Material material, Light light, float3 normal, float3 camera_to_pixel)
{
	// Compute some commmon vectors
	float3 h 	= normalize(light.direction - camera_to_pixel);
	float NdotV = abs(dot(normal, -camera_to_pixel)) + 1e-5;
    float NdotL = clamp(dot(normal, light.direction), 0.0f, 1.0f);   
    float NdotH = clamp(dot(normal, h), 0.0f, 1.0f);
    float VdotH = clamp(dot(-camera_to_pixel, h), 0.0f, 1.0f);
	
	 // BRDF Diffuse
    float3 cDiffuse 	= Diffuse_OrenNayar(material.color_diffuse, material.roughness, NdotV, NdotL, VdotH);
	
	// BRDF Specular	
	float3 F 			= F_FresnelSchlick(VdotH, material.color_specular);
    float G 			= G_SmithSchlickGGX(NdotV, NdotL, material.alpha);
    float D 			= D_GGX(material.alpha, NdotH);
	float3 nominator 	= F * G * D;
	float denominator 	= 4.0f * NdotL * NdotV;
	float3 cSpecular 	= nominator / max(0.001f, denominator);
	
	return light.color * light.intensity * NdotL * (cDiffuse * (1.0f - cSpecular) + cSpecular);
}