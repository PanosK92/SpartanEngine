float4 ResolveTAA(float2 texCoord, Texture2D tex_history, Texture2D tex_current, Texture2D tex_velocity, SamplerState sampler_bilinear)
{
	//float2 velocity				= GetVelocity(texCoord, tex_current, sampler_bilinear);
    //float2 texCoord_reprojected	= texCoord + velocity;
	
	float blendfactor 		= 0.05f;
    float4 color_history    = tex_history.Sample(sampler_bilinear, texCoord);
	float4 color_current 	= tex_current.Sample(sampler_bilinear, texCoord);
	float4 color_result		= lerp(color_history, color_current, blendfactor);

	return color_result;
}