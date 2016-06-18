float4 Godrays(float2 texCoord)
{  
	float illuminationDecay = 1.0f;  
	float exposure = 1.0f;
	float density = 1.0f;
	int samples = 2;
	
	// Calculate vector from pixel to light source in screen space.  
	float2 deltaTexCoord = (texCoord - ScreenLightPos.xy); 
   
	// Divide by number of samples and scale by control factor.  
	deltaTexCoord *= 1.0f / samples * density;  
  
	// Store initial sample.  
	float3 color = tex2D(frameSampler, texCoord);  
   
	// Evaluate summation from Equation 3 NUM_SAMPLES iterations.  
	for (int i = 0; i < samples; i++)  
	{  
		// Step sample location along ray.  
		texCoord -= deltaTexCoord;  
		// Retrieve sample at new location.  
		float3 sample = tex2D(frameSampler, texCoord);  
		// Apply sample attenuation scale/decay factors.  
		sample *= illuminationDecay * Weight;  
		// Accumulate combined color.  
		color += sample;  
		// Update exponential decay factor.  
		illuminationDecay *= Decay;  
	}  
	
	// Output final color with a further scale control factor. 
	return float4(color * exposure, 1);  
} 