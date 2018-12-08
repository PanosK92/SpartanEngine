// Based on "Temporal Antialiasing In Uncharted 4" by Ke XU

float4 ResolveTAA(float2 texCoord, Texture2D tex_history, Texture2D tex_current, Texture2D tex_velocity, Texture2D tex_depth, SamplerState sampler_bilinear)
{
	float2 velocity			= GetVelocity(texCoord, tex_current, tex_depth, sampler_bilinear) * g_texelSize;
    float2 texCoord_history	= texCoord - velocity;
	
	// For non-existing and lighting change cases, clamp out too different history colors 
	float4 samples[9];
	samples[0] = tex_current.Sample(sampler_bilinear, texCoord + float2(-1, -1) * g_texelSize);
	samples[1] = tex_current.Sample(sampler_bilinear, texCoord + float2(0, -1) * g_texelSize);
	samples[2] = tex_current.Sample(sampler_bilinear, texCoord + float2(1, -1) * g_texelSize);
	samples[3] = tex_current.Sample(sampler_bilinear, texCoord + float2(-1, 0) * g_texelSize);
	samples[4] = tex_current.Sample(sampler_bilinear, texCoord + float2(0, 0));
	samples[5] = tex_current.Sample(sampler_bilinear, texCoord + float2(1, 0) * g_texelSize);
	samples[6] = tex_current.Sample(sampler_bilinear, texCoord + float2(-1, 1) * g_texelSize);
	samples[7] = tex_current.Sample(sampler_bilinear, texCoord + float2(0, 1) * g_texelSize);
	samples[8] = tex_current.Sample(sampler_bilinear, texCoord + float2(1, 1) * g_texelSize);
	float4 sampleMin = samples[0];
	float4 sampleMax = samples[0];
	[unroll]
	for (uint i = 1; i < 9; ++i)
	{
		sampleMin = min(sampleMin, samples[i]);
		sampleMax = max(sampleMax, samples[i]);
	}

	// Get current color
	float4 color_current = samples[4];

	// Compute history color
	float4 color_history 	= tex_history.Sample(sampler_bilinear, texCoord_history);
	color_history 			= clamp(color_history, sampleMin, sampleMax);
	
	//= Compute blend factor =============================================================
	// Decrease when pixel motion gets subpixel 
	//float factor_subpixelMotion = sin(frac(length(velocity)) * PI);
	
	// Decrease when history is near clamp values
	//float factor_clampMin = 1.0f - saturate(abs(length(color_history - sampleMin)));
	//float factor_clampMax = 1.0f - saturate(abs(length(color_history - sampleMax)));
	
	// Total
	float alpha 		= 0.0f;
	float blendfactor 	= lerp(0.05f, 0.8f, 0);
	// Don't blend when out of screen
	blendfactor = any(texCoord_history - saturate(texCoord_history)) ? 1.0f : blendfactor;
	//====================================================================================
	
	return lerp(color_history, color_current, blendfactor);
}