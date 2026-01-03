/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ======================
#include "common.hlsl"
#include "common_tessellation.hlsl"
#include "g_buffer.hlsl"
//=================================

// Adapted snow shader based off Ben Cloward's tutorial: https://www.bencloward.com/snow-shader-in-hlsl/
// TODO: Make snow accumulate on top of objects
// TODO: Add snow depth map to avoid snow intersecting with objects


// SPARKLING EFFECT
//=================================
// - UV Space Sparkles
//=================================
// 1) Get texture coordinate
// 2) Multiply the uv space per preference.
//=================================
// - Screen Space Sparkles
//=================================
// 1) Get screen/viewport UV coordinate of the display.
// 2) Get the screen/viewport size in pixels.
// 3) Divide the screen resolution by the size of the snow texture.
// 4) Multiply the divide output with the screen UV coordinates.
// - NOTE: This will make the sparkly snow texture in screen space.
//=================================
// 1) Multiply the UV space output with the screen space output.
// 2) Create a mask to control the visibility of the sparkles.
//      - Get the direction of the sun light.
//      - Get the dot product between the sun light direction and the normal of the snow surface.
//      - Use a power function to control the sharpness of the mask.
// 3) Multiply the mask with the sparkle texture output to control the visibility of the sparkles.


