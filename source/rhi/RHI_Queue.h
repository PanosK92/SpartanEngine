/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include "../core/SpartanObject.h"
#include "RHI_Definitions.h"
#include "RHI_CommandList.h"
#include <vector>
//================================

namespace spartan
{
    class RHI_Queue : public SpartanObject
    {
    public:
        RHI_Queue(const RHI_Queue_Type queue_type, const char* name);
        ~RHI_Queue();

        void Wait(const bool flush = false);
        uint64_t Submit(
            void* cmd_buffer, const uint32_t wait_flags,
            RHI_SyncPrimitive* semaphore_wait, RHI_SyncPrimitive* semaphore_signal, RHI_SyncPrimitive* semaphore_timeline_signal,
            RHI_SyncPrimitive* semaphore_timeline_wait = nullptr, uint64_t timeline_wait_value = 0, uint64_t* submission_order = nullptr
        );
        bool Present(void* swapchain, const uint32_t image_index, RHI_SyncPrimitive* semaphore_wait);
        RHI_CommandList* NextCommandList();
        RHI_Queue_Type GetType() const { return m_type; }

    private:
        std::vector<
            std::shared_ptr<RHI_CommandList>
        > m_cmd_lists;
        void* m_rhi_resource                                        = nullptr;
        std::atomic<uint32_t> m_index                               = 0;
        uint64_t m_submission_order = 0; // protected by the queue submission mutex
        RHI_Queue_Type m_type                                       = RHI_Queue_Type::Max;
    };
}
