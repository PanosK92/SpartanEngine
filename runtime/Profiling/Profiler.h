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

#pragma once

//= INCLUDES ===================
#include <string>
#include <vector>
#include "TimeBlock.h"
#include "../Core/Definitions.h"
//==============================

#define SP_PROFILE_CPU_START(name) Spartan::Profiler::TimeBlockStart(name, Spartan::TimeBlockType::Cpu, nullptr);
#define SP_PROFILE_CPU_END()       Spartan::Profiler::TimeBlockEnd();
#define SP_PROFILE_CPU()           ScopedTimeBlock time_block = ScopedTimeBlock(__FUNCTION__);

namespace Spartan
{
    enum class ProfilerGranularity
    {
        Light,
        Full
    };

    class SP_CLASS Profiler
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void PreTick();
        static void PostTick();

        static void TimeBlockStart(const char* func_name, TimeBlockType type, RHI_CommandList* cmd_list = nullptr);
        static void TimeBlockEnd();
        static void ClearMetrics();
        
        // properties
        static const std::vector<TimeBlock>& GetTimeBlocks();
        static float GetTimeCpuLast();
        static float GetTimeGpuLast();
        static float GetTimeFrameLast();
        static float GetFps();
        static float GetUpdateInterval();
        static void SetUpdateInterval(float interval);
        static const std::string& GpuGetName();
        static uint32_t GpuGetMemoryAvailable();
        static uint32_t GpuGetMemoryUsed();
        static bool IsCpuStuttering();
        static bool IsGpuStuttering();
        
        // metrics - rhi
        static uint32_t m_rhi_draw;
        static uint32_t m_rhi_bindings_buffer_index;
        static uint32_t m_rhi_bindings_buffer_vertex;
        static uint32_t m_rhi_bindings_buffer_constant;
        static uint32_t m_rhi_bindings_buffer_structured;
        static uint32_t m_rhi_bindings_sampler;
        static uint32_t m_rhi_bindings_texture_sampled;
        static uint32_t m_rhi_bindings_shader_vertex;
        static uint32_t m_rhi_bindings_shader_pixel;
        static uint32_t m_rhi_bindings_shader_compute;
        static uint32_t m_rhi_bindings_render_target;
        static uint32_t m_rhi_bindings_texture_storage;
        static uint32_t m_rhi_bindings_descriptor_set;
        static uint32_t m_rhi_bindings_pipeline;
        static uint32_t m_rhi_pipeline_barriers;
        static uint32_t m_rhi_timeblock_count;

        // metrics - time
        static float m_time_frame_avg ;
        static float m_time_frame_min ;
        static float m_time_frame_max ;
        static float m_time_frame_last;
        static float m_time_cpu_avg;
        static float m_time_cpu_min;
        static float m_time_cpu_max;
        static float m_time_cpu_last;
        static float m_time_gpu_avg;
        static float m_time_gpu_min;
        static float m_time_gpu_max;
        static float m_time_gpu_last;

        // misc
        static uint32_t m_descriptor_set_count;
        static ProfilerGranularity GetGranularity();
        static bool IsValidationLayerEnabled();
        static bool IsGpuAssistedValidationEnabled();
        static bool IsGpuMarkingEnabled();
        static bool IsGpuTimingEnabled();
        static void SetGpuTimingEnabled(const bool enabled);
        static bool IsRenderdocEnabled();
        static bool IsShaderOptimizationEnabled();

    private:
        static void SwapBuffers();

        static void ClearRhiMetrics()
        {
            m_rhi_draw                       = 0;
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
            m_rhi_bindings_descriptor_set    = 0;
            m_rhi_bindings_pipeline          = 0;
            m_rhi_pipeline_barriers          = 0;
            m_rhi_timeblock_count            = 0;
        }

        static TimeBlock* GetNewTimeBlock();
        static void AcquireGpuData();
        static void DrawPerformanceMetrics();
        static TimeBlock* GetLastIncompleteTimeBlock(TimeBlockType type = TimeBlockType::Undefined);
    };

    class ScopedTimeBlock
    {
    public:
        ScopedTimeBlock(const char* name = nullptr)
        {
            Profiler::TimeBlockStart(name, Spartan::TimeBlockType::Cpu);
        }

        ~ScopedTimeBlock()
        {
            Profiler::TimeBlockEnd();
        }
    };
}
