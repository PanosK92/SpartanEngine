float4 VolumetricLighting(float2 texCoord, float3 lightPosScreenSpace, float3 color)
{
	int NUM_SAMPLES = 40;
	float density = 0.97f;
	float weight = 0.5f;
	float decay = 0.97f;
	float exposure = 0.25f;
	
	// Calculate vector from pixel to light source in screen space.  
	float2 deltaTexCoord = (texCoord - lightPosScreenSpace.xy);  
	 
	// Divide by number of samples and scale by control factor.  
	deltaTexCoord *= 1.0f / NUM_SAMPLES * density; 
	
	// Store initial sample.  
	//float3 color = tex2D(frameSampler, texCoord);
	
	// Set up illumination decay factor.  
	float illuminationDecay = 1.0f;  
	
	// Evaluate summation from Equation 3 NUM_SAMPLES iterations.  
	for (int j = 0; j < NUM_SAMPLES; j++)  
	{  	
		texCoord -= deltaTexCoord; // Step sample location along ray.  	
		float3 sample = ToLinear(albedoTexture.Sample(samplerAniso, texCoord)); // Retrieve sample at new location.  	
		sample *= illuminationDecay * weight; // Apply sample attenuation scale/decay factors.  
		color += sample;  // Accumulate combined color.  
		illuminationDecay *= decay;  // Update exponential decay factor.  
	}  
	
	// Output final color with a further scale control factor.  
	return float4(color * exposure, 1);  
}