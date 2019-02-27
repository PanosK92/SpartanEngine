// Downsample with a 4x4 box filter
float4 Downsample_Box(float2 uv, float2 texelSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
	float4 d = texelSize.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f);
	
	float4 s;
	s  = sourceTexture.Sample(bilinearSampler, uv + d.xy);
	s += sourceTexture.Sample(bilinearSampler, uv + d.zy);
	s += sourceTexture.Sample(bilinearSampler, uv + d.xw);
	s += sourceTexture.Sample(bilinearSampler, uv + d.zw);
	
	return s * (1.0f / 4.0f);
}

// Downsample with a 4x4 box filter + anti-flicker filter
float4 Downsample_BoxAntiFlicker(float2 uv, float2 texelSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
	float4 d = texelSize.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f);

	float4 s1 = degamma(sourceTexture.Sample(bilinearSampler, uv + d.xy));
	float4 s2 = degamma(sourceTexture.Sample(bilinearSampler, uv + d.zy));
	float4 s3 = degamma(sourceTexture.Sample(bilinearSampler, uv + d.xw));
	float4 s4 = degamma(sourceTexture.Sample(bilinearSampler, uv + d.zw));
	
	// Karis's luma weighted average
	float s1w = 1 / (luminance(s1) + 1);
	float s2w = 1 / (luminance(s2) + 1);
	float s3w = 1 / (luminance(s3) + 1);
	float s4w = 1 / (luminance(s4) + 1);
	float one_div_wsum = 1.0 / (s1w + s2w + s3w + s4w);
	
	return gamma((s1 * s1w + s2 * s2w + s3 * s3w + s4 * s4w) * one_div_wsum);
}