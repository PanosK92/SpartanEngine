/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "../Math/Vector3.h"
#include "RHI_Definitions.h"
//==========================

struct FrameBufferData;

namespace spartan
{
    using Cb_Frame = FrameBufferData;

    // easy nrd entry points, each maps to a tuned denoiser
    enum class Nrd_Preset : uint32_t
    {
        Gi,          // reblur diffuse, restir gi
        Reflections, // reblur specular, rt reflections
        Shadows      // sigma, directional rt shadows
    };

    // the vendor sdk glue, xess/dlss upscaling, nrd denoising and the rest, kept behind one facade
    class RHI_VendorTechnology
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Tick(Cb_Frame* cb_frame, const math::Vector2& resolution_render, const math::Vector2& resolution_output, const float resolution_scale);
        static void ResetHistory();

        // xess
        static void XeSS_GenerateJitterSample(float* x, float* y);
        static void XeSS_Dispatch(
            RHI_CommandList* cmd_list,
            RHI_Texture* tex_color,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_velocity,
            RHI_Texture* tex_output
        );

        // ngx project id, guid format required by nvidia, must match vulkan discovery
        static constexpr const char* dlss_project_id     = "6f8a2c1e-9d34-4b71-a5e8-2c7f91d0b463";
        static constexpr const char* dlss_engine_version = "1.0";

        // dlss
        static void DLSS_GenerateJitterSample(float* x, float* y);
        static void DLSS_Dispatch(
            RHI_CommandList* cmd_list,
            RHI_Texture* tex_color,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_velocity,
            RHI_Texture* tex_output
        );

        // nrd, guides + signal in/out, light_direction only for shadows
        static bool NRD_Dispatch(
            RHI_CommandList* cmd_list,
            Nrd_Preset preset,
            RHI_Texture* tex_mv,
            RHI_Texture* tex_normal_roughness,
            RHI_Texture* tex_view_z,
            RHI_Texture* tex_signal_in,
            RHI_Texture* tex_signal_out,
            const math::Vector3* light_direction = nullptr
        );
    };
}
