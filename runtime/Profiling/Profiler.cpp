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

//= INCLUDES =========================
#include "pch.h"
#include "Profiler.h"
#include "RenderDoc.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_SwapChain.h"
#include "../Core/ThreadPool.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Display/Display.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    ProfilerGranularity granularity = ProfilerGranularity::Light;

    // metrics - rhi
    uint32_t Profiler::m_rhi_draw                       = 0;
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

    // metrics - time
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

    // misc
    uint32_t Profiler::m_descriptor_set_count = 0;

    namespace
    {
        //= DEBUGGING OPTIONS ========================================================================================================
        bool is_validation_layer_enabled        = false; // cpu cost: high - per draw cost, especially high with large bindless arrays
        bool is_gpu_assisted_validation_enabled = false; // cpu cost: high - per draw cost
        bool is_renderdoc_enabled               = false; // cpu cost: high - intercepts every API call and wraps it
        bool is_gpu_marking_enabled             = true;  // cpu cost: imperceptible
        bool is_gpu_timing_enabled              = true;  // cpu cost: imperceptible
        bool is_shader_optimization_enabled     = true;  // gpu cost: high (when disabled)
        //============================================================================================================================

        // profiling options
        const uint32_t initial_capacity = 256;
        bool profile_cpu                = true;
        bool profile_gpu                = true;
        float profiling_interval_sec    = 0.25f;
        float time_since_profiling_sec  = profiling_interval_sec;

        const uint32_t frames_to_accumulate = static_cast<uint32_t>(4.0f / profiling_interval_sec);
        const float weight_delta            = 1.0f / static_cast<float>(frames_to_accumulate);
        const float weight_history          = (1.0f - weight_delta);

        // time blocks (double buffered)
        int m_time_block_index = -1;
        vector<TimeBlock> m_time_blocks_write;
        vector<TimeBlock> m_time_blocks_read;

        // fps
        float m_fps = 0.0f;

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

        // metric drawing
        ostringstream oss_metrics;
        string metrics_str;
        float metrics_time_since_last_update = profiling_interval_sec;

        // misc
        bool poll                 = false;
        bool increase_capacity    = false;
        bool allow_time_block_end = true;

        string format_float(float value)
        {
            stringstream ss;

            // clamp to a certain range to avoid padding and alignment headaches
            value = Math::Helper::Clamp(value, 0.0f, 99.99f);

            // set fixed-point notation with 2 decimal places
            ss << fixed << setprecision(2);

            // output the integer part with the fill character '0' and the minimum width of 2 characters
            int integer_part = static_cast<int>(value);
            ss << setfill('0') << setw(2) << integer_part;

            // output the decimal point and decimal part
            float decimal_part = value - integer_part;
            ss << "." << setfill('0') << setw(2) << static_cast<int>(round(decimal_part * 100));

            return ss.str();
        }
    }
  
    void Profiler::Initialize()
    {
        m_time_blocks_read.reserve(initial_capacity);
        m_time_blocks_read.resize(initial_capacity);
        m_time_blocks_write.reserve(initial_capacity);
        m_time_blocks_write.resize(initial_capacity);

        if (IsRenderdocEnabled())
        {
            RenderDoc::OnPreDeviceCreation();
        }
    }

    void Profiler::Shutdown()
    {
        ClearRhiMetrics();
        RenderDoc::Shutdown();
    }

    void Profiler::PreTick()
    {
        if (!Profiler::IsGpuTimingEnabled())
            return;

        if (increase_capacity)
        {
            SwapBuffers();

            // double the size
            const uint32_t size_old = static_cast<uint32_t>(m_time_blocks_write.size());
            const uint32_t size_new = size_old << 1;

            m_time_blocks_read.reserve(size_new);
            m_time_blocks_read.resize(size_new);
            m_time_blocks_write.reserve(size_new);
            m_time_blocks_write.resize(size_new);

            increase_capacity = false;
            poll              = true;

            SP_LOG_WARNING("Time block list has grown to %d. Consider making the default capacity as large by default, to avoid re-allocating.", size_new);
        }

        ClearRhiMetrics();
    }

    void Profiler::PostTick()
    {
        // compute timings
        {
            is_stuttering_cpu = m_time_cpu_last > (m_time_cpu_avg + stutter_delta_ms);
            is_stuttering_gpu = m_time_gpu_last > (m_time_gpu_avg + stutter_delta_ms);
            m_time_cpu_last   = 0.0f;
            m_time_gpu_last   = 0.0f;

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

            // cpu
            m_time_cpu_avg = (m_time_cpu_avg * weight_history) + m_time_cpu_last * weight_delta;
            m_time_cpu_min = Math::Helper::Min(m_time_cpu_min, m_time_cpu_last);
            m_time_cpu_max = Math::Helper::Max(m_time_cpu_max, m_time_cpu_last);

            // gpu
            m_time_gpu_avg = (m_time_gpu_avg * weight_history) + m_time_gpu_last * weight_delta;
            m_time_gpu_min = Math::Helper::Min(m_time_gpu_min, m_time_gpu_last);
            m_time_gpu_max = Math::Helper::Max(m_time_gpu_max, m_time_gpu_last);

            // frame
            m_time_frame_last = static_cast<float>(Timer::GetDeltaTimeMs());
            m_time_frame_avg  = (m_time_frame_avg * weight_history) + m_time_frame_last * weight_delta;
            m_time_frame_min  = Math::Helper::Min(m_time_frame_min, m_time_frame_last);
            m_time_frame_max  = Math::Helper::Max(m_time_frame_max, m_time_frame_last);

            // fps  
            m_fps = 1000.0f / m_time_frame_avg;
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

        if (poll && Profiler::IsGpuTimingEnabled())
        {
            AcquireGpuData();
            SwapBuffers();
        }

        if (Renderer::GetOption<bool>(Renderer_Option::PerformanceMetrics))
        {
            DrawPerformanceMetrics();
        }
    }

    void Profiler::SwapBuffers()
    {
        // copy completed time blocks write to time blocks read vector (double buffering)
        {
            uint32_t pass_index_gpu = 0;

            for (uint32_t i = 0; i < static_cast<uint32_t>(m_time_blocks_read.size()); i++)
            {
                TimeBlock& time_block = m_time_blocks_write[i];

                // compute time block duration
                if (time_block.IsComplete())
                {
                    if (time_block.GetType() == TimeBlockType::Gpu)
                    {
                        pass_index_gpu += 2;
                    }
                }
                else if (time_block.GetType() != TimeBlockType::Undefined) // if undefined, then it wasn't used this frame, nothing wrong with that
                {
                    SP_LOG_WARNING("TimeBlockEnd() was not called for time block \"%s\"", time_block.GetName());
                }

                // copy over
                m_time_blocks_read[i] = time_block;

                // reset
                time_block.Reset();
            }
        }
        
        m_time_block_index = -1;
    }

    void Profiler::TimeBlockStart(const char* func_name, TimeBlockType type, RHI_CommandList* cmd_list /*= nullptr*/)
    {
        if (!Profiler::IsGpuTimingEnabled() || !poll)
            return;

        const bool can_profile_cpu = (type == TimeBlockType::Cpu) && profile_cpu;
        const bool can_profile_gpu = (type == TimeBlockType::Gpu) && profile_gpu;

        if (!can_profile_cpu && !can_profile_gpu)
            return;

        // last incomplete block of the same type, is the parent
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

    TimeBlock* Profiler::GetNewTimeBlock()
    {
        // increase capacity if needed
        if (m_time_block_index + 1 >= static_cast<int>(m_time_blocks_write.size()))
        {
            increase_capacity = true;
            return nullptr;
        }

        // return a time block
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
            gpu_name             = physical_device->GetName();
            gpu_memory_used      = RHI_Device::MemoryGetUsageMb();
            gpu_memory_available = RHI_Device::MemoryGetBudgetMb();
            gpu_driver           = physical_device->GetDriverVersion();
            gpu_api              = RHI_Context::api_version_str;
        }
    }

    void Profiler::DrawPerformanceMetrics()
    {
        metrics_time_since_last_update += static_cast<float>(Timer::GetDeltaTimeSec());
        if (metrics_time_since_last_update < profiling_interval_sec)
        {
            Renderer::DrawString(metrics_str, Math::Vector2(0.01f, 0.01f));
            return;
        }
        metrics_time_since_last_update = 0.0f;
        
        const uint32_t texture_count  = ResourceCache::GetResourceCount(ResourceType::Texture) + ResourceCache::GetResourceCount(ResourceType::Texture2d) + ResourceCache::GetResourceCount(ResourceType::TextureCube);
        const uint32_t material_count = ResourceCache::GetResourceCount(ResourceType::Material);
        const uint32_t pipeline_count = RHI_Device::GetPipelineCount();

        // get the graphics driver vendor
        string api_vendor_name = "NVIDIA";
        if (RHI_Device::GetPrimaryPhysicalDevice()->IsAmd())
        {
            api_vendor_name = "AMD";
        }

        // clear
        oss_metrics.str("");
        oss_metrics.clear();

        // set fixed-point notation with 2 decimal places
        oss_metrics << fixed << setprecision(2);

        // overview
        oss_metrics
            << "FPS:\t\t\t" << m_fps << endl
            << "Time:\t\t\t"  << m_time_frame_avg << " ms" << endl
            << "Frame:\t\t"   << Renderer::GetFrameNum() << endl;

        // detailed times
        oss_metrics
            << endl     << "\t\t" << "\t\tavg"                      << "\t\t" << "\tmin"                        << "\t\t" << "\tmax"                        << "\t\t" << "\tlast"                                 << endl
            << "Total:" << "\t\t" << format_float(m_time_frame_avg) << "\t\t" << format_float(m_time_frame_min) << "\t\t" << format_float(m_time_frame_max) << "\t\t" << format_float(m_time_frame_last) << " ms" << endl
            << "CPU:"   << "\t\t" << format_float(m_time_cpu_avg)   << "\t\t" << format_float(m_time_cpu_min)   << "\t\t" << format_float(m_time_cpu_max)   << "\t\t" << format_float(m_time_cpu_last)   << " ms" << endl
            << "GPU:"   << "\t\t" << format_float(m_time_gpu_avg)   << "\t\t" << format_float(m_time_gpu_min)   << "\t\t" << format_float(m_time_gpu_max)   << "\t\t" << format_float(m_time_gpu_last)   << " ms" << endl;

        // gpu
        oss_metrics << endl << "GPU" << endl
            << "Name:\t\t\t"    << gpu_name << endl
            << "Memory:\t\t"    << gpu_memory_used << "/" << gpu_memory_available << " MB" << endl
            << "API:\t\t\t\t\t" << RHI_Context::api_type_str << "\t" << gpu_api << endl
            << "Driver:\t\t\t"  << RHI_Device::GetPrimaryPhysicalDevice()->GetVendorName() << "\t\t" << gpu_driver << endl;

        // display
        float resolution_scale = Renderer::GetOption<float>(Renderer_Option::ResolutionScale);
        oss_metrics << "\nDisplay\n"
            << "Render:\t\t\t" << static_cast<uint32_t>(Renderer::GetResolutionRender().x) << "x" << static_cast<int>(Renderer::GetResolutionRender().y) << " - " << resolution_scale * 100.0f << "%" << endl
            << "Output:\t\t\t" << static_cast<uint32_t>(Renderer::GetResolutionOutput().x) << "x" << static_cast<int>(Renderer::GetResolutionOutput().y) << endl
            << "Viewport:\t\t" << static_cast<uint32_t>(Renderer::GetViewport().width)     << "x" << static_cast<int>(Renderer::GetViewport().height)    << endl
            << "HDR:\t\t\t\t"  << (Renderer::GetSwapChain()->IsHdr() ? "Enabled" : "Disabled") << endl
            << "Max nits:\t\t" << Display::GetLuminanceMax() << endl;

        // cpu
        oss_metrics << endl << "CPU" << endl
            << "Worker threads: " << ThreadPool::GetWorkingThreadCount() << "/" << ThreadPool::GetThreadCount() << endl;

        // api calls
        oss_metrics << "\nAPI calls" << endl;
        oss_metrics << "Draw:\t\t\t\t\t\t\t\t\t\t\t" << m_rhi_draw << endl;
        oss_metrics
            << "Index buffer bindings:\t\t\t"  << m_rhi_bindings_buffer_index   << endl
            << "Vertex buffer bindings:\t\t"   << m_rhi_bindings_buffer_vertex  << endl
            << "Descriptor set bindings:\t\t"  << m_rhi_bindings_descriptor_set << endl
            << "Pipeline bindings:\t\t\t\t\t"  << m_rhi_bindings_pipeline       << endl
            << "Pipeline barriers:\t\t\t\t\t"  << m_rhi_pipeline_barriers       << endl;

        // resources
        oss_metrics << "\nResources\n"
            << "Textures:\t\t\t\t\t\t\t\t"  << texture_count          << endl
            << "Materials:\t\t\t\t\t\t\t"   << material_count         << endl
            << "Pipelines:\t\t\t\t\t\t\t\t" << pipeline_count         << endl
            << "Descriptor set capacity:\t" << m_descriptor_set_count << "/" << rhi_max_descriptor_set_count;

        // draw at the top-left of the screen
        metrics_str = oss_metrics.str();
        Renderer::DrawString(metrics_str, Math::Vector2(0.01f, 0.01f));
    }

    ProfilerGranularity Profiler::GetGranularity()
    {
        return granularity;
    }

    bool Profiler::IsValidationLayerEnabled()
    {
        return is_validation_layer_enabled;
    }

    bool Profiler::IsGpuAssistedValidationEnabled()
    {
        return is_gpu_assisted_validation_enabled;
    }

    bool Profiler::IsGpuMarkingEnabled()
    {
        return is_gpu_marking_enabled;
    }

    bool Profiler::IsGpuTimingEnabled()
    {
        return is_gpu_timing_enabled;
    }

    void Profiler::SetGpuTimingEnabled(const bool enabled)
    {
        is_gpu_timing_enabled = enabled;
    }

    bool Profiler::IsRenderdocEnabled()
    {
        return is_renderdoc_enabled;
    }

    bool Profiler::IsShaderOptimizationEnabled()
    {
        return is_shader_optimization_enabled;
    }
}
