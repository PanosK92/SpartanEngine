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
			if(depth > closestDepth) // Reverse-Z
			{
				closestDepth	= depth;
				closestTexCoord	= texCoord + offset;
			}
        }
	}

	return texture_velocity.Sample(sampler_bilinear, closestTexCoord).xy;
}