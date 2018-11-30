float4 MotionBlur(float2 texCoord, Texture2D texture_color, Texture2D texture_velocity, SamplerState bilinearSampler)
{
	//float2 velocity 		= texture_velocity.Sample(bilinearSampler, texCoord).xy;	
	//float2 blur_direction = velocity * 300.0f;
	//float blur_sigma 		= 5.0f;
	//float4 color 			= Blur_Gaussian(texCoord, texture_color, bilinearSampler, resolution, blur_direction, blur_sigma);
	
	int samples = 16;
	float velocity_scale = motionBlur_strength;
	
	float2 velocity = texture_velocity.Sample(bilinearSampler, texCoord).xy;
	velocity *= velocity_scale;
	
	float4 result = texture_color.Sample(bilinearSampler, texCoord);	
	for (int i = 1; i < samples; ++i) 
	{
		float2 offset = velocity * (float(i) / float(samples - 1) - 0.5f);
		result += texture_color.Sample(bilinearSampler, texCoord + offset);
	}
	result /= float(samples);

	return result;
}