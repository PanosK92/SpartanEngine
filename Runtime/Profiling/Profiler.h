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

#pragma once

//= INCLUDES ===========================
#include <string>
#include <vector>
#include "TimeBlock.h"
#include "../Core/ISubsystem.h"
#include "../Core/Stopwatch.h"
#include "../Core/Spartan_Definitions.h"
//======================================

#define TIME_BLOCK_START_NAMED(profiler, name)  profiler->TimeBlockStart(name, Spartan::TimeBlock_Type::TimeBlock_Cpu, nullptr);
#define TIME_BLOCK_END(profiler)                profiler->TimeBlockEnd();
#define SCOPED_TIME_BLOCK(profiler)             ScopedTimeBlock time_block = ScopedTimeBlock(profiler, __FUNCTION__)

namespace Spartan
{
    class Context;
    class Timer;
    class ResourceCache;
    class Renderer;
    class Variant;
    class Timer;

    class SPARTAN_CLASS Profiler : public ISubsystem
    {
    public:
        Profiler(Context* context);
        ~Profiler();

        //= Subsystem =======================
        bool Initialize() override;
        void Tick(float delta_time) override;
        //===================================

        void OnFrameEnd();
        void TimeBlockStart(const char* func_name, TimeBlock_Type type, RHI_CommandList* cmd_list = nullptr);
        void TimeBlockEnd();
        void ResetMetrics();

        // Properties
        void SetProfilingEnabledCpu(const bool enabled)    { m_profile_cpu_enabled = enabled; }
        void SetProfilingEnabledGpu(const bool enabled)    { m_profile_gpu_enabled = enabled; }
        const std::string& GetMetrics()                 const { return m_metrics; }
        const auto& GetTimeBlocks()                     const { return m_time_blocks_read; }
        float GetTimeCpuLast()                          const { return m_time_cpu_last; }
        float GetTimeGpuLast()                          const { return m_time_gpu_last; }
        float GetTimeFrameLast()                        const { return m_time_frame_last; }
        float GetFps()                                  const { return m_fps; }
        float GetUpdateInterval()                       const { return m_profiling_interval_sec; }
        void SetUpdateInterval(float internval)               { m_profiling_interval_sec = internval; }
        const auto& GpuGetName()                        const { return m_gpu_name; }
        auto GpuGetMemoryAvailable()                    const { return m_gpu_memory_available; }
        auto GpuGetMemoryUsed()                         const { return m_gpu_memory_used; }
        bool IsCpuStuttering()                          const { return m_is_stuttering_cpu; }
        bool IsGpuStuttering()                          const { return m_is_stuttering_gpu; }
        
        // Metrics - RHI
        uint32_t m_rhi_draw                                = 0;
        uint32_t m_rhi_dispatch                         = 0;
        uint32_t m_rhi_bindings_buffer_index            = 0;
        uint32_t m_rhi_bindings_buffer_vertex            = 0;
        uint32_t m_rhi_bindings_buffer_constant         = 0;
        uint32_t m_rhi_bindings_sampler                    = 0;
        uint32_t m_rhi_bindings_texture_sampled            = 0;
        uint32_t m_rhi_bindings_shader_vertex            = 0;
        uint32_t m_rhi_bindings_shader_pixel            = 0;
        uint32_t m_rhi_bindings_shader_compute          = 0;
        uint32_t m_rhi_bindings_render_target            = 0;
        uint32_t m_rhi_bindings_texture_storage         = 0;
        uint32_t m_rhi_bindings_descriptor_set          = 0;     
        uint32_t m_rhi_bindings_pipeline                = 0;
        uint32_t m_rhi_pipeline_barriers                = 0;

        // Metrics - Renderer
        uint32_t m_renderer_meshes_rendered = 0;

        // Metrics - Time
        float m_time_frame_avg  = 0.0f;
        float m_time_frame_min  = std::numeric_limits<float>::max();
        float m_time_frame_max  = std::numeric_limits<float>::lowest();
        float m_time_frame_last = 0.0f;
        float m_time_cpu_avg    = 0.0f;
        float m_time_cpu_min    = std::numeric_limits<float>::max();
        float m_time_cpu_max    = std::numeric_limits<float>::lowest();
        float m_time_cpu_last   = 0.0f;
        float m_time_gpu_avg    = 0.0f;
        float m_time_gpu_min    = std::numeric_limits<float>::max();
        float m_time_gpu_max    = std::numeric_limits<float>::lowest();
        float m_time_gpu_last   = 0.0f;

    private:
        void ClearRhiMetrics()
        {
            m_rhi_draw                          = 0;
            m_rhi_dispatch                      = 0;
            m_renderer_meshes_rendered          = 0;
            m_rhi_bindings_buffer_index         = 0;
            m_rhi_bindings_buffer_vertex        = 0;
            m_rhi_bindings_buffer_constant      = 0;
            m_rhi_bindings_sampler              = 0;
            m_rhi_bindings_texture_sampled      = 0;
            m_rhi_bindings_shader_vertex        = 0;
            m_rhi_bindings_shader_pixel         = 0;
            m_rhi_bindings_shader_compute       = 0;
            m_rhi_bindings_render_target        = 0;
            m_rhi_bindings_texture_storage      = 0;
            m_rhi_bindings_descriptor_set       = 0;
            m_rhi_bindings_pipeline             = 0;
            m_rhi_pipeline_barriers             = 0;
        }

        TimeBlock* GetNewTimeBlock();
        TimeBlock* GetLastIncompleteTimeBlock(TimeBlock_Type type = TimeBlock_Undefined);
        void ComputeFps(float delta_time);
        void AcquireGpuData();
        void UpdateRhiMetricsString();

        // Profiling options
        bool m_profile_cpu_enabled            = true; // cheap
        bool m_profile_gpu_enabled            = true; // expensive
        float m_profiling_interval_sec        = 0.3f;
        float m_time_since_profiling_sec    = m_profiling_interval_sec;

        // Time blocks (double buffered)
        uint32_t m_time_block_capacity    = 200;
        uint32_t m_time_block_count        = 0;
        std::vector<TimeBlock> m_time_blocks_write;
        std::vector<TimeBlock> m_time_blocks_read;

        // FPS
        float m_delta_time          = 0.0f;
        float m_fps                 = 0.0f;
        float m_time_passed         = 0.0f;
        uint32_t m_frames_since_last_fps_computation = 0;

        // Hardware - GPU
        std::string m_gpu_name            = "N/A";
        std::string m_gpu_driver        = "N/A";
        std::string m_gpu_api           = "N/A";
        uint32_t m_gpu_memory_available    = 0;
        uint32_t m_gpu_memory_used        = 0;

        // Stutter detection
        float m_stutter_delta_ms    = 0.5f;
        bool m_is_stuttering_cpu    = false;
        bool m_is_stuttering_gpu    = false;

        // Misc
        std::string m_metrics = "N/A";
        bool m_profile = true;
        bool m_increase_capacity = 0.0f;
        bool m_allow_time_block_end = true;
    
        // Dependencies
        ResourceCache* m_resource_manager    = nullptr;
        Renderer* m_renderer                = nullptr;
        Timer* m_timer                      = nullptr;
    };

    class ScopedTimeBlock
    {
    public:
        ScopedTimeBlock(Profiler* profiler, const char* name = nullptr)
        {
            this->profiler = profiler;
            profiler->TimeBlockStart(name, Spartan::TimeBlock_Type::TimeBlock_Cpu);
        }

        ~ScopedTimeBlock()
        {
            profiler->TimeBlockEnd();
        }

    private:
        Profiler* profiler = nullptr;
    };
}
