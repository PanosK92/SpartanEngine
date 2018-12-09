// Based on "Temporal Antialiasing In Uncharted 4" by Ke XU - Naughty Dog.
// Clamping based on Playdead's INSIDE approach.

static const float g_blendMin = 0.0f;
static const float g_blendMax = 1.0f;

float4 ResolveTAA(float2 texCoord, Texture2D tex_history, Texture2D tex_current, Texture2D tex_velocity, Texture2D tex_depth, SamplerState sampler_bilinear)
{
	// Get velocity for nearest depth (and the depth)
	float depth 	= 1.0f;
	float2 velocity	= GetVelocity_Dilate_Depth(texCoord, tex_current, tex_depth, sampler_bilinear, depth) * g_texelSize;	
	
	float4 color_current 	= tex_current.Sample(sampler_bilinear, texCoord);
	float4 color_history 	= tex_history.Sample(sampler_bilinear, texCoord - velocity);

	//= Clamp out too different history colors - For non-existing and lighting change cases ==========
	const float _SubpixelThreshold 		= 0.5f;
	const float _GatherBase 			= 0.5f;
	const float _GatherSubpixelMotion 	= 0.5f;

	float2 texel_vel 		= velocity / g_texelSize;
	float texel_vel_mag 	= length(texel_vel) * depth;
	float k_subpixel_motion = saturate(_SubpixelThreshold / (EPSILON + texel_vel_mag));
	float k_min_max_support = _GatherBase + _GatherSubpixelMotion * k_subpixel_motion;

	float2 ss_offset01 	= k_min_max_support * float2(-g_texelSize.x, g_texelSize.y);
	float2 ss_offset11 	= k_min_max_support * float2(g_texelSize.x, g_texelSize.y);
	float4 c00 			= tex_current.Sample(sampler_bilinear, texCoord - ss_offset11);
	float4 c10 			= tex_current.Sample(sampler_bilinear, texCoord - ss_offset01);
	float4 c01 			= tex_current.Sample(sampler_bilinear, texCoord + ss_offset01);
	float4 c11 			= tex_current.Sample(sampler_bilinear, texCoord + ss_offset11);

	float4 cMin 	= min(c00, min(c10, min(c01, c11)));
	float4 cMax 	= max(c00, max(c10, max(c01, c11)));
	color_history 	= clamp(color_history, cMin, cMax);
	//================================================================================================
	
	//= Compute feedback weight from unbiased luminance difference (Timothy Lottes)=
	float lum0 					= Luminance(color_current);
	float lum1 					= Luminance(color_history);
	float unbiased_diff 		= abs(lum0 - lum1) / max(lum0, max(lum1, 0.2f));
	float unbiased_weight 		= 1.0f - unbiased_diff;
	float unbiased_weight_sqr 	= unbiased_weight * unbiased_weight;
	float feedback 				= lerp(g_blendMin, g_blendMax, unbiased_weight_sqr);
	//==============================================================================
	
	return lerp(color_history, color_current, feedback);
}