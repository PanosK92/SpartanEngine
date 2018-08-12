//= TEXTURES ==============================
Texture2D texAlbedo 		: register(t0);
Texture2D texNormal 		: register(t1);
Texture2D texDepth 			: register(t2);
Texture2D texSpecular 		: register(t3);
Texture2D texShadowing 		: register(t4);
Texture2D texLastFrame 		: register(t5);
TextureCube environmentTex 	: register(t6);
//=========================================

//= SAMPLERS ==============================
SamplerState samplerLinear 	: register(s0);
//=========================================

//= CONSTANT BUFFERS ===================
cbuffer MatrixBuffer : register(b0)
{
	matrix mWorldViewProjection;
	matrix mProjection;
	matrix mProjectionInverse;
	matrix mViewProjection;
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
	
    float2 resolution;
    float2 padding;
};
//=====================================

// = INCLUDES ========
#include "Common.hlsl"
#include "SSRR.hlsl"
#include "BRDF.hlsl"
//====================

//= INPUT LAYOUT ======================
struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};
//=====================================

//= VS() ==================================================================================
PixelInputType DirectusVertexShader(Vertex_PosUv input)
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
	float2 texCoord = input.uv;
    float3 finalColor = float3(0, 0, 0);
	
	// Sample from G-Buffer
    float4 albedo 			= ToLinear(texAlbedo.Sample(samplerLinear, texCoord));
    float4 normalSample 	= texNormal.Sample(samplerLinear, texCoord);
	float4 specularSample	= texSpecular.Sample(samplerLinear, texCoord);
    float4 depthSample 		= texDepth.Sample(samplerLinear, texCoord);
    	
	// Create material
	Material material;
	material.albedo 	= albedo.rgb;
    material.roughness 	= specularSample.r;
    material.metallic 	= specularSample.g;
	material.emission	= specularSample.b * 2.0f;
		
	// Extract any values out of those samples
    float3 normal 	= normalize(UnpackNormal(normalSample.rgb));
	float depth 	= depthSample.g;
	float3 worldPos = ReconstructPositionWorld(depth, mViewProjectionInverse, texCoord);
    
	// Shadows + SSAO
	float2 shadowing 	= texShadowing.Sample(samplerLinear, texCoord).rg;
	float shadow 		= shadowing.r;
	float ssao 			= shadowing.g;
	
	// Calculate view direction
    float3 viewDir = normalize(cameraPosWS.xyz - worldPos.xyz);
   	
	// some totally fake dynamic skybox
	float ambientLight = clamp(dirLightIntensity, 0.01f, 1.0f); 
	
    if (specularSample.a == 1.0f) // Render technique
    {
        finalColor = ToLinear(environmentTex.Sample(samplerLinear, -viewDir)).rgb;
        finalColor *= ambientLight; // some totally fake day/night effect	
        return float4(finalColor, 1.0f);
    }
	
	// Ambient terms
	ambientLight *= ssao;
	ambientLight += material.emission * 10.0f;
	 
	//= DIRECTIONAL LIGHT ========================================================================================================================================
	Light directionalLight;

	// Compute
	directionalLight.color 		= dirLightColor.rgb;
	directionalLight.intensity 	= dirLightIntensity.r;
	directionalLight.direction	= normalize(-dirLightDirection).xyz;
	directionalLight.intensity 	*= shadow;
	
	finalColor += ImageBasedLighting(material, directionalLight.direction, normal, viewDir, samplerLinear) * ambientLight;
	
	// Compute illumination
	finalColor += BRDF(material, directionalLight, normal, viewDir);
	//============================================================================================================================================================
	
	//= POINT LIGHTS =============================================================================================================================================
	Light pointLight;
    for (int i = 0; i < pointlightCount; i++)
    {
		// Get light data
        pointLight.color 		= pointLightColor[i].rgb;
        float3 position 		= pointLightPosition[i].xyz;
		pointLight.intensity 	= pointLightIntenRange[i].x;
        float range 			= pointLightIntenRange[i].y;
        
		// Compute light
        pointLight.direction 	= normalize(position - worldPos);
        float dist 				= length(worldPos - position);
        float attunation 		= clamp(1.0f - dist / range, 0.0f, 1.0f); attunation *= attunation;
		pointLight.intensity 	*= attunation;

		// Compute illumination
        if (dist < range)
		{
            finalColor += BRDF(material, pointLight, normal, viewDir);
		}
    }
	//============================================================================================================================================================
	
	//= SPOT LIGHTS ==============================================================================================================================================
	Light spotLight;
    for (int j = 0; j < spotlightCount; j++)
    {
		// Get light data
        spotLight.color 	= spotLightColor[j].rgb;
        float3 position 	= spotLightPosition[j].xyz;
		spotLight.intensity = spotLightIntenRangeAngle[j].x;
		spotLight.direction = normalize(-spotLightDirection[j].xyz);
		float range 		= spotLightIntenRangeAngle[j].y;
        float cutoffAngle 	= 1.0f - spotLightIntenRangeAngle[j].z;
        
		// Compute light
		float3 direction 	= normalize(position - worldPos);
		float dist 			= length(worldPos - position);	
		float theta 		= dot(direction, spotLight.direction);
		float epsilon   	= cutoffAngle - cutoffAngle * 0.9f;
		float attunation 	= clamp((theta - cutoffAngle) / epsilon, 0.0f, 1.0f);  // attunate when approaching the outer cone
		attunation 			*= clamp(1.0f - dist / range, 0.0f, 1.0f); attunation *= attunation; // attunate with distance as well
		spotLight.intensity *= attunation;

		// Compute illumination
		if(theta > cutoffAngle)
		{
            finalColor += BRDF(material, spotLight, normal, viewDir);
		}
    }
	//============================================================================================================================================================

	float luma = dot(finalColor, float3(0.299f, 0.587f, 0.114f));
    return float4(finalColor, luma);
}