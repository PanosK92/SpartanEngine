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

//= INCLUDES ===============
#include "pch.h"
#include "RHI_Device.h"
#include "RHI_SwapChain.h"
#include "RHI_Queue.h"
#include "RHI_CommandList.h"
#include "RHI_Texture.h"
//==========================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    // device properties
    float RHI_Device::m_timestamp_period                            = 0;
    uint64_t RHI_Device::m_min_uniform_buffer_offset_alignment      = 0;
    uint64_t RHI_Device::m_min_storage_buffer_offset_alignment      = 0;
    uint64_t RHI_Device::m_min_acceleration_buffer_offset_alignment = 0;
    uint32_t RHI_Device::m_max_texture_1d_dimension                 = 0;
    uint32_t RHI_Device::m_max_texture_2d_dimension                 = 0;
    uint32_t RHI_Device::m_max_texture_3d_dimension                 = 0;
    uint32_t RHI_Device::m_max_texture_cube_dimension               = 0;
    uint32_t RHI_Device::m_max_texture_array_layers                 = 0;
    uint32_t RHI_Device::m_max_push_constant_size                   = 0;
    uint32_t RHI_Device::m_max_shading_rate_texel_size_x            = 0;
    uint32_t RHI_Device::m_max_shading_rate_texel_size_y            = 0;
    uint64_t RHI_Device::m_optimal_buffer_copy_offset_alignment     = 0;
    uint32_t RHI_Device::m_shader_group_handle_size                 = 0;
    uint32_t RHI_Device::m_shader_group_handle_alignment            = 0;
    uint32_t RHI_Device::m_shader_group_base_alignment              = 0;
    bool RHI_Device::m_is_shading_rate_supported                    = false;
    bool RHI_Device::m_xess_supported                               = false;
    bool RHI_Device::m_dlss_supported                               = false;
    bool RHI_Device::m_is_ray_tracing_supported                     = false;
    bool RHI_Device::m_is_mesh_shaders_supported                    = false;
    void (*RHI_Device::m_pipeline_bound_callback)(RHI_CommandList*) = nullptr;
    void (*RHI_Device::m_default_push_constants_callback)(RHI_CommandList*) = nullptr;
    void (*RHI_Device::m_pass_reset_callback)() = nullptr;
    uint32_t (*RHI_Device::m_scale_dimension_callback)(uint32_t, float) = nullptr;
    RHI_Buffer* RHI_Device::m_dummy_vertex_buffer                   = nullptr;

    // misc
    bool RHI_Device::m_wide_lines                = false;
    bool RHI_Device::m_device_lost               = false;
    uint32_t RHI_Device::m_physical_device_index = 0;
    vector<RHI_PhysicalDevice> physical_devices;

    void RHI_Device::PhysicalDeviceRegister(const RHI_PhysicalDevice& physical_device)
    {
        // discrete devices come first
        vector<RHI_PhysicalDevice>::const_iterator iter = find_if(physical_devices.begin(), physical_devices.end(), [](const RHI_PhysicalDevice& device)
        {
            return device.GetType() != RHI_PhysicalDevice_Type::Discrete;
        });

        physical_devices.emplace(iter, physical_device);

        // sort devices by memory, in an ascending order, the type order will be maintained
        sort(physical_devices.begin(), physical_devices.end(), [](const RHI_PhysicalDevice& adapter1, const RHI_PhysicalDevice& adapter2)
        {
            return adapter1.GetMemory() > adapter2.GetMemory() && adapter1.GetType() == adapter2.GetType();
        });

        SP_LOG_INFO("%s (%d MB)", physical_device.GetName(), physical_device.GetMemory());
    }

    RHI_PhysicalDevice* RHI_Device::GetPrimaryPhysicalDevice()
    {
        SP_ASSERT_MSG(physical_devices.size() != 0, "No physical devices detected");
        SP_ASSERT_MSG(m_physical_device_index < physical_devices.size(), "Index out of bounds");

        return &physical_devices[m_physical_device_index];
    }

    void RHI_Device::PhysicalDeviceSetPrimary(const uint32_t index)
    {
        m_physical_device_index = index;

        if (const RHI_PhysicalDevice* physical_device = GetPrimaryPhysicalDevice())
        {
            SP_LOG_INFO("%s (%d MB)", physical_device->GetName(), physical_device->GetMemory());
        }
    }
 
    vector<RHI_PhysicalDevice>& RHI_Device::PhysicalDeviceGet()
    {
        return physical_devices;
    }

    bool RHI_Device::IsValidResolution(const uint32_t width, const uint32_t height)
    {
        return width  >= 4 && width  <= m_max_texture_2d_dimension &&
               height >= 4 && height <= m_max_texture_2d_dimension;
    }

    void RHI_Device::SetPipelineBoundCallback(void (*callback)(RHI_CommandList*))
    {
        m_pipeline_bound_callback = callback;
    }

    void RHI_Device::InvokePipelineBound(RHI_CommandList* cmd_list)
    {
        if (m_pipeline_bound_callback)
        {
            m_pipeline_bound_callback(cmd_list);
        }
    }

    void RHI_Device::SetDefaultPushConstantsCallback(void (*callback)(RHI_CommandList*))
    {
        m_default_push_constants_callback = callback;
    }

    void RHI_Device::InvokeDefaultPushConstants(RHI_CommandList* cmd_list)
    {
        if (m_default_push_constants_callback)
        {
            m_default_push_constants_callback(cmd_list);
        }
    }

    void RHI_Device::SetPassResetCallback(void (*callback)())
    {
        m_pass_reset_callback = callback;
    }

    void RHI_Device::InvokePassReset()
    {
        if (m_pass_reset_callback)
        {
            m_pass_reset_callback();
        }
    }

    void RHI_Device::SetScaleDimensionCallback(uint32_t (*callback)(uint32_t, float))
    {
        m_scale_dimension_callback = callback;
    }

    uint32_t RHI_Device::ScaleDimension(uint32_t dimension, float scale)
    {
        if (m_scale_dimension_callback)
        {
            return m_scale_dimension_callback(dimension, scale);
        }

        return dimension;
    }

    void RHI_Device::SetDummyVertexBuffer(RHI_Buffer* buffer)
    {
        m_dummy_vertex_buffer = buffer;
    }

    RHI_Buffer* RHI_Device::GetDummyVertexBuffer()
    {
        return m_dummy_vertex_buffer;
    }

    namespace
    {
        shared_ptr<RHI_SwapChain> frame_swapchain;
        RHI_CommandList* frame_lists[3] = {};
        thread_local RHI_Frame_List frame_bound      = RHI_Frame_List::Graphics;
        thread_local RHI_CommandList* frame_override = nullptr;
        RHI_SyncPrimitive* frame_wait_timeline = nullptr;
        uint64_t frame_wait_value              = 0;

        uint32_t frame_list_index(RHI_Frame_List list)
        {
            return static_cast<uint32_t>(list);
        }

        RHI_Queue* frame_queue(RHI_Frame_List list)
        {
            if (list == RHI_Frame_List::Graphics)
            {
                return RHI_Device::GetQueue(RHI_Queue_Type::Graphics);
            }
            return RHI_Device::GetQueue(RHI_Queue_Type::Compute);
        }
    }

    void RHI_Device::CreateSwapChain(
        void* sdl_window,
        uint32_t width,
        uint32_t height,
        RHI_Present_Mode present_mode,
        uint32_t buffer_count,
        bool hdr,
        const char* name
    )
    {
        frame_swapchain = make_shared<RHI_SwapChain>(
            sdl_window,
            width,
            height,
            present_mode,
            buffer_count,
            hdr,
            name
        );
    }

    void RHI_Device::DestroySwapChain()
    {
        frame_swapchain = nullptr;
    }

    RHI_SwapChain* RHI_Device::GetSwapChain()
    {
        return frame_swapchain.get();
    }

    void RHI_Device::AcquireSwapChainImage()
    {
        if (frame_swapchain && !frame_swapchain->IsImageAcquired())
        {
            frame_swapchain->AcquireNextImage();
        }
    }

    void RHI_Device::BlitToBackBuffer(RHI_Texture* texture)
    {
        RHI_CommandList* cmd_list = Cmd();
        if (!cmd_list || !texture || !frame_swapchain)
        {
            return;
        }
        cmd_list->begin_marker("blit_to_back_buffer");
        cmd_list->blit(texture, frame_swapchain.get());
        cmd_list->end_marker();
    }

    void RHI_Device::BeginFrame(bool acquire_compute)
    {
        frame_override = nullptr;
        frame_lists[frame_list_index(RHI_Frame_List::Graphics)] = GetQueue(RHI_Queue_Type::Graphics)->NextCommandList();
        frame_lists[frame_list_index(RHI_Frame_List::Graphics)]->Begin();
        frame_lists[frame_list_index(RHI_Frame_List::ComputeA)] = nullptr;
        frame_lists[frame_list_index(RHI_Frame_List::ComputeB)] = nullptr;
        if (acquire_compute)
        {
            RHI_Queue* queue_compute = GetQueue(RHI_Queue_Type::Compute);
            frame_lists[frame_list_index(RHI_Frame_List::ComputeA)] = queue_compute->NextCommandList();
            frame_lists[frame_list_index(RHI_Frame_List::ComputeA)]->Begin();
            frame_lists[frame_list_index(RHI_Frame_List::ComputeB)] = queue_compute->NextCommandList();
            frame_lists[frame_list_index(RHI_Frame_List::ComputeB)]->Begin();
        }
        Bind(RHI_Frame_List::Graphics);
    }

    RHI_CommandList* RHI_Device::Cmd()
    {
        if (frame_override)
        {
            return frame_override;
        }
        return frame_lists[frame_list_index(frame_bound)];
    }

    RHI_CommandList* RHI_Device::Cmd(RHI_Frame_List list)
    {
        return frame_lists[frame_list_index(list)];
    }

    bool RHI_Device::IsRecording()
    {
        RHI_CommandList* cmd_list = Cmd();
        return cmd_list && cmd_list->GetState() == RHI_CommandListState::Recording;
    }

    bool RHI_Device::IsRecording(RHI_Frame_List list)
    {
        RHI_CommandList* cmd_list = Cmd(list);
        return cmd_list && cmd_list->GetState() == RHI_CommandListState::Recording;
    }

    RHI_Queue_Type RHI_Device::GetBoundQueueType()
    {
        RHI_CommandList* cmd_list = Cmd();
        if (!cmd_list || !cmd_list->GetQueue())
        {
            return RHI_Queue_Type::Max;
        }
        return cmd_list->GetQueue()->GetType();
    }

    void RHI_Device::Bind(RHI_Frame_List list)
    {
        frame_override = nullptr;
        frame_bound = list;
    }

    void RHI_Device::Bind(RHI_CommandList* cmd_list)
    {
        frame_override = cmd_list;
    }

    RHI_Work RHI_Device::Submit(
        RHI_Frame_List list,
        RHI_SyncPrimitive* semaphore_wait,
        bool is_immediate,
        RHI_SyncPrimitive* semaphore_signal,
        RHI_SyncPrimitive* semaphore_timeline_wait,
        uint64_t timeline_wait_value
    )
    {
        RHI_CommandList* cmd_list = frame_lists[frame_list_index(list)];
        if (!cmd_list)
        {
            return {};
        }
        cmd_list->Submit(
            semaphore_wait,
            is_immediate,
            semaphore_signal,
            semaphore_timeline_wait,
            timeline_wait_value
        );
        // pipeline bind pins this list as the override, drop it so cmd() cannot hit a submitted list
        if (frame_override == cmd_list)
        {
            frame_override = nullptr;
        }
        RHI_Work work;
        work.timeline = cmd_list->GetTimelineSemaphore();
        work.value    = cmd_list->GetLastTimelineSignalValue();
        if (list == RHI_Frame_List::Graphics)
        {
            frame_lists[frame_list_index(RHI_Frame_List::Graphics)] = frame_queue(list)->NextCommandList();
            frame_lists[frame_list_index(RHI_Frame_List::Graphics)]->Begin();
        }
        return work;
    }

    void RHI_Device::SetFrameWait(RHI_SyncPrimitive* timeline_wait, uint64_t timeline_value)
    {
        frame_wait_timeline = timeline_wait;
        frame_wait_value    = timeline_value;
    }

    RHI_Work RHI_Device::EndFrame(RHI_SyncPrimitive* timeline_wait, uint64_t timeline_value)
    {
        if (!timeline_wait)
        {
            timeline_wait = frame_wait_timeline;
            timeline_value = frame_wait_value;
        }
        frame_wait_timeline = nullptr;
        frame_wait_value    = 0;
        RHI_CommandList* cmd_list = frame_lists[frame_list_index(RHI_Frame_List::Graphics)];
        if (!cmd_list)
        {
            return {};
        }
        SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Recording);
        if (frame_swapchain && frame_swapchain->IsImageAcquired())
        {
            cmd_list->render_pass_end();
            cmd_list->prepare_for_present(frame_swapchain.get());
            cmd_list->Submit(
                frame_swapchain->GetImageAcquiredSemaphore(),
                false,
                frame_swapchain->GetRenderingCompleteSemaphore(),
                timeline_wait,
                timeline_value
            );
            frame_swapchain->Present(cmd_list);
        }
        else
        {
            cmd_list->Submit(nullptr, true, nullptr, timeline_wait, timeline_value);
        }
        frame_lists[frame_list_index(RHI_Frame_List::Graphics)] = cmd_list;
        RHI_Work work;
        work.timeline = cmd_list->GetTimelineSemaphore();
        work.value    = cmd_list->GetLastTimelineSignalValue();
        return work;
    }
}
