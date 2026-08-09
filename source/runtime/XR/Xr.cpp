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

//= INCLUDES ==================
#include "pch.h"
#include "Xr.h"
#include "../Rendering/Renderer.h"
#include "../Commands/Console/ConsoleCommands.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // static member definitions, the active rhi backend (Vulkan_Xr.cpp / D3D12_Xr.cpp) drives them
    atomic<bool> Xr::m_initialized          = false;
    bool Xr::m_hmd_connected                = false;
    bool Xr::m_session_running              = false;
    bool Xr::m_session_focused              = false;
    bool Xr::m_frame_began                  = false;
    string Xr::m_runtime_name               = "N/A";
    string Xr::m_device_name                = "N/A";
    uint32_t Xr::m_recommended_width        = 0;
    uint32_t Xr::m_recommended_height       = 0;
    math::Vector3 Xr::m_head_position       = math::Vector3::Zero;
    math::Quaternion Xr::m_head_orientation = math::Quaternion::Identity;
    bool Xr::m_stereo_3d                    = false;
    array<XrEyeView, Xr::eye_count> Xr::m_eye_views;

    namespace
    {
        uint32_t saved_output_w         = 0;
        uint32_t saved_output_h         = 0;
        uint32_t saved_render_w         = 0;
        uint32_t saved_render_h         = 0;
        float saved_viewport_w          = 0.0f;
        float saved_viewport_h          = 0.0f;
        bool desktop_resolution_saved   = false;

        float saved_resolution_scale    = 1.0f;
        bool resolution_scale_saved     = false;
    }

    // api-agnostic accessor implementations, lifecycle and frame methods live in the per-rhi xr files
    bool Xr::IsHmdConnected()
    {
        return m_hmd_connected;
    }

    bool Xr::GetStereoMode()
    {
        return m_stereo_3d;
    }

    bool Xr::TryGetPersistedDesktopResolution(
        uint32_t& output_w,
        uint32_t& output_h,
        uint32_t& render_w,
        uint32_t& render_h,
        float& viewport_w,
        float& viewport_h
    )
    {
        if (!desktop_resolution_saved || saved_output_w == 0 || saved_output_h == 0)
        {
            return false;
        }

        output_w   = saved_output_w;
        output_h   = saved_output_h;
        render_w   = saved_render_w;
        render_h   = saved_render_h;
        viewport_w = saved_viewport_w;
        viewport_h = saved_viewport_h;
        return true;
    }

    void Xr::SaveDesktopResolutionIfNeeded()
    {
        if (desktop_resolution_saved)
        {
            return;
        }

        saved_output_w = static_cast<uint32_t>(Renderer::GetResolutionOutput().x);
        saved_output_h = static_cast<uint32_t>(Renderer::GetResolutionOutput().y);
        saved_render_w = static_cast<uint32_t>(Renderer::GetResolutionRender().x);
        saved_render_h = static_cast<uint32_t>(Renderer::GetResolutionRender().y);
        saved_viewport_w = Renderer::GetViewport().width;
        saved_viewport_h = Renderer::GetViewport().height;
        desktop_resolution_saved = true;
    }

    void Xr::ClampResolutionScaleForVr()
    {
        if (!resolution_scale_saved)
        {
            saved_resolution_scale = cvar_resolution_scale.GetValue();
            resolution_scale_saved = true;
        }

        if (cvar_resolution_scale.GetValue() > 0.5f + 1e-3f)
        {
            ConsoleRegistry::Get().SetValueFromString("r.resolution_scale", "0.5");
            SP_LOG_INFO(
                "openxr: r.resolution_scale %.2f -> 0.50 (restore on exit)",
                saved_resolution_scale
            );
        }
    }

    void Xr::ApplyHmdResolution()
    {
        SaveDesktopResolutionIfNeeded();
        ClampResolutionScaleForVr();

        if (m_recommended_width == 0 || m_recommended_height == 0)
        {
            return;
        }

        Renderer::SetResolutionOutput(m_recommended_width, m_recommended_height, false);
        Renderer::SetResolutionRender(
            Renderer::GetScaledDimension(m_recommended_width),
            Renderer::GetScaledDimension(m_recommended_height),
            false
        );
        Renderer::SetViewport(
            static_cast<float>(m_recommended_width),
            static_cast<float>(m_recommended_height)
        );
    }

    void Xr::RestoreDesktopResolution(const bool restore_resolution_scale)
    {
        if (desktop_resolution_saved && saved_output_w > 0 && saved_output_h > 0)
        {
            Renderer::SetResolutionOutput(saved_output_w, saved_output_h, false);
            Renderer::SetResolutionRender(saved_render_w, saved_render_h, false);
            Renderer::SetViewport(saved_viewport_w, saved_viewport_h);
            desktop_resolution_saved = false;
            saved_output_w = 0;
            saved_output_h = 0;
        }

        if (restore_resolution_scale && resolution_scale_saved)
        {
            ConsoleRegistry::Get().SetValueFromString(
                "r.resolution_scale",
                to_string(saved_resolution_scale)
            );
            resolution_scale_saved = false;
        }
    }

    void Xr::SetStereoMode(bool enabled)
    {
        if (m_stereo_3d == enabled)
        {
            return;
        }

        m_stereo_3d = enabled;

        if (enabled)
        {
            // snapshot desktop before any hmd override, even if the session is not running yet
            SaveDesktopResolutionIfNeeded();
            ClampResolutionScaleForVr();

            if (m_session_running && m_recommended_width > 0 && m_recommended_height > 0)
            {
                ApplyHmdResolution();
            }
        }
        else
        {
            // always restore, previous path skipped this when session had already stopped
            RestoreDesktopResolution(true);
        }

        Renderer::RecreateRenderTargets();
    }

    bool Xr::IsSessionRunning()
    {
        return m_session_running;
    }

    bool Xr::IsSessionFocused()
    {
        return m_session_focused;
    }

    const string& Xr::GetRuntimeName()
    {
        return m_runtime_name;
    }

    const string& Xr::GetDeviceName()
    {
        return m_device_name;
    }

    uint32_t Xr::GetRecommendedWidth()
    {
        return m_recommended_width;
    }

    uint32_t Xr::GetRecommendedHeight()
    {
        return m_recommended_height;
    }

    const XrEyeView& Xr::GetEyeView(uint32_t eye_index)
    {
        SP_ASSERT(eye_index < eye_count);
        return m_eye_views[eye_index];
    }

    const math::Matrix& Xr::GetViewMatrix(uint32_t eye_index)
    {
        SP_ASSERT(eye_index < eye_count);
        return m_eye_views[eye_index].view;
    }

    const math::Matrix& Xr::GetProjectionMatrix(uint32_t eye_index)
    {
        SP_ASSERT(eye_index < eye_count);
        return m_eye_views[eye_index].projection;
    }

    const math::Vector3& Xr::GetHeadPosition()
    {
        return m_head_position;
    }

    const math::Quaternion& Xr::GetHeadOrientation()
    {
        return m_head_orientation;
    }
}
