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

//= INCLUDES ===========
#include "Velocity.hlsl"
//======================

static const float g_blendMin = 0.01f;
static const float g_blendMax = 0.5f;

float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
	float3 r = q - p;
	float3 rmax = aabb_max - p;
	float3 rmin = aabb_min - p;
	
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
	float2 velocity			= GetVelocity_Dilate_Min(texCoord, tex_velocity, tex_depth, sampler_bilinear);
	float2 texCoord_history = texCoord - velocity;
	
	// Get current and history colors
	float3 color_current 	= Reinhard(tex_current.Sample(sampler_bilinear, texCoord).rgb);
	float3 color_history 	= Reinhard(tex_history.Sample(sampler_bilinear, texCoord_history).rgb);

	//= Sample neighbourhood ==============================================================================
	float2 du = float2(g_texel_size.x, 0.0f);
	float2 dv = float2(0.0f, g_texel_size.y);

	float3 ctl = Reinhard(tex_current.Sample(sampler_bilinear, texCoord - dv - du).rgb);
	float3 ctc = Reinhard(tex_current.Sample(sampler_bilinear, texCoord - dv).rgb);
	float3 ctr = Reinhard(tex_current.Sample(sampler_bilinear, texCoord - dv + du).rgb);
	float3 cml = Reinhard(tex_current.Sample(sampler_bilinear, texCoord - du).rgb);
	float3 cmc = Reinhard(tex_current.Sample(sampler_bilinear, texCoord).rgb);
	float3 cmr = Reinhard(tex_current.Sample(sampler_bilinear, texCoord + du).rgb);
	float3 cbl =Reinhard( tex_current.Sample(sampler_bilinear, texCoord + dv - du).rgb);
	float3 cbc = Reinhard(tex_current.Sample(sampler_bilinear, texCoord + dv).rgb);
	float3 cbr = Reinhard(tex_current.Sample(sampler_bilinear, texCoord + dv + du).rgb);
	
	float3 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
	float3 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
	float3 color_avg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0f;
	//=====================================================================================================
	
	// Clip to neighbourhood of current sample
	color_history = clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history);

	// Decrease blend factor when motion gets sub-pixel
	float factor_subpixel = saturate(length(velocity * g_resolution));
	
	// Compute blend factor (but simply use max blend if the re-projected texcoord is out of screen)
	float blendfactor = is_saturated(texCoord_history) ? lerp(g_blendMin, g_blendMax, factor_subpixel) : 1.0f;
	
	// Resolve
	float3 resolved = lerp(color_history, color_current, blendfactor);
	
	// Inverse tonemap
	return float4(ReinhardInverse(resolved), 1.0f);
}