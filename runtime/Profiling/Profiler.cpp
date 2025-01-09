#/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_SwapChain.h"
#include "../Core/ThreadPool.h"
#include "../Core/Debugging.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Display/Display.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // metrics - rhi
    uint32_t Profiler::m_rhi_draw                       = 0;
    uint32_t Profiler::m_rhi_timeblock_count            = 0;
    uint32_t Profiler::m_rhi_pipeline_barriers          = 0;
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

    // misc
    uint32_t Profiler::m_descriptor_set_count = 0;

    namespace
    {
        // profiling options
        ProfilerGranularity granularity = ProfilerGranularity::Light;
        const uint32_t max_timeblocks   = 256;
        bool profile_cpu                = true;
        bool profile_gpu                = true;
        float profiling_interval_sec    = 0.25f;
        float time_since_profiling_sec  = profiling_interval_sec;

        // metrics - time
        float time_frame_avg                = 0.0f;
        float time_frame_min                = numeric_limits<float>::max();
        float time_frame_max                = numeric_limits<float>::lowest();
        float time_frame_last               = 0.0f;
        float time_cpu_avg                  = 0.0f;
        float time_cpu_min                  = numeric_limits<float>::max();
        float time_cpu_max                  = numeric_limits<float>::lowest();
        float time_cpu_last                 = 0.0f;
        float time_gpu_avg                  = 0.0f;
        float time_gpu_min                  = numeric_limits<float>::max();
        float time_gpu_max                  = numeric_limits<float>::lowest();
        float time_gpu_last                 = 0.0f;
        const uint32_t frames_to_accumulate = static_cast<uint32_t>(4.0f / profiling_interval_sec);
        const float weight_delta            = 1.0f / static_cast<float>(frames_to_accumulate);
        const float weight_history          = (1.0f - weight_delta);
        float m_fps                         = 0.0f;

        // time blocks (double buffered)
        int m_time_block_index = -1;
        vector<TimeBlock> m_time_blocks_write;
        vector<TimeBlock> m_time_blocks_read;

        // gpu
        string gpu_name               = "N/A";
        string gpu_driver             = "N/A";
        string gpu_api                = "N/A";
        uint32_t gpu_memory_available = 0;
        uint32_t gpu_memory_used      = 0;

        // stutter detection
        float stutter_delta_ms = 1.0f;
        bool is_stuttering_cpu = false;
        bool is_stuttering_gpu = false;

        // misc
        string cpu_name           = "N/A";
        bool poll                 = false;
        bool allow_time_block_end = true;

        string get_cpu_name()
        {
            #ifdef _WIN32

            // windows: use __cpuid to get CPU name
            int cpu_info[4]   = { -1 };
            char cpu_name[49] = { 0 };
            
            __cpuid(cpu_info, 0x80000002);
            memcpy(cpu_name, cpu_info, sizeof(cpu_info));

            __cpuid(cpu_info, 0x80000003);
            memcpy(cpu_name + 16, cpu_info, sizeof(cpu_info));

            __cpuid(cpu_info, 0x80000004);
            memcpy(cpu_name + 32, cpu_info, sizeof(cpu_info));

            return string(cpu_name);

            #elif __linux__

            // linux: read from /proc/cpuinfo
            ifstream cpuinfo("/proc/cpuinfo");
            string line;
            while (getline(cpuinfo, line))
            {
                if (line.find("model name") != string::npos)
                {
                    return line.substr(line.find(":") + 2);
                }
            }
            return "Unknown CPU";

            #else

            // unsupported platform
            return "N/A";

            #endif
        }
    }
  
    void Profiler::Initialize()
    {
        m_time_blocks_read.reserve(max_timeblocks);
        m_time_blocks_read.resize(max_timeblocks);
        m_time_blocks_write.reserve(max_timeblocks);
        m_time_blocks_write.resize(max_timeblocks);

        cpu_name = get_cpu_name();
    }

    void Profiler::PostTick()
    {
        // compute timings
        {
            is_stuttering_cpu = time_cpu_last > (time_cpu_avg + stutter_delta_ms);
            is_stuttering_gpu = time_gpu_last > (time_gpu_avg + stutter_delta_ms);
            time_cpu_last   = 0.0f;
            time_gpu_last   = 0.0f;

            for (const TimeBlock& time_block : m_time_blocks_read)
            {
                if (!time_block.IsComplete())
                    continue;

                if (!time_block.GetParent() && time_block.GetType() == TimeBlockType::Cpu)
                {
                    time_cpu_last += time_block.GetDuration();
                }

                if (!time_block.GetParent() && time_block.GetType() == TimeBlockType::Gpu)
                {
                    time_gpu_last += time_block.GetDuration();
                }
            }

            // cpu
            time_cpu_avg = (time_cpu_avg * weight_history) + time_cpu_last * weight_delta;
            time_cpu_min = Math::Helper::Min(time_cpu_min, time_cpu_last);
            time_cpu_max = Math::Helper::Max(time_cpu_max, time_cpu_last);

            // gpu
            time_gpu_avg = (time_gpu_avg * weight_history) + time_gpu_last * weight_delta;
            time_gpu_min = Math::Helper::Min(time_gpu_min, time_gpu_last);
            time_gpu_max = Math::Helper::Max(time_gpu_max, time_gpu_last);

            // frame
            time_frame_last = static_cast<float>(Timer::GetDeltaTimeMs());
            time_frame_avg  = (time_frame_avg * weight_history) + time_frame_last * weight_delta;
            time_frame_min  = Math::Helper::Min(time_frame_min, time_frame_last);
            time_frame_max  = Math::Helper::Max(time_frame_max, time_frame_last);

            // fps  
            m_fps = 1000.0f / time_frame_avg;
        }

        // check whether we should profile or not
        time_since_profiling_sec += static_cast<float>(Timer::GetDeltaTimeSec());
        if (time_since_profiling_sec >= profiling_interval_sec)
        {
            time_since_profiling_sec = 0.0f;
            poll                     = true;
        }
        else if (poll)
        {
            poll = false;
        }

        if (poll && Debugging::IsGpuTimingEnabled())
        {
            AcquireGpuData();
            ReadTimeBlocks();
        }

        if (Renderer::GetOption<bool>(Renderer_Option::PerformanceMetrics))
        {
            DrawPerformanceMetrics();
        }

        m_rhi_draw                       = 0;
        m_rhi_timeblock_count            = 0;
        m_rhi_pipeline_barriers          = 0;
        m_rhi_bindings_buffer_index      = 0;
        m_rhi_bindings_buffer_vertex     = 0;
        m_rhi_bindings_buffer_constant   = 0;
        m_rhi_bindings_buffer_structured = 0;
        m_rhi_bindings_sampler           = 0;
        m_rhi_bindings_texture_sampled   = 0;
        m_rhi_bindings_shader_vertex     = 0;
        m_rhi_bindings_shader_pixel      = 0;
        m_rhi_bindings_shader_compute    = 0;
        m_rhi_bindings_render_target     = 0;
        m_rhi_bindings_texture_storage   = 0;
    }

    void Profiler::ReadTimeBlocks()
    {
        // clear read array
        m_time_blocks_read.clear();
        m_time_blocks_read.resize(max_timeblocks);

        // copy from write array to read array
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_time_blocks_write.size()); i++)
        {
            TimeBlock& time_block = m_time_blocks_write[i];

            // check if we reached the end of the array
            if (time_block.GetType() == TimeBlockType::Max)
                break;

            // skip incomplete time blocks and let the use know
            if (!time_block.IsComplete())
            {
                SP_LOG_WARNING("TimeBlockEnd() was not called for time block \"%s\"", time_block.GetName());
                continue;
            }

            // copy
            m_time_blocks_read[i] = time_block;
        }

        // clear write array
        m_time_blocks_write.clear();
        m_time_blocks_write.resize(max_timeblocks);

        m_time_block_index = -1;
    }

    void Profiler::TimeBlockStart(const char* func_name, TimeBlockType type, RHI_CommandList* cmd_list /*= nullptr*/)
    {
        if (!Debugging::IsGpuTimingEnabled() || !poll)
            return;

        const bool can_profile_cpu = (type == TimeBlockType::Cpu) && profile_cpu;
        const bool can_profile_gpu = (type == TimeBlockType::Gpu) && profile_gpu;

        if (!can_profile_cpu && !can_profile_gpu)
            return;

        // last incomplete block of the same type, is the parent
        TimeBlock* time_block_parent = GetLastIncompleteTimeBlock(type);

        // get new time block
        TimeBlock& new_time_block = m_time_blocks_write[++m_time_block_index];
        new_time_block.Begin(++m_rhi_timeblock_count, func_name, type, time_block_parent, cmd_list);
    }

    void Profiler::TimeBlockEnd()
    {
        if (TimeBlock* time_block = GetLastIncompleteTimeBlock(TimeBlockType::Cpu))
        {
            time_block->End();
        }

        if (TimeBlock* time_block = GetLastIncompleteTimeBlock(TimeBlockType::Gpu))
        {
            time_block->End();
        }
    }

    void Profiler::ClearMetrics()
    {
        time_frame_avg  = 0.0f;
        time_frame_min  = numeric_limits<float>::max();
        time_frame_max  = numeric_limits<float>::lowest();
        time_frame_last = 0.0f;
        time_cpu_avg    = 0.0f;
        time_cpu_min    = numeric_limits<float>::max();
        time_cpu_max    = numeric_limits<float>::lowest();
        time_cpu_last   = 0.0f;
        time_gpu_avg    = 0.0f;
        time_gpu_min    = numeric_limits<float>::max();
        time_gpu_max    = numeric_limits<float>::lowest();
        time_gpu_last   = 0.0f;
    }

    const vector<TimeBlock>& Profiler::GetTimeBlocks()
    {
        return m_time_blocks_read;
    }

    float Profiler::GetTimeCpuLast()
    {
        return time_cpu_last;
    }

    float Profiler::GetTimeGpuLast()
    {
        return time_gpu_last;
    }

    float Profiler::GetTimeFrameLast()
    {
        return time_frame_last;
    }

    float Profiler::GetFps()
    {
        return m_fps;
    }

    float Profiler::GetUpdateInterval()
    {
        return profiling_interval_sec;
    }

    void Profiler::SetUpdateInterval(float interval)
    {
        profiling_interval_sec = interval;
    }

    const string& Profiler::GpuGetName()
    {
        return gpu_name;
    }

    uint32_t Profiler::GpuGetMemoryAvailable()
    {
        return gpu_memory_available;
    }

    uint32_t Profiler::GpuGetMemoryUsed()
    {
        return gpu_memory_used;
    }

    bool Profiler::IsCpuStuttering()
    {
        return is_stuttering_cpu;
    }

    bool Profiler::IsGpuStuttering()
    {
        return is_stuttering_gpu;
    }

   TimeBlock* Profiler::GetLastIncompleteTimeBlock(const TimeBlockType type)
    {
        for (int i = m_time_block_index; i >= 0; i--)
        {
            TimeBlock& time_block = m_time_blocks_write[i];
    
            // if type is Max, match any type; otherwise, match the requested type
            if (type == TimeBlockType::Max || time_block.GetType() == type)
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
            gpu_name             = physical_device->GetName();
            gpu_memory_used      = RHI_Device::MemoryGetUsageMb();
            gpu_memory_available = RHI_Device::MemoryGetBudgetMb();
            gpu_driver           = physical_device->GetDriverVersion();
            gpu_api              = RHI_Context::api_version_str;
        }
    }

   void Profiler::DrawPerformanceMetrics()
    {
        static char metrics_buffer[4096];
        static float metrics_time_since_last_update = profiling_interval_sec;
    
        metrics_time_since_last_update += static_cast<float>(Timer::GetDeltaTimeSec());
        if (metrics_time_since_last_update >= profiling_interval_sec)
        {
            metrics_time_since_last_update = 0.0f;
    
            snprintf(metrics_buffer, sizeof(metrics_buffer),
                "FPS:\t\t\t%.1f\n"
                "Time:\t\t\t%.2f ms\n"
                "Frame:\t\t%llu\n\n"
                "\t\t\t\tavg\t\tmin\t\tmax\t\tlast\n"
                "Total:\t\t%05.2f\t\t%05.2f\t\t%05.2f\t\t%05.2f ms\n"
                "CPU:\t\t%05.2f\t\t%05.2f\t\t%05.2f\t\t%05.2f ms\n"
                "GPU:\t\t%05.2f\t\t%05.2f\t\t%05.2f\t\t%05.2f ms\n\n"
                "GPU\n"
                "Name:\t\t\t%s\n"
                "Memory:\t\t%u/%u MB\n"
                "API:\t\t\t\t\t%s\t%s\n"
                "Driver:\t\t\t%s\t\t%s\n\n"
                "CPU\n"
                "Name:\t\t\t\t\t\t%s\n"
                "Threads:\t\t\t\t\t%u\n"
                "Worker threads:\t%u/%u\n"
                #ifdef __AVX2__
                "AVX2:\t\t\t\t\t\t\tYes\n"
                #else
                "AVX2:\t\t\t\t\t\t\tNo\n"
                #endif
                "\nDisplay\n"
                "Name:\t\t\t%s\n"
                "Hz:\t\t\t\t\t%u\n"
                "HDR:\t\t\t\t%s\n"
                "Max nits:\t\t%u\n"
                "Render:\t\t\t%u x %u - %.0f%%\n"
                "Output:\t\t\t%u x %u\n"
                "Viewport:\t\t%u x %u\n\n"
                "Graphics API\n"
                "Draw:\t\t\t\t\t\t\t\t\t\t\t%u\n"
                "Index buffer bindings:\t\t\t%u\n"
                "Vertex buffer bindings:\t\t%u\n"
                "Barriers:\t\t\t\t\t\t\t\t\t%u\n"
                "Pipelines:\t\t\t\t\t\t\t\t\t%u\n"
                "Descriptor set capacity:\t\t%u/%u",

                m_fps,
                time_frame_avg,
                Renderer::GetFrameNumber(),

                time_frame_avg, time_frame_min, time_frame_max, time_frame_last,
                time_cpu_avg, time_cpu_min, time_cpu_max, time_cpu_last,
                time_gpu_avg, time_gpu_min, time_gpu_max, time_gpu_last,

                gpu_name.c_str(),
                gpu_memory_used, gpu_memory_available,
                RHI_Context::api_type_str.c_str(), gpu_api.c_str(),
                RHI_Device::GetPrimaryPhysicalDevice()->GetVendorName().c_str(), gpu_driver.c_str(),

                cpu_name.c_str(),
                thread::hardware_concurrency(),
                ThreadPool::GetWorkingThreadCount(), ThreadPool::GetThreadCount(),

                Display::GetName(),
                Display::GetRefreshRate(),
                Renderer::GetSwapChain()->IsHdr() ? "Enabled" : "Disabled",
                static_cast<uint32_t>(Display::GetLuminanceMax()),

                static_cast<uint32_t>(Renderer::GetResolutionRender().x),
                static_cast<uint32_t>(Renderer::GetResolutionRender().y),
                Renderer::GetOption<float>(Renderer_Option::ResolutionScale) * 100.0f,

                static_cast<uint32_t>(Renderer::GetResolutionOutput().x),
                static_cast<uint32_t>(Renderer::GetResolutionOutput().y),
                
                static_cast<uint32_t>(Renderer::GetViewport().width),
                static_cast<uint32_t>(Renderer::GetViewport().height),

                m_rhi_draw,
                m_rhi_bindings_buffer_index,
                m_rhi_bindings_buffer_vertex,
                m_rhi_pipeline_barriers,
                RHI_Device::GetPipelineCount(),
                m_descriptor_set_count, rhi_max_descriptor_set_count
            );
        }
    
        // Draw directly from the static buffer
        Renderer::DrawString(string(metrics_buffer), Math::Vector2(0.01f, 0.01f));
    }

    ProfilerGranularity Profiler::GetGranularity()
    {
        return granularity;
    }
}
