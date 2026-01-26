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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Queue.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

// forward declaration from d3d12_device.cpp
namespace spartan::d3d12_descriptors
{
    ID3D12CommandAllocator* GetGraphicsAllocator();
    ID3D12Fence* GetGraphicsFence();
    uint64_t& GetGraphicsFenceValue();
    HANDLE GetFenceEvent();
}

namespace spartan
{
    namespace
    {
        array<mutex, 3> mutexes;

        mutex& get_mutex(RHI_Queue* queue)
        {
            return mutexes[static_cast<uint32_t>(queue->GetType())];
        }
    }

    RHI_Queue::RHI_Queue(const RHI_Queue_Type queue_type, const char* name)
    {
        m_object_name = name;
        m_type        = queue_type;

        // get the d3d12 command queue
        m_rhi_resource = RHI_Device::GetQueueRhiResource(queue_type);

        // create command allocator for this queue
        D3D12_COMMAND_LIST_TYPE cmd_list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (queue_type == RHI_Queue_Type::Compute)
            cmd_list_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        else if (queue_type == RHI_Queue_Type::Copy)
            cmd_list_type = D3D12_COMMAND_LIST_TYPE_COPY;

        // create command lists for this queue
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cmd_lists.size()); i++)
        {
            // each command list needs its own allocator
            ID3D12CommandAllocator* allocator = nullptr;
            SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandAllocator(
                cmd_list_type, IID_PPV_ARGS(&allocator))),
                "Failed to create command allocator for command list");

            m_cmd_lists[i] = make_shared<RHI_CommandList>(this, allocator, (string("cmd_list_") + to_string(i)).c_str());
        }
    }

    RHI_Queue::~RHI_Queue()
    {
        Wait();

        // command lists will clean up their own resources
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cmd_lists.size()); i++)
        {
            m_cmd_lists[i] = nullptr;
        }
    }

    RHI_CommandList* RHI_Queue::NextCommandList()
    {
        m_index        = (m_index + 1) % static_cast<uint32_t>(m_cmd_lists.size());
        auto& cmd_list = m_cmd_lists[m_index];

        // submit any pending work
        if (cmd_list->GetState() == RHI_CommandListState::Recording)
        {
            cmd_list->Submit(nullptr, false);
        }

        // wait for the command list if it's still executing
        if (cmd_list->GetState() == RHI_CommandListState::Submitted)
        {
            cmd_list->WaitForExecution();
        }

        SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Idle);

        return cmd_list.get();
    }

    void RHI_Queue::Wait(const bool flush)
    {
        // ensure that any submitted command lists have completed execution
        for (auto& cmd_list : m_cmd_lists)
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

            // if we flushed, start recording again
            if (got_flushed && cmd_list)
            { 
                cmd_list->Begin();
            }
        }

        // wait for the queue itself
        lock_guard<mutex> lock(get_mutex(this));
        
        ID3D12CommandQueue* d3d12_queue = static_cast<ID3D12CommandQueue*>(m_rhi_resource);
        if (d3d12_queue)
        {
            ID3D12Fence* fence = d3d12_descriptors::GetGraphicsFence();
            uint64_t& fence_value = d3d12_descriptors::GetGraphicsFenceValue();
            HANDLE fence_event = d3d12_descriptors::GetFenceEvent();

            const uint64_t current_fence_value = fence_value;
            d3d12_queue->Signal(fence, current_fence_value);
            fence_value++;

            if (fence->GetCompletedValue() < current_fence_value)
            {
                fence->SetEventOnCompletion(current_fence_value, fence_event);
                WaitForSingleObject(fence_event, INFINITE);
            }
        }
    }

    void RHI_Queue::Submit(void* cmd_buffer, const uint32_t wait_flags, RHI_SyncPrimitive* semaphore_wait, RHI_SyncPrimitive* semaphore_signal, RHI_SyncPrimitive* semaphore_timeline_signal)
    {
        lock_guard<mutex> lock(get_mutex(this));

        ID3D12CommandQueue* d3d12_queue = static_cast<ID3D12CommandQueue*>(m_rhi_resource);
        ID3D12GraphicsCommandList* d3d12_cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_buffer);

        if (d3d12_queue && d3d12_cmd_list)
        {
            ID3D12CommandList* cmd_lists[] = { d3d12_cmd_list };
            d3d12_queue->ExecuteCommandLists(1, cmd_lists);
        }
    }

    bool RHI_Queue::Present(void* swapchain, const uint32_t image_index, RHI_SyncPrimitive* semaphore_wait)
    {
        // d3d12 doesn't use this - present is done directly on the swapchain
        return true;
    }
}
