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

static const uint g_mb_samples = 16;

float4 MotionBlur(float2 texCoord, Texture2D texture_color, Texture2D texture_velocity, Texture2D texture_depth)
{	
	float4 color 	= texture_color.Sample(sampler_point_clamp, texCoord);	
	float2 velocity = GetVelocity_Dilate_Max(texCoord, texture_velocity, texture_depth);
	
	// Make velocity scale based on user preference instead of frame rate
	float velocity_scale = g_motionBlur_strength / g_delta_time;
	velocity			*= velocity_scale;
	
	// Early exit
	if (abs(velocity.x) + abs(velocity.y) < EPSILON)
		return color;
	
    [unroll]
	for (uint i = 1; i < g_mb_samples; ++i) 
	{
		float2 offset 	= velocity * (float(i) / float(g_mb_samples - 1) - 0.5f);
		color 			+= texture_color.SampleLevel(sampler_bilinear_clamp, texCoord + offset, 0);
	}

	return color / float(g_mb_samples);
}