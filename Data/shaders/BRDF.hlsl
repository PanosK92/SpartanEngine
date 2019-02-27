//= F - Fresnel ============================================================
float3 F_FresnelSchlick(float HdV, float3 F0)
{
	return F0 + (1.0f - F0) * pow(1.0f - HdV, 5.0f);
}
 
//= G - Geometric shadowing ================================================
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

//= D - Normal distribution ================================================
float DistributionGGX(float NdotH, float a)
{
    float a2     = a*a;
    float NdotH2 = NdotH * NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom 		= PI * denom * denom;
	
    return num / denom;
}

//= BRDF - DIFFUSE ==========================================================
// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
float3 Diffuse_OrenNayar(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
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
	// Compute some common vectors
	float3 h 	= normalize(light.direction - camera_to_pixel);
	float NdotV = saturate(dot(normal, -camera_to_pixel));
    float NdotL = saturate(dot(normal, light.direction));   
    float NdotH = saturate(dot(normal, h));
    float VdotH = saturate(dot(-camera_to_pixel, h));
	// BRDF Diffuse
    float3 cDiffuse 	= Diffuse_OrenNayar(material.albedo, material.roughness, NdotV, NdotL, VdotH);
	
	// BRDF Specular	
	float3 F 			= F_FresnelSchlick(VdotH, material.F0);
    float G 			= GeometrySmith(NdotV, NdotL, material.roughness);
    float D 			= DistributionGGX(NdotH, material.roughness_alpha);
	float3 nominator 	= F * G * D;
	float denominator 	= 4.0f * NdotL * NdotV;
	float3 cSpecular 	= nominator / max(0.00001f, denominator);
	
	float3 kS 	= F; 			// The energy of light that gets reflected
	float3 kD 	= 1.0f - kS; 	// Remaining energy, light that gets refracted
	kD 			*= 1.0f - material.metallic;	
	
	float3 radiance = light.color * light.intensity;
	return (kD * cDiffuse + cSpecular) * radiance * NdotL;
}