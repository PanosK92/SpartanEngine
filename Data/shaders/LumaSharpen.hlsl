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

    /*
        LumaSharpen 1.4.1
        original hlsl by Christian Cann Schuldt Jensen ~ CeeJay.dk
        port to glsl by Anon
        port back to hlsl by Panos karabelas
        It blurs the original pixel with the surrounding pixels and then subtracts this blur to sharpen the image.
        It does this in luma to avoid color artifacts and allows limiting the maximum sharpning to avoid or lessen halo artifacts.
        This is similar to using Unsharp Mask in Photoshop.
    */

    // -- Sharpening --
    //#define sharp_clamp    0.35f  //[0.000 to 1.000] Limits maximum amount of sharpening a pixel receives - Default is 0.035
    
    // -- Advanced sharpening settings --
    static const float offset_bias = 1.0f; //[0.0 to 6.0] Offset bias adjusts the radius of the sampling pattern.
    static const float3 CoefLuma = float3(0.2126f, 0.7152f, 0.0722f); // BT.709 & sRBG luma coefficient (Monitors and HD Television)

float4 LumaSharpen(float2 texCoord, Texture2D sourceTexture, float2 resolution, float sharp_strength, float sharp_clamp)
{
    // -- Combining the strength and luma multipliers --
    float3 sharp_strength_luma = (CoefLuma * sharp_strength); //I'll be combining even more multipliers with it later on
    
    // -- Gaussian filter --
    //   [ .25, .50, .25]     [ 1 , 2 , 1 ]
    //   [ .50,   1, .50]  =  [ 2 , 4 , 2 ]
    //   [ .25, .50, .25]     [ 1 , 2 , 1 ]

    float px = 1.0f / resolution[0];
    float py = 1.0f / resolution[1];

    float3 blur_ori  = sourceTexture.SampleLevel(sampler_bilinear_clamp, texCoord + float2(px,-py) * 0.5f * offset_bias, 0).rgb; // South East
    blur_ori        += sourceTexture.SampleLevel(sampler_bilinear_clamp, texCoord + float2(-px, -py) * 0.5f * offset_bias, 0).rgb; // South West
    blur_ori        += sourceTexture.SampleLevel(sampler_bilinear_clamp, texCoord + float2(px, py) * 0.5f * offset_bias, 0).rgb; // North East
    blur_ori        += sourceTexture.SampleLevel(sampler_bilinear_clamp, texCoord + float2(-px, py) * 0.5f * offset_bias, 0).rgb; // North West
    blur_ori        *= 0.25f;  // ( /= 4) Divide by the number of texture fetches

    // -- Calculate the sharpening --
    float4 colorInput = sourceTexture.SampleLevel(sampler_point_clamp, texCoord, 0);
    float3 ori = colorInput.rgb;
    float3 sharp = ori - blur_ori;  // Subtracting the blurred image from the original image

    // -- Adjust strength of the sharpening and clamp it--
    float4 sharp_strength_luma_clamp = float4(sharp_strength_luma * (0.5f / sharp_clamp), 0.5f); // Roll part of the clamp into the dot

    float sharp_luma = clamp((dot(float4(sharp, 1.0f), sharp_strength_luma_clamp)), 0.0f, 1.0f); // Calculate the luma, adjust the strength, scale up and clamp
    sharp_luma = (sharp_clamp * 2.0f) * sharp_luma - sharp_clamp; //  scale down

    // -- Combining the values to get the final sharpened pixel --
    colorInput.rgb = colorInput.rgb + sharp_luma; // Add the sharpening to the input color.
    return float4(saturate(colorInput.rgb), colorInput.a);
}
