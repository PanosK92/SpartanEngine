#if COMPILE_PS == 1
/*------------------------------------------------------------------------------
							[G-BUFFER]
------------------------------------------------------------------------------*/
Texture2D albedoTexture			: register(t0);
Texture2D normalTexture 		: register(t1);
Texture2D depthTexture			: register(t2);
Texture2D materialTexture		: register(t3);

/*------------------------------------------------------------------------------
						[VARIOUS TEXTURES]
------------------------------------------------------------------------------*/
Texture2D noiseTexture 			: register(t4);
TextureCube environmentMap 		: register(t5);
TextureCube irradianceMap		: register(t6);

/*------------------------------------------------------------------------------
							[SAMPLERS]
------------------------------------------------------------------------------*/
SamplerState samplerPoint 		: register(s0);
SamplerState samplerAniso	 	: register(s1);

#define MaxLights 300

#include "Helper.hlsl"
#include "PBR.hlsl"
#include "ToneMapping.hlsl"
#include "Normal.hlsl"

#endif

/*------------------------------------------------------------------------------
							[CONSTANT BUFFERS]
------------------------------------------------------------------------------*/
cbuffer MiscBuffer : register(b0)
{
	matrix mWorldViewProjection;
	matrix mViewProjectionInverse;
	float4 cameraPosWS;
	float nearPlane;
	float farPlane;
	float2 padding3;
};

#if COMPILE_PS == 1
cbuffer DirectionalLightBuffer : register (b1)
{
	matrix dirLightViewProjection[MaxLights];
	float4 dirLightDirection[MaxLights];
	float4 dirLightColor[MaxLights];	
	float4 dirLightIntensity[MaxLights];
	float dirLightCount;
	float3 padding;
};

cbuffer PointLightBuffer : register(b2)
{
	float4 pointLightPosition[MaxLights];
	float4 pointLightColor[MaxLights];
	float4 pointLightRange[MaxLights];
	float4 pointLightIntensity[MaxLights];
	float pointlightCount;
	float3 padding2;
};
#endif

//= INPUT LAYOUT ================
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

//= VS() =============================================================
#if COMPILE_VS == 1
PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output;
    
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mWorldViewProjection);
    output.uv 			= input.uv;
	
	return output;
}
#endif

//= PS() =================================================================================
#if COMPILE_PS == 1
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	// Misc
	float ambientLightIntensity = 0.2f;
	float3 finalColor			= float3(0,0,0);
	
	// Sample from G-Buffer
	float4 albedo				= ToLinear(albedoTexture.Sample(samplerAniso, input.uv));	
	float4 normalSample			= normalTexture.Sample(samplerAniso, input.uv); 
	float4 depthSample 			= depthTexture.Sample(samplerPoint, input.uv);
	float4 materialSample		= materialTexture.Sample(samplerPoint, input.uv);
	
	// Extract any values out of those samples
	float3 normal				= normalize(UnpackNormal(normalSample.rgb));	
	float shadowing				= normalSample.a; // alpha channel contains shadows
	shadowing					= clamp(shadowing, ambientLightIntensity, 1.0f);
	float depthLinear			= depthSample.g;
	float3 worldPos				= ReconstructPosition(depthLinear, input.uv, mViewProjectionInverse);
	float roughness				= materialSample.r;
	float metallic				= materialSample.g;
	float reflectivity			= materialSample.b;	
	float renderMode			= materialSample.a;
	
	// Calculate view direction and the reflection vector
	float3 viewDir				= normalize(cameraPosWS.xyz - worldPos.xyz); 
	float3 reflectionVector		= reflect(-viewDir, normal);
	
    // NOTE: The cubemap is already in linear space
	// Sample the skybox and the irradiance texture
	float mipIndex				= roughness * roughness * 8.0f;
	float3 envColor				= environmentMap.SampleLevel(samplerAniso, reflectionVector, mipIndex);
	float3 irradiance			= irradianceMap.Sample(samplerAniso, reflectionVector);	
	
	if (renderMode == 0.0f) // Texture mapping
	{
		finalColor = environmentMap.Sample(samplerAniso, -viewDir);
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
			
		finalColor += BRDF(albedo, roughness, metallic, reflectivity, normal, viewDir, lightDir, lightColor, lightAttunation, lightIntensity, ambientLightIntensity, envColor, irradiance);	
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
			finalColor += BRDF(albedo, roughness, metallic, reflectivity, normal, viewDir, lightDir, lightColor, attunation, lightIntensity, ambientLightIntensity, envColor, irradiance);				
	}
	
	finalColor = ACESFilm(finalColor); // ACES Filmic Tone Mapping (default tone mapping curve in Unreal Engine 4)
	finalColor = ToGamma(finalColor); // gamma correction
	float luma = dot(finalColor, float3(0.299f, 0.587f, 0.114f)); // compute luma as alpha for fxaa
	
	return float4(finalColor, luma);
}
#endif // COMPILE_PS