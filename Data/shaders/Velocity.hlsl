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

// Returns average velocity
float2 GetVelocity_Dilate_Average(float2 texCoord, Texture2D texture_velocity, SamplerState sampler_bilinear)
{
	float dx = g_texel_size.x;
	float dy = g_texel_size.y;
	
	float2 tl 	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(-dx, -dy)).xy;
	float2 tr	= texture_velocity.Sample(sampler_bilinear, texCoord + float2( dx, -dy)).xy;
	float2 bl	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(-dx, dy)).xy;
	float2 br 	= texture_velocity.Sample(sampler_bilinear, texCoord + float2( dx, dy)).xy;
	float2 ce 	= texture_velocity.Sample(sampler_bilinear, texCoord).xy;
	
	return (tl + tr + bl + br + ce) / 5.0f;
}

// Returns velocity with min depth (in a 3x3 neighborhood)
float2 GetVelocity_Dilate_Min(float2 texCoord, Texture2D texture_velocity, Texture2D texture_depth, SamplerState sampler_bilinear)
{	
	float min_depth	= 0.0f;
	float2 min_uv 	= texCoord;
	
	[unroll]
    for(int y = -1; y <= 1; ++y)
    {
		[unroll]
        for(int x = -1; x <= 1; ++x)
        {
			float2 offset 	= float2(x, y) * g_texel_size;
			float depth		= texture_depth.Sample(sampler_bilinear, texCoord + offset).r;
			if(depth > min_depth) // Reverse-z, so looking for max to find min depth
			{
				min_depth	= depth;
				min_uv	= texCoord + offset;
			}
        }
	}

	return texture_velocity.Sample(sampler_bilinear, min_uv).xy;
}

// Returns velocity with max depth (in a 3x3 neighborhood)
float2 GetVelocity_Dilate_Max(float2 texCoord, Texture2D texture_velocity, Texture2D texture_depth, SamplerState sampler_bilinear)
{	
	float max_depth	= 1.0f;
	float2 max_uv 	= texCoord;
	
	[unroll]
    for(int y = -1; y <= 1; ++y)
    {
		[unroll]
        for(int x = -1; x <= 1; ++x)
        {
			float2 offset 	= float2(x, y) * g_texel_size;
			float depth		= texture_depth.Sample(sampler_bilinear, texCoord + offset).r;
			if(depth < max_depth) // Reverse-z, so looking for min to find max depth
			{
				max_depth	= depth;
				max_uv		= texCoord + offset;
			}
        }
	}

	return texture_velocity.Sample(sampler_bilinear, max_uv).xy;
}