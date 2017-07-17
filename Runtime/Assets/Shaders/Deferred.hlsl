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

#define MaxLights 128
cbuffer MiscBuffer : register(b1)
{
    float4 cameraPosWS;
    float4 dirLightDirection;
    float4 dirLightColor;
    float4 dirLightIntensity;
    float4 pointLightPosition[MaxLights];
    float4 pointLightColor[MaxLights];
    float4 pointLightRange[MaxLights];
    float4 pointLightIntensity[MaxLights];
    float pointlightCount;
    float nearPlane;
    float farPlane;
	float softShadows;
    float2 resolution;
    float2 padding;
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
	float shadowing = softShadows ? texShadows.Sample(samplerAniso, input.uv).a : normalSample.a;	
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
    float3 envColor = ToLinear(environmentTex.SampleLevel(samplerAniso, reflectionVector, mipIndex));
    float3 irradiance = ToLinear(environmentTex.SampleLevel(samplerAniso, reflectionVector, 10.0f));
	
    if (type == 0.1f) // Texture mapping
    {
        finalColor = ToLinear(environmentTex.Sample(samplerAniso, -viewDir));
        finalColor = ACESFilm(finalColor);
        finalColor = ToGamma(finalColor);
        finalColor *= clamp(dirLightIntensity[0], 0.1f, 1.0f); // some totally fake day/night effect	
        float luma = dot(finalColor, float3(0.299f, 0.587f, 0.114f)); // compute luma as alpha for fxaa

        return float4(finalColor, luma);
    }
	
    float ambientLightIntensity = 0.3f;

	//= DIRECTIONAL LIGHT ========================================================================================================================================
	float3 lightColor = dirLightColor;
	float lightIntensity = dirLightIntensity;
	float3 lightDir = normalize(-dirLightDirection);

	ambientLightIntensity = clamp(lightIntensity * 0.3f, 0.0f, 1.0f);
	lightIntensity *= shadowing;
	lightIntensity += emission;
	envColor *= clamp(lightIntensity, 0.0f, 1.0f);
	irradiance *= clamp(lightIntensity, 0.0f, 1.0f);
			
	finalColor += BRDF(albedo, roughness, metallic, specular, normal, viewDir, lightDir, lightColor, lightIntensity, ambientLightIntensity, envColor, irradiance);
	//============================================================================================================================================================
	
	//= POINT LIGHTS =============================================================================================================================================
    for (int i = 0; i < pointlightCount; i++)
    {
        float3 lightColor = pointLightColor[i];
        float3 lightPos = pointLightPosition[i];
        float radius = pointLightRange[i];
        float lightIntensity = pointLightIntensity[i];

        float3 lightDir = normalize(lightPos - worldPos);
        float dist = length(worldPos - lightPos);
        float attunation = clamp(1.0f - dist / radius, 0.0f, 1.0f); attunation *= attunation;
		lightIntensity *= attunation;
		lightIntensity += emission;

		// Do expensive lighting
        if (dist < radius)
            finalColor += BRDF(albedo, roughness, metallic, specular, normal, viewDir, lightDir, lightColor, lightIntensity, 0.0f, envColor, irradiance);
    }
	//============================================================================================================================================================
	
    finalColor = ACESFilm(finalColor); // ACES Filmic Tone Mapping (default tone mapping curve in Unreal Engine 4)
    finalColor = ToGamma(finalColor); // gamma correction
    float luma = dot(worldPos, float3(0.299f, 0.587f, 0.114f)); // compute luma as alpha for fxaa

    return float4(finalColor, luma);
}