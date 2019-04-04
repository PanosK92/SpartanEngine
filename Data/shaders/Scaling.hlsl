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

	float4 s1 = sourceTexture.Sample(bilinearSampler, uv + d.xy);
	float4 s2 = sourceTexture.Sample(bilinearSampler, uv + d.zy);
	float4 s3 = sourceTexture.Sample(bilinearSampler, uv + d.xw);
	float4 s4 = sourceTexture.Sample(bilinearSampler, uv + d.zw);
	
	// Karis's luma weighted average
	float s1w = 1 / (luminance(s1) + 1);
	float s2w = 1 / (luminance(s2) + 1);
	float s3w = 1 / (luminance(s3) + 1);
	float s4w = 1 / (luminance(s4) + 1);
	float one_div_wsum = 1.0 / (s1w + s2w + s3w + s4w);
	
	return (s1 * s1w + s2 * s2w + s3 * s3w + s4 * s4w) * one_div_wsum;
}

// Better, temporally stable box filtering
// [Jimenez14] http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
// . . . . . . .
// . A . B . C .
// . . D . E . .
// . F . G . H .
// . . I . J . .
// . K . L . M .
// . . . . . . .
float4 Downsample_Box13Tap(float2 uv, float2 texelSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
    float4 A = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2(-1.0f, -1.0f));
    float4 B = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2( 0.0f, -1.0f));
    float4 C = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2( 1.0f, -1.0f));
    float4 D = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2(-0.5f, -0.5f));
    float4 E = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2( 0.5f, -0.5f));
    float4 F = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2(-1.0f,  0.0f));
    float4 G = sourceTexture.Sample(bilinearSampler, uv);
    float4 H = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2( 1.0f,  0.0f));
    float4 I = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2(-0.5f,  0.5f));
    float4 J = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2( 0.5f,  0.5f));
    float4 K = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2(-1.0f,  1.0f));
    float4 L = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2( 0.0f,  1.0f));
    float4 M = sourceTexture.Sample(bilinearSampler, uv + texelSize * float2( 1.0f,  1.0f));

    float2 div = (1.0f / 4.0f) * float2(0.5f, 0.125f);

    float4 o = (D + E + I + J) * div.x;
    o += (A + B + G + F) * div.y;
    o += (B + C + H + G) * div.y;
    o += (F + G + L + K) * div.y;
    o += (G + H + M + L) * div.y;

    return o;
}

// Upsample with a 4x4 box filter
float4 Upsample_Box(float2 uv, float2 texelSize, Texture2D sourceTexture, SamplerState bilinearSampler, float4 sampleScale)
{
	float4 d = texelSize.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f) * (sampleScale * 0.5f);
	
	float4 s;
	s  = sourceTexture.Sample(bilinearSampler, uv + d.xy);
	s += sourceTexture.Sample(bilinearSampler, uv + d.zy);
	s += sourceTexture.Sample(bilinearSampler, uv + d.xw);
	s += sourceTexture.Sample(bilinearSampler, uv + d.zw);
	
	return s * (1.0f / 4.0f);
}