/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "pch.h"
#include "../RHI_VendorTechnology.h"
#include "../RHI_CommandList.h"
//==============================

//= NAMESPACES ===============
using namespace spartan::math;
//============================

namespace spartan
{
    // xess and dlss have no metal backends, nrd is a passthrough, see NRD_Dispatch
    void RHI_VendorTechnology::Initialize()
    {
    }

    void RHI_VendorTechnology::Shutdown()
    {
    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame, const Vector2& resolution_render, const Vector2& resolution_output, const float resolution_scale)
    {
    }

    void RHI_VendorTechnology::ResetHistory()
    {
    }

    float RHI_VendorTechnology::XeSS_GetResolutionScaleMin(const Vector2& resolution_render, const Vector2& resolution_output)
    {
        return 0.0f;
    }

    float RHI_VendorTechnology::DLSS_GetResolutionScaleMin(const Vector2& resolution_render, const Vector2& resolution_output)
    {
        return 0.0f;
    }

    void RHI_VendorTechnology::XeSS_GenerateJitterSample(float* x, float* y)
    {
        *x = 0.0f;
        *y = 0.0f;
    }

    void RHI_VendorTechnology::DLSS_GenerateJitterSample(float* x, float* y)
    {
        *x = 0.0f;
        *y = 0.0f;
    }

    void RHI_VendorTechnology::XeSS_Dispatch(RHI_Texture* tex_color, RHI_Texture* tex_depth, RHI_Texture* tex_velocity, RHI_Texture* tex_output, RHI_Texture* tex_reactive)
    {
    }

    void RHI_VendorTechnology::DLSS_Dispatch(RHI_Texture* tex_color, RHI_Texture* tex_depth, RHI_Texture* tex_velocity, RHI_Texture* tex_output, RHI_Texture* tex_bias)
    {
    }

    bool RHI_VendorTechnology::NRD_Dispatch(
        Nrd_Preset preset, RHI_Texture* tex_mv, RHI_Texture* tex_normal_roughness, RHI_Texture* tex_view_z, RHI_Texture* tex_signal_in, RHI_Texture* tex_signal_out,
        const Vector3* light_direction, uint32_t shadow_denoiser_index, uint32_t eye, RHI_Texture* tex_specular_in, RHI_Texture* tex_specular_out)
    {
        // nrd has no metal backend, reblur signals go through unfiltered, pack and unpack are exact inverses so the
        // renderer's unpack still remodulates them and averages a still camera into a converged image
        // sigma shadows use another encoding, they take the renderer's own fallback
        if (preset == Nrd_Preset::Shadows || !tex_signal_in || !tex_signal_out)
            return false;

        RHI_CommandList::Copy(tex_signal_in, tex_signal_out, false);
        if (tex_specular_in && tex_specular_out)
        {
            RHI_CommandList::Copy(tex_specular_in, tex_specular_out, false);
        }

        return true;
    }
}
