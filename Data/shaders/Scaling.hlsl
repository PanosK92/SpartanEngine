/*
Copyright(c) 2016-2019 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// Downsample with a 4x4 box filter
float4 Downsample_Box(float2 uv, float2 texel_size, Texture2D _texture)
{
	float4 uv_delta = texel_size.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f);
	
	float4 downsampled =
	_texture.Sample(sampler_bilinear_clamp, uv + uv_delta.xy) +
	_texture.Sample(sampler_bilinear_clamp, uv + uv_delta.zy) +
	_texture.Sample(sampler_bilinear_clamp, uv + uv_delta.xw) +
	_texture.Sample(sampler_bilinear_clamp, uv + uv_delta.zw);

	return downsampled / 4.0f;
}

// Downsample with a 4x4 box filter + anti-flicker filter
float4 Downsample_BoxAntiFlicker(float2 uv, float2 texelSize, Texture2D sourceTexture)
{
	float4 d = texelSize.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f);

	float4 s1 = sourceTexture.Sample(sampler_bilinear_clamp, uv + d.xy);
	float4 s2 = sourceTexture.Sample(sampler_bilinear_clamp, uv + d.zy);
	float4 s3 = sourceTexture.Sample(sampler_bilinear_clamp, uv + d.xw);
	float4 s4 = sourceTexture.Sample(sampler_bilinear_clamp, uv + d.zw);
	
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
float4 Downsample_Box13Tap(float2 uv, float2 texelSize, Texture2D sourceTexture)
{
    float4 A = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2(-1.0f, -1.0f));
    float4 B = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2( 0.0f, -1.0f));
    float4 C = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2( 1.0f, -1.0f));
    float4 D = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2(-0.5f, -0.5f));
    float4 E = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2( 0.5f, -0.5f));
    float4 F = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2(-1.0f,  0.0f));
    float4 G = sourceTexture.Sample(sampler_point_clamp, uv);
    float4 H = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2( 1.0f,  0.0f));
    float4 I = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2(-0.5f,  0.5f));
    float4 J = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2( 0.5f,  0.5f));
    float4 K = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2(-1.0f,  1.0f));
    float4 L = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2( 0.0f,  1.0f));
    float4 M = sourceTexture.Sample(sampler_bilinear_clamp, uv + texelSize * float2( 1.0f,  1.0f));

    float2 div = (1.0f / 4.0f) * float2(0.5f, 0.125f);

    float4 o = (D + E + I + J) * div.x;
    o += (A + B + G + F) * div.y;
    o += (B + C + H + G) * div.y;
    o += (F + G + L + K) * div.y;
    o += (G + H + M + L) * div.y;

    return o;
}

// Upsample with a 4x4 box filter
float4 Upsample_Box(float2 uv, float2 texel_size, Texture2D _texture)
{
	float4 uv_delta = texel_size.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f) * 0.5f;
	
	float4 upsampled =
	_texture.Sample(sampler_bilinear_clamp, uv + uv_delta.xy) +
	_texture.Sample(sampler_bilinear_clamp, uv + uv_delta.zy) +
	_texture.Sample(sampler_bilinear_clamp, uv + uv_delta.xw) +
	_texture.Sample(sampler_bilinear_clamp, uv + uv_delta.zw);

	return upsampled / 4.0f;
}