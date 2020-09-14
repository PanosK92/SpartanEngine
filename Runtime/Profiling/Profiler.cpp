/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =========================
#include "Spartan.h"
#include "Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Implementation.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Profiler::Profiler(Context* context) : ISubsystem(context)
    {
        m_time_blocks_read.reserve(m_time_block_capacity);
        m_time_blocks_read.resize(m_time_block_capacity);
        m_time_blocks_write.reserve(m_time_block_capacity);
        m_time_blocks_write.resize(m_time_block_capacity);
    }

    Profiler::~Profiler()
    {
        if (m_profile) OnFrameEnd();
        m_time_blocks_write.clear();
        m_time_blocks_read.clear();
        ClearRhiMetrics();
    }

    bool Profiler::Initialize()
    {
        m_resource_manager    = m_context->GetSubsystem<ResourceCache>();
        m_renderer            = m_context->GetSubsystem<Renderer>();
        m_timer             = m_context->GetSubsystem<Timer>();

        return true;
    }

    void Profiler::Tick(float delta_time)
    {
        if (!m_renderer)
            return;

        RHI_Device* rhi_device = m_renderer->GetRhiDevice().get();
        if (!rhi_device || !rhi_device->GetContextRhi()->profiler)
            return;

        if (m_increase_capacity)
        {
            OnFrameEnd();

            const uint32_t new_size = m_time_block_count + 100;
            m_time_blocks_read.reserve(new_size);
            m_time_blocks_read.resize(new_size);
            m_time_blocks_write.reserve(new_size);
            m_time_blocks_write.resize(new_size);
            LOG_WARNING("Time block list has grown to fit %d commands. Consider making the capacity larger to avoid re-allocations.", m_time_block_count + 1);
            m_increase_capacity = false;
            m_profile = true;
        }
        else
        {
            if (m_profile)
            {
                OnFrameEnd();
            }
        }

        // Compute fps
        ComputeFps(delta_time);

        // Check whether we should profile or not
        m_time_since_profiling_sec += delta_time;
        if (m_time_since_profiling_sec >= m_profiling_interval_sec)
        {
            m_time_since_profiling_sec  = 0.0f;
            m_profile                   = true;
        }
        else if (m_profile)
        {
            m_profile = false;
        }

        // Updating every m_profiling_interval_sec
        if (m_profile)
        {
            AcquireGpuData();

            // Create a string version of the rhi metrics
            if (m_renderer->GetOptions() & Render_Debug_PerformanceMetrics)
            {
                UpdateRhiMetricsString();
            }
        }

        ClearRhiMetrics();
    }

    void Profiler::OnFrameEnd()
    {
        // Clear time blocks
        {
            uint32_t pass_index_gpu = 0;

            for (uint32_t i = 0; i < m_time_block_count; i++)
            {
                TimeBlock& time_block = m_time_blocks_write[i];

                if (time_block.IsComplete())
                {
                    // Must not happen when TimeBlockEnd() ends as D3D11 waits
                    // too much for the results to be ready, which increases CPU time.
                    time_block.ComputeDuration(pass_index_gpu);
                    if (time_block.GetType() == TimeBlock_Gpu)
                    {
                        pass_index_gpu += 2;
                    }

                    m_time_blocks_read[i] = time_block;
                }
                else
                {
                    LOG_WARNING("TimeBlockEnd() was not called for time block \"%s\"", time_block.GetName());
                }
                
                time_block.Reset();
            }

            m_time_block_count = 0;
        }

        // Detect stutters
        float frames_to_accumulate  = 5.0f;
        float delta_feedback        = 1.0f / frames_to_accumulate;
        m_is_stuttering_cpu         = m_time_cpu_last > (m_time_cpu_avg + m_stutter_delta_ms);
        m_is_stuttering_gpu         = m_time_gpu_last > (m_time_gpu_avg + m_stutter_delta_ms);

        // Compute cpu and gpu times
        {
            frames_to_accumulate    = 20.0f;
            delta_feedback          = 1.0f / frames_to_accumulate;
            m_time_cpu_last         = 0.0f;
            m_time_gpu_last         = 0.0f;

            for (const TimeBlock& time_block : m_time_blocks_read)
            {
                if (!time_block.IsComplete())
                    continue;

                if (!time_block.GetParent() && time_block.GetType() == TimeBlock_Cpu)
                {
                    m_time_cpu_last += time_block.GetDuration();
                }

                if (!time_block.GetParent() && time_block.GetType() == TimeBlock_Gpu)
                {
                    m_time_gpu_last += time_block.GetDuration();
                }
            }

            // CPU
            m_time_cpu_avg = m_time_cpu_avg * (1.0f - delta_feedback) + m_time_cpu_last * delta_feedback;
            m_time_cpu_min = Math::Helper::Min(m_time_cpu_min, m_time_cpu_last);
            m_time_cpu_max = Math::Helper::Max(m_time_cpu_max, m_time_cpu_last);

            // GPU
            m_time_gpu_avg = m_time_gpu_avg * (1.0f - delta_feedback) + m_time_gpu_last * delta_feedback;
            m_time_gpu_min = Math::Helper::Min(m_time_gpu_min, m_time_gpu_last);
            m_time_gpu_max = Math::Helper::Max(m_time_gpu_max, m_time_gpu_last);

            // Frame
            m_time_frame_last   = static_cast<float>(m_timer->GetDeltaTimeMs());
            m_time_frame_avg    = m_time_frame_avg * (1.0f - delta_feedback) + m_time_frame_last * delta_feedback;
            m_time_frame_min    = Math::Helper::Min(m_time_frame_min, m_time_frame_last);
            m_time_frame_max    = Math::Helper::Max(m_time_frame_max, m_time_frame_last);
        }
    }

    void Profiler::TimeBlockStart(const char* func_name, TimeBlock_Type type, RHI_CommandList* cmd_list /*= nullptr*/)
    {
        if (!m_profile)
            return;

        const bool can_profile_cpu = (type == TimeBlock_Cpu) && m_profile_cpu_enabled;
        const bool can_profile_gpu = (type == TimeBlock_Gpu) && m_profile_gpu_enabled;

        if (!can_profile_cpu && !can_profile_gpu)
            return;

        // Last incomplete block of the same type, is the parent
        TimeBlock* time_block_parent = GetLastIncompleteTimeBlock(type);

        if (TimeBlock* time_block = GetNewTimeBlock())
        {
            time_block->Begin(func_name, type, time_block_parent, cmd_list, m_renderer->GetRhiDevice());
        }
    }

    void Profiler::TimeBlockEnd()
    {
        // If the capacity 
        if (m_increase_capacity)
            return;

        if (TimeBlock* time_block = GetLastIncompleteTimeBlock())
        {
            time_block->End();
        }
    }

    void Profiler::ResetMetrics()
    {
        m_time_frame_avg    = 0.0f;
        m_time_frame_min    = std::numeric_limits<float>::max();
        m_time_frame_max    = std::numeric_limits<float>::lowest();
        m_time_frame_last   = 0.0f;
        m_time_cpu_avg      = 0.0f;
        m_time_cpu_min      = std::numeric_limits<float>::max();
        m_time_cpu_max      = std::numeric_limits<float>::lowest();
        m_time_cpu_last     = 0.0f;
        m_time_gpu_avg      = 0.0f;
        m_time_gpu_min      = std::numeric_limits<float>::max();
        m_time_gpu_max      = std::numeric_limits<float>::lowest();
        m_time_gpu_last     = 0.0f;
    }

    TimeBlock* Profiler::GetNewTimeBlock()
    {
        // Increase capacity if needed
        if (m_time_block_count >= static_cast<uint32_t>(m_time_blocks_write.size()))
        {
            m_increase_capacity = true;
            return nullptr;
        }

        // Return a time block
        return &m_time_blocks_write[m_time_block_count++];
    }

    TimeBlock* Profiler::GetLastIncompleteTimeBlock(TimeBlock_Type type /*= TimeBlock_Undefined*/)
    {
        for (int i = m_time_block_count - 1; i >= 0; i--)
        {
            TimeBlock& time_block = m_time_blocks_write[i];

            if (type == time_block.GetType() || type == TimeBlock_Undefined)
            {
                if (!time_block.IsComplete())
                    return &time_block;
            }
        }

        return nullptr;
    }

    void Profiler::ComputeFps(const float delta_time)
    {
        m_frames_since_last_fps_computation++;
        m_time_passed += delta_time;
        m_fps = static_cast<float>(m_frames_since_last_fps_computation) / (m_time_passed / 1.0f);

        if (m_time_passed >= 1.0f)
        {
            m_frames_since_last_fps_computation = 0;
            m_time_passed = 0;
        }
    }

    void Profiler::AcquireGpuData()
    {
        RHI_Device* rhi_device = m_renderer->GetRhiDevice().get();
        if (const PhysicalDevice* physical_device = rhi_device->GetPrimaryPhysicalDevice())
        {
            m_gpu_name              = physical_device->GetName();
            m_gpu_memory_used       = RHI_CommandList::Gpu_GetMemoryUsed(rhi_device);
            m_gpu_memory_available  = RHI_CommandList::Gpu_GetMemory(rhi_device);
            m_gpu_driver            = physical_device->GetDriverVersion();
            m_gpu_api               = physical_device->GetApiVersion();
        }
    }

    void Profiler::UpdateRhiMetricsString()
    {
        const auto texture_count    = m_resource_manager->GetResourceCount(ResourceType::Texture) + m_resource_manager->GetResourceCount(ResourceType::Texture2d) + m_resource_manager->GetResourceCount(ResourceType::TextureCube);
        const auto material_count   = m_resource_manager->GetResourceCount(ResourceType::Material);

        static const char* text =
            // Times
            "FPS:\t\t%.2f\n"
            "Frame:\t%d\n"
            "Time:\t%.2f ms\n"
            "\n"
            // Detailed times
            "\t\tavg\t\tmin\t\tmax\t\tlast\n"
            "Total:\t%06.2f\t%06.2f\t%06.2f\t%06.2f ms\n"
            "CPU:\t\t%06.2f\t%06.2f\t%06.2f\t%06.2f ms\n"
            "GPU:\t%06.2f\t%06.2f\t%06.2f\t%06.2f ms\n"
            "\n"
            // GPU
            "API:\t\t%s\n"
            "GPU:\t%s\n"
            "VRAM:\t%d/%d MB\n"
            "Driver:\t%s\n"
            "\n"
            // Renderer
            "Resolution:\t\t%dx%d\n"
            "Meshes rendered:\t%d\n"
            "Textures:\t\t\t%d\n"
            "Materials:\t\t%d\n"
            "\n"
            // RHI
            "Draw:\t\t\t%d\n"
            "Dispatch:\t\t\t%d\n"
            "Index buffer:\t\t%d\n"
            "Vertex buffer:\t\t%d\n"
            "Constant buffer:\t%d\n"
            "Sampler:\t\t\t%d\n"
            "Texture sampled:\t%d\n"
            "Texture storage:\t%d\n"
            "Shader vertex:\t%d\n"
            "Shader pixel:\t\t%d\n"
            "Shader compute:\t%d\n"
            "Render target:\t%d\n"
            "Pipeline:\t\t\t%d\n"
            "Descriptor set:\t%d\n"
            "Pipeline barrier:\t%d";

        static char buffer[2048];
        sprintf_s
        (
            buffer, text,

            // Performance
            m_fps,
            m_renderer->GetFrameNum(),
            m_time_frame_last,
            m_time_frame_avg,   m_time_frame_min,   m_time_frame_max,   m_time_frame_last,
            m_time_cpu_avg,     m_time_cpu_min,     m_time_cpu_max,     m_time_cpu_last,
            m_time_gpu_avg,     m_time_gpu_min,     m_time_gpu_max,     m_time_gpu_last,
            m_gpu_api.c_str(),
            m_gpu_name.c_str(),
            m_gpu_memory_used, m_gpu_memory_available,
            m_gpu_driver.c_str(),

            // Renderer
            static_cast<int>(m_renderer->GetResolution().x), static_cast<int>(m_renderer->GetResolution().y),
            m_renderer_meshes_rendered,
            texture_count,
            material_count,

            // RHI
            m_rhi_draw,
            m_rhi_dispatch,
            m_rhi_bindings_buffer_index,
            m_rhi_bindings_buffer_vertex,
            m_rhi_bindings_buffer_constant,
            m_rhi_bindings_sampler,
            m_rhi_bindings_texture_sampled,
            m_rhi_bindings_texture_storage,
            m_rhi_bindings_shader_vertex,
            m_rhi_bindings_shader_pixel,
            m_rhi_bindings_shader_compute,
            m_rhi_bindings_render_target,
            m_rhi_bindings_pipeline,
            m_rhi_bindings_descriptor_set,
            m_rhi_pipeline_barriers
        );

        m_metrics = string(buffer);
    }
}
