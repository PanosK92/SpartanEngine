/*------------------------------------------------------------------------------
							[VERTEX SHADER INPUTS]
------------------------------------------------------------------------------*/
struct Vertex_Pos
{
    float4 position : POSITION0;
};

struct Vertex_PosUv
{
    float4 position : POSITION0;
    float2 uv 		: TEXCOORD0;
};

struct Vertex_PosColor
{
    float4 position : POSITION0;
    float4 color 	: COLOR;
};


struct Vertex_PosUvTbn
{
	float4 position 	: POSITION0;
    float2 uv 			: TEXCOORD0;
    float3 normal 		: NORMAL;
    float3 tangent		: TANGENT;
	float3 bitangent 	: BITANGENT;
};


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
};

struct Light
{
	float3 color;
	float intensity;
	float3 direction;
	float padding;
};

/*------------------------------------------------------------------------------
								[GLOBALS]
------------------------------------------------------------------------------*/
#define PI 3.1415926535897932384626433832795
#define EPSILON 2.7182818284

/*------------------------------------------------------------------------------
							[GAMMA CORRECTION]
------------------------------------------------------------------------------*/
float4 ToLinear(float4 color)
{
	return pow(abs(color), 2.2f);
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
	normalMapSample.r *= intensity;
	normalMapSample.g *= intensity;
	
	// construct TBN matrix
	float3 N = normalW;
	float3 T = tangentW;
	float3 B = bitangentW;
	float3x3 TBN = float3x3(T, B, N); 
	
	// transform from tangent space to world space
	float3 bumpedNormal = normalize(mul(normalMapSample, TBN)); 
	
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

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
float LinerizeDepth(float depth, float near, float far)
{
	return (far / (far - near)) * (1.0f - (near / depth));
}

float3 ReconstructPositionWorld(float depth, matrix mViewProjectionInverse, float2 texCoord)
{	
	float x = texCoord.x * 2.0f - 1.0f;
	float y = (1.0f - texCoord.y) * 2.0f - 1.0f;
	float z = depth;
    float4 projectedPos = float4(x, y, z, 1.0f); // clip space
	float4 worldPos = mul(projectedPos, mViewProjectionInverse); // world space
    return worldPos.xyz / worldPos.w;  
}

// Returns linear depth
float GetDepthLinear(Texture2D texDepth, SamplerState samplerState, float nearPlane, float farPlane, float2 texCoord)
{
	float depth = texDepth.Sample(samplerState, texCoord).r;
	return 1.0f - LinerizeDepth(depth, nearPlane, farPlane);
}

// Returns normal
float3 GetNormalUnpacked(Texture2D texNormal, SamplerState samplerState, float2 texCoord)
{
	float3 normal = texNormal.Sample(samplerState, texCoord).rgb;
	return normalize(UnpackNormal(normal));
}

// Returns world position
float3 GetPositionWorldFromDepth(Texture2D texDepth, SamplerState samplerState, matrix mViewProjectionInverse, float2 texCoord)
{
	float depth = texDepth.Sample(samplerState, texCoord).g;
	return ReconstructPositionWorld(depth, mViewProjectionInverse, texCoord);
}