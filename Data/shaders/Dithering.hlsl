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

float4 Dither_Ordered(float4 color, float2 texcoord)
{
    float ditherBits = 8.0;

    float2 ditherSize	= float2(1.0 / 16.0, 10.0 / 36.0);
    float gridPosition	= frac(dot(texcoord, (g_resolution * ditherSize)) + 0.25);
    float ditherShift	= (0.25) * (1.0 / (pow(2.0, ditherBits) - 1.0));

    float3 RGBShift = float3(ditherShift, -ditherShift, ditherShift);
    RGBShift = lerp(2.0 * RGBShift, -2.0 * RGBShift, gridPosition);

    color.rgb += RGBShift;

    return color;
}

// note: valve edition
// from http://alex.vlachos.com/graphics/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf
// note: input in pixels (ie not normalized uv)
float3 Dither_Valve(float2 screen_pos)
{
	float _dot = dot(float2(171.0, 231.0), screen_pos.xy);
    float3 dither = float3(_dot, _dot, _dot);
    dither.rgb = frac(dither.rgb / float3(103.0, 71.0, 97.0));
    
    return dither.rgb / 255.0;
}