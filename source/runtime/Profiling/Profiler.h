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

#pragma once

//= INCLUDES =========
#include <string>
#include <vector>
#include "TimeBlock.h"
//====================

#define SP_PROFILE_CPU_START(name) spartan::Profiler::TimeBlockStart(name, spartan::TimeBlockType::Cpu, nullptr);
#define SP_PROFILE_CPU_END()       spartan::Profiler::TimeBlockEnd();
#define SP_PROFILE_CPU()           ScopedTimeBlock time_block = ScopedTimeBlock(__FUNCTION__);

namespace spartan
{
    class Profiler
    {
    public:
        static void Initialize();
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
        static bool IsCpuStuttering();
        static bool IsGpuStuttering();
        
        // metrics - rhi
        static uint32_t m_rhi_draw;
        static uint32_t m_rhi_instance_count;
        static uint32_t m_rhi_timeblock_count;
        static uint32_t m_rhi_pipeline_barriers;
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
        static uint32_t m_rhi_bindings_pipeline;
        static uint32_t m_rhi_descriptor_set_count;

    private:
        static void ReadTimeBlocks();

        static void ClearRhiMetrics()
        {
            m_rhi_draw                       = 0;
            m_rhi_instance_count             = 0;
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
            m_rhi_bindings_pipeline          = 0;
        }

        static void DrawPerformanceMetrics();
        static TimeBlock* GetLastIncompleteTimeBlock(const TimeBlockType type);
    };

    class ScopedTimeBlock
    {
    public:
        ScopedTimeBlock(const char* name = nullptr)
        {
            Profiler::TimeBlockStart(name, spartan::TimeBlockType::Cpu);
        }

        ~ScopedTimeBlock()
        {
            Profiler::TimeBlockEnd();
        }
    };
}
