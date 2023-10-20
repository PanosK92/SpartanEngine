/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =========
#include "common.hlsl"
//====================

void mainPS(Pixel_PosUv input)
{
    const float3 f3_value     = pass_get_f3_value();
    const bool has_alpha_mask = f3_value.x == 1.0f;
    const bool has_albedo     = f3_value.y == 1.0f;
    const float alpha         = f3_value.z;

    bool mask_alpha  = has_alpha_mask && tex_material_mask.Sample(samplers[sampler_anisotropic_wrap], input.uv).r <= ALPHA_THRESHOLD;
    bool mask_albedo = alpha == 1.0f && has_albedo && tex_material_albedo.Sample(samplers[sampler_anisotropic_wrap], input.uv).a <= ALPHA_THRESHOLD;
    
    if (mask_alpha || mask_albedo)
        discard;
}
