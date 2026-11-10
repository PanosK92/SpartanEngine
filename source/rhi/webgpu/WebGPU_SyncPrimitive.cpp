/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../RHI_SyncPrimitive.h"
#include "../RHI_Device.h"
#include "WebGPU_Internal.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // webgpu orders every queue operation after the work submitted before it, so the cpu never has to wait on
    // the gpu to reuse memory, a primitive is signalled the moment its submission is handed to the queue
    namespace
    {
        atomic<uint64_t>& get_signaled(void* resource)
        {
            return *static_cast<atomic<uint64_t>*>(resource);
        }
    }

    RHI_SyncPrimitive::RHI_SyncPrimitive(const RHI_SyncPrimitive_Type type, const char* name)
    {
        m_type         = type;
        m_object_name  = name ? name : "";
        m_value        = 0;
        m_rhi_resource = new atomic<uint64_t>(0);
    }

    RHI_SyncPrimitive::~RHI_SyncPrimitive()
    {
        delete static_cast<atomic<uint64_t>*>(m_rhi_resource);
        m_rhi_resource = nullptr;
    }

    void RHI_SyncPrimitive::Wait(const uint64_t timeout)
    {
        Wait(timeout, GetValue());
    }

    void RHI_SyncPrimitive::Wait(const uint64_t timeout, const uint64_t value)
    {
        webgpu::process_events();
    }

    void RHI_SyncPrimitive::Signal(const uint64_t value)
    {
        m_value = value;
        atomic<uint64_t>& signaled = get_signaled(m_rhi_resource);
        uint64_t current           = signaled.load();
        while (current < value && !signaled.compare_exchange_weak(current, value))
        {
        }
    }

    bool RHI_SyncPrimitive::IsSignaled()
    {
        return IsSignaled(GetValue());
    }

    bool RHI_SyncPrimitive::IsSignaled(uint64_t value)
    {
        return get_signaled(m_rhi_resource).load() >= value;
    }

    void RHI_SyncPrimitive::Reset()
    {
    }
}
