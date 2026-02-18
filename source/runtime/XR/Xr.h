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

//= INCLUDES ===================
#include <cstdint>
#include <string>
#include <array>
#include "../Math/Matrix.h"
#include "../Math/Vector3.h"
#include "../Math/Quaternion.h"
//==============================

namespace spartan
{
    // forward declarations
    class RHI_Texture;

    // per-eye view data
    struct XrEyeView
    {
        math::Matrix view;
        math::Matrix projection;
        math::Vector3 position;
        math::Quaternion orientation;
        float fov_left;
        float fov_right;
        float fov_up;
        float fov_down;
    };

    class Xr
    {
    public:
        // lifecycle
        static void Initialize();
        static void Shutdown();
        static void Tick();

        // frame management (call these to bracket xr rendering)
        static bool BeginFrame();  // returns false if frame should be skipped
        static void EndFrame();

        // state
        static bool IsAvailable();
        static bool IsHmdConnected();
        static bool IsSessionRunning();
        static bool IsSessionFocused();

        // properties
        static const std::string& GetRuntimeName();
        static const std::string& GetDeviceName();
        static uint32_t GetRecommendedWidth();
        static uint32_t GetRecommendedHeight();

        // view data for rendering (call after BeginFrame)
        static const XrEyeView& GetEyeView(uint32_t eye_index);
        static const math::Matrix& GetViewMatrix(uint32_t eye_index);
        static const math::Matrix& GetProjectionMatrix(uint32_t eye_index);
        static const math::Vector3& GetHeadPosition();
        static const math::Quaternion& GetHeadOrientation();

        // swapchain access for rendering
        static void* GetSwapchainImage();        // returns native image handle (array texture with 2 layers)
        static void* GetSwapchainImageView();    // returns native image view handle for the array
        static uint32_t GetSwapchainImageIndex();
        static uint32_t GetSwapchainLength();
        static bool AcquireSwapchainImage();
        static void ReleaseSwapchainImage();

        // multiview configuration
        static constexpr uint32_t eye_count = 2;
        static bool IsMultiviewSupported();

        // stereo mode (2D uses center pose for both eyes, 3D uses per-eye poses)
        static void SetStereoMode(bool enabled) { m_stereo_3d = enabled; }
        static bool GetStereoMode()             { return m_stereo_3d; }

    private:
        static bool CreateSession();
        static void DestroySession();
        static bool CreateSwapchain();
        static void DestroySwapchain();
        static bool CreateReferenceSpace();
        static void ProcessEvents();
        static void UpdateViews();

        // state
        static bool m_initialized;
        static bool m_hmd_connected;
        static bool m_session_running;
        static bool m_session_focused;
        static bool m_frame_began;

        // info
        static std::string m_runtime_name;
        static std::string m_device_name;
        static uint32_t m_recommended_width;
        static uint32_t m_recommended_height;

        // view data
        static std::array<XrEyeView, eye_count> m_eye_views;
        static math::Vector3 m_head_position;
        static math::Quaternion m_head_orientation;

        // stereo mode
        static bool m_stereo_3d;
    };
}
