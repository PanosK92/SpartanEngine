static const int MAX_SAMPLES = 16;

float4 MotionBlur(float2 texCoord, Texture2D texture_color, Texture2D texture_velocity, SamplerState bilinearSampler)
{	
	float4 color 	= texture_color.Sample(bilinearSampler, texCoord);	
	float2 velocity = GetVelocity(texCoord, texture_velocity, bilinearSampler);
	
	// Scale velocity based on delta time and user preference
	float velocity_scale 	= g_motionBlur_strength * g_deltaTime;
	velocity				*= velocity_scale;
	
	// Early exit
	if (velocity.x + velocity.y == 0.0f)
		return color;
	
	// Improve performance by adapting sample count to velocity
	float speed = length(velocity / g_texelSize);
	int samples = clamp(int(speed), 1, MAX_SAMPLES);
		
	for (int i = 1; i < samples; ++i) 
	{
		float2 offset 	= velocity * (float(i) / float(samples - 1) - 0.5f);
		color 			+= texture_color.SampleLevel(bilinearSampler, texCoord + offset, 0);
	}
	color /= float(samples);

	return color;
}