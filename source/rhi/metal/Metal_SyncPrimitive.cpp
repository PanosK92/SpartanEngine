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
#include "Metal_Internal.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // a shared event covers fences, binary and timeline semaphores, like an id3d12fence does
    // its value only grows, a signaled state is any value at or above the one waited on

    RHI_SyncPrimitive::RHI_SyncPrimitive(const RHI_SyncPrimitive_Type type, const char* name)
    {
        m_type        = type;
        m_object_name = name ? name : "";
        m_value       = 0;

        MTL::SharedEvent* event = RHI_Context::device->newSharedEvent();
        SP_ASSERT_MSG(event != nullptr, "Failed to create shared event");
        metal_utility::set_label(event, m_object_name.c_str());
        m_rhi_resource = event;
    }

    RHI_SyncPrimitive::~RHI_SyncPrimitive()
    {
        static_cast<MTL::SharedEvent*>(m_rhi_resource)->release();
        m_rhi_resource = nullptr;
    }

    void RHI_SyncPrimitive::Wait(const uint64_t timeout)
    {
        Wait(timeout, GetValue());
    }

    void RHI_SyncPrimitive::Wait(const uint64_t timeout, const uint64_t value)
    {
        const uint64_t timeout_ms = timeout == UINT64_MAX ? UINT64_MAX : timeout / 1000000ull;
        static_cast<MTL::SharedEvent*>(m_rhi_resource)->waitUntilSignaledValue(value, timeout_ms);
    }

    void RHI_SyncPrimitive::Signal(const uint64_t value)
    {
        m_value = value;
        static_cast<MTL::SharedEvent*>(m_rhi_resource)->setSignaledValue(value);
    }

    bool RHI_SyncPrimitive::IsSignaled()
    {
        return IsSignaled(GetValue());
    }

    bool RHI_SyncPrimitive::IsSignaled(uint64_t value)
    {
        return static_cast<MTL::SharedEvent*>(m_rhi_resource)->signaledValue() >= value;
    }

    void RHI_SyncPrimitive::Reset()
    {
        // shared events cannot go backwards, state only advances through signals
    }
}
