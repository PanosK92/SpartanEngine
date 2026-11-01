/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../../xr/Xr.h"
//=============================

namespace spartan
{
    // openxr has no metal binding, xr stays unavailable
    void Xr::InitializeWorker()
    {
    }

    void Xr::Initialize()
    {
    }

    void Xr::Shutdown()
    {
    }

    void Xr::Tick()
    {
    }

    bool Xr::CreateSession()
    {
        return false;
    }

    void Xr::DestroySession()
    {
    }

    bool Xr::CreateSwapchain()
    {
        return false;
    }

    void Xr::DestroySwapchain()
    {
    }

    bool Xr::CreateReferenceSpace()
    {
        return false;
    }

    void Xr::ProcessEvents()
    {
    }

    void Xr::UpdateViews()
    {
    }

    bool Xr::BeginFrame()
    {
        return false;
    }

    void Xr::EndFrame()
    {
    }

    bool Xr::AcquireSwapchainImage()
    {
        return false;
    }

    void Xr::ReleaseSwapchainImage()
    {
    }

    bool Xr::IsAvailable()
    {
        return false;
    }

    void* Xr::GetSwapchainImage()
    {
        return nullptr;
    }

    uint32_t Xr::GetSwapchainImageIndex()
    {
        return 0;
    }

    bool Xr::IsMultiviewSupported()
    {
        return false;
    }
}
