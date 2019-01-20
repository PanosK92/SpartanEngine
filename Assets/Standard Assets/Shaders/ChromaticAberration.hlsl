float3 ChromaticAberration(float2 texCoord, float2 texelSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
	float2 shift 	= float2(2.5f, -2.5f);	// 	[-10, 10]
	float strength 	= 0.75f;  				//	[0, 1]
	
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