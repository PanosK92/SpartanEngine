/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "pch.h"
#include "Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Implementation.h"
#include "../Core/ThreadPool.h"
#include "../RHI/RHI_SwapChain.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    // Metrics - RHI
    uint32_t Profiler::m_rhi_draw                       = 0;
    uint32_t Profiler::m_rhi_dispatch                   = 0;
    uint32_t Profiler::m_rhi_bindings_buffer_index      = 0;
    uint32_t Profiler::m_rhi_bindings_buffer_vertex     = 0;
    uint32_t Profiler::m_rhi_bindings_buffer_constant   = 0;
    uint32_t Profiler::m_rhi_bindings_buffer_structured = 0;
    uint32_t Profiler::m_rhi_bindings_sampler           = 0;
    uint32_t Profiler::m_rhi_bindings_texture_sampled   = 0;
    uint32_t Profiler::m_rhi_bindings_shader_vertex     = 0;
    uint32_t Profiler::m_rhi_bindings_shader_pixel      = 0;
    uint32_t Profiler::m_rhi_bindings_shader_compute    = 0;
    uint32_t Profiler::m_rhi_bindings_render_target     = 0;
    uint32_t Profiler::m_rhi_bindings_texture_storage   = 0;
    uint32_t Profiler::m_rhi_bindings_descriptor_set    = 0;
    uint32_t Profiler::m_rhi_bindings_pipeline          = 0;
    uint32_t Profiler::m_rhi_pipeline_barriers          = 0;
    uint32_t Profiler::m_rhi_timeblock_count            = 0;

    // Metrics - Renderer
    uint32_t Profiler::m_renderer_meshes_rendered = 0;

    // Metrics - Time
    float Profiler::m_time_frame_avg  = 0.0f;
    float Profiler::m_time_frame_min  = numeric_limits<float>::max();
    float Profiler::m_time_frame_max  = numeric_limits<float>::lowest();
    float Profiler::m_time_frame_last = 0.0f;
    float Profiler::m_time_cpu_avg    = 0.0f;
    float Profiler::m_time_cpu_min    = numeric_limits<float>::max();
    float Profiler::m_time_cpu_max    = numeric_limits<float>::lowest();
    float Profiler::m_time_cpu_last   = 0.0f;
    float Profiler::m_time_gpu_avg    = 0.0f;
    float Profiler::m_time_gpu_min    = numeric_limits<float>::max();
    float Profiler::m_time_gpu_max    = numeric_limits<float>::lowest();
    float Profiler::m_time_gpu_last   = 0.0f;

    // Memory
    uint32_t Profiler::m_descriptor_set_count    = 0;
    uint32_t Profiler::m_descriptor_set_capacity = 0;

    namespace
    {
        // Profiling options
        static bool m_profile                   = false;
        static bool m_profile_cpu               = true; // cheap
        static bool m_profile_gpu               = true; // expensive
        static float m_profiling_interval_sec   = 0.2f;
        static float m_time_since_profiling_sec = m_profiling_interval_sec;

        // Time blocks (double buffered)
        static int m_time_block_index = -1;
        static vector<TimeBlock> m_time_blocks_write;
        static vector<TimeBlock> m_time_blocks_read;

        // FPS
        static float m_fps = 0.0f;

        // Hardware - GPU
        static string m_gpu_name          = "N/A";
        static string m_gpu_driver        = "N/A";
        static string m_gpu_api           = "N/A";
        static uint32_t m_gpu_memory_available = 0;
        static uint32_t m_gpu_memory_used      = 0;

        // Stutter detection
        static float m_stutter_delta_ms = 0.5f;
        static bool m_is_stuttering_cpu = false;
        static bool m_is_stuttering_gpu = false;

        // Misc
        static bool m_poll                 = false;
        static bool m_increase_capacity    = false;
        static bool m_allow_time_block_end = true;
        static void* m_query_disjoint      = nullptr;
        static ostringstream m_oss_metrics;
    }
  
    void Profiler::Initialize()
    {
        static const int initial_capacity = 256;

        m_time_blocks_read.reserve(initial_capacity);
        m_time_blocks_read.resize(initial_capacity);
        m_time_blocks_write.reserve(initial_capacity);
        m_time_blocks_write.resize(initial_capacity);

        SP_SUBSCRIBE_TO_EVENT(EventType::RendererPostPresent, SP_EVENT_HANDLER_STATIC(OnPostPresent));
    }

    void Profiler::Shutdown()
    {
        if (m_poll)
        {
            SwapBuffers();
        }

        RHI_Device::QueryRelease(m_query_disjoint);

        ClearRhiMetrics();
    }

    void Profiler::PreTick()
    {
        if (!RHI_Context::gpu_profiling)
            return;

        if (m_query_disjoint == nullptr)
        {
            RHI_Device::QueryCreate(&m_query_disjoint, RHI_Query_Type::Timestamp_Disjoint);
        }

        // Increase time block capacity (if needed)
        if (m_increase_capacity)
        {
            SwapBuffers();

            // Double the size
            const uint32_t size_old = static_cast<uint32_t>(m_time_blocks_write.size());
            const uint32_t size_new = size_old << 1;

            m_time_blocks_read.reserve(size_new);
            m_time_blocks_read.resize(size_new);
            m_time_blocks_write.reserve(size_new);
            m_time_blocks_write.resize(size_new);

            m_increase_capacity = false;
            m_poll              = true;

            SP_LOG_WARNING("Time block list has grown to %d. Consider making the default capacity as large by default, to avoid re-allocating.", size_new);
        }

        ClearRhiMetrics();
    }

    void Profiler::PostTick()
    {
        // Compute timings
        {
            // Detect stutters
            float frames_to_accumulate = 5.0f;
            float delta_feedback       = 1.0f / frames_to_accumulate;
            m_is_stuttering_cpu        = m_time_cpu_last > (m_time_cpu_avg + m_stutter_delta_ms);
            m_is_stuttering_gpu        = m_time_gpu_last > (m_time_gpu_avg + m_stutter_delta_ms);

            frames_to_accumulate = 20.0f;
            delta_feedback       = 1.0f / frames_to_accumulate;
            m_time_cpu_last      = 0.0f;
            m_time_gpu_last      = 0.0f;

            for (const TimeBlock& time_block : m_time_blocks_read)
            {
                if (!time_block.IsComplete())
                    continue;

                if (!time_block.GetParent() && time_block.GetType() == TimeBlockType::Cpu)
                {
                    m_time_cpu_last += time_block.GetDuration();
                }

                if (!time_block.GetParent() && time_block.GetType() == TimeBlockType::Gpu)
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
            m_time_frame_last = static_cast<float>(Timer::GetDeltaTimeMs());
            m_time_frame_avg  = m_time_frame_avg * (1.0f - delta_feedback) + m_time_frame_last * delta_feedback;
            m_time_frame_min  = Math::Helper::Min(m_time_frame_min, m_time_frame_last);
            m_time_frame_max  = Math::Helper::Max(m_time_frame_max, m_time_frame_last);

            // FPS
            m_fps = static_cast<float>(1.0 / Timer::GetDeltaTimeSec());
        }

        // Check whether we should profile or not
        m_time_since_profiling_sec += static_cast<float>(Timer::GetDeltaTimeSec());
        if (m_time_since_profiling_sec >= m_profiling_interval_sec)
        {
            m_time_since_profiling_sec = 0.0f;
            m_poll                     = true;
        }
        else if (m_poll)
        {
            m_poll = false;
        }

        // Updating every m_profiling_interval_sec
        if (m_poll)
        {
            RHI_Device::QueryBegin(m_query_disjoint);

            AcquireGpuData();

            // Create a string version of the RHI metrics
            if (Renderer::GetOption<bool>(RendererOption::Debug_PerformanceMetrics))
            {
                UpdateMetrics();
            }
        }

        if (m_profile && m_poll)
        {
            SwapBuffers();
        }
    }

    void Profiler::OnPostPresent()
    {
        if (m_poll)
        {
            RHI_Device::QueryEnd(m_query_disjoint);
            RHI_Device::QueryGetData(m_query_disjoint);
        }
    }

    void Profiler::SwapBuffers()
    {
        // Copy completed time blocks write to time blocks read vector (double buffering)
        {
            uint32_t pass_index_gpu = 0;

            for (uint32_t i = 0; i < static_cast<uint32_t>(m_time_blocks_read.size()); i++)
            {
                TimeBlock& time_block = m_time_blocks_write[i];

                // Compute time block duration
                if (time_block.IsComplete())
                {
                    // ComputeDuration() must only be called here, at the end of the frame, and not in TimeBlockEnd().
                    // This is because D3D11 waits too much for the results to be ready, which increases CPU time.
                    time_block.ComputeDuration(pass_index_gpu);

                    if (time_block.GetType() == TimeBlockType::Gpu)
                    {
                        pass_index_gpu += 2;
                    }
                }
                else if (time_block.GetType() != TimeBlockType::Undefined) // If undefined, then it wasn't used this frame, nothing wrong with that.
                {
                    SP_LOG_WARNING("TimeBlockEnd() was not called for time block \"%s\"", time_block.GetName());
                }

                // Copy over
                m_time_blocks_read[i] = time_block;
                // Nullify GPU query objects as we don't want them to de-allocate twice (read and write vectors) once the profiler deconstructs.
                m_time_blocks_read[i].ClearGpuObjects();

                // Reset
                time_block.Reset();
            }
        }
        
        m_time_block_index = -1;
    }

    void Profiler::TimeBlockStart(const char* func_name, TimeBlockType type, RHI_CommandList* cmd_list /*= nullptr*/)
    {
        if (!m_profile || !m_poll)
            return;

        const bool can_profile_cpu = (type == TimeBlockType::Cpu) && m_profile_cpu;
        const bool can_profile_gpu = (type == TimeBlockType::Gpu) && m_profile_gpu;

        if (!can_profile_cpu && !can_profile_gpu)
            return;

        // Last incomplete block of the same type, is the parent
        TimeBlock* time_block_parent = GetLastIncompleteTimeBlock(type);

        if (TimeBlock* time_block = GetNewTimeBlock())
        {
            time_block->Begin(++m_rhi_timeblock_count, func_name, type, time_block_parent, cmd_list);
        }
    }

    void Profiler::TimeBlockEnd()
    {
        if (TimeBlock* time_block = GetLastIncompleteTimeBlock())
        {
            time_block->End();
        }
    }

    void Profiler::ClearMetrics()
    {
        m_time_frame_avg  = 0.0f;
        m_time_frame_min  = numeric_limits<float>::max();
        m_time_frame_max  = numeric_limits<float>::lowest();
        m_time_frame_last = 0.0f;
        m_time_cpu_avg    = 0.0f;
        m_time_cpu_min    = numeric_limits<float>::max();
        m_time_cpu_max    = numeric_limits<float>::lowest();
        m_time_cpu_last   = 0.0f;
        m_time_gpu_avg    = 0.0f;
        m_time_gpu_min    = numeric_limits<float>::max();
        m_time_gpu_max    = numeric_limits<float>::lowest();
        m_time_gpu_last   = 0.0f;
    }

    bool Profiler::GetEnabled()
    {
        return m_profile;
    }

    void Profiler::SetEnabled(const bool enabled)
    {
        m_profile = enabled;
    }

    string Profiler::GetMetrics()
    {
        return m_oss_metrics.str();
    }

    const vector<TimeBlock>& Profiler::GetTimeBlocks()
    {
        return m_time_blocks_read;
    }

    float Profiler::GetTimeCpuLast()
    {
        return m_time_cpu_last;
    }

    float Profiler::GetTimeGpuLast()
    {
        return m_time_gpu_last;
    }

    float Profiler::GetTimeFrameLast()
    {
        return m_time_frame_last;
    }

    float Profiler::GetFps()
    {
        return m_fps;
    }

    float Profiler::GetUpdateInterval()
    {
        return m_profiling_interval_sec;
    }

    void Profiler::SetUpdateInterval(float internval)
    {
        m_profiling_interval_sec = internval;
    }

    const string& Profiler::GpuGetName()
    {
        return m_gpu_name;
    }

    uint32_t Profiler::GpuGetMemoryAvailable()
    {
        return m_gpu_memory_available;
    }

    uint32_t Profiler::GpuGetMemoryUsed()
    {
        return m_gpu_memory_used;
    }

    bool Profiler::IsCpuStuttering()
    {
        return m_is_stuttering_cpu;
    }

    bool Profiler::IsGpuStuttering()
    {
        return m_is_stuttering_gpu;
    }

    TimeBlock* Profiler::GetNewTimeBlock()
    {
        // Increase capacity if needed
        if (m_time_block_index + 1 >= static_cast<int>(m_time_blocks_write.size()))
        {
            m_increase_capacity = true;
            return nullptr;
        }

        // Return a time block
        return &m_time_blocks_write[++m_time_block_index];
    }

    TimeBlock* Profiler::GetLastIncompleteTimeBlock(TimeBlockType type /*= TimeBlock_Undefined*/)
    {
        for (int i = m_time_block_index; i >= 0; i--)
        {
            TimeBlock& time_block = m_time_blocks_write[i];

            if (type == time_block.GetType() || type == TimeBlockType::Undefined)
            {
                if (!time_block.IsComplete())
                    return &time_block;
            }
        }

        return nullptr;
    }

    void Profiler::AcquireGpuData()
    {
        if (const PhysicalDevice* physical_device = RHI_Device::GetPrimaryPhysicalDevice())
        {
            m_gpu_name             = physical_device->GetName();
            m_gpu_memory_used      = RHI_CommandList::GetGpuMemoryUsed();
            m_gpu_memory_available = RHI_CommandList::GetGpuMemory();
            m_gpu_driver           = physical_device->GetDriverVersion();
            m_gpu_api              = RHI_Context::api_version_str;
        }
    }

    static string format_float(float value)
    {
        std::stringstream ss;

        // Clamp to a certain range to avoid padding and alignment headaches
        value = Math::Helper::Clamp(value, 0.0f, 99.99f);

        // Set fixed-point notation with 2 decimal places
        ss << std::fixed << std::setprecision(2);

        // Output the integer part with the fill character '0' and the minimum width of 2 characters
        int integer_part = static_cast<int>(value);
        ss << std::setfill('0') << std::setw(2) << integer_part;

        // Output the decimal point and decimal part
        float decimal_part = value - integer_part;
        ss << "." << std::setfill('0') << std::setw(2) << static_cast<int>(round(decimal_part * 100));

        return ss.str();
    }

    void Profiler::UpdateMetrics()
    {
        const uint32_t texture_count = ResourceCache::GetResourceCount(ResourceType::Texture) + ResourceCache::GetResourceCount(ResourceType::Texture2d) + ResourceCache::GetResourceCount(ResourceType::TextureCube);
        const uint32_t material_count = ResourceCache::GetResourceCount(ResourceType::Material);

        // Get the graphics driver vendor
        string api_vendor_name = "AMD";
        if (RHI_Device::GetPrimaryPhysicalDevice()->IsNvidia())
        {
            api_vendor_name = "NVIDIA";
        }

        // Clear
        m_oss_metrics.str("");
        m_oss_metrics.clear();

        // Set fixed-point notation with 2 decimal places
        m_oss_metrics << std::fixed << std::setprecision(2);

        // Overview
        m_oss_metrics
            << "FPS:\t\t" << m_fps << endl
            << "Time:\t"  << m_time_frame_last << " ms" << endl
            << "Frame:\t" << Renderer::GetFrameNum() << endl;

        // Detailed times
        m_oss_metrics
            << endl     << setw(18) << "avg"                          << setw(10) << "min"                          << setw(10) << "max"                          << setw(10) << "last"                                   << endl
            << "Total:" << setw(10) << format_float(m_time_frame_avg) << setw(8)  << format_float(m_time_frame_min) << setw(9)  << format_float(m_time_frame_max) << setw(9)  << format_float(m_time_frame_last) << " ms" << endl
            << "CPU:"   << setw(11) << format_float(m_time_cpu_avg)   << setw(8)  << format_float(m_time_cpu_min)   << setw(9)  << format_float(m_time_cpu_max)   << setw(9)  << format_float(m_time_cpu_last)   << " ms" << endl
            << "GPU:"   << setw(11) << format_float(m_time_gpu_avg)   << setw(8)  << format_float(m_time_gpu_min)   << setw(9)  << format_float(m_time_gpu_max)   << setw(9)  << format_float(m_time_gpu_last)   << " ms" << endl;

        // GPU
        m_oss_metrics
            << endl << "GPU" << endl
            << "Name:\t\t"   << m_gpu_name << endl
            << "Memory:\t"   << m_gpu_memory_used << "/" << m_gpu_memory_available << " MB" << endl
            << "API:\t\t\t"  << RHI_Context::api_type_str << "\t" << m_gpu_api << endl
            << "Driver:\t\t" << RHI_Device::GetPrimaryPhysicalDevice()->GetVendorName() << "\t" << m_gpu_driver << endl;

        // CPU
        m_oss_metrics << endl << "CPU" << endl
            << "Worker threads: " << ThreadPool::GetWorkingThreadCount() << "/" << ThreadPool::GetThreadCount() << endl;

        // Resolution
        m_oss_metrics
            << "\nResolution\n"
            << "Output:\t\t" << static_cast<int>(Renderer::GetResolutionOutput().x) << "x" << static_cast<int>(Renderer::GetResolutionOutput().y) << endl
            << "Render:\t\t" << static_cast<int>(Renderer::GetResolutionRender().x) << "x" << static_cast<int>(Renderer::GetResolutionRender().y) << endl
            << "Viewport:\t" << static_cast<int>(Renderer::GetViewport().width)     << "x" << static_cast<int>(Renderer::GetViewport().height)    << endl
            << "HDR:\t\t"    << (Renderer::GetSwapChain()->IsHdr() ? "Enabled" : "Disabled") << endl;

        // API Calls
        m_oss_metrics
            << "\nAPI calls\n"
            << "Draw:\t\t\t\t\t"            << m_rhi_draw                    << endl
            << "Dispatch:\t\t\t\t"          << m_rhi_dispatch                << endl
            << "Index buffer bindings:\t\t" << m_rhi_bindings_buffer_index   << endl
            << "Vertex buffer bindings:\t"  << m_rhi_bindings_buffer_vertex  << endl
            << "Descriptor set bindings:\t" << m_rhi_bindings_descriptor_set << endl
            << "Pipeline bindings:\t\t\t"   << m_rhi_bindings_pipeline       << endl
            << "Pipeline barriers:\t\t\t"   << m_rhi_pipeline_barriers       << endl;

        // Resources
        m_oss_metrics
            << "\nResources\n"
            << "Meshes rendered:\t\t\t"     << m_renderer_meshes_rendered << endl
            << "Textures:\t\t\t\t"          << texture_count << endl
            << "Materials:\t\t\t\t"         << material_count << endl
            << "Descriptor set capacity:\t" << m_descriptor_set_count << "/" << m_descriptor_set_capacity;
    }
}
