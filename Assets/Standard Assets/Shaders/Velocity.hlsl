// Return average velocity
float2 GetVelocity_Dilate_Average(float2 texCoord, Texture2D texture_velocity, SamplerState sampler_bilinear)
{
	float dx = 2.0f * g_texelSize.x;
	float dy = 2.0f * g_texelSize.y;
	
	float2 velocity_tl 	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(-dx, -dy)).xy;
	float2 velocity_tr	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(dx, -dy)).xy;
	float2 velocity_bl	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(-dx, dy)).xy;
	float2 velocity_br 	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(dx, dy)).xy;
	float2 velocity_ce 	= texture_velocity.Sample(sampler_bilinear, texCoord).xy;
	float2 velocity 	= (velocity_tl + velocity_tr + velocity_bl + velocity_br + velocity_ce) / 5.0f;	
	
	return velocity;
}

// Returns velocity with closest depth
float2 GetVelocity_Dilate_Depth3X3(float2 texCoord, Texture2D texture_velocity, Texture2D texture_depth, SamplerState sampler_bilinear, out float closestDepth)
{	
	closestDepth			= 1.0f;
	float2 closestTexCoord 	= texCoord;
	[unroll]
    for(int y = -1; y <= 1; ++y)
    {
		[unroll]
        for(int x = -1; x <= 1; ++x)
        {
			float2 offset 	= float2(x, y) * g_texelSize;
			float depth		= texture_depth.Sample(sampler_bilinear, texCoord + offset).r;
			if(depth < closestDepth)
			{
				closestDepth	= depth;
				closestTexCoord	= texCoord + offset;
			}
        }
	}

	return texture_velocity.Sample(sampler_bilinear, closestTexCoord).xy;
}