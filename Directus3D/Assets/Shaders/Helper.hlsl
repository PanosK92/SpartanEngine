/*------------------------------------------------------------------------------
								[GLOBALS]
------------------------------------------------------------------------------*/
#define PI 3.1415926535897932384626433832795
#define EPSILON 2.7182818284

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
float4 ToLinear(float4 color)
{
	return pow(color, 2.2f);
}

float3 ToLinear(float3 color)
{
	return pow(color, 2.2f);
}

float4 ToGamma(float4 color)
{
	return pow(color, 1.0f / 2.2f); 
}

float3 ToGamma(float3 color)
{
	return pow(color, 1.0f / 2.2f); 
}

float LinerizeDepth(float depth, float nearPlane, float farPlane)
{
	return (farPlane / (farPlane - nearPlane)) * (1.0f - (nearPlane / depth));
}

float3 ReconstructPosition(float depth, float2 texCoord, matrix viewProjectionInverse)
{	
	// screen space position
	float x = texCoord.x * 2.0f - 1.0f;
	float y = (1.0f - texCoord.y) * 2.0f - 1.0f;
	float z = depth;
    float4 projectedPos = float4(x, y, z, 1.0f);
	
	// transform to world space
	float4 worldPos = mul(projectedPos, viewProjectionInverse);
	
    return worldPos.xyz / worldPos.w;  
}