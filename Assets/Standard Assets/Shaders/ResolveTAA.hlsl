// Based on "Temporal Antialiasing In Uncharted 4" by Ke XU - Naughty Dog.

#include "Velocity.hlsl"

static const float g_blendMin = 0.05f;
static const float g_blendMax = 0.8f;

// TODO: This can be improved further by weighting the blend factor from unbiased luminance diff

// Resolving works better with tonemapped input
float4 Reinhard(float4 color)
{
	return color / (1 + color);
}

// Inverse tonemapping before returning
float4 ReinhardInverse(float4 color)
{
	return -color / (color - 1);
}

float4 ResolveTAA(float2 texCoord, Texture2D tex_history, Texture2D tex_current, Texture2D tex_velocity, Texture2D tex_depth, SamplerState sampler_bilinear)
{
	// Reproject
	float depth 			= 1.0f;
	float2 velocity			= GetVelocity_Dilate_Depth3X3(texCoord, tex_velocity, tex_depth, sampler_bilinear, depth);
	float2 texCoord_history = texCoord - velocity;
	
	// Get current and history colors
	float4 color_current 	= Reinhard(tex_current.Sample(sampler_bilinear, texCoord));
	float4 color_history 	= Reinhard(tex_history.Sample(sampler_bilinear, texCoord_history));

	//= Clamp out too different history colors (for non-existing and lighting change cases) =========================
	float2 du = float2(g_texelSize.x, 0.0f);
	float2 dv = float2(0.0f, g_texelSize.y);

	float4 ctl = tex_current.Sample(sampler_bilinear, texCoord - dv - du);
	float4 ctc = tex_current.Sample(sampler_bilinear, texCoord - dv);
	float4 ctr = tex_current.Sample(sampler_bilinear, texCoord - dv + du);
	float4 cml = tex_current.Sample(sampler_bilinear, texCoord - du);
	float4 cmc = tex_current.Sample(sampler_bilinear, texCoord);
	float4 cmr = tex_current.Sample(sampler_bilinear, texCoord + du);
	float4 cbl = tex_current.Sample(sampler_bilinear, texCoord + dv - du);
	float4 cbc = tex_current.Sample(sampler_bilinear, texCoord + dv);
	float4 cbr = tex_current.Sample(sampler_bilinear, texCoord + dv + du);
	
	float4 color_min = Reinhard(min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr)))))))));
	float4 color_max = Reinhard(max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr)))))))));
	
	color_history = clamp(color_history, color_min, color_max);
	//===============================================================================================================
	
	//= Compute blend factor ==========================================================================
	float factor_subpixel	= abs(sin(frac(length(velocity)) * PI)); // Decrease if motion is sub-pixel
	float blendfactor 		= lerp(g_blendMin, g_blendMax, saturate(factor_subpixel));
	//=================================================================================================
	
	float4 resolved_tonemapped 	= lerp(color_history, color_current, blendfactor);
	float4 resolved 			= ReinhardInverse(resolved_tonemapped);
	
	return resolved;
}