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

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 normalW, float3 tangentW, float strength)
{
	normalMapSample = 2.0f * normalMapSample - 1.0f; // unpack normal
	normalMapSample = normalize(normalMapSample); // normalize normal
	
	// normal intensity
	normalMapSample.r *= strength;
	normalMapSample.g *= strength;
	
	float3 N = normalW;
	float3 T = normalize(tangentW - dot(tangentW, N) * N); // re-orthogonalize T with respect to N
	float3 B = cross(N, T); // calculate the perpendicular vector B with the cross product of T and N
	
	// construct TBN matrix
	float3x3 TBN = float3x3(T, B, N); 
	
	float3 bumpedNormal = normalize(mul(normalMapSample, TBN)); // transform from tangent space to world space
	
    return bumpedNormal;
}

/*------------------------------------------------------------------------------
								[TONEMAPPING]
------------------------------------------------------------------------------*/
float3 Uncharted2Tonemap(float3 color)
{
	float A = 0.15f;
	float B = 0.50f;
	float C = 0.10f;
	float D = 0.20f;
	float E = 0.02f;
	float F = 0.30f;

	return ((color * ( A * color + C * B) + D * E ) / ( color * ( A * color + B) + D * F)) - E / F;
}

float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}