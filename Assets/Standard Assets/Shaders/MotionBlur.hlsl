float4 MotionBlur(float2 texCoord, Texture2D texture_color, Texture2D texture_velocity, SamplerState bilinearSampler)
{
	int samples 			= 16;
	float velocity_scale 	= motionBlur_strength;
	
	float4 color 	= texture_color.Sample(bilinearSampler, texCoord);	
	float2 velocity = texture_velocity.Sample(bilinearSampler, texCoord).xy;
	velocity		*= velocity_scale;
	
	// Early exit
	if (velocity.x + velocity.y == 0.0f)
		return color;
		
	for (int i = 1; i < samples; ++i) 
	{
		float2 offset 	= velocity * (float(i) / float(samples - 1) - 0.5f);
		color 			+= texture_color.SampleLevel(bilinearSampler, texCoord + offset, 0);
	}
	color /= float(samples);

	return color;
}