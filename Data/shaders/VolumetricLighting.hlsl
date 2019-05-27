/*
Copyright(c) 2016-2019 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

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