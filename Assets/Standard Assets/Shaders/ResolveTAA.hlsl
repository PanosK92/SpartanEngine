float4 ResolveTAA(float2 texCoord, Texture2D texHistory, Texture2D texCurrent, Texture2D texVelocity, SamplerState bilinearSampler)
{
	//float2 velocity 		= texVelocity.Sample(bilinearSampler, texCoord).xy * resolution;
    //float2 reprojectedPos = texCoord - velocity;
	//float2 history_uv 	= reprojectedPos / resolution;

	float blendfactor 		= 0.05f;
    float4 color_history    = texHistory.Sample(bilinearSampler, texCoord);
	float4 color_current 	= texCurrent.Sample(bilinearSampler, texCoord);
	float4 color_result		= lerp(color_history, color_current, blendfactor);

	return color_result;
}