//= TEXTURES ==============================
Texture2D texAlbedo 		: register(t0);
Texture2D texNormal 		: register(t1);
Texture2D texDepth 			: register(t2);
Texture2D texMaterial 		: register(t3);
Texture2D texNoise			: register(t4);
Texture2D texShadows 		: register(t5);
TextureCube environmentTex 	: register(t6);
//=========================================

//= SAMPLERS ==============================
SamplerState samplerPoint : register(s0);
SamplerState samplerAniso : register(s1);
//=========================================

//= CONSTANT BUFFERS ===================
cbuffer MatrixBuffer : register(b0)
{
	matrix mWorldViewProjection;
    matrix mViewProjectionInverse;
	matrix mView;
}

#define MaxLights 64
cbuffer MiscBuffer : register(b1)
{
    float4 cameraPosWS;
	
    float4 dirLightColor;
    float4 dirLightIntensity;
	float4 dirLightDirection;
	
    float4 pointLightPosition[MaxLights];
    float4 pointLightColor[MaxLights];
    float4 pointLightIntenRange[MaxLights];
	
	float4 spotLightColor[MaxLights];
	float4 spotLightPosition[MaxLights];
    float4 spotLightDirection[MaxLights];
    float4 spotLightIntenRangeAngle[MaxLights];
	
    float pointlightCount;
	float spotlightCount;
    float nearPlane;
    float farPlane;
	float softShadows;
    float2 resolution;
    float padding;
};
//=====================================

// = INCLUDES ========
#include "Helper.hlsl"
#include "PBR.hlsl"
//====================

//= INPUT LAYOUT ======================
struct VertexInputType
{
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
//=====================================

//= VS() ==================================================================================
PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output;
    
    input.position.w = 1.0f;
    output.position = mul(input.position, mWorldViewProjection);
    output.uv = input.uv;
	
    return output;
}

//= PS() =================================================================================
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
    float3 finalColor = float3(0, 0, 0);
	
	// Sample from G-Buffer
    float3 albedo = ToLinear(texAlbedo.Sample(samplerAniso, input.uv)).rgb;
    float4 normalSample = texNormal.Sample(samplerAniso, input.uv);
    float4 depthSample = texDepth.Sample(samplerPoint, input.uv);
    float4 materialSample = texMaterial.Sample(samplerPoint, input.uv);
		
	// Extract any values out of those samples
    float3 normal = normalize(UnpackNormal(normalSample.rgb));
    float depth = depthSample.g;
	float emission = depthSample.b * 100.0f;
    float3 worldPos = ReconstructPosition(depth, input.uv, mViewProjectionInverse);
	float shadowing = softShadows == 1.0f ? texShadows.Sample(samplerAniso, input.uv).a : normalSample.a;	
    shadowing = clamp(shadowing, 0.1f, 1.0f);
    float roughness = materialSample.r;
    float metallic = materialSample.g;
    float specular = materialSample.b;
    float type = materialSample.a;
	
	// Calculate view direction and the reflection vector
    float3 viewDir = normalize(cameraPosWS.xyz - worldPos.xyz);
    float3 reflectionVector = reflect(-viewDir, normal);
	
	// Sample the skybox and the irradiance texture
    float mipIndex = roughness * roughness * 10.0f;
    float3 envColor = ToLinear(environmentTex.SampleLevel(samplerAniso, reflectionVector, mipIndex)).rgb;
    float3 irradiance = ToLinear(environmentTex.SampleLevel(samplerAniso, reflectionVector, 10.0f)).rgb;
	
    if (type == 0.1f) // Texture mapping
    {
        finalColor = ToLinear(environmentTex.Sample(samplerAniso, -viewDir)).rgb;
        finalColor = ACESFilm(finalColor);
        finalColor = ToGamma(finalColor);
        finalColor *= clamp(dirLightIntensity[0], 0.1f, 1.0f); // some totally fake day/night effect	
        float luma = dot(finalColor, float3(0.299f, 0.587f, 0.114f)); // compute luma as alpha for fxaa

        return float4(finalColor, luma);
    }
	
    float ambientLightIntensity = 0.3f;

	//= DIRECTIONAL LIGHT ========================================================================================================================================
	// Get light data
	float3 lightColor 		= dirLightColor.rgb;
	float lightIntensity 	= dirLightIntensity.r;
	float3 lightDir 		= normalize(-dirLightDirection).rgb;

	// Compute
	ambientLightIntensity 	= clamp(lightIntensity * 0.3f, 0.0f, 1.0f);
	lightIntensity 			*= shadowing;
	lightIntensity 			+= emission;
	envColor 				*= clamp(lightIntensity, 0.0f, 1.0f);
	irradiance 				*= clamp(lightIntensity, 0.0f, 1.0f);
	
	// Compute
	finalColor += PBR(albedo, roughness, metallic, specular, normal, viewDir, lightDir, lightColor, lightIntensity, ambientLightIntensity, envColor, irradiance);
	//============================================================================================================================================================
	
	//= POINT LIGHTS =============================================================================================================================================
    for (int i = 0; i < pointlightCount; i++)
    {
		// Get light data
        float3 color 		= pointLightColor[i].rgb;
        float3 position 	= pointLightPosition[i].xyz;
		float intensity 	= pointLightIntenRange[i].x;
        float range 		= pointLightIntenRange[i].y;
        
		// Compute light
        float3 direction 	= normalize(position - worldPos);
        float dist 			= length(worldPos - position);
        float attunation 	= clamp(1.0f - dist / range, 0.0f, 1.0f); attunation *= attunation;
		intensity 			*= attunation;
		intensity 			+= emission;

		// Compute illumination
        if (dist < range)
		{
            finalColor += PBR(albedo, roughness, metallic, specular, normal, viewDir, direction, color, intensity, 0.0f, envColor, irradiance);
		}
    }
	//============================================================================================================================================================
	
	//= SPOT LIGHTS ==============================================================================================================================================
    for (int j = 0; j < spotlightCount; j++)
    {
		// Get light data
        float3 color 		= spotLightColor[j].rgb;
        float3 position 	= spotLightPosition[j].xyz;
		float intensity 	= spotLightIntenRangeAngle[j].x;
		float3 spotDir 		= normalize(-spotLightDirection[j].xyz);
		float range 		= spotLightIntenRangeAngle[j].y;
        float cutoffAngle 	= 1.0f - spotLightIntenRangeAngle[j].z;
        
		// Compute light
		float3 direction 	= normalize(position - worldPos);
		float dist 			= length(worldPos - position);	
		float theta 		= dot(direction, spotDir);
		float epsilon   	= cutoffAngle - cutoffAngle * 0.9f;
		float attunation 	= clamp((theta - cutoffAngle) / epsilon, 0.0f, 1.0f);  // attunate when approaching the outer cone
		attunation 			*= clamp(1.0f - dist / range, 0.0f, 1.0f); attunation *= attunation; // attunate with distance as well
		intensity 			*= attunation;
		intensity 			+= emission;

		// Compute illumination
		if(theta > cutoffAngle)
		{
            finalColor += PBR(albedo, roughness, metallic, specular, normal, viewDir, spotDir, color, intensity, 0.0f, envColor, irradiance);
		}
    }
	//============================================================================================================================================================
	
    finalColor = ACESFilm(finalColor); // ACES Filmic Tone Mapping (default tone mapping curve in Unreal Engine 4)
    finalColor = ToGamma(finalColor); // gamma correction
    float luma = dot(finalColor.rgb, float3(0.299f, 0.587f, 0.114f)); // compute luma as alpha for fxaa

    return float4(finalColor, luma);
}