//= DEFINES ===================
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 29
//=============================

// = INCLUDES ======
#include "FXAA.hlsl"
#include "ACES.hlsl"
//==================

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
								[BLUR]
------------------------------------------------------------------------------*/
float4 Pass_BlurBox(float2 texCoord, float2 texelSize, int blurSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
	float4 result 	= float4(0.0f, 0.0f, 0.0f, 0.0f);
	float temp 		= float(-blurSize) * 0.5f + 0.5f;
	float2 hlim 	= float2(temp, temp);
	for (int i = 0; i < blurSize; ++i)
	{
		for (int j = 0; j < blurSize; ++j) 
		{
			float2 offset = (hlim + float2(float(i), float(j))) * texelSize;
			result += sourceTexture.Sample(bilinearSampler, texCoord + offset);
		}
	}
		
	result = result / float(blurSize * blurSize);
	   
	return result;
}

// Calculates the gaussian blur weight for a given distance and sigmas
float CalcGaussianWeight(int sampleDist, float sigma)
{
    float g = 1.0f / sqrt(2.0f * 3.14159f * sigma * sigma);
    return (g * exp(-(sampleDist * sampleDist) / (2.0f * sigma * sigma)));
}

// Performs a gaussian blur in one direction
float4 Pass_BlurGaussian(float2 uv, Texture2D sourceTexture, SamplerState bilinearSampler, float2 resolution, float2 direction, float sigma)
{
	// https://github.com/TheRealMJP/MSAAFilter/blob/master/MSAAFilter/PostProcessing.hlsl#L50
	float weightSum = 0.0f;
    float4 color = 0;
    for (int i = -7; i < 7; i++)
    {
        float weight = CalcGaussianWeight(i, sigma);
        weightSum += weight;
        float2 texCoord = uv;
        texCoord += (i / resolution) * direction;
        float4 sample = sourceTexture.Sample(bilinearSampler, texCoord);
        color += sample * weight;
    }

    color /= weightSum;

	return color;
}	

/*------------------------------------------------------------------------------
							[Chromatic Aberration]
------------------------------------------------------------------------------*/
float3 ChromaticAberrationPass(float2 texCoord, float2 texelSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
	float2 shift 	= float2(2.5f, -0.5f);	// 	[-10, 10]
	float strength 	= 0.5f;  				//	[0, 1]
	
	// supposedly, lens effect
	shift.x *= abs(texCoord.x * 2.0f - 1.0f);
	shift.y *= abs(texCoord.y * 2.0f - 1.0f);
	
	float3 color 		= float3(0.0f, 0.0f, 0.0f);
	float3 colorInput 	= sourceTexture.Sample(bilinearSampler, texCoord).rgb;
	
	// sample the color components
	color.r = sourceTexture.Sample(bilinearSampler, texCoord + (texelSize * shift)).r;
	color.g = colorInput.g;
	color.b = sourceTexture.Sample(bilinearSampler, texCoord - (texelSize * shift)).b;

	// adjust the strength of the effect
	return lerp(colorInput, color, strength);
}

/*------------------------------------------------------------------------------
							[FXAA]
------------------------------------------------------------------------------*/
float3 FXAA(float2 texCoord, float2 texelSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
	FxaaTex tex 						= { bilinearSampler, sourceTexture };	
    float2 fxaaQualityRcpFrame			= texelSize;
    float fxaaQualitySubpix				= 1.5f; 	// 0.75f	// The amount of sub-pixel aliasing removal.
    float fxaaQualityEdgeThreshold		= 0.125f; 	// 0.125f	// Edge detection threshold. The minimum amount of local contrast required to apply algorithm.
    float fxaaQualityEdgeThresholdMin	= 0.0833f; 	// 0.0833f	// Darkness threshold. Trims the algorithm from processing darks.
	
	return FxaaPixelShader
	( 
		texCoord, 0, tex, tex, tex,
		fxaaQualityRcpFrame, 0, 0, 0,
		fxaaQualitySubpix,
		fxaaQualityEdgeThreshold,
		fxaaQualityEdgeThresholdMin,
		0, 0, 0, 0
	).rgb;
}

/*------------------------------------------------------------------------------
								[SHARPENING]
------------------------------------------------------------------------------*/
float3 LumaSharpen(float2 texCoord, Texture2D sourceTexture, SamplerState bilinearSampler, float2 resolution)
{
	/*
		LumaSharpen 1.4.1
		original hlsl by Christian Cann Schuldt Jensen ~ CeeJay.dk
		port to glsl by Anon
		ported back to hlsl and modified by Panos karabelas
		It blurs the original pixel with the surrounding pixels and then subtracts this blur to sharpen the image.
		It does this in luma to avoid color artifacts and allows limiting the maximum sharpning to avoid or lessen halo artifacts.
		This is similar to using Unsharp Mask in Photoshop.
	*/
	
	// -- Sharpening --
	#define sharp_strength 1.0f   //[0.10 to 3.00] Strength of the sharpening
	#define sharp_clamp    0.35f  //[0.000 to 1.000] Limits maximum amount of sharpening a pixel recieves - Default is 0.035
	
	// -- Advanced sharpening settings --
	#define offset_bias 1.0f  //[0.0 to 6.0] Offset bias adjusts the radius of the sampling pattern.
	#define CoefLuma float3(0.2126f, 0.7152f, 0.0722f)      // BT.709 & sRBG luma coefficient (Monitors and HD Television)

	float4 colorInput = sourceTexture.Sample(bilinearSampler, texCoord);  	
	float3 ori = colorInput.rgb;

	// -- Combining the strength and luma multipliers --
	float3 sharp_strength_luma = (CoefLuma * sharp_strength); //I'll be combining even more multipliers with it later on
	
	// -- Gaussian filter --
	//   [ .25, .50, .25]     [ 1 , 2 , 1 ]
	//   [ .50,   1, .50]  =  [ 2 , 4 , 2 ]
 	//   [ .25, .50, .25]     [ 1 , 2 , 1 ]

	float px = 1.0f / resolution[0];
	float py = 1.0f / resolution[1];

	float3 blur_ori = sourceTexture.Sample(bilinearSampler, texCoord + float2(px,-py) * 0.5f * offset_bias).rgb; // South East
	blur_ori += sourceTexture.Sample(bilinearSampler, texCoord + float2(-px,-py) * 0.5f * offset_bias).rgb;  // South West
	blur_ori += sourceTexture.Sample(bilinearSampler, texCoord + float2(px,py) * 0.5f * offset_bias).rgb; // North East
	blur_ori += sourceTexture.Sample(bilinearSampler, texCoord + float2(-px,py) * 0.5f * offset_bias).rgb; // North West
	blur_ori *= 0.25f;  // ( /= 4) Divide by the number of texture fetches

	// -- Calculate the sharpening --
	float3 sharp = ori - blur_ori;  //Subtracting the blurred image from the original image

	// -- Adjust strength of the sharpening and clamp it--
	float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5f / sharp_clamp), 0.5f); //Roll part of the clamp into the dot

	float sharp_luma = clamp((dot(float4(sharp, 1.0f), sharp_strength_luma_clamp)), 0.0f, 1.0f); //Calculate the luma, adjust the strength, scale up and clamp
	sharp_luma = (sharp_clamp * 2.0f) * sharp_luma - sharp_clamp; //scale down

	// -- Combining the values to get the final sharpened pixel	--
	colorInput.rgb = colorInput.rgb + sharp_luma;    // Add the sharpening to the input color.
	return clamp(colorInput, 0.0f, 1.0f).rgb;
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