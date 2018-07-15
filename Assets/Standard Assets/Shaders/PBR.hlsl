//= F - Fresnel ============================================================
float3 Fresnel_Schlick(float HdV, float3 F0)
{
	return F0 + (1.0f - F0) * pow(1.0f - HdV, 5.0f);
}

float3 Fresnel_Schlick(float3 specularColor, float3 h, float3 v)
{
    return (specularColor + (1.0f - specularColor) * pow((1.0f - saturate(dot(v, h))), 5));
}

float3 Fresnel_Schlick(float3 specularColor, float a, float3 h, float3 v)
{
	// Sclick using roughness to attenuate fresnel.
    return specularColor + (max(1.0f - a, specularColor) - specularColor) * pow((1.0f - saturate(dot(v, h))), 5.0f);
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
float Geometric_Smith_Schlick_GGX(float NdotV, float NdotL, float a)
{
    float k = a * 0.5f;
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
float Pow5(float value)
{
	return pow(value, 5.0f);
}

float3 Diffuse_Lambert( float3 DiffuseColor )
{
	return DiffuseColor * (1 / PI);
}

// [Burley 2012, "Physically-Based Shading at Disney"]
float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
	float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * Pow5( 1 - NoV );
	float FdL = 1 + (FD90 - 1) * Pow5( 1 - NoL );
	return DiffuseColor * ( (1 / PI) * FdV * FdL );
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
float3 Diffuse_OrenNayar( float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
	float a = Roughness * Roughness;
	float s = a;// / ( 1.29 + 0.5 * a );
	float s2 = s * s;
	float VoL = 2 * VoH * VoH - 1;		// double angle identity
	float Cosri = VoL - NoV * NoL;
	float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * ( Cosri >= 0 ? rcp( max( NoL, NoV + 0.0001f ) ) : 1 );
	return DiffuseColor / PI * ( C1 + C2 ) * ( 1 + Roughness * 0.5 );
}

float3 GetDiffuse(float3 albedo, float roughness, float NdotV, float NdotL, float VdotH)
{
	return Diffuse_OrenNayar(albedo, roughness, NdotV, NdotL, VdotH);
}
//=============================================================================

//= SPECULAR ==================================================================
float3 GetSpecular(float3 F0, float VdotH, float NdotV, float NdotL, float NdotH, float roughness, float a)
{
    float3 F 				= Fresnel_Schlick(VdotH, F0);
    float G 				= Geometric_Smith_Schlick_GGX(NdotV, NdotL, a);
    float D 				= Distribution_GGX(a, NdotH);
	float3 nominator 		= F * G * D;
	float denominator 		= 4.0f * NdotL * NdotV + 0.0001f;
    float3 specular 		= nominator / denominator;
	
	return specular;
}
//============================================================================

// IMAGE BASED LIGHTING ======================================================
// [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
half3 EnvBRDFApprox( half3 SpecularColor, half Roughness, half NoV )
{
	// Adaptation to fit our G term.
	const half4 c0 = { -1, -0.0275, -0.572, 0.022 };
	const half4 c1 = { 1, 0.0425, 1.04, -0.04 };
	half4 r = Roughness * c0 + c1;
	half a004 = min( r.x * r.x, exp2( -9.28 * NoV ) ) * r.x + r.y;
	half2 AB = half2( -1.04, 1.04 ) * a004 + r.zw;

	// Anything less than 2% is physically impossible and is instead considered to be shadowing
	// Note: this is needed for the 'specular' show flag to work, since it uses a SpecularColor of 0
	AB.y *= saturate( 50.0 * SpecularColor.g );

	return SpecularColor * AB.x + AB.y;
}

float3 IBL(float3 v, float3 h, float roughness, float a, float3 normal, float3 reflectionVector, float3 albedo, float3 specular, float3 viewDir, SamplerState samplerAniso)
{
	// Note: Currently this function assumes a cube texture resolution of 1024x1024
	float smoothness = 1.0f - roughness;
	float mipLevel = (1.0f - smoothness * smoothness) * 10.0f;

	float3 indirectDiffuse  = ToLinear(environmentTex.SampleLevel(samplerAniso, normal, 10.0f)).rgb;
	float3 indirectSpecular = ToLinear(environmentTex.SampleLevel(samplerAniso, reflectionVector, mipLevel)).rgb;
	float3 envFresnel = Fresnel_Schlick(specular, a, normal, v); //EnvBRDFApprox(specular, roughness, dot(normal,v));

	float3 cSpecular = indirectSpecular * envFresnel;
	float3 cDiffuse = indirectDiffuse * albedo;
	
	return cSpecular + cDiffuse;
}
//============================================================================

float3 PBR(Material material, Light light, float3 normal, float3 viewDir)
{
	//= Compute some useful values ================================
    float NdotL = saturate(dot(normal, light.direction));
    float NdotV = saturate(dot(normal, viewDir));
    float3 h = normalize(light.direction + viewDir);
    float NdotH = saturate(dot(normal, h));
    float VdotH = saturate(dot(viewDir, h));
	float a = max(0.001f, material.roughness * material.roughness);
	//=============================================================
	
	float3 albedo 	= material.albedo - material.albedo * material.metallic;
	float3 specular = lerp(0.03f, material.albedo, material.metallic); // aka F0
	 
    float3 cDiff = GetDiffuse(albedo, material.roughness, NdotV, NdotL, VdotH);
	float3 cSpec = GetSpecular(specular, VdotH, NdotV, NdotL, NdotH, material.roughness, a);
	
	return light.color * light.intensity * NdotL * (cDiff * (1.0f - cSpec) + cSpec);
}

float3 ImageBasedLighting(Material material, float3 lightDirection, float3 normal, float3 viewDir, SamplerState samplerAniso)
{
	//= Compute some useful values ================================
    float3 h = normalize(lightDirection + viewDir);
	float a = max(0.001f, material.roughness * material.roughness);
	float3 reflectionVector = normalize(reflect(-viewDir, normal));
	//=============================================================
	
	float3 albedo 	= material.albedo - material.albedo * material.metallic;
	float3 specular = lerp(0.03f, material.albedo, material.metallic); // aka F0
	
	return IBL(viewDir, h, material.roughness, a, normal, reflectionVector, albedo, specular, viewDir, samplerAniso);
}