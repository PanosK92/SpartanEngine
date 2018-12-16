// Based on "Temporal Antialiasing In Uncharted 4" by Ke XU - Naughty Dog.

static const float g_blendMin = 0.05f;
static const float g_blendMax = 0.8f;

float4 ResolveTAA(float2 texCoord, Texture2D tex_history, Texture2D tex_current, Texture2D tex_velocity, Texture2D tex_depth, SamplerState sampler_bilinear)
{
	// Get velocity for nearest depth (and the depth)
	float depth 	= 1.0f;
	float2 velocity	= GetVelocity_Dilate_Depth3X3(texCoord, tex_velocity, tex_depth, sampler_bilinear, depth);
	
	float4 color_current 	= tex_current.Sample(sampler_bilinear, texCoord);
	float2 texCoord_history = texCoord - velocity * g_texelSize;
	float4 color_history 	= tex_history.Sample(sampler_bilinear, texCoord_history);

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
	
	color_history = clamp(color_history, cMin, cMax);
	//================================================================================================
	
	//= Compute blend factor based on the amount of subpixel velocity ==========================================================
	float factor_velocity 	= abs(sin(frac(length(velocity)) * PI) - 1.0f); // Decrease when pixel motion gets subpixel
	float factor_clampMin 	= length(color_history - cMin); 				// Decrease when history is near min clamp
	float factor_clampMax 	= length(color_history - cMax); 				// Decrease when history is near max clamp
	//float factor_contrast	= saturate(1.0f - Luminance(cMax - cMin)); 		// Increase when local contrast is low
	float alpha				= saturate(1.0f - ((factor_velocity + factor_clampMin + factor_clampMax) / 3.0f));
	float blendfactor 		= lerp(g_blendMin, g_blendMax, alpha);
	//==========================================================================================================================
	
	return lerp(color_history, color_current, blendfactor);
}