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

//= INCLUDES =====================
#include "pch.h"
#include "../../XR/Xr.h"
//================================

namespace spartan
{
    // d3d12 xr stubs, openxr's d3d12 graphics binding is not wired up yet so every
    // entry point is a no-op that keeps the engine running without xr support

    void Xr::Initialize()
    {
        m_initialized.store(true);
    }

    void Xr::Shutdown()
    {
        m_initialized.store(false);
    }

    void Xr::Tick()
    {
    }

    void Xr::InitializeWorker()
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

    void* Xr::GetSwapchainImageView()
    {
        return nullptr;
    }

    uint32_t Xr::GetSwapchainImageIndex()
    {
        return 0;
    }

    uint32_t Xr::GetSwapchainLength()
    {
        return 0;
    }

    bool Xr::IsMultiviewSupported()
    {
        return false;
    }
}
