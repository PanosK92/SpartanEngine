/*
Copyright(c) 2016-2020 Panos Karabelas

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

// Returns average velocity (cross pattern)
float2 GetVelocity_Average(float2 texCoord)
{
    float dx = g_texel_size.x;
    float dy = g_texel_size.y;
    
    float2 tl = tex_velocity.SampleLevel(sampler_point_clamp, texCoord + float2(-dx, -dy), 0).xy;
    float2 tr = tex_velocity.SampleLevel(sampler_point_clamp, texCoord + float2(dx, -dy), 0).xy;
    float2 bl = tex_velocity.SampleLevel(sampler_point_clamp, texCoord + float2(-dx, dy), 0).xy;
    float2 br = tex_velocity.SampleLevel(sampler_point_clamp, texCoord + float2(dx, dy), 0).xy;
    float2 ce = tex_velocity.SampleLevel(sampler_point_clamp, texCoord, 0).xy;
    
    return (tl + tr + bl + br + ce) / 5.0f;
}

// Returns max velocity (3x3 neighborhood)
float2 GetVelocity_Max(float2 texCoord, Texture2D texture_velocity, Texture2D texture_depth)
{   
    float2 max_velocity = 0.0f;
	float max_length2 	= 0.0f;
    
    [unroll]
    for(int y = -1; y <= 1; ++y)
    {
        [unroll]
        for(int x = -1; x <= 1; ++x)
        {
            float2 offset	= float2(x, y) * g_texel_size;
            float2 velocity = tex_velocity.SampleLevel(sampler_point_clamp, texCoord + offset, 0).xy;
			float length2   = dot(velocity, velocity);
            if(length2 > max_length2)
            {
                max_velocity 	= velocity;
				max_length2 	= length2;
            }
        }
    }

    return max_velocity;
}

// Returns velocity with min depth (3x3 neighborhood)
float2 GetVelocity_DepthMin(float2 texCoord)
{   
    float min_depth = 0.0f;
    float2 min_uv   = texCoord;
    
    [unroll]
    for(int y = -1; y <= 1; ++y)
    {
        [unroll]
        for(int x = -1; x <= 1; ++x)
        {
            float2 offset	= float2(x, y) * g_texel_size;
            float depth = tex_depth.SampleLevel(sampler_point_clamp, texCoord + offset, 0).r;
            if(depth > min_depth) // Reverse-z, so looking for max to find min depth
            {
                min_depth   = depth;
                min_uv  	= texCoord + offset;
            }
        }
    }

    return tex_velocity.SampleLevel(sampler_point_clamp, min_uv, 0).xy;
}

// Returns velocity with max depth (3x3 neighborhood)
float2 GetVelocity_DepthMax(float2 texCoord, Texture2D texture_velocity, Texture2D texture_depth)
{   
    float max_depth = 1.0f;
    float2 max_uv   = texCoord;
    
    [unroll]
    for(int y = -1; y <= 1; ++y)
    {
        [unroll]
        for(int x = -1; x <= 1; ++x)
        {
            float2 offset   = float2(x, y) * g_texel_size;
            float depth = tex_depth.SampleLevel(sampler_point_clamp, texCoord + offset, 0).r;
            if(depth < max_depth) // Reverse-z, so looking for min to find max depth
            {
                max_depth   = depth;
                max_uv      = texCoord + offset;
            }
        }
    }

    return tex_velocity.SampleLevel(sampler_point_clamp, max_uv, 0).xy;
}
