/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Queue.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../RHI_SyncPrimitive.h"
#include "../../profiling/Profiler.h"
#include "WebGPU_Internal.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        uint32_t command_list_count(const RHI_Queue_Type type)
        {
            // a frame submits up to ~8 graphics and ~5 compute lists, the rings must cover every frame in flight
            if (type == RHI_Queue_Type::Graphics)
                return 32;

            if (type == RHI_Queue_Type::Compute)
                return 24;

            return 4;
        }
    }

    RHI_Queue::RHI_Queue(const RHI_Queue_Type queue_type, const char* name)
    {
        m_object_name  = name;
        m_type         = queue_type;
        m_rhi_resource = webgpu::queue();

        // webgpu has one queue, every rhi queue records into its own lists and submits to it
        m_cmd_lists.resize(command_list_count(queue_type));
        m_index = static_cast<uint32_t>(m_cmd_lists.size() - 1);
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cmd_lists.size()); i++)
        {
            m_cmd_lists[i] = make_shared<RHI_CommandList>(this, m_rhi_resource, (string(name) + "_cmd_list_" + to_string(i)).c_str());
        }
    }

    RHI_Queue::~RHI_Queue()
    {
        Wait();

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cmd_lists.size()); i++)
        {
            m_cmd_lists[i] = nullptr;
        }
    }

    RHI_CommandList* RHI_Queue::NextCommandList()
    {
        const uint32_t count       = static_cast<uint32_t>(m_cmd_lists.size());
        const uint32_t first_index = (m_index.load() + 1) % count;

        for (uint32_t offset = 0; offset < count; offset++)
        {
            const uint32_t index                  = (first_index + offset) % count;
            shared_ptr<RHI_CommandList>& cmd_list = m_cmd_lists[index];

            if (cmd_list->GetState() == RHI_CommandListState::Idle)
            {
                m_index = index;
                return cmd_list.get();
            }

            if (cmd_list->GetState() == RHI_CommandListState::Submitted)
            {
                cmd_list->WaitForExecution();
                m_index = index;
                return cmd_list.get();
            }
        }

        // every list is recording, the oldest one goes out so it can be reused
        shared_ptr<RHI_CommandList>& cmd_list = m_cmd_lists[first_index];
        cmd_list->Submit(nullptr, false);
        cmd_list->WaitForExecution();
        SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Idle);

        m_index = first_index;
        return cmd_list.get();
    }

    void RHI_Queue::Wait(const bool flush)
    {
        for (shared_ptr<RHI_CommandList>& cmd_list : m_cmd_lists)
        {
            bool got_flushed = false;
            if (cmd_list && cmd_list->GetState() == RHI_CommandListState::Recording && flush)
            {
                cmd_list->Submit(nullptr, false);
                got_flushed = true;
            }

            if (cmd_list && cmd_list->GetState() == RHI_CommandListState::Submitted)
            {
                cmd_list->WaitForExecution();
            }

            if (got_flushed && cmd_list)
            {
                cmd_list->Begin();
            }
        }

        webgpu::process_events();
    }

    uint64_t RHI_Queue::Submit(
        void* cmd_buffer, const uint32_t wait_flags,
        RHI_SyncPrimitive* semaphore_wait, RHI_SyncPrimitive* semaphore_signal, RHI_SyncPrimitive* semaphore_timeline_signal,
        RHI_SyncPrimitive* semaphore_timeline_wait, uint64_t timeline_wait_value, uint64_t* submission_order
    )
    {
        ScopedTimeBlock time_block("queue_submit");

        // the caller holds the submission lock, see RHI_CommandList::Submit
        WGPUCommandBuffer command_buffer = static_cast<WGPUCommandBuffer>(cmd_buffer);
        if (command_buffer)
        {
            wgpuQueueSubmit(webgpu::queue(), 1, &command_buffer);
        }

        if (semaphore_signal)
        {
            semaphore_signal->Signal(semaphore_signal->GetNextSignalValue());
        }

        uint64_t timeline_signal_value = 0;
        if (semaphore_timeline_signal)
        {
            // the list reserved this value when it began recording
            timeline_signal_value = semaphore_timeline_signal->GetValue();
            semaphore_timeline_signal->Signal(timeline_signal_value);
        }

        if (submission_order)
        {
            *submission_order = ++m_submission_order;
        }

        return timeline_signal_value;
    }

    bool RHI_Queue::Present(void* swapchain, const uint32_t image_index, RHI_SyncPrimitive* semaphore_wait)
    {
        // RHI_SwapChain::Present presents the surface
        return true;
    }
}
