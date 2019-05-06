//= F - Fresnel ============================================================
float3 F_Schlick(float HdV, float3 f0)
{
	return f0 + (1.0f - f0) * pow(1.0f - HdV, 5.0f);
}

//= G - Geometric shadowing ================================================
float G_GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float G_GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx2  = G_GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = G_GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

//= D - Normal distribution ================================================
float D_GGX(float NdotH, float a)
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

float3 BRDF_Specular(Material material, float n_dot_v, float n_dot_l, float n_dot_h, float v_dot_h, out float3 F)
{
	F 					= F_Schlick(v_dot_h, material.F0);
    float G 			= G_GeometrySmith(n_dot_v, n_dot_l, material.roughness);
    float D 			= D_GGX(n_dot_h, material.roughness_alpha);
	float3 nominator 	= F * G * D;
	float denominator 	= 4.0f * n_dot_l * n_dot_v;
	return nominator / max(0.00001f, denominator);
}

float3 BRDF_Diffuse(Material material, float n_dot_v, float n_dot_l, float v_dot_h)
{
	return Diffuse_OrenNayar(material.albedo, material.roughness, n_dot_v, n_dot_l, v_dot_h);
}

float3 BRDF(Material material, Light light, float3 normal, float3 camera_to_pixel)
{
	// Compute some common dot products
	float3 h 		= normalize(light.direction - camera_to_pixel);
	float n_dot_v 	= saturate(dot(normal, -camera_to_pixel));
    float n_dot_l 	= saturate(dot(normal, light.direction));   
    float n_dot_h 	= saturate(dot(normal, h));
    float v_dot_h 	= saturate(dot(-camera_to_pixel, h));
	
	// BRDF components
    float3 cDiffuse 	= BRDF_Diffuse(material, n_dot_v, n_dot_l, v_dot_h);
	float3 f 			= 0.0f;
	float3 cSpecular 	= BRDF_Specular(material, n_dot_v, n_dot_l, n_dot_h, v_dot_h, f);
	
	float3 kS 	= f; 			// The energy of light that gets reflected
	float3 kD 	= 1.0f - kS; 	// Remaining energy, light that gets refracted
	kD 			*= 1.0f - material.metallic;	
	
	float3 radiance = light.color * light.intensity;
	return (kD * cDiffuse + cSpecular) * radiance * n_dot_l;
}