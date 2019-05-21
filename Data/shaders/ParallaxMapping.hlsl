// Based on: https://learnopengl.com/Advanced-Lighting/Parallax-Mapping
float2 ParallaxMapping(Texture2D depth_tex, SamplerState depth_sampler, float2 texCoords, float3 view_dir, float3x3 TBN, float height_scale)
{ 
	float3x3 to_tangent = transpose(TBN);
	view_dir 			= mul(view_dir, to_tangent);	
	height_scale 		*= -1.0f;

    // number of depth layers
    const float minLayers = 8;
    const float maxLayers = 32;
    float numLayers = lerp(maxLayers, minLayers, abs(dot(float3(0.0, 0.0, 1.0), view_dir)));  
    
	// calculate the size of each layer
    float layerDepth = 1.0 / numLayers;

    // depth of current layer
    float currentLayerDepth = 0.0;

    // the amount to shift the texture coordinates per layer (from vector P)
    float2 P = view_dir.xy / view_dir.z * height_scale; 
    float2 deltaTexCoords = P / numLayers;
  
    // get initial values
    float2 currentTexCoords = texCoords;
	float2 deriv_x = ddx_coarse(currentTexCoords);
	float2 deriv_y = ddy_coarse(currentTexCoords);
    float currentDepthMapValue = depth_tex.SampleGrad(depth_sampler, currentTexCoords, deriv_x, deriv_y).r;

	[loop]
    while(currentLayerDepth < currentDepthMapValue)
    {
        // shift texture coordinates along direction of P
        currentTexCoords -= deltaTexCoords;

        // get depthmap value at current texture coordinates
        currentDepthMapValue = depth_tex.SampleGrad(depth_sampler, currentTexCoords, deriv_x, deriv_y).r;

        // get depth of next layer
        currentLayerDepth += layerDepth;  
    }
    
    // get texture coordinates before collision (reverse operations)
    float2 prevTexCoords = currentTexCoords + deltaTexCoords;

    // get depth after and before collision for linear interpolation
    float afterDepth  = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = depth_tex.SampleGrad(depth_sampler, prevTexCoords, deriv_x, deriv_y).r - currentLayerDepth + layerDepth;
	
    // interpolation of texture coordinates
    float weight = afterDepth / (afterDepth - beforeDepth);
    float2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;
}