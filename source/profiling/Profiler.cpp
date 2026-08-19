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

//= INCLUDES =========================
#include "pch.h"
#include "Profiler.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Implementation.h"
#include "../rhi/RHI_SwapChain.h"
#include "../core/ThreadPool.h"
#include "../core/Debugging.h"
#include "../core/Timer.h"
#include "../rendering/Renderer.h"
#include "../rhi/RHI_Viewport.h"
#include "../display/Display.h"
#include "../memory/Allocator.h"
#include "../file_system/FileSystem.h"
#include <array>
#include <fstream>
#include <thread>
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
    uint32_t Profiler::m_rhi_layout_barriers            = 0;
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
    uint32_t Profiler::m_rhi_timestamps_dropped         = 0;

    namespace
    {
        // profiling
        const uint32_t initial_timeblock_capacity = 1024;
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
        uint32_t timing_sample_count   = 0;

        // time blocks (double buffered)
        int m_time_block_index = -1;
        vector<TimeBlock> m_time_blocks_write;
        vector<TimeBlock> m_time_blocks_read;
        uint32_t incomplete_blocks_last = 0;

        // stutter detection
        float stutter_delta_ms = 1.0f;
        bool is_stuttering_cpu = false;
        bool is_stuttering_gpu = false;

        // misc
        bool poll          = false;
        bool is_visualized = false;
        thread::id profiling_thread_id;

        // command lists used during the current poll frame (for deferred timestamp readback)
        vector<RHI_CommandList*> cmd_lists_used;

        // frame start reference for timeline
        chrono::high_resolution_clock::time_point frame_start_cpu;
        float frame_duration_ms = 0.0f;
        float captured_frame_duration_ms = 0.0f;
        float captured_pacing_time_ms = 0.0f;

        // csv capture
        bool capture_requested = false;
        bool capture_this_frame = false;
        bool capture_gpu_sample_this_frame = false;
        bool capture_stop_pending = false;
        bool capture_reset_metrics_pending = false;
        uint64_t capture_frame_count = 0;
        double capture_start_time_ms = 0.0;
        string capture_file_path;
        string capture_error;
        string capture_buffer;
        ofstream capture_stream;
        const size_t capture_buffer_flush_size =
            1024 * 1024;
        float capture_write_time_ms = 0.0f;

        // cpu
        const char* cpu_name = "N/A";
        bool is_cpu_wait(const char* name)
        {
            if (!name)
            {
                return false;
            }

            return
                strcmp(name, "frame_slot_wait") == 0 ||
                strcmp(name, "frame_acquire") == 0 ||
                strcmp(name, "frame_present") == 0 ||
                strcmp(name, "queue_wait_idle") == 0 ||
                strncmp(name, "cmd_wait", 8) == 0;
        }

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

        enum CaptureColumn : uint32_t
        {
            CaptureColumn_RowType,
            CaptureColumn_CaptureFrame,
            CaptureColumn_EngineFrame,
            CaptureColumn_ElapsedMs,
            CaptureColumn_BlockIndex,
            CaptureColumn_BlockId,
            CaptureColumn_ParentId,
            CaptureColumn_TreeDepth,
            CaptureColumn_Name,
            CaptureColumn_BlockType,
            CaptureColumn_Queue,
            CaptureColumn_StartMs,
            CaptureColumn_EndMs,
            CaptureColumn_DurationMs,
            CaptureColumn_WallMs,
            CaptureColumn_CpuMs,
            CaptureColumn_GpuSpanMs,
            CaptureColumn_PacingMs,
            CaptureColumn_WaitMs,
            CaptureColumn_AcquireMs,
            CaptureColumn_SubmitMs,
            CaptureColumn_PresentMs,
            CaptureColumn_ProfilerReadbackMs,
            CaptureColumn_ProfilerSerializeMs,
            CaptureColumn_ProfilerWriteMsPrevious,
            CaptureColumn_FrameAvgMs,
            CaptureColumn_FrameMinMs,
            CaptureColumn_FrameMaxMs,
            CaptureColumn_CpuAvgMs,
            CaptureColumn_CpuMinMs,
            CaptureColumn_CpuMaxMs,
            CaptureColumn_GpuAvgMs,
            CaptureColumn_GpuMinMs,
            CaptureColumn_GpuMaxMs,
            CaptureColumn_Fps,
            CaptureColumn_FpsLimit,
            CaptureColumn_CpuStutter,
            CaptureColumn_GpuStutter,
            CaptureColumn_IncompleteBlocks,
            CaptureColumn_RhiDraws,
            CaptureColumn_RhiInstances,
            CaptureColumn_RhiTimeblocks,
            CaptureColumn_RhiBarriers,
            CaptureColumn_RhiBindIndex,
            CaptureColumn_RhiBindVertex,
            CaptureColumn_RhiBindConstant,
            CaptureColumn_RhiBindStructured,
            CaptureColumn_RhiBindSampler,
            CaptureColumn_RhiBindTextureSampled,
            CaptureColumn_RhiBindShaderVertex,
            CaptureColumn_RhiBindShaderPixel,
            CaptureColumn_RhiBindShaderCompute,
            CaptureColumn_RhiBindRenderTarget,
            CaptureColumn_RhiBindTextureStorage,
            CaptureColumn_RhiBindPipeline,
            CaptureColumn_RhiDescriptorSets,
            CaptureColumn_RhiTimestampsDropped,
            CaptureColumn_RhiPipelineCount,
            CaptureColumn_VramAllocatedMb,
            CaptureColumn_VramAvailableMb,
            CaptureColumn_VramTotalMb,
            CaptureColumn_RamAllocatedMb,
            CaptureColumn_RamPeakMb,
            CaptureColumn_RamProcessMb,
            CaptureColumn_RamAvailableMb,
            CaptureColumn_RamTotalMb,
            CaptureColumn_Api,
            CaptureColumn_Mode,
            CaptureColumn_GpuTimingEnabled,
            CaptureColumn_GpuTimingValid,
            CaptureColumn_CpuScope,
            CaptureColumn_Count
        };

        string csv_escape(const string& value)
        {
            if (
                value.find_first_of(",\"\r\n") ==
                string::npos
            )
            {
                return value;
            }

            string escaped = "\"";
            for (const char character : value)
            {
                if (character == '"')
                {
                    escaped += "\"\"";
                }
                else
                {
                    escaped += character;
                }
            }
            escaped += '"';
            return escaped;
        }

        string format_float(const double value)
        {
            char text[64];
            snprintf(
                text,
                sizeof(text),
                "%.6f",
                value
            );
            return text;
        }

        void append_capture_row(
            string& destination,
            const array<
                string,
                CaptureColumn_Count
            >& fields
        )
        {
            for (
                uint32_t i = 0;
                i < CaptureColumn_Count;
                i++
            )
            {
                if (i != 0)
                {
                    destination += ',';
                }
                destination += csv_escape(fields[i]);
            }
            destination += '\n';
        }

        const char* queue_name(
            const RHI_Queue_Type type
        )
        {
            switch (type)
            {
                case RHI_Queue_Type::Graphics:
                    return "graphics";
                case RHI_Queue_Type::Compute:
                    return "compute";
                case RHI_Queue_Type::Copy:
                    return "copy";
                case RHI_Queue_Type::Present:
                    return "present";
                default:
                    return "none";
            }
        }

        float captured_block_duration(
            const char* name
        )
        {
            float duration = 0.0f;
            for (
                const TimeBlock& block :
                m_time_blocks_read
            )
            {
                if (
                    block.IsComplete() &&
                    block.GetType() ==
                        TimeBlockType::Cpu &&
                    block.GetName() &&
                    strcmp(
                        block.GetName(),
                        name
                    ) == 0
                )
                {
                    duration += block.GetDuration();
                }
            }
            return duration;
        }

        bool write_capture_buffer()
        {
            if (
                capture_buffer.empty() ||
                !capture_stream.is_open()
            )
            {
                return true;
            }

            capture_stream.write(
                capture_buffer.data(),
                static_cast<streamsize>(
                    capture_buffer.size()
                )
            );
            if (!capture_stream.good())
            {
                capture_error =
                    "failed to write profiler capture";
                return false;
            }
            capture_buffer.clear();
            return true;
        }

        void record_capture_frame(
            const float profiler_readback_ms
        )
        {
            const auto serialize_start =
                chrono::high_resolution_clock::now();
            const uint64_t engine_frame =
                Renderer::GetFrameNumber();
            const string api =
                Renderer::GetRhiApiType() ==
                    RHI_Api_Type::Vulkan ?
                        "vulkan" :
                        "d3d12";
            const bool gpu_timing_enabled =
                Debugging::IsGpuTimingEnabled();
            const bool gpu_timing_valid =
                any_of(
                    m_time_blocks_read.begin(),
                    m_time_blocks_read.end(),
                    [](const TimeBlock& block)
                    {
                        return
                            block.IsComplete() &&
                            block.GetType() ==
                                TimeBlockType::Gpu &&
                            block.GetDuration() > 0.0f;
                    }
                );
            const char* capture_mode =
                capture_gpu_sample_this_frame ?
                    "cpu_per_frame_gpu_sample" :
                    "low_overhead_cpu_per_frame";
            string block_rows;
            block_rows.reserve(
                m_time_blocks_read.size() *
                192
            );

            uint32_t block_index = 0;
            for (
                const TimeBlock& block :
                m_time_blocks_read
            )
            {
                if (!block.IsComplete())
                {
                    continue;
                }

                array<
                    string,
                    CaptureColumn_Count
                > fields;
                fields[CaptureColumn_RowType] =
                    "block";
                fields[CaptureColumn_CaptureFrame] =
                    to_string(capture_frame_count);
                fields[CaptureColumn_EngineFrame] =
                    to_string(engine_frame);
                fields[CaptureColumn_BlockIndex] =
                    to_string(block_index++);
                fields[CaptureColumn_BlockId] =
                    to_string(block.GetId());
                fields[CaptureColumn_ParentId] =
                    to_string(block.GetParentId());
                fields[CaptureColumn_TreeDepth] =
                    to_string(block.GetTreeDepth());
                fields[CaptureColumn_Name] =
                    block.GetName() ?
                        block.GetName() :
                        "";
                fields[CaptureColumn_BlockType] =
                    block.GetType() ==
                        TimeBlockType::Cpu ?
                            "cpu" :
                            "gpu";
                fields[CaptureColumn_Queue] =
                    queue_name(block.GetQueueType());
                fields[CaptureColumn_StartMs] =
                    format_float(block.GetStartMs());
                fields[CaptureColumn_EndMs] =
                    format_float(block.GetEndMs());
                fields[CaptureColumn_DurationMs] =
                    format_float(block.GetDuration());
                fields[CaptureColumn_Api] = api;
                fields[CaptureColumn_Mode] =
                    capture_mode;
                fields[CaptureColumn_GpuTimingEnabled] =
                    gpu_timing_enabled ? "1" : "0";
                if (
                    block.GetType() ==
                    TimeBlockType::Gpu
                )
                {
                    fields[
                        CaptureColumn_GpuTimingValid
                    ] =
                        gpu_timing_valid ?
                            "1" :
                            "0";
                }
                fields[CaptureColumn_CpuScope] =
                    "main_thread";
                fields[
                    CaptureColumn_RhiTimestampsDropped
                ] =
                    to_string(
                        Profiler::
                            m_rhi_timestamps_dropped
                    );
                append_capture_row(
                    block_rows,
                    fields
                );
            }

            const float wait_ms =
                captured_block_duration(
                    "frame_slot_wait"
                ) +
                captured_block_duration(
                    "cmd_wait_graphics"
                ) +
                captured_block_duration(
                    "cmd_wait_compute"
                ) +
                captured_block_duration(
                    "cmd_wait_copy"
                ) +
                captured_block_duration(
                    "cmd_wait_present"
                ) +
                captured_block_duration(
                    "queue_wait_idle"
                );
            const float acquire_ms =
                captured_block_duration(
                    "frame_acquire"
                );
            const float submit_ms =
                captured_block_duration(
                    "queue_submit_graphics"
                ) +
                captured_block_duration(
                    "queue_submit_compute"
                ) +
                captured_block_duration(
                    "queue_submit_copy"
                ) +
                captured_block_duration(
                    "queue_submit_present"
                ) +
                captured_block_duration(
                    "queue_submit"
                );
            const float present_ms =
                captured_block_duration(
                    "frame_present"
                );

            const float serialize_ms =
                static_cast<float>(
                    chrono::duration<double, milli>(
                        chrono::high_resolution_clock::now() -
                        serialize_start
                    ).count()
                );
            array<
                string,
                CaptureColumn_Count
            > fields;
            fields[CaptureColumn_RowType] = "frame";
            fields[CaptureColumn_CaptureFrame] =
                to_string(capture_frame_count);
            fields[CaptureColumn_EngineFrame] =
                to_string(engine_frame);
            fields[CaptureColumn_ElapsedMs] =
                format_float(
                    Timer::GetTimeMs() -
                    capture_start_time_ms
                );
            fields[CaptureColumn_WallMs] =
                format_float(
                    captured_frame_duration_ms
                );
            fields[CaptureColumn_CpuMs] =
                format_float(time_cpu_last);
            fields[CaptureColumn_GpuSpanMs] =
                format_float(time_gpu_last);
            fields[CaptureColumn_PacingMs] =
                format_float(
                    captured_pacing_time_ms
                );
            fields[CaptureColumn_WaitMs] =
                format_float(wait_ms);
            fields[CaptureColumn_AcquireMs] =
                format_float(acquire_ms);
            fields[CaptureColumn_SubmitMs] =
                format_float(submit_ms);
            fields[CaptureColumn_PresentMs] =
                format_float(present_ms);
            fields[CaptureColumn_ProfilerReadbackMs] =
                format_float(profiler_readback_ms);
            fields[CaptureColumn_ProfilerSerializeMs] =
                format_float(serialize_ms);
            fields[
                CaptureColumn_ProfilerWriteMsPrevious
            ] =
                format_float(
                    capture_write_time_ms
                );
            capture_write_time_ms = 0.0f;
            fields[CaptureColumn_FrameAvgMs] =
                format_float(time_frame_avg);
            fields[CaptureColumn_FrameMinMs] =
                format_float(time_frame_min);
            fields[CaptureColumn_FrameMaxMs] =
                format_float(time_frame_max);
            fields[CaptureColumn_CpuAvgMs] =
                format_float(time_cpu_avg);
            fields[CaptureColumn_CpuMinMs] =
                format_float(time_cpu_min);
            fields[CaptureColumn_CpuMaxMs] =
                format_float(time_cpu_max);
            fields[CaptureColumn_GpuAvgMs] =
                format_float(time_gpu_avg);
            fields[CaptureColumn_GpuMinMs] =
                format_float(time_gpu_min);
            fields[CaptureColumn_GpuMaxMs] =
                format_float(time_gpu_max);
            fields[CaptureColumn_Fps] =
                format_float(m_fps);
            fields[CaptureColumn_FpsLimit] =
                format_float(Timer::GetFpsLimit());
            fields[CaptureColumn_CpuStutter] =
                is_stuttering_cpu ? "1" : "0";
            fields[CaptureColumn_GpuStutter] =
                is_stuttering_gpu ? "1" : "0";
            fields[CaptureColumn_IncompleteBlocks] =
                to_string(incomplete_blocks_last);
            fields[CaptureColumn_RhiDraws] =
                to_string(Profiler::m_rhi_draw);
            fields[CaptureColumn_RhiInstances] =
                to_string(
                    Profiler::m_rhi_instance_count
                );
            fields[CaptureColumn_RhiTimeblocks] =
                to_string(
                    Profiler::m_rhi_timeblock_count
                );
            fields[CaptureColumn_RhiBarriers] =
                to_string(
                    Profiler::m_rhi_pipeline_barriers
                );
            fields[CaptureColumn_RhiBindIndex] =
                to_string(
                    Profiler::
                        m_rhi_bindings_buffer_index
                );
            fields[CaptureColumn_RhiBindVertex] =
                to_string(
                    Profiler::
                        m_rhi_bindings_buffer_vertex
                );
            fields[CaptureColumn_RhiBindConstant] =
                to_string(
                    Profiler::
                        m_rhi_bindings_buffer_constant
                );
            fields[CaptureColumn_RhiBindStructured] =
                to_string(
                    Profiler::
                        m_rhi_bindings_buffer_structured
                );
            fields[CaptureColumn_RhiBindSampler] =
                to_string(
                    Profiler::m_rhi_bindings_sampler
                );
            fields[CaptureColumn_RhiBindTextureSampled] =
                to_string(
                    Profiler::
                        m_rhi_bindings_texture_sampled
                );
            fields[CaptureColumn_RhiBindShaderVertex] =
                to_string(
                    Profiler::
                        m_rhi_bindings_shader_vertex
                );
            fields[CaptureColumn_RhiBindShaderPixel] =
                to_string(
                    Profiler::
                        m_rhi_bindings_shader_pixel
                );
            fields[CaptureColumn_RhiBindShaderCompute] =
                to_string(
                    Profiler::
                        m_rhi_bindings_shader_compute
                );
            fields[CaptureColumn_RhiBindRenderTarget] =
                to_string(
                    Profiler::
                        m_rhi_bindings_render_target
                );
            fields[CaptureColumn_RhiBindTextureStorage] =
                to_string(
                    Profiler::
                        m_rhi_bindings_texture_storage
                );
            fields[CaptureColumn_RhiBindPipeline] =
                to_string(
                    Profiler::
                        m_rhi_bindings_pipeline
                );
            fields[CaptureColumn_RhiDescriptorSets] =
                to_string(
                    Profiler::
                        m_rhi_descriptor_set_count
                );
            fields[CaptureColumn_RhiTimestampsDropped] =
                to_string(
                    Profiler::
                        m_rhi_timestamps_dropped
                );
            fields[CaptureColumn_RhiPipelineCount] =
                to_string(
                    RHI_Device::GetPipelineCount()
                );
            fields[CaptureColumn_VramAllocatedMb] =
                format_float(
                    static_cast<double>(
                        RHI_Device::MemoryGetAllocatedMb()
                    )
                );
            fields[CaptureColumn_VramAvailableMb] =
                format_float(
                    static_cast<double>(
                        RHI_Device::MemoryGetAvailableMb()
                    )
                );
            fields[CaptureColumn_VramTotalMb] =
                format_float(
                    static_cast<double>(
                        RHI_Device::MemoryGetTotalMb()
                    )
                );
            fields[CaptureColumn_RamAllocatedMb] =
                format_float(
                    Allocator::GetMemoryAllocatedMb()
                );
            fields[CaptureColumn_RamPeakMb] =
                format_float(
                    Allocator::
                        GetMemoryAllocatedPeakMb()
                );
            fields[CaptureColumn_RamProcessMb] =
                format_float(
                    Allocator::GetMemoryProcessUsedMb()
                );
            fields[CaptureColumn_RamAvailableMb] =
                format_float(
                    Allocator::GetMemoryAvailableMb()
                );
            fields[CaptureColumn_RamTotalMb] =
                format_float(
                    Allocator::GetMemoryTotalMb()
                );
            fields[CaptureColumn_Api] = api;
            fields[CaptureColumn_Mode] =
                capture_mode;
            fields[CaptureColumn_GpuTimingEnabled] =
                gpu_timing_enabled ? "1" : "0";
            fields[CaptureColumn_GpuTimingValid] =
                gpu_timing_valid ?
                    "1" :
                    "0";
            fields[CaptureColumn_CpuScope] =
                "main_thread";

            append_capture_row(
                capture_buffer,
                fields
            );
            capture_buffer += block_rows;
            capture_frame_count++;

            if (
                capture_buffer.size() >=
                capture_buffer_flush_size
            )
            {
                const auto write_start =
                    chrono::high_resolution_clock::now();
                const bool write_succeeded =
                    write_capture_buffer();
                capture_write_time_ms =
                    static_cast<float>(
                        chrono::duration<double, milli>(
                            chrono::high_resolution_clock::now() -
                            write_start
                        ).count()
                    );
                if (!write_succeeded)
                {
                    capture_requested = false;
                    capture_stop_pending = true;
                }
            }
        }

        void close_capture()
        {
            const auto write_start =
                chrono::high_resolution_clock::now();
            bool write_succeeded =
                write_capture_buffer();
            if (
                !write_succeeded &&
                capture_stream.is_open()
            )
            {
                capture_stream.clear();
                write_succeeded =
                    write_capture_buffer();
                if (write_succeeded)
                {
                    capture_error.clear();
                }
            }
            const float final_write_ms =
                static_cast<float>(
                    chrono::duration<double, milli>(
                        chrono::high_resolution_clock::now() -
                        write_start
                    ).count()
                );
            if (write_succeeded)
            {
                array<
                    string,
                    CaptureColumn_Count
                > fields;
                fields[CaptureColumn_RowType] =
                    "capture_end";
                fields[CaptureColumn_CaptureFrame] =
                    to_string(capture_frame_count);
                fields[CaptureColumn_ElapsedMs] =
                    format_float(
                        Timer::GetTimeMs() -
                        capture_start_time_ms
                    );
                fields[
                    CaptureColumn_ProfilerWriteMsPrevious
                ] =
                    format_float(final_write_ms);
                append_capture_row(
                    capture_buffer,
                    fields
                );
                if (!write_capture_buffer())
                {
                    capture_stream.clear();
                    if (write_capture_buffer())
                    {
                        capture_error.clear();
                    }
                }
            }
            if (capture_stream.is_open())
            {
                capture_stream.flush();
                capture_stream.close();
            }

            capture_requested = false;
            capture_this_frame = false;
            capture_gpu_sample_this_frame = false;
            capture_stop_pending = false;
            capture_reset_metrics_pending = false;
            if (capture_error.empty())
            {
                SP_LOG_INFO(
                    "profiler capture saved to %s",
                    capture_file_path.c_str()
                );
            }
            else
            {
                SP_LOG_ERROR(
                    "%s, partial capture is at %s",
                    capture_error.c_str(),
                    capture_file_path.c_str()
                );
            }
        }

    }

    void Profiler::Initialize()
    {
        m_time_blocks_write.reserve(
            initial_timeblock_capacity
        );
        m_time_blocks_read.reserve(
            initial_timeblock_capacity
        );
        profiling_thread_id =
            this_thread::get_id();
        cpu_name = get_cpu_name();
    }

    void Profiler::Shutdown()
    {
        if (capture_stream.is_open())
        {
            close_capture();
        }
    }

    bool Profiler::StartRecording()
    {
        if (
            capture_stream.is_open() ||
            capture_stop_pending
        )
        {
            return false;
        }

        capture_error.clear();
        capture_file_path.clear();
        capture_buffer.clear();
        capture_buffer.reserve(
            capture_buffer_flush_size *
            2
        );
        capture_frame_count = 0;
        capture_start_time_ms = Timer::GetTimeMs();
        capture_write_time_ms = 0.0f;

        capture_file_path =
            FileSystem::GetExecutableDirectory() +
            "/profiler.csv";
        capture_stream.open(
            capture_file_path,
            ios::binary |
                ios::out |
                ios::trunc
        );
        if (!capture_stream.is_open())
        {
            capture_error =
                "failed to open profiler capture file";
            return false;
        }

        capture_buffer =
            "row_type,capture_frame,engine_frame,elapsed_ms,"
            "block_index,block_id,parent_id,tree_depth,"
            "name,block_type,queue,start_ms,end_ms,"
            "duration_ms,wall_ms,cpu_active_ms,gpu_busy_ms,"
            "pacing_ms,wait_ms,acquire_ms,submit_ms,"
            "present_ms,profiler_readback_ms,"
            "profiler_block_serialize_ms,"
            "profiler_write_ms_previous,frame_avg_ms,"
            "frame_min_ms,frame_max_ms,cpu_avg_ms,"
            "cpu_min_ms,cpu_max_ms,gpu_avg_ms,"
            "gpu_min_ms,gpu_max_ms,fps,fps_limit,"
            "cpu_stutter,gpu_stutter,incomplete_blocks,"
            "rhi_draws,rhi_instances,"
            "rhi_timeblocks,rhi_barriers,rhi_bind_index,"
            "rhi_bind_vertex,rhi_bind_constant,"
            "rhi_bind_structured,rhi_bind_sampler,"
            "rhi_bind_texture_sampled,"
            "rhi_bind_shader_vertex,"
            "rhi_bind_shader_pixel,"
            "rhi_bind_shader_compute,"
            "rhi_bind_render_target,"
            "rhi_bind_texture_storage,"
            "rhi_bind_pipeline,rhi_descriptor_sets,"
            "rhi_timestamps_dropped,"
            "rhi_pipeline_count,vram_allocated_mb,"
            "vram_available_mb,vram_total_mb,"
            "ram_allocated_mb,ram_peak_mb,"
            "ram_process_mb,ram_available_mb,"
            "ram_total_mb,api,capture_mode,"
            "gpu_timing_enabled,gpu_timing_valid,"
            "cpu_scope\n";
        if (!write_capture_buffer())
        {
            close_capture();
            return false;
        }
        capture_requested = true;
        capture_stop_pending = false;
        capture_reset_metrics_pending = true;
        SP_LOG_INFO(
            "profiler capture recording to %s",
            capture_file_path.c_str()
        );
        return true;
    }

    void Profiler::StopRecording()
    {
        if (!capture_stream.is_open())
        {
            return;
        }

        capture_requested = false;
        capture_stop_pending = true;
    }

    bool Profiler::IsRecording()
    {
        return capture_requested;
    }

    bool Profiler::IsRecordingStopping()
    {
        return capture_stop_pending;
    }

    uint64_t Profiler::GetRecordedFrameCount()
    {
        return capture_frame_count;
    }

    const string& Profiler::GetRecordingFilePath()
    {
        return capture_file_path;
    }

    const string& Profiler::GetRecordingError()
    {
        return capture_error;
    }

    void Profiler::FrameStart()
    {
        frame_start_cpu = chrono::high_resolution_clock::now();
        if (
            capture_requested &&
            capture_reset_metrics_pending
        )
        {
            ClearMetrics();
            capture_reset_metrics_pending = false;
        }
        capture_this_frame = capture_requested;
        // sample gpu periodically during capture so csv busy time stays honest without stalling every frame
        capture_gpu_sample_this_frame =
            capture_this_frame &&
            Debugging::IsGpuTimingEnabled() &&
            (
                capture_frame_count == 0 ||
                (capture_frame_count % 30) == 0
            );
        if (capture_this_frame)
        {
            poll = true;
        }
    }

    float Profiler::GetCpuOffsetMs(const chrono::high_resolution_clock::time_point& time_point)
    {
        const chrono::duration<double, milli> ms = time_point - frame_start_cpu;
        return static_cast<float>(ms.count());
    }

    float Profiler::GetFrameDurationMs()
    {
        return frame_duration_ms;
    }

    float Profiler::GetCapturedFrameDurationMs()
    {
        return captured_frame_duration_ms;
    }

    float Profiler::GetCapturedPacingTimeMs()
    {
        return captured_pacing_time_ms;
    }

    void Profiler::PostTick()
    {
        // measure frame duration for timeline
        frame_duration_ms = GetCpuOffsetMs(chrono::high_resolution_clock::now());

        // if this frame was marked for sampling, resolve it now before command lists get reused
        // by later frames and invalidate the timestamp/query data we recorded against.
        const bool sampled_frame = poll;
        float profiler_readback_ms = 0.0f;
        if (sampled_frame)
        {
            captured_frame_duration_ms =
                frame_duration_ms;
            captured_pacing_time_ms =
                static_cast<float>(
                    Timer::GetPacingTimeMs()
                );
            poll = false;
            const auto readback_start =
                chrono::high_resolution_clock::now();
            ReadTimeBlocks();
            if (capture_this_frame)
            {
                profiler_readback_ms =
                    static_cast<float>(
                        chrono::duration<double, milli>(
                            chrono::high_resolution_clock::now() -
                            readback_start
                        ).count()
                    );
            }
        }

        // compute timings
        {
            time_cpu_last     = 0.0f;
            float time_gpu_busy = 0.0f;
            vector<pair<float, float>> cpu_wait_intervals;
            for (const TimeBlock& time_block : m_time_blocks_read)
            {
                if (!time_block.IsComplete())
                {
                    continue;
                }
                if (
                    time_block.GetType() ==
                        TimeBlockType::Cpu
                )
                {
                    if (!time_block.HasParent())
                    {
                        time_cpu_last +=
                            time_block.GetDuration();
                    }
                    else if (
                        is_cpu_wait(
                            time_block.GetName()
                        )
                    )
                    {
                        cpu_wait_intervals.emplace_back(
                            time_block.GetStartMs(),
                            time_block.GetEndMs()
                        );
                    }
                }
                // top-level gpu busy time, not first-to-last span which counts bubbles between submits
                if (
                    !time_block.HasParent() &&
                    time_block.GetType() ==
                        TimeBlockType::Gpu
                )
                {
                    time_gpu_busy +=
                        time_block.GetDuration();
                }
            }

            sort(
                cpu_wait_intervals.begin(),
                cpu_wait_intervals.end()
            );
            float wait_end_ms = 0.0f;
            for (
                const auto& [start_ms, end_ms] :
                    cpu_wait_intervals
            )
            {
                if (end_ms <= wait_end_ms)
                {
                    continue;
                }

                time_cpu_last -=
                    end_ms -
                    max(start_ms, wait_end_ms);
                wait_end_ms = end_ms;
            }
            time_cpu_last =
                max(time_cpu_last, 0.0f);

            // csv low-overhead frames skip gpu blocks, keep the last real sample instead of blending zeros
            const bool has_gpu_sample =
                time_gpu_busy > 0.0f ||
                capture_gpu_sample_this_frame;
            if (has_gpu_sample)
            {
                time_gpu_last = time_gpu_busy;
            }

            time_frame_last = frame_duration_ms;
            const bool has_history =
                timing_sample_count > 0;
            is_stuttering_cpu =
                has_history &&
                time_cpu_last >
                (
                    time_cpu_avg +
                    stutter_delta_ms
                );
            is_stuttering_gpu =
                has_history &&
                has_gpu_sample &&
                time_gpu_last >
                (
                    time_gpu_avg +
                    stutter_delta_ms
                );

            if (!has_history)
            {
                time_cpu_avg = time_cpu_last;
                time_cpu_min = time_cpu_last;
                time_cpu_max = time_cpu_last;
                if (has_gpu_sample)
                {
                    time_gpu_avg = time_gpu_last;
                    time_gpu_min = time_gpu_last;
                    time_gpu_max = time_gpu_last;
                }
                time_frame_avg = time_frame_last;
                time_frame_min = time_frame_last;
                time_frame_max = time_frame_last;
            }
            else
            {
                time_cpu_avg =
                    time_cpu_avg * weight_history +
                    time_cpu_last * weight_delta;
                time_cpu_min =
                    min(time_cpu_min, time_cpu_last);
                time_cpu_max =
                    max(time_cpu_max, time_cpu_last);
                if (has_gpu_sample)
                {
                    time_gpu_avg =
                        time_gpu_avg * weight_history +
                        time_gpu_last * weight_delta;
                    time_gpu_min =
                        min(time_gpu_min, time_gpu_last);
                    time_gpu_max =
                        max(time_gpu_max, time_gpu_last);
                }
                time_frame_avg =
                    time_frame_avg * weight_history +
                    time_frame_last * weight_delta;
                time_frame_min =
                    min(time_frame_min, time_frame_last);
                time_frame_max =
                    max(time_frame_max, time_frame_last);
            }
            timing_sample_count++;

            // fps
            m_fps =
                time_frame_avg > 0.0f ?
                    1000.0f /
                        time_frame_avg :
                    0.0f;
        }

        if (
            capture_this_frame &&
            sampled_frame
        )
        {
            record_capture_frame(
                profiler_readback_ms
            );
        }

        if (capture_stop_pending)
        {
            close_capture();
        }

        // check whether we should profile or not
        time_since_profiling_sec += static_cast<float>(Timer::GetDeltaTimeSec());
        if (time_since_profiling_sec >= profiling_interval_sec)
        {
            time_since_profiling_sec = 0.0f;
            poll = true;
        }

        if (cvar_performance_metrics.GetValueAs<bool>())
        {
            DrawPerformanceMetrics();
        }

        ClearRhiMetrics();
    }

    void Profiler::ReadTimeBlocks()
    {
        m_time_blocks_read.clear();
        incomplete_blocks_last = 0;
        if (m_time_block_index < 0)
        {
            m_time_blocks_write.clear();
            m_time_block_index = -1;
            cmd_lists_used.clear();
            return;
        }

        // only pay the gpu stall cost when the profiler widget is actually open and
        // we need accurate timeline positions; otherwise use cheap stale durations
        if (
            (
                is_visualized &&
                !capture_this_frame
            ) ||
            capture_gpu_sample_this_frame
        )
        {
            // wait for gpu completion and read back fresh timestamps from all used command lists
            for (RHI_CommandList* cmd_list : cmd_lists_used)
            {
                cmd_list->ReadbackTimestampsForProfiler();
            }

            // use the earliest timestamp written by a gpu block
            uint64_t global_reference_tick = 0;
            for (
                uint32_t i = 0;
                i <= static_cast<uint32_t>(
                    m_time_block_index
                );
                i++
            )
            {
                const TimeBlock& time_block =
                    m_time_blocks_write[i];
                if (
                    !time_block.IsComplete() ||
                    time_block.GetType() !=
                        TimeBlockType::Gpu ||
                    !time_block.GetCmdList()
                )
                {
                    continue;
                }

                const uint64_t first_tick =
                    time_block.GetCmdList()->
                        GetTimestampRawTick(
                            time_block.
                                GetTimestampIndexStart()
                        );
                if (
                    first_tick != 0 &&
                    (
                        global_reference_tick == 0 ||
                        first_tick <
                            global_reference_tick
                    )
                )
                {
                    global_reference_tick = first_tick;
                }
            }

            // resolve gpu timeblocks with fresh data and the global reference
            float timestamp_period = RHI_Device::PropertyGetTimestampPeriod();
            for (uint32_t i = 0; i <= static_cast<uint32_t>(m_time_block_index); i++)
            {
                TimeBlock& time_block = m_time_blocks_write[i];

                if (!time_block.IsComplete())
                {
                    incomplete_blocks_last++;
                    SP_LOG_WARNING("TimeBlockEnd() was not called for time block \"%s\"", time_block.GetName());
                    continue;
                }

                if (time_block.GetType() == TimeBlockType::Gpu && global_reference_tick != 0)
                {
                    time_block.ResolveGpuTimestamps(
                        global_reference_tick,
                        timestamp_period
                    );
                }

                m_time_blocks_read.push_back(time_block);
            }
        }
        else
        {
            // cheap path: no gpu wait, approximate durations from existing query pool data
            for (uint32_t i = 0; i <= static_cast<uint32_t>(m_time_block_index); i++)
            {
                TimeBlock& time_block = m_time_blocks_write[i];

                if (!time_block.IsComplete())
                {
                    incomplete_blocks_last++;
                    SP_LOG_WARNING("TimeBlockEnd() was not called for time block \"%s\"", time_block.GetName());
                    continue;
                }

                if (time_block.GetType() == TimeBlockType::Gpu)
                {
                    time_block.ResolveGpuDuration();
                }

                m_time_blocks_read.push_back(time_block);
            }
        }

        // clear write array and tracking state
        m_time_blocks_write.clear();
        m_time_block_index = -1;
        cmd_lists_used.clear();
    }

    void Profiler::TimeBlockStart(const char* func_name, TimeBlockType type, RHI_CommandList* cmd_list /*= nullptr*/, RHI_Queue_Type queue_type /*= RHI_Queue_Type::Max*/)
    {
        if (
            !poll ||
            this_thread::get_id() !=
                profiling_thread_id
        )
        {
            return;
        }

        if (
            capture_this_frame &&
            !capture_gpu_sample_this_frame &&
            type == TimeBlockType::Gpu
        )
        {
            return;
        }

        const bool can_profile_cpu = (type == TimeBlockType::Cpu) && profile_cpu;
        const bool can_profile_gpu = (type == TimeBlockType::Gpu) && profile_gpu && Debugging::IsGpuTimingEnabled();
        if (!can_profile_cpu && !can_profile_gpu)
        {
            return;
        }

        // track command lists used during this poll for deferred timestamp readback
        if (type == TimeBlockType::Gpu && cmd_list)
        {
            if (find(cmd_lists_used.begin(), cmd_lists_used.end(), cmd_list) == cmd_lists_used.end())
            {
                cmd_lists_used.push_back(cmd_list);
            }
        }

        // last incomplete block of the same type, is the parent
        TimeBlock* time_block_parent =
            GetLastIncompleteTimeBlock(
                type,
                cmd_list,
                true
            );

        const uint32_t parent_id =
            time_block_parent ?
                time_block_parent->GetId() :
                0;
        const uint32_t parent_tree_depth =
            time_block_parent ?
                time_block_parent->GetTreeDepth() :
                0;

        m_time_blocks_write.emplace_back();
        m_time_block_index =
            static_cast<int>(
                m_time_blocks_write.size()
            ) -
            1;
        m_time_blocks_write.back().Begin(
            ++m_rhi_timeblock_count,
            func_name,
            type,
            parent_id,
            parent_tree_depth,
            cmd_list,
            queue_type
        );
    }

    void Profiler::TimeBlockEnd(TimeBlockType type /*= TimeBlockType::Max*/, RHI_CommandList* cmd_list /*= nullptr*/)
    {
        if (
            this_thread::get_id() !=
            profiling_thread_id
        )
        {
            return;
        }

        if (type == TimeBlockType::Max)
        {
            TimeBlockEnd(TimeBlockType::Gpu, cmd_list);
            TimeBlockEnd(TimeBlockType::Cpu, cmd_list);
            return;
        }

        if (TimeBlock* time_block = GetLastIncompleteTimeBlock(type, cmd_list))
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
        captured_frame_duration_ms = 0.0f;
        captured_pacing_time_ms = 0.0f;
        timing_sample_count = 0;
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
        profiling_interval_sec =
            clamp(
                interval,
                0.05f,
                2.0f
            );
        frames_to_accumulate   =
            max(
                1u,
                static_cast<uint32_t>(
                    4.0f /
                    profiling_interval_sec
                )
            );
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

    void Profiler::SetVisualized(bool value)
    {
        is_visualized = value;
    }

    bool Profiler::IsVisualized()
    {
        return is_visualized;
    }

    TimeBlock* Profiler::GetLastIncompleteTimeBlock(
        const TimeBlockType type,
        RHI_CommandList* cmd_list,
        const bool allow_cpu_root
    )
    {
        for (int i = m_time_block_index; i >= 0; i--)
        {
            TimeBlock& time_block = m_time_blocks_write[i];

            // if type is max, match any type; otherwise, match the requested type
            if (type == TimeBlockType::Max || time_block.GetType() == type)
            {
                if (!time_block.IsComplete())
                {
                    const bool cpu_root =
                        allow_cpu_root &&
                        type == TimeBlockType::Cpu &&
                        time_block.GetCmdList() == nullptr;
                    const bool same_context =
                        cmd_list == nullptr ?
                            time_block.GetCmdList() == nullptr :
                            time_block.GetCmdList() == cmd_list;
                    if (same_context || cpu_root)
                    {
                        return &time_block;
                    }
                }
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
                "FPS:\t\t%.1f\n"
                "Time:\t\t%.2f ms\n"
                "Frame:\t\t%llu\n\n",
                m_fps, time_frame_avg, Renderer::GetFrameNumber());
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // timings
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "\t\tavg\tmin\tmax\tlast\n"
                "Total:\t\t%.2f\t%.2f\t%.2f\t%.2f ms\n"
                "CPU:\t\t%.2f\t%.2f\t%.2f\t%.2f ms\n"
                "GPU:\t\t%.2f\t%.2f\t%.2f\t%.2f ms\n\n",
                time_frame_avg, time_frame_min, time_frame_max, time_frame_last,
                time_cpu_avg, time_cpu_min, time_cpu_max, time_cpu_last,
                time_gpu_avg, time_gpu_min, time_gpu_max, time_gpu_last);
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // gpu
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "GPU\n"
                "Name:\t\t%s\n"
                "Memory:\t\t%u/%u MB\n"
                "API:\t\t%s %s\n"
                "Driver:\t\t%s %s\n\n",
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
                "CPU\n"
                "Name:\t\t%s\n"
                "Threads:\t\t%u/%u\n"
                "AVX2:\t\t%s\n\n",
                cpu_name,
                static_cast<unsigned int>(ThreadPool::GetWorkingThreadCount()),
                static_cast<unsigned int>(ThreadPool::GetThreadCount()),
#ifdef __AVX2__
                "Yes"
#else
                "No"
#endif
            );
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // memory
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Memory\n"
                "Allocated:\t%.2f MB (Peak: %.2f MB)\n"
                "Process:\t\t%.2f MB (Avail: %.2f MB, Total: %.2f MB)\n\n",
                Allocator::GetMemoryAllocatedMb(),
                Allocator::GetMemoryAllocatedPeakMb(),
                Allocator::GetMemoryProcessUsedMb(),
                Allocator::GetMemoryAvailableMb(),
                Allocator::GetMemoryTotalMb());
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // display
            const auto& res_render = Renderer::GetResolutionRender();
            const auto& res_output = Renderer::GetResolutionOutput();
            const auto& vp = Renderer::GetViewport();
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Display\n"
                "Name:\t\t%s\n"
                "Hz:\t\t%d\n"
                "HDR:\t\t%s\n"
                "Max nits:\t\t%u\n"
                "Render:\t\t%u x %u (%.0f%%)\n"
                "Output:\t\t%u x %u\n"
                "Viewport:\t\t%u x %u\n\n",
                Display::GetName(),
                static_cast<int>(Display::GetRefreshRate()),
                RHI_Device::GetSwapChain()->IsHdr() ? "Enabled" : "Disabled",
                static_cast<uint32_t>(Display::GetLuminanceMax()),
                static_cast<uint32_t>(res_render.x),
                static_cast<uint32_t>(res_render.y),
                Renderer::GetResolutionScale() * 100.0f,
                static_cast<uint32_t>(res_output.x),
                static_cast<uint32_t>(res_output.y),
                static_cast<uint32_t>(vp.width),
                static_cast<uint32_t>(vp.height));
            SP_ASSERT(offset < sizeof(metrics_buffer));

            // graphics api
            offset += snprintf(metrics_buffer + offset, sizeof(metrics_buffer) - offset,
                "Graphics API\n"
                "Draw:\t\t%u\n"
                "Instances:\t%u\n"
                "Index bindings:\t%u\n"
                "Vertex bindings:\t%u\n"
                "Barriers:\t\t%u (%u layout)\n"
                "Pipeline bindings:\t%u/%u\n"
                "Descriptor sets:\t%u/%u",
                static_cast<uint32_t>(m_rhi_draw),
                static_cast<uint32_t>(m_rhi_instance_count),
                static_cast<uint32_t>(m_rhi_bindings_buffer_index),
                static_cast<uint32_t>(m_rhi_bindings_buffer_vertex),
                static_cast<uint32_t>(m_rhi_pipeline_barriers),
                static_cast<uint32_t>(m_rhi_layout_barriers),
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
