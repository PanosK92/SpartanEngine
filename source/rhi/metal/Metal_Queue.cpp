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
#include "Metal_Internal.h"
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

        // drains the native queue, signalled after everything committed so far
        MTL::SharedEvent* idle_event = nullptr;
        uint64_t idle_value          = 0;
    }

    RHI_Queue::RHI_Queue(const RHI_Queue_Type queue_type, const char* name)
    {
        m_object_name  = name;
        m_type         = queue_type;
        m_rhi_resource = metal_queue::Get();

        m_cmd_lists.resize(command_list_count(queue_type));
        m_index = static_cast<uint32_t>(m_cmd_lists.size() - 1);
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cmd_lists.size()); i++)
        {
            // each command list records with its own allocator, a reset only reclaims memory once the gpu is done
            MTL4::CommandAllocator* allocator = RHI_Context::device->newCommandAllocator();
            SP_ASSERT_MSG(allocator != nullptr, "Failed to create command allocator");

            m_cmd_lists[i] = make_shared<RHI_CommandList>(this, allocator, (string(name) + "_cmd_list_" + to_string(i)).c_str());
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
        uint32_t submitted_index   = count;

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
                if (cmd_list->IsExecutionComplete())
                {
                    cmd_list->WaitForExecution();
                    m_index = index;
                    return cmd_list.get();
                }

                // lists are reserved before they are submitted, ring order is not gpu order
                if (submitted_index == count || cmd_list->GetSubmissionOrder() < m_cmd_lists[submitted_index]->GetSubmissionOrder())
                {
                    submitted_index = index;
                }
            }
        }

        const uint32_t index                  = submitted_index != count ? submitted_index : first_index;
        shared_ptr<RHI_CommandList>& cmd_list = m_cmd_lists[index];
        if (cmd_list->GetState() == RHI_CommandListState::Recording)
        {
            cmd_list->Submit(nullptr, false);
        }

        if (cmd_list->GetState() == RHI_CommandListState::Submitted)
        {
            ScopedTimeBlock time_block("cmd_wait");
            cmd_list->WaitForExecution();
        }

        SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Idle);

        m_index = index;
        return cmd_list.get();
    }

    void RHI_Queue::Wait(const bool flush)
    {
        ScopedTimeBlock time_block("queue_wait_idle");

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

        // the native queue is shared, so also drain work committed outside these command lists
        uint64_t value = 0;
        {
            lock_guard<mutex> lock(metal_queue::GetMutex());
            if (!idle_event)
            {
                idle_event = RHI_Context::device->newSharedEvent();
            }
            value = ++idle_value;
            metal_queue::Get()->signalEvent(idle_event, value);
        }
        idle_event->waitUntilSignaledValue(value, UINT64_MAX);
    }

    uint64_t RHI_Queue::Submit(
        void* cmd_buffer, const uint32_t wait_flags,
        RHI_SyncPrimitive* semaphore_wait, RHI_SyncPrimitive* semaphore_signal, RHI_SyncPrimitive* semaphore_timeline_signal,
        RHI_SyncPrimitive* semaphore_timeline_wait, uint64_t timeline_wait_value, uint64_t* submission_order
    )
    {
        ScopedTimeBlock time_block("queue_submit");
        lock_guard<mutex> lock(metal_queue::GetMutex());

        MTL4::CommandQueue* queue           = metal_queue::Get();
        MTL4::CommandBuffer* command_buffer = static_cast<MTL4::CommandBuffer*>(cmd_buffer);

        // allocations created since the last commit become resident
        metal_residency::Commit();

        if (semaphore_wait)
        {
            queue->wait(static_cast<MTL::SharedEvent*>(semaphore_wait->GetRhiResource()), semaphore_wait->GetValue());
        }
        if (semaphore_timeline_wait && timeline_wait_value > 0)
        {
            queue->wait(static_cast<MTL::SharedEvent*>(semaphore_timeline_wait->GetRhiResource()), timeline_wait_value);
        }

        // metal 4 reports gpu faults only through commit feedback
        MTL4::CommitOptions* options = MTL4::CommitOptions::alloc()->init();
        options->addFeedbackHandler([](MTL4::CommitFeedback* feedback)
        {
            if (NS::Error* error = feedback->error())
            {
                SP_LOG_ERROR("GPU error: %s", error->localizedDescription()->utf8String());
            }
        });
        const MTL4::CommandBuffer* command_buffers[] = { command_buffer };
        queue->commit(command_buffers, 1, options);
        options->release();

        if (semaphore_signal)
        {
            queue->signalEvent(static_cast<MTL::SharedEvent*>(semaphore_signal->GetRhiResource()), semaphore_signal->GetNextSignalValue());
        }

        uint64_t timeline_signal_value = 0;
        if (semaphore_timeline_signal)
        {
            timeline_signal_value = semaphore_timeline_signal->GetValue();
            queue->signalEvent(static_cast<MTL::SharedEvent*>(semaphore_timeline_signal->GetRhiResource()), timeline_signal_value);
        }

        if (submission_order)
        {
            *submission_order = ++m_submission_order;
        }

        return timeline_signal_value;
    }

    bool RHI_Queue::Present(void* swapchain, const uint32_t image_index, RHI_SyncPrimitive* semaphore_wait)
    {
        // the drawable is signalled and presented by RHI_SwapChain::Present, ordering on the shared queue covers the wait
        return true;
    }
}
