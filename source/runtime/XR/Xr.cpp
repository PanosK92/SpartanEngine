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

    // api-agnostic accessor implementations, lifecycle and frame methods live in the per-rhi xr files
    bool Xr::IsHmdConnected()
    {
        return m_hmd_connected;
    }

    void Xr::SetStereoMode(bool enabled)
    {
        if (m_stereo_3d == enabled)
            return;

        m_stereo_3d = enabled;
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
