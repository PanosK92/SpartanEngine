float3 GetSpecularDominantDir(float3 normal, float3 reflection, float roughness)
{
	const float smoothness = 1.0f - roughness;
	const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(normal, reflection, lerpFactor);
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

float3 ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, TextureCube tex_cube, SamplerState samplerLinear, float ambientTerm)
{
	float brightness 	= clamp(ambientTerm, 0.0, 1.0);
	float3 reflection 	= reflect(camera_to_pixel, normal);
	reflection			= GetSpecularDominantDir(normal, reflection, material.roughness);
	float NdV 			= saturate(dot(camera_to_pixel, normal));
	
	float mipSelect 		= GetMipFromRoughness(material.roughness);	
	float3 cube_specular	= ToLinear(tex_cube.SampleLevel(samplerLinear, FixCubeLookup(reflection), mipSelect)).rgb;
	float3 cube_diffuse  	= ToLinear(tex_cube.SampleLevel(samplerLinear, FixCubeLookup(reflection), 10.0f)).rgb;
	
	float3 env_specular = EnvBRDFApprox(material.color_specular, material.roughness, NdV);
	float3 env_diffuse 	= EnvBRDFApprox(material.color_diffuse, 1.0f, NdV);

	float3 cSpecular 	= cube_specular * env_specular;
	float3 cDiffuse 	= cube_diffuse * env_diffuse;
		
	return (cDiffuse + cSpecular) * brightness;
}