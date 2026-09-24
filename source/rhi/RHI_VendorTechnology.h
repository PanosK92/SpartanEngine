/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===============
#include "../math/Vector2.h"
#include "../math/Vector3.h"
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
            RHI_Texture* tex_color,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_velocity,
            RHI_Texture* tex_output,
            RHI_Texture* tex_reactive
        );

        // ngx project id, guid format required by nvidia, must match vulkan discovery
        static constexpr const char* dlss_project_id     = "6f8a2c1e-9d34-4b71-a5e8-2c7f91d0b463";
        static constexpr const char* dlss_engine_version = "1.0";

        // dlss
        static void DLSS_GenerateJitterSample(float* x, float* y);
        static void DLSS_Dispatch(
            RHI_Texture* tex_color,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_velocity,
            RHI_Texture* tex_output,
            RHI_Texture* tex_bias = nullptr
        );

        // nrd, guides + signal in/out, light_direction only for shadows
        // shadow_denoiser_index 0 is the sun, 1-4 are local sigma instances
        static bool NRD_Dispatch(
            Nrd_Preset preset,
            RHI_Texture* tex_mv,
            RHI_Texture* tex_normal_roughness,
            RHI_Texture* tex_view_z,
            RHI_Texture* tex_signal_in,
            RHI_Texture* tex_signal_out,
            const math::Vector3* light_direction = nullptr,
            uint32_t shadow_denoiser_index = 0
        );
    };
}
