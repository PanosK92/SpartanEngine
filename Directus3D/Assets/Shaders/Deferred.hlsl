//= TEXTURES ==============================
Texture2D texAlbedo 		: register(t0);
Texture2D texNormal 		: register(t1);
Texture2D texDepth 			: register(t2);
Texture2D texMaterial 		: register(t3);
Texture2D texNoise 			: register(t4);
TextureCube environmentTex 	: register(t5);
TextureCube irradianceTex 	: register(t6);
//=========================================

//= SAMPLERS ==============================
SamplerState samplerPoint 	: register(s0);
SamplerState samplerAniso 	: register(s1);
//=========================================

//= DEFINES =========
#define MaxLights 300
//===================

// = INCLUDES ========
#include "Helper.hlsl"
#include "PBR.hlsl"
//====================

//= CONSTANT BUFFERS ===================
cbuffer MiscBuffer : register(b0)
{
	matrix mWorldViewProjection;
	matrix mViewProjectionInverse;
	float4 cameraPosWS;
	float4 dirLightDirection[MaxLights];
	float4 dirLightColor[MaxLights];	
	float4 dirLightIntensity[MaxLights];
	float4 pointLightPosition[MaxLights];
	float4 pointLightColor[MaxLights];
	float4 pointLightRange[MaxLights];
	float4 pointLightIntensity[MaxLights];
	float dirLightCount;
	float pointlightCount;
	float nearPlane;
	float farPlane;
	float2 viewport;
	float2 padding;
};
//=====================================

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
    
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mWorldViewProjection);
    output.uv 			= input.uv;
	
	return output;
}

//= PS() =================================================================================
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	// Misc
	float ambientLightIntensity = 0.1f;
	float3 finalColor			= float3(0,0,0);
	
	// Sample from G-Buffer
    float3 albedo               = ToLinear(texAlbedo.Sample(samplerAniso, input.uv)).rgb;
    float4 normalSample         = texNormal.Sample(samplerAniso, input.uv);
    float4 depthSample          = texDepth.Sample(samplerPoint, input.uv);
    float4 materialSample       = texMaterial.Sample(samplerPoint, input.uv);
		
	// Extract any values out of those samples
	float3 normal				= normalize(UnpackNormal(normalSample.rgb));	
	float depth					= depthSample.g;
	float shadowing 			= depthSample.b;
	float3 worldPos				= ReconstructPosition(depth, input.uv, mViewProjectionInverse);
	float roughness				= materialSample.r;
	float metallic				= materialSample.g;
	float specular				= materialSample.b;	
	float type					= materialSample.a;
	//return float4(shadowing,shadowing,shadowing,1.0f);
	// Calculate view direction and the reflection vector
	float3 viewDir				= normalize(cameraPosWS.xyz - worldPos.xyz); 
	float3 reflectionVector		= reflect(-viewDir, normal);
	
    // NOTE: The cubemap is already in linear space
	// Sample the skybox and the irradiance texture
	float mipIndex				= roughness * 8.0f;
    float3 envColor             = ToLinear(environmentTex.SampleLevel(samplerAniso, reflectionVector, mipIndex));
    float3 irradiance           = ToLinear(irradianceTex.Sample(samplerAniso, reflectionVector));
	
	if (type == 0.1f) // Texture mapping
	{
        finalColor = ToLinear(environmentTex.Sample(samplerAniso, -viewDir));
		finalColor = ACESFilm(finalColor); // ACES Filmic Tone Mapping (default tone mapping curve in Unreal Engine 4)
		finalColor = ToGamma(finalColor); // gamma correction
		float luma = dot(finalColor, float3(0.299f, 0.587f, 0.114f)); // compute luma as alpha for fxaa
	
		return float4(finalColor, luma);
	}

	// directional lights
	for (int i = 0; i < dirLightCount; i++)
	{
		float3 lightColor 		= dirLightColor[i];
		float lightIntensity	= dirLightIntensity[i] * shadowing;		
		float3 lightDir 		= normalize(-dirLightDirection[i]);
		float lightAttunation	= 0.99f;
			
		finalColor += BRDF(albedo, roughness, metallic, specular, normal, viewDir, lightDir, lightColor, lightAttunation, lightIntensity, ambientLightIntensity, envColor, irradiance);	
	}
		
	// point lights
	for (int i = 0; i < pointlightCount; i++)
	{
		float3 lightColor 		= pointLightColor[i];
		float3 lightPos 		= pointLightPosition[i];
		float radius 			= pointLightRange[i];
		float lightIntensity	= pointLightIntensity[i] * shadowing;		
		float3 lightDir			= normalize(lightPos - worldPos);
		float dist 				= length(worldPos - lightPos);
		float attunation 		= clamp(1.0f - dist/radius, 0.0f, 1.0f); attunation *= attunation;

		 // Calculate distance between light source and current fragment
        float dx = length(lightPos - worldPos);
		
		 // Do expensive lighting
		if (dx < radius)
			finalColor += BRDF(albedo, roughness, metallic, specular, normal, viewDir, lightDir, lightColor, attunation, lightIntensity, ambientLightIntensity, envColor, irradiance);				
	}
	
	finalColor = ACESFilm(finalColor); // ACES Filmic Tone Mapping (default tone mapping curve in Unreal Engine 4)
	finalColor = ToGamma(finalColor); // gamma correction
	float luma = dot(finalColor, float3(0.299f, 0.587f, 0.114f)); // compute luma as alpha for fxaa
	
	return float4(finalColor, luma);
}