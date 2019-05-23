// Based on: https://learnopengl.com/Advanced-Lighting/Parallax-Mapping
float2 ParallaxMapping(Texture2D depth_tex, SamplerState depth_sampler, float2 texCoords, float3 view_dir, float3x3 TBN, float height_scale)
{ 
	float3x3 to_tangent = transpose(TBN);
	view_dir 			= mul(view_dir, to_tangent);	
	height_scale 		*= -1.0f;

    // number of depth layers
    const float minLayers 	= 32;
    const float maxLayers 	= 64;
    float numLayers 		= lerp(maxLayers, minLayers, abs(dot(float3(0.0, 0.0, 1.0), view_dir)));  
    
    // the amount to shift the texture coordinates per layer (from vector P)
    float2 P = view_dir.xy / view_dir.z * height_scale; 
    float2 deltaTexCoords = P / numLayers;
  
    // get initial values
    float2 currentTexCoords = texCoords;
	float2 deriv_x = ddx_coarse(currentTexCoords);
	float2 deriv_y = ddy_coarse(currentTexCoords);
    float currentDepthMapValue = depth_tex.SampleGrad(depth_sampler, currentTexCoords, deriv_x, deriv_y).r;

	// depth of current layer
    float layer_depth_current = 0.0;

	// calculate the step size (size of each layer)
    float step_size = 1.0 / numLayers;

	int steps = 0;
	[loop]
    while(layer_depth_current < currentDepthMapValue && steps < maxLayers)
    {
        // shift texture coordinates along direction of P
        currentTexCoords -= deltaTexCoords;

        // get depthmap value at current texture coordinates
        currentDepthMapValue = depth_tex.SampleGrad(depth_sampler, currentTexCoords, deriv_x, deriv_y).r;

        // get depth of next layer
        layer_depth_current += step_size;  
		
		steps++;
    }
    
    // get texture coordinates before collision (reverse operations)
    float2 prevTexCoords = currentTexCoords + deltaTexCoords;

    // get depth after and before collision for linear interpolation
    float afterDepth  = currentDepthMapValue - layer_depth_current;
    float beforeDepth = depth_tex.SampleGrad(depth_sampler, prevTexCoords, deriv_x, deriv_y).r - layer_depth_current + step_size;
	
    // interpolation of texture coordinates
    float weight = afterDepth / (afterDepth - beforeDepth);
    float2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;
}