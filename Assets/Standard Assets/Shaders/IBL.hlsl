float3 F_FresnelSchlick(float3 specularColor, float a, float3 h, float3 v)
{
	// Sclick using roughness to attenuate fresnel.
    return specularColor + (max(1.0f - a, specularColor) - specularColor) * pow((1.0f - saturate(dot(v, h))), 5.0f);
}

float GetMipFromRoughness(float roughness)
{
	return (roughness * 12.0f - pow(roughness, 6.0f) * 1.5f);
}

float3 EnvBRDFApprox(float3 specColor, float roughness, float ndv)
{
    const float4 c0 = float4(-1, -0.0275, -0.572, 0.022 );
    const float4 c1 = float4(1, 0.0425, 1.0, -0.04 );
    float4 r 		= roughness * c0 + c1;
    float a004 		= min( r.x * r.x, exp2( -9.28 * ndv ) ) * r.x + r.y;
    float2 AB 		= float2( -1.04, 1.04 ) * a004 + r.zw;
    return specColor * AB.x + AB.y;
}

float3 FixCubeLookup(float3 v) 
 {
    float M = max(max(abs(v.x), abs(v.y)), abs(v.z));
    float scale = (512 - 1) / 512;
    
    if (abs(v.x) != M) v.x += scale;
    if (abs(v.y) != M) v.y += scale;
    if (abs(v.z) != M) v.z += scale; 
    
    return v;
}

float3 ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, SamplerState samplerLinear)
{
	float3 reflection 	= reflect(camera_to_pixel, normal);
	const float ndv 	= saturate(dot(camera_to_pixel, normal));
	
	const float mipSelect 	= GetMipFromRoughness(material.roughness);	
	float3 cubeSpecular		= ToLinear(environmentTex.SampleLevel(samplerLinear, FixCubeLookup(reflection), mipSelect)).rgb;
	float3 cubeDiffuse  	= ToLinear(environmentTex.SampleLevel(samplerLinear, FixCubeLookup(normal), 10.0f)).rgb;

	const float3 environmentSpecular 	= EnvBRDFApprox(material.color_specular, material.roughness, ndv);
	const float3 environmentDiffuse 	= EnvBRDFApprox(material.color_diffuse, 1.0f, ndv);

	float3 cDiffuse 	= cubeDiffuse * environmentDiffuse;
	float3 cSpecular 	= cubeSpecular * environmentSpecular;
	
	return cDiffuse + cSpecular;
}