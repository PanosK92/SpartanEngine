/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ===============
#include "../Math/Vector2.h"
#include "RHI_Definitions.h"
//==========================

namespace Spartan
{
    class Camera;
    struct Cb_Frame;

    class RHI_FidelityFX
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Resize(const Math::Vector2& resolution_render, const Math::Vector2& resolution_output);

        // FSR 3
        static void FSR3_ResetHistory();
        static void FSR3_GenerateJitterSample(float* x, float* y);
        static void FSR3_Dispatch(
            RHI_CommandList* cmd_list,
            RHI_Texture* tex_color,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_velocity,
            RHI_Texture* tex_color_opaque,
            RHI_Texture* tex_reactive,
            RHI_Texture* tex_output,
            Camera* camera,
            const float delta_time,
            const float sharpness,
            const float exposure,
            const float resolution_scale
        );

        // SSSR
        static void SSSR_Dispatch(
            RHI_CommandList* cmd_list,
            RHI_Texture* tex_color,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_motion_vectors,
            RHI_Texture* tex_normal,
            RHI_Texture* tex_material,
            RHI_Texture* tex_brdf,
            RHI_Texture* tex_output,
            Cb_Frame* cb_frame,
            const float resolution_scale
        );

        // Brixelizer GI
        static void BrixelizerGI_Dispatch(
            RHI_CommandList* cmd_list
        );
    };
}
