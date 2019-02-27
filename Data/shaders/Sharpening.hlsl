float3 LumaSharpen(float2 texCoord, Texture2D sourceTexture, SamplerState bilinearSampler, float2 resolution, float sharp_strength, float sharp_clamp)
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
	//#define sharp_strength 1.0f   //[0.10 to 3.00] Strength of the sharpening
	//#define sharp_clamp    0.35f  //[0.000 to 1.000] Limits maximum amount of sharpening a pixel receives - Default is 0.035
	
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