/*
Copyright(c) 2016-2020 Panos Karabelas

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

static const float g_blendMin = 0.0f;
static const float g_blendMax = 0.5f;

// From "Temporal Reprojection Anti-Aliasing"
// https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
#if USE_OPTIMIZATIONS
	// note: only clips towards aabb center (but fast!)
	float3 p_clip = 0.5f * (aabb_max + aabb_min);
	float3 e_clip = 0.5f * (aabb_max - aabb_min) + EPSILON;

	float3 v_clip = q - p_clip;
	float3 v_unit = v_clip.xyz / e_clip;
	float3 a_unit = abs(v_unit);
	float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

	if (ma_unit > 1.0)
		return p_clip + v_clip / ma_unit;
	else
		return q;// point inside aabb
#else
	float3 r = q - p;
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
#endif
}

float4 ResolveTAA(float2 uv, Texture2D tex_history, Texture2D tex_current, Texture2D tex_velocity, Texture2D tex_depth)
{
	//= Sample neighbourhood ==============================================================================
	float2 du = float2(g_texel_size.x, 0.0f);
	float2 dv = float2(0.0f, g_texel_size.y);

	float3 ctl = Reinhard(tex_current.Sample(sampler_point_clamp, uv - dv - du).rgb);
	float3 ctc = Reinhard(tex_current.Sample(sampler_point_clamp, uv - dv).rgb);
	float3 ctr = Reinhard(tex_current.Sample(sampler_point_clamp, uv - dv + du).rgb);
	float3 cml = Reinhard(tex_current.Sample(sampler_point_clamp, uv - du).rgb);
	float3 cmc = Reinhard(tex_current.Sample(sampler_point_clamp, uv).rgb);
	float3 cmr = Reinhard(tex_current.Sample(sampler_point_clamp, uv + du).rgb);
	float3 cbl = Reinhard(tex_current.Sample(sampler_point_clamp, uv + dv - du).rgb);
	float3 cbc = Reinhard(tex_current.Sample(sampler_point_clamp, uv + dv).rgb);
	float3 cbr = Reinhard(tex_current.Sample(sampler_point_clamp, uv + dv + du).rgb);

	float3 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
	float3 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
	float3 color_avg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0f;
	//=====================================================================================================
	
    // Get history and current colors
	float2 velocity			= GetVelocity_Dilate_Min(uv, tex_velocity, tex_depth);
	float2 uv_reprojected   = uv - velocity;
	float3 color_history    = Reinhard(tex_history.Sample(sampler_bilinear_clamp, uv_reprojected).rgb);
	float3 color_current    = cmc;

	// Clip history to the neighbourhood of the current sample
	color_history = clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history);

	// Decrease blend factor when motion gets sub-pixel
	float factor_subpixel = max(0.01f, saturate(length(velocity * g_resolution)));
    
    // Decrease blend factor when contrast is high
    float lum0              = luminance(color_current);
	float lum1              = luminance(color_history);
    float factor_contrast   = 1.0f - (abs(lum0 - lum1) / max(lum0, max(lum1, 0.7f)));
	factor_contrast         = factor_contrast * factor_contrast;

    // Compute blend factor
    float blend_factor = factor_subpixel * factor_contrast;
    
	// Use max blend if the re-projected uv is out of screen
	blend_factor = is_saturated(uv_reprojected) ? blend_factor : 1.0f;
	
	// Resolve
	float3 resolved = lerp(color_history, color_current, blend_factor);
	
	// Inverse tonemap
	return float4(ReinhardInverse(resolved), 1.0f);
}