//= F - Fresnel ============================================================
float3 Fresnel_Schlick(float HdV, float3 F0)
{
	return F0 + (1.0f - F0) * pow(1.0f - HdV, 5.0f);
}

float3 Fresnel_Schlick(float3 specularColor, float3 h, float3 v)
{
    return (specularColor + (1.0f - specularColor) * pow((1.0f - saturate(dot(v, h))), 5));
}

float3 Specular_F_Roughness(float3 F0, float a, float VdH)
{
	// Sclick using roughness to attenuate fresnel.
    return (F0 + (max(1.0f - a, F0) - F0) * pow((1.0f - VdH), 5.0f));
}

// Used by Disney BRDF.
float Fresnel_Schlick(float u)
{
    float m = saturate( 1.0f - u);
    float m2 = m*m;
    return m2*m2*m;
}
//==========================================================================

//= G - Geometric shadowing ================================================
float Geometric_Smith_Schlick_GGX(float NdotV, float NdotL, float alpha)
{
    float k = alpha * 0.5f;
    float GV = NdotV / (NdotV * (1.0f - k) + k);
    float GL = NdotL / (NdotL * (1.0f - k) + k);

    return GV * GL;
}
//==========================================================================

//= D - Normal distribution ================================================
float Distribution_GGX(float a, float NdotH)
{
    // Isotropic ggx.
    float a2 = a*a;
    float NdotH2 = NdotH * NdotH;

    float denominator = NdotH2 * (a2 - 1.0f) + 1.0f;
    denominator *= denominator;
    denominator *= PI;

    return a2 / denominator;
}
//===========================================================================

//= DIFFUSE =================================================================
float3 GetDiffuse(float3 albedo)
{
	return albedo / PI;
}
//=============================================================================

//= SPECULAR ==================================================================
float3 GetSpecular(float3 F0, float VdotH, float NdotV, float NdotL, float NdotH, float roughness, float alpha)
{
    float3 F 				= Fresnel_Schlick(VdotH, F0);
    float G 				= Geometric_Smith_Schlick_GGX(NdotV, NdotL, alpha);
    float D 				= Distribution_GGX(alpha, NdotH);
	float3 nominator 		= F * G * D;
	float denominator 		= 4.0f * NdotL * NdotV + 0.0001f;
    float3 specular 		= nominator / denominator;
	
	return specular;
}
//============================================================================

// IMAGE BASED LIGHTING ======================================================
float3 IBL(float NdotV, float VdotH, float roughness, float alpha, float3 normal, float3 reflectionVector, float3 albedo, float3 specular)
{
	// Note: Currently this function assumes a cube texture resolution of 1024x1024
	float smoothness = 1.0f - roughness;
	float mipLevel = (1.0f - smoothness * smoothness) * 10.0f;

	float3 indirectDiffuse  = ToLinear(environmentTex.SampleLevel(samplerAniso, normal, 10.0f)).rgb;
	float3 indirectSpecular = ToLinear(environmentTex.SampleLevel(samplerAniso, reflectionVector, mipLevel)).rgb;
	float3 envFresnel = Specular_F_Roughness(specular, alpha, VdotH); 
	
	return (indirectSpecular * envFresnel) + (indirectDiffuse * albedo);
}
//============================================================================

float3 PBR(Material material, Light light, float3 normal, float3 viewDir)
{
	//= Compute some useful values ====================================
    float NdotL = saturate(dot(normal, light.direction));
    float NdotV = saturate(dot(normal, viewDir));
    float3 h = normalize(light.direction + viewDir);
    float NdotH = saturate(dot(normal, h));
    float VdotH = saturate(dot(viewDir, h));
	float alpha = max(0.001f, material.roughness * material.roughness);
	float3 reflectionVector = normalize(reflect(-viewDir, normal));
	//=================================================================
	
	float3 albedo 	= material.albedo - material.albedo * material.metallic;
	float3 specular = lerp(0.03f, material.albedo, material.metallic); // aka F0
	 
    float3 cDiff = GetDiffuse(albedo) ;
	float3 cSpec = GetSpecular(specular, VdotH, NdotV, NdotL, NdotH, material.roughness, alpha);
	float3 ibl = IBL(NdotV, VdotH, material.roughness, alpha, normal, reflectionVector, albedo, specular);

	float3 finalLight		= light.color * light.intensity * NdotL * (cDiff * (1.0f - cSpec) + cSpec);
	float3 finalReflection 	= light.intensity * ibl;
	
	return finalLight + finalReflection;
}