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

#pragma once

//= INCLUDES =======

//==================

namespace Spartan
{
    enum class RendererBindingsTex
    {
        // Material
        material_albedo    = 0,
        material_roughness = 1,
        material_metallic  = 2,
        material_normal    = 3,
        material_height    = 4,
        material_occlusion = 5,
        material_emission  = 6,
        material_mask      = 7,

        // G-buffer
        gbuffer_albedo     = 8,
        gbuffer_normal     = 9,
        gbuffer_material   = 10,
        gbuffer_velocity   = 11,
        gbuffer_depth      = 12,

        // Light depth/color maps
        light_directional_depth    = 13,
        light_directional_color    = 14,
        light_point_depth          = 15,
        light_point_color          = 16,
        light_spot_depth           = 17,
        light_spot_color           = 18,

        // Misc
        lutIbl             = 19,
        environment        = 20,
        normal_noise       = 21,
        hbao               = 22,
        light_diffuse      = 23,
        light_specular     = 24,
        light_volumetric   = 25,
        ssr                = 26,
        frame              = 27,
        tex                = 28,
        tex2               = 29,
        font_atlas         = 30,
        ssgi               = 31
    };

    enum class RendererBindingsUav
    {
        r       = 0,
        rg      = 1,
        rgb     = 2,
        rgba    = 3,
        rgb2    = 4,
        rgb3    = 5
    };
}
