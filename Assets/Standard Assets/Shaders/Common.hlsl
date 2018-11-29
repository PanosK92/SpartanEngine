/*------------------------------------------------------------------------------
								[GLOBALS]
------------------------------------------------------------------------------*/
#define PI 3.1415926535897932384626433832795
#define EPSILON 2.7182818284

/*------------------------------------------------------------------------------
							[STRUCTS]
------------------------------------------------------------------------------*/
struct Material
{
	float3 albedo;
	float roughness;
	float metallic;
	float3 padding;
	float emission;
	float3 color_diffuse;
	float3 color_specular; // Aka F0
	float alpha;
};

struct Light
{
	float3 color;
	float intensity;
	float3 direction;
	float padding;
};

/*------------------------------------------------------------------------------
							[GAMMA CORRECTION]
------------------------------------------------------------------------------*/
float4 ToLinear(float4 color)
{
	return pow(abs(color), 2.2f);
}

float3 ToLinear(float3 color)
{
	return pow(abs(color), 2.2f);
}

float4 ToGamma(float4 color)
{
	return pow(abs(color), 1.0f / 2.2f); 
}

float3 ToGamma(float3 color)
{
	return pow(abs(color), 1.0f / 2.2f); 
}

/*------------------------------------------------------------------------------
								[NORMALS]
------------------------------------------------------------------------------*/
float3 UnpackNormal(float3 normal)
{
	return normal * 2.0f - 1.0f;
}

float3 PackNormal(float3 normal)
{
	return normal * 0.5f + 0.5f;
}

float3 TangentToWorld(float3 normalMapSample, float3 normalW, float3 tangentW, float3 bitangentW, float intensity)
{
	// normal intensity
	intensity			= clamp(intensity, 0.01f, 1.0f);
	normalMapSample.r 	*= intensity;
	normalMapSample.g 	*= intensity;
	
	// construct TBN matrix
	float3 N 		= normalW;
	float3 T 		= tangentW;
	float3 B 		= bitangentW;
	float3x3 TBN 	= float3x3(T, B, N); 
	
	// transform from tangent space to world space
	float3 bumpedNormal = normalize(mul(normalMapSample, TBN)); 
	
    return bumpedNormal;
}

/*------------------------------------------------------------------------------
						[POSITION RECONSTRUCTION]
------------------------------------------------------------------------------*/
float3 ReconstructPositionWorld(float depth, matrix viewProjectionInverse, float2 texCoord)
{	
	float x 			= texCoord.x * 2.0f - 1.0f;
	float y 			= (1.0f - texCoord.y) * 2.0f - 1.0f;
	float z				= depth;
    float4 pos_clip 	= float4(x, y, z, 1.0f);
	float4 pos_world 	= mul(pos_clip, viewProjectionInverse);
	
    return pos_world.xyz / pos_world.w;  
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
float LinerizeDepth(float depth, float near, float far)
{
	return (far / (far - near)) * (1.0f - (near / depth));
}

// Returns normal
float3 GetNormalUnpacked(Texture2D texNormal, SamplerState samplerState, float2 texCoord)
{
	float3 normal = texNormal.Sample(samplerState, texCoord).rgb;
	return normalize(UnpackNormal(normal));
}