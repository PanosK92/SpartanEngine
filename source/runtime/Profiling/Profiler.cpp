/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "../Display/Display.h"
#include "../Memory/Allocator.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // metrics - rhi
    uint32_t Profiler::m_rhi_draw                       = 0;
    uint32_t Profiler::m_rhi_instance_count             = 0;
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
    uint32_t Profiler::m_rhi_bindings_pipeline          = 0;
    uint32_t Profiler::m_rhi_descriptor_set_count       = 0;

    namespace
    {
        // profiling
        const uint32_t max_timeblocks  = 256;
        bool profile_cpu               = true;
        bool profile_gpu               = true;
        float profiling_interval_sec   = 0.25f;
        float time_since_profiling_sec = profiling_interval_sec;

        // time
        float time_frame_avg          = 0.0f;
        float time_frame_min          = numeric_limits<float>::max();
        float time_frame_max          = numeric_limits<float>::lowest();
        float time_frame_last         = 0.0f;
        float time_cpu_avg            = 0.0f;
        float time_cpu_min            = numeric_limits<float>::max();
        float time_cpu_max            = numeric_limits<float>::lowest();
        float time_cpu_last           = 0.0f;
        float time_gpu_avg            = 0.0f;
        float time_gpu_min            = numeric_limits<float>::max();
        float time_gpu_max            = numeric_limits<float>::lowest();
        float time_gpu_last           = 0.0f;
        uint32_t frames_to_accumulate = static_cast<uint32_t>(4.0f / profiling_interval_sec);
        float weight_delta            = 1.0f / static_cast<float>(frames_to_accumulate);
        float weight_history          = (1.0f - weight_delta);
        float m_fps                   = 0.0f;

        // time blocks (double buffered)
        int m_time_block_index = -1;
        vector<TimeBlock> m_time_blocks_write;
        vector<TimeBlock> m_time_blocks_read;

        // stutter detection
        float stutter_delta_ms = 1.0f;
        bool is_stuttering_cpu = false;
        bool is_stuttering_gpu = false;

        // misc
        bool poll = false;

        // cpu
        const char* cpu_name = "N/A";
        const char* get_cpu_name()
        {
#ifdef _WIN32
            static char cpu_id_name[49] = { 0 };
            int cpu_info[4] = { -1 };
            __cpuid(cpu_info, 0x80000002);
            memcpy(cpu_id_name, cpu_info, sizeof(cpu_info));
            __cpuid(cpu_info, 0x80000003);
            memcpy(cpu_id_name + 16, cpu_info, sizeof(cpu_info));
            __cpuid(cpu_info, 0x80000004);
            memcpy(cpu_id_name + 32, cpu_info, sizeof(cpu_info));
            return cpu_id_name;
#elif __linux__
            static char name[128] = { 0 };
            ifstream cpuinfo("/proc/cpuinfo");
            string line;
            while (getline(cpuinfo, line))
            {
                if (line.find("model name") != string::npos)
                {
                    strncpy(name, line.substr(line.find(":") + 2).c_str(), sizeof(name) - 1);
                    name[sizeof(name) - 1] = '\0';
                    return name;
                }
            }
            strncpy(name, "Unknown CPU", sizeof(name) - 1);
            return name;
#else
            return "N/A";
#endif
        }
    }

    void Profiler::Initialize()
    {
        m_time_blocks_write.resize(max_timeblocks);
        m_time_blocks_read.resize(max_timeblocks);
        cpu_name = get_cpu_name();
    }

    void Profiler::PostTick()
    {
        // compute timings
        {
            is_stuttering_cpu = time_cpu_last > (time_cpu_avg + stutter_delta_ms);
            is_stuttering_gpu = time_gpu_last > (time_gpu_avg + stutter_delta_ms);
            time_cpu_last     = 0.0f;
            time_gpu_last     = 0.0f;
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
            time_cpu_min = min(time_cpu_min, time_cpu_last);
            time_cpu_max = max(time_cpu_max, time_cpu_last);

            // gpu
            time_gpu_avg = (time_gpu_avg * weight_history) + time_gpu_last * weight_delta;
            time_gpu_min = min(time_gpu_min, time_gpu_last);
            time_gpu_max = max(time_gpu_max, time_gpu_last);

            // frame
            time_frame_last = static_cast<float>(Timer::GetDeltaTimeMs());
            time_frame_avg  = (time_frame_avg * weight_history) + time_frame_last * weight_delta;
            time_frame_min  = min(time_frame_min, time_frame_last);
            time_frame_max  = max(time_frame_max, time_frame_last);

            // fps
            m_fps = 1000.0f / time_frame_avg;
        }

        // check whether we should profile or not
        time_since_profiling_sec += static_cast<float>(Timer::GetDeltaTimeSec());
        if (time_since_profiling_sec >= profiling_interval_sec)
        {
            time_since_profiling_sec = 0.0f;
            poll = true;
        }
        else if (poll)
        {
            poll = false;
        }

        if (poll)
        {
            ReadTimeBlocks();
        }

        if (Renderer::GetRenderOptionsPool().GetOption<bool>(Renderer_Option::PerformanceMetrics))
        {
            DrawPerformanceMetrics();
        }

        ClearRhiMetrics();
    }

    void Profiler::ReadTimeBlocks()
    {
        // clear read array
        m_time_blocks_read.clear();
        if (m_time_block_index < 0)
        {
            // no time blocks to copy, just reset write array
            m_time_blocks_write.clear();
            m_time_blocks_write.resize(max_timeblocks);
            m_time_block_index = -1;
            return;
        }

        // copy from write array to read array
        for (uint32_t i = 0; i <= static_cast<uint32_t>(m_time_block_index); i++)
        {
            TimeBlock& time_block = m_time_blocks_write[i];

            // skip incomplete time blocks and let the user know
            if (!time_block.IsComplete())
            {
                SP_LOG_WARNING("TimeBlockEnd() was not called for time block \"%s\"", time_block.GetName());
                continue;
            }

            // copy
            m_time_blocks_read.push_back(time_block);
        }

        // clear write array
        m_time_blocks_write.clear();
        m_time_blocks_write.resize(max_timeblocks);
        m_time_block_index = -1;
    }

    void Profiler::TimeBlockStart(const char* func_name, TimeBlockType type, RHI_CommandList* cmd_list /*= nullptr*/)
    {
        if (!poll)
            return;

        const bool can_profile_cpu = (type == TimeBlockType::Cpu) && profile_cpu;
        const bool can_profile_gpu = (type == TimeBlockType::Gpu) && profile_gpu && Debugging::IsGpuTimingEnabled();
        if (!can_profile_cpu && !can_profile_gpu)
            return;

        SP_ASSERT(m_time_block_index < static_cast<int>(max_timeblocks) - 1);

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
        ClearRhiMetrics();

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
        frames_to_accumulate   = static_cast<uint32_t>(4.0f / profiling_interval_sec);
        weight_delta           = 1.0f / static_cast<float>(frames_to_accumulate);
        weight_history         = (1.0f - weight_delta);
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

            // if type is max, match any type; otherwise, match the requested type
            if (type == TimeBlockType::Max || time_block.GetType() == type)
            {
                if (!time_block.IsComplete())
                    return &time_block;
            }
        }
        return nullptr;
    }

    void Profiler::DrawPerformanceMetrics()
    {
        static char metrics_buffer[16384]            = { 0 };
        static float metrics_time_since_last_update  = profiling_interval_sec;
        metrics_time_since_last_update              += static_cast<float>(Timer::GetDeltaTimeSec());

        if (metrics_time_since_last_update >= profiling_interval_sec)
        {
            metrics_time_since_last_update = 0.0f;
            int offset = 0;

            // fps and frames
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "FPS:\t\t\t%.1f\n", m_fps);
            SP_ASSERT(offset < sizeof(metrics_buffer));
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Time:\t\t%.2f ms\n", time_frame_avg);
            SP_ASSERT(offset < sizeof(metrics_buffer));
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Frame:\t%llu\n\n", Renderer::GetFrameNumber());
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // timings
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "\t\t\t\tavg\t\tmin\t\tmax\tlast\n");
            SP_ASSERT(offset < sizeof(metrics_buffer));
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Total:\t\t%.2f\t\t%.2f\t\t%.2f\t\t%.2f ms\n",
                time_frame_avg, time_frame_min, time_frame_max, time_frame_last);
            SP_ASSERT(offset < sizeof(metrics_buffer));
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "CPU:\t\t%.2f\t\t%.2f\t\t%.2f\t\t%.2f ms\n",
                time_cpu_avg, time_cpu_min, time_cpu_max, time_cpu_last);
            SP_ASSERT(offset < sizeof(metrics_buffer));
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "GPU:\t\t%.2f\t\t%.2f\t\t%.2f\t\t%.2f ms\n\n",
                time_gpu_avg, time_gpu_min, time_gpu_max, time_gpu_last);
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // gpu
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "GPU\nName:\t\t%s\nMemory:\t%u/%u MB\nAPI:\t\t\t\t%s %s\nDriver:\t\t%s %s\n\n",
                RHI_Device::GetPrimaryPhysicalDevice()->GetName(),
                static_cast<unsigned int>(RHI_Device::MemoryGetAllocatedMb()),
                static_cast<unsigned int>(RHI_Device::MemoryGetAvailableMb()),
                RHI_Context::api_type_str,
                RHI_Context::api_version_cstr ? RHI_Context::api_version_cstr : "N/A",
                RHI_Device::GetPrimaryPhysicalDevice() ? RHI_Device::GetPrimaryPhysicalDevice()->GetVendorName() : "N/A",
                RHI_Device::GetPrimaryPhysicalDevice()->GetDriverVersion());
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // cpu
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "CPU\nName:\t\t\t\t\t%s\nWorker threads:\t%u/%u\nAVX2:\t\t%s\n\n",
                cpu_name,
                static_cast<unsigned int>(ThreadPool::GetWorkingThreadCount()),
                static_cast<unsigned int>(ThreadPool::GetThreadCount()),
#ifdef __AVX2__
                "\t\t\t\tYes"
#else
                "\t\t\t\tNo"
#endif
            );
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // memory
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Memory\n");
            SP_ASSERT(offset < sizeof(metrics_buffer));
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Allocated:\t%.2f MB | Peak: %.2f MB\n",
                Allocator::GetMemoryAllocatedMb(),
                Allocator::GetMemoryAllocatedPeakMb());
            SP_ASSERT(offset < sizeof(metrics_buffer));
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Process:\t\t%.2f MB | Available: %.2f MB | Total: %.2f MB\n\n",
                Allocator::GetMemoryProcessUsedMb(),
                Allocator::GetMemoryAvailableMb(),
                Allocator::GetMemoryTotalMb());
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // display
            const auto& res_render = Renderer::GetResolutionRender();
            const auto& res_output = Renderer::GetResolutionOutput();
            const auto& vp = Renderer::GetViewport();
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Display\nName:\t\t%s\nHz:\t\t\t\t%d\nHDR:\t\t\t%s\nMax nits:\t%u\n"
                "Render:\t\t%u x %u - %.0f%%\nOutput:\t\t%u x %u\nViewport:\t%u x %u\n\n",
                Display::GetName(),
                static_cast<int>(Display::GetRefreshRate()),
                Renderer::GetSwapChain()->IsHdr() ? "Enabled" : "Disabled",
                static_cast<uint32_t>(Display::GetLuminanceMax()),
                static_cast<uint32_t>(res_render.x),
                static_cast<uint32_t>(res_render.y),
                Renderer::GetRenderOptionsPool().GetOption<float>(Renderer_Option::ResolutionScale) * 100.0f,
                static_cast<uint32_t>(res_output.x),
                static_cast<uint32_t>(res_output.y),
                static_cast<uint32_t>(vp.width),
                static_cast<uint32_t>(vp.height));
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // graphics api
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Graphics API\nDraw:\t\t\t\t\t\t\t\t\t\t%u\nInstances:\t\t\t\t\t\t\t\t%u\nIndex buffer bindings:\t\t%u\n"
                "Vertex buffer bindings:\t\t%u\nBarriers:\t\t\t\t\t\t\t\t\t%u\nBindings from pipelines:\t%u/%u\n"
                "Descriptor set capacity:\t%u/%u",
                static_cast<uint32_t>(m_rhi_draw),
                static_cast<uint32_t>(m_rhi_instance_count),
                static_cast<uint32_t>(m_rhi_bindings_buffer_index),
                static_cast<uint32_t>(m_rhi_bindings_buffer_vertex),
                static_cast<uint32_t>(m_rhi_pipeline_barriers),
                static_cast<uint32_t>(m_rhi_bindings_pipeline),
                static_cast<uint32_t>(RHI_Device::GetPipelineCount()),
                static_cast<uint32_t>(m_rhi_descriptor_set_count),
                static_cast<uint32_t>(rhi_max_descriptor_set_count));
            SP_ASSERT(offset < sizeof(metrics_buffer));
        }

        // draw directly from the static buffer
        Renderer::DrawString(metrics_buffer, math::Vector2(0.005f, 0.02f));
    }
}
