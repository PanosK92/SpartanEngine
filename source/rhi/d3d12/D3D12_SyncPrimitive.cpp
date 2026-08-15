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

//= INCLUDES ====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_SyncPrimitive.h"
#include "../RHI_Device.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // d3d12 has a single primitive (id3d12fence) that covers fence + (timeline-)semaphore semantics
    // a fence value of 0 means unsignaled; signalled values are tracked via m_value on the primitive

    RHI_SyncPrimitive::RHI_SyncPrimitive(const RHI_SyncPrimitive_Type type, const char* name)
    {
        m_type        = type;
        m_object_name = name ? name : "";
        m_value       = 0;

        ID3D12Fence* fence = nullptr;
        if (RHI_Context::device)
        {
            HRESULT hr = RHI_Context::device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
            if (FAILED(hr))
            {
                m_rhi_resource = nullptr;
                return;
            }
        }

        m_rhi_resource = fence;
    }

    RHI_SyncPrimitive::~RHI_SyncPrimitive()
    {
        if (m_rhi_resource)
        {
            static_cast<ID3D12Fence*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }
    }

    void RHI_SyncPrimitive::Wait(const uint64_t timeout)
    {
        Wait(timeout, GetValue());
    }

    void RHI_SyncPrimitive::Wait(const uint64_t timeout, const uint64_t value)
    {
        if (!m_rhi_resource)
        {
            return;
        }

        ID3D12Fence* fence = static_cast<ID3D12Fence*>(m_rhi_resource);
        if (fence->GetCompletedValue() >= value)
        {
            return;
        }

        HANDLE event_handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!event_handle)
        {
            return;
        }

        if (SUCCEEDED(fence->SetEventOnCompletion(value, event_handle)))
        {
            // d3d12 waits in milliseconds, vulkan callers pass nanoseconds, scale appropriately
            DWORD timeout_ms = INFINITE;
            if (timeout != UINT64_MAX)
            {
                uint64_t ms = timeout / 1000000ull;
                timeout_ms  = ms > MAXDWORD ? INFINITE : static_cast<DWORD>(ms);
            }
            WaitForSingleObject(event_handle, timeout_ms);
        }

        CloseHandle(event_handle);

        // a fence value of uint64 max signals the gpu was removed, report it like the vulkan device-lost path
        if (fence->GetCompletedValue() == UINT64_MAX)
        {
            RHI_Device::SetDeviceLost();
        }
    }

    void RHI_SyncPrimitive::Signal(const uint64_t value)
    {
        if (!m_rhi_resource)
        {
            return;
        }

        m_value = value;
        static_cast<ID3D12Fence*>(m_rhi_resource)->Signal(value);
    }

    bool RHI_SyncPrimitive::IsSignaled()
    {
        if (!m_rhi_resource)
        {
            return true;
        }

        return static_cast<ID3D12Fence*>(m_rhi_resource)->GetCompletedValue() >= GetValue();
    }

    void RHI_SyncPrimitive::Reset()
    {
        // d3d12 fences cannot decrement, monotonic increments via Signal are how state advances
        // callers that previously relied on explicit reset semantics get a no-op here, value stays as-is
    }
}
