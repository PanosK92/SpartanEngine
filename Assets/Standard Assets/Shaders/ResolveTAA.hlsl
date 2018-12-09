// Based on "Temporal Antialiasing In Uncharted 4" by Ke XU

static const float g_blendMin = 0.05f;
static const float g_blendMax = 0.8f;

float4 ResolveTAA(float2 texCoord, Texture2D tex_history, Texture2D tex_current, Texture2D tex_velocity, Texture2D tex_depth, SamplerState sampler_bilinear)
{
	// Reproject texture coordinates
	float2 velocity			= GetVelocity_Dilate_Depth(texCoord, tex_current, tex_depth, sampler_bilinear) * g_texelSize;
    float2 texCoord_history	= texCoord - velocity;
	
	//= Clamp out too different history colors - For non-existing and lighting change cases ==========
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
	
	float4 cMin = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
	float4 cMax = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
	//================================================================================================
	
	// Get current and history color
	float4 color_current 	= cmc;
	float4 color_history 	= clamp(tex_history.Sample(sampler_bilinear, texCoord_history), cMin, cMax);
	
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