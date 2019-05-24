//= INCLUDES ===========
#include "Velocity.hlsl"
//======================

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

float4 clip_aabb(float3 aabb_min, float3 aabb_max, float4 p, float4 q)
{
	float4 r = q - p;
	float3 rmax = aabb_max - p.xyz;
	float3 rmin = aabb_min - p.xyz;
	
	if (r.x > rmax.x + EPSILON)
		r *= (rmax.x / r.x);
	if (r.y > rmax.y + EPSILON)
		r *= (rmax.y / r.y);
	if (r.z > rmax.z + EPSILON)
		r *= (rmax.z / r.z);
	
	if (r.x < rmin.x - EPSILON)
		r *= (rmin.x / r.x);
	if (r.y < rmin.y - EPSILON)
		r *= (rmin.y / r.y);
	if (r.z < rmin.z - EPSILON)
		r *= (rmin.z / r.z);
	
	return p + r;
}

float4 ResolveTAA(float2 texCoord, Texture2D tex_history, Texture2D tex_current, Texture2D tex_velocity, Texture2D tex_depth, SamplerState sampler_bilinear)
{
	// Reproject
	float depth 			= 1.0f;
	float2 velocity			= GetVelocity_Dilate_Depth3X3(texCoord, tex_velocity, tex_depth, sampler_bilinear, depth);
	float2 texCoord_history = texCoord - velocity;
	
	// Get current and history colors
	float4 color_current 	= tex_current.Sample(sampler_bilinear, texCoord);
	float4 color_history 	= tex_history.Sample(sampler_bilinear, texCoord_history);

	//= Sample neighbourhood ==============================================================================
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
	
	float4 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
	float4 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
	float4 color_avg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0f;
	//=====================================================================================================
	
	// Shrink chroma min-max - Prevents jitter in high contrast areas
	//float2 chroma_extent = 0.25f * 0.5f * (color_max.r - color_min.r);
	//float2 chroma_center = color_history.gb;
	//color_min.yz = chroma_center - chroma_extent;
	//color_max.yz = chroma_center + chroma_extent;
	//color_avg.yz = chroma_center;
	
	// Clip to neighbourhood of current sample
	color_history = clip_aabb(color_min.xyz, color_max.xyz, clamp(color_avg, color_min, color_max), color_history);
	
	// Decrease blend factor when motion gets sub-pixel
	float factor_subpixel = sin(frac(length(velocity * g_resolution)) * PI); 

	// Compute blend factor (but simple use max blend if the re-projected texcoord is out of screen)
	float blendfactor = is_saturated(texCoord_history) ? lerp(g_blendMin, g_blendMax, factor_subpixel) : g_blendMax;
	
	// Tonemap
	color_history = Reinhard(color_history);
	color_current = Reinhard(color_history);
	
	// Resolve
	float4 resolved = lerp(color_history, color_current, blendfactor);
	
	// Inverse tonemap
	return ReinhardInverse(resolved);
}