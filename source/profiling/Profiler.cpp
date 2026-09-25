/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
#include "../core/Window.h"
#include "../font/Font.h"
#include "../rendering/Renderer.h"
#include "../rhi/RHI_Viewport.h"
#include "../display/Display.h"
#include "../memory/Allocator.h"
#include "../file_system/FileSystem.h"
#include <array>
#include <deque>
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

        // Live throughput is independent of the slower CPU/GPU profiling averages.
        constexpr double fps_update_interval_sec = 0.25;
        double fps_elapsed_sec = 0.0;
        uint32_t fps_frame_count = 0;

        // every presented frame, including pacing, feeds the overlay graph and the percentile lows
        const uint32_t frame_history_size = 512;
        array<float, frame_history_size> frame_history_ms = {};
        uint32_t frame_history_head  = 0;
        uint32_t frame_history_count = 0;

        // time blocks (double buffered)
        int m_time_block_index = -1;
        vector<int> open_time_blocks;
        vector<TimeBlock> m_time_blocks_write;
        vector<TimeBlock> m_time_blocks_read;
        uint64_t capture_revision = 0;
        uint64_t metrics_revision = 0;
        vector<TimeBlock> published_time_blocks;
        uint64_t captured_engine_frame = 0;
        uint32_t captured_incomplete = 0;
        uint32_t captured_dropped = 0;
        bool continuous = false;
        bool timeline_needs_wall = false;
        array<RHI_TimestampCalibration, static_cast<size_t>(RHI_Queue_Type::Max)> gpu_calibrations;
        uint32_t incomplete_blocks_last = 0;

        // stutter detection
        float stutter_delta_ms = 1.0f;
        bool is_stuttering_cpu = false;
        bool is_stuttering_gpu = false;

        // misc
        bool poll          = false;
        bool is_visualized = false;
        thread::id profiling_thread_id;

        struct PendingTimeline
        {
            vector<TimeBlock> blocks;
            float duration;
            float pacing;
            uint64_t engine_frame;
            uint32_t incomplete;
            uint32_t dropped;
        };
        deque<PendingTimeline> pending_timelines;

        bool resolve_time_blocks(vector<TimeBlock>& blocks)
        {
            for (TimeBlock& block : blocks)
                if (!block.TryResolveGpu()) return false;
            uint64_t reference = 0;
            for (const TimeBlock& block : blocks)
            {
                if (block.GetType() != TimeBlockType::Gpu) continue;
                const uint64_t tick = block.GetTimestampRawTick(block.GetTimestampIndexStart());
                if (tick && (!reference || tick < reference)) reference = tick;
            }
            for (TimeBlock& block : blocks)
                if (block.GetType() == TimeBlockType::Gpu)
                {
                    uint64_t queue_reference = reference;
#if defined(API_GRAPHICS_D3D12)
                    // Uncalibrated D3D12 queues have independent clocks.
                    queue_reference = UINT64_MAX;
                    for (const TimeBlock& other : blocks)
                        if (other.GetType() == TimeBlockType::Gpu && other.GetQueueType() == block.GetQueueType())
                            queue_reference = min(queue_reference, other.GetTimestampRawTick(other.GetTimestampIndexStart()));
#endif
                    block.ResolveGpuTimestamps(queue_reference, RHI_Device::PropertyGetTimestampPeriod());
                }
            return true;
        }

        // frame start reference for timeline
        double frame_start_cpu = 0.0;
        float frame_duration_ms = 0.0f;
        float captured_frame_duration_ms = 0.0f;
        float captured_pacing_time_ms = 0.0f;

        void read_pending_timelines()
        {
            if (!pending_timelines.empty() && resolve_time_blocks(pending_timelines.front().blocks))
            {
                auto& frame = pending_timelines.front();
                published_time_blocks = move(frame.blocks);
                captured_engine_frame = frame.engine_frame;
                captured_incomplete = frame.incomplete;
                captured_dropped = frame.dropped;
                ++capture_revision;
                captured_frame_duration_ms = frame.duration;
                captured_pacing_time_ms = frame.pacing;
                pending_timelines.pop_front();
            }
        }


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
                strcmp(name, "acquire_semaphore_wait") == 0 ||
                strcmp(name, "acquire_image_wait") == 0 ||
                strcmp(name, "frame_present") == 0 ||
                strcmp(name, "queue_present") == 0 ||
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
            CaptureColumn_GpuBusyMs,
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
            CaptureColumn_GpuSpanMs,
            CaptureColumn_GpuCalibrated,
            CaptureColumn_CalibrationDeviationMs,
            CaptureColumn_InvalidGpuScopes,
            CaptureColumn_Count
        };

        using CaptureRow = array<string, CaptureColumn_Count>;
        struct PendingCapture
        {
            CaptureRow frame;
            vector<CaptureRow> rows;
            vector<TimeBlock> blocks;
        };
        deque<PendingCapture> pending_captures;

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

        void read_pending_captures()
        {
            while (!pending_captures.empty() && resolve_time_blocks(pending_captures.front().blocks))
            {
                auto& capture = pending_captures.front();
                float gpu_busy_ms = 0.0f;
                bool gpu_valid = false;
                bool calibrated = true;
                double deviation_ms = 0.0;
                uint32_t invalid_gpu = 0;
                float first_gpu = numeric_limits<float>::max(), last_gpu = numeric_limits<float>::lowest();
                vector<pair<float, float>> gpu_intervals;
                for (size_t i = 0; i < capture.blocks.size(); i++)
                {
                    const TimeBlock& block = capture.blocks[i];
                    if (block.GetType() != TimeBlockType::Gpu) continue;
                    auto& row = capture.rows[i];
                    row[CaptureColumn_StartMs] = format_float(block.GetStartMs());
                    row[CaptureColumn_EndMs] = format_float(block.GetEndMs());
                    row[CaptureColumn_DurationMs] = format_float(block.GetDuration());
                    row[CaptureColumn_GpuTimingValid] = block.IsTimingValid() ? "1" : "0";
                    gpu_valid |= block.IsTimingValid();
                    row[CaptureColumn_GpuCalibrated] = block.IsGpuCalibrated() ? "1" : "0";
                    row[CaptureColumn_CalibrationDeviationMs] = format_float(static_cast<float>(block.GetCalibrationDeviationMs()));
                    if (block.IsTimingValid())
                    {
                        first_gpu = min(first_gpu, block.GetStartMs());
                        last_gpu = max(last_gpu, block.GetEndMs());
                        calibrated &= block.IsGpuCalibrated();
                        deviation_ms = max(deviation_ms, block.GetCalibrationDeviationMs());
                    }
                    else ++invalid_gpu;
                    if (block.IsTimingValid()) gpu_intervals.emplace_back(block.GetStartMs(), block.GetEndMs());
                }
                sort(gpu_intervals.begin(), gpu_intervals.end());
                float gpu_end_ms = numeric_limits<float>::lowest();
                for (const auto& [start_ms, end_ms] : gpu_intervals)
                {
                    gpu_busy_ms += max(0.0f, end_ms - max(start_ms, gpu_end_ms));
                    gpu_end_ms = max(gpu_end_ms, end_ms);
                }
                capture.frame[CaptureColumn_GpuTimingValid] = gpu_valid ? "1" : "0";
                capture.frame[CaptureColumn_GpuBusyMs] = gpu_valid && calibrated ? format_float(gpu_busy_ms) : "";
                capture.frame[CaptureColumn_GpuSpanMs] = gpu_valid && calibrated ? format_float(last_gpu - first_gpu) : "";
                capture.frame[CaptureColumn_GpuCalibrated] = gpu_valid && calibrated ? "1" : "0";
                capture.frame[CaptureColumn_CalibrationDeviationMs] = format_float(static_cast<float>(deviation_ms));
                capture.frame[CaptureColumn_InvalidGpuScopes] = to_string(invalid_gpu);
                append_capture_row(capture_buffer, capture.frame);
                for (const auto& row : capture.rows) append_capture_row(capture_buffer, row);
                pending_captures.pop_front();
            }
        }

        void record_capture_frame(
            const float profiler_readback_ms
        )
        {
            const auto serialize_start =
                chrono::steady_clock::now();
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
                    "cpu_gpu_per_frame" :
                    "low_overhead_cpu_per_frame";
            PendingCapture capture;
            capture.rows.reserve(m_time_blocks_read.size());
            capture.blocks.reserve(m_time_blocks_read.size());

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
                capture.rows.push_back(move(fields));
                capture.blocks.push_back(block);
            }

            vector<pair<float, float>> waits;
            for (const auto& block : m_time_blocks_read)
                if (block.GetType() == TimeBlockType::Cpu && is_cpu_wait(block.GetName()))
                    waits.emplace_back(block.GetStartMs(), block.GetEndMs());
            sort(waits.begin(), waits.end());
            float wait_ms = 0.0f, wait_end_ms = numeric_limits<float>::lowest();
            for (const auto& [start, end] : waits)
            {
                wait_ms += max(0.0f, end - max(start, wait_end_ms));
                wait_end_ms = max(wait_end_ms, end);
            }
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
                        chrono::steady_clock::now() -
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
                    frame_duration_ms
                );
            float cpu_elapsed_ms = 0.0f;
            for (const auto& block : m_time_blocks_read)
                if (block.GetType() == TimeBlockType::Cpu && !block.HasParent()) cpu_elapsed_ms += block.GetDuration();
            fields[CaptureColumn_CpuMs] = format_float(max(0.0f, cpu_elapsed_ms - wait_ms));
            fields[CaptureColumn_GpuBusyMs] =
                format_float(time_gpu_last);
            fields[CaptureColumn_PacingMs] =
                format_float(
                    static_cast<float>(Timer::GetPacingTimeMs())
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

            capture.frame = move(fields);
            pending_captures.push_back(move(capture));
            capture_frame_count++;

            if (
                capture_buffer.size() >=
                capture_buffer_flush_size
            )
            {
                const auto write_start =
                    chrono::steady_clock::now();
                const bool write_succeeded =
                    write_capture_buffer();
                capture_write_time_ms =
                    static_cast<float>(
                        chrono::duration<double, milli>(
                            chrono::steady_clock::now() -
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
                chrono::steady_clock::now();
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
                        chrono::steady_clock::now() -
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
        // Shutdown is allowed to drain GPU work; the render loop never waits for profiling.
        if (!pending_captures.empty()) RHI_Device::QueueWaitAll();
        read_pending_captures();
        pending_timelines.clear();
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
            "cpu_scope,gpu_span_ms,gpu_calibrated,calibration_deviation_ms,invalid_gpu_scopes\n";
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
        const double now = RHI_Device::GetCpuTimestampMs();
        if (frame_start_cpu != 0.0)
        {
            const float wall_ms = static_cast<float>(now - frame_start_cpu);
            if (timeline_needs_wall && !pending_timelines.empty()) pending_timelines.back().duration = wall_ms;
            timeline_needs_wall = false;
            if (capture_this_frame && !pending_captures.empty())
                pending_captures.back().frame[CaptureColumn_WallMs] = format_float(wall_ms);
        }
        frame_start_cpu = now;
        if (
            capture_requested &&
            capture_reset_metrics_pending
        )
        {
            ClearMetrics();
            capture_reset_metrics_pending = false;
        }
        capture_this_frame = capture_requested;
        // Consecutive GPU samples are necessary for diagnosing short stalls.
        capture_gpu_sample_this_frame = capture_this_frame && Debugging::IsGpuTimingEnabled();
        if (capture_this_frame || (continuous && is_visualized)) poll = true;
        if (poll && Debugging::IsGpuTimingEnabled())
        {
            for (RHI_Queue_Type queue : {RHI_Queue_Type::Graphics, RHI_Queue_Type::Compute, RHI_Queue_Type::Copy})
                gpu_calibrations[static_cast<size_t>(queue)] = RHI_Device::GetTimestampCalibration(queue);
        }
    }

    float Profiler::GetCpuOffsetMs(double time_ms)
    {
        return static_cast<float>(time_ms - frame_start_cpu);
    }

    double Profiler::GetFrameStartMs() { return frame_start_cpu; }
    const RHI_TimestampCalibration& Profiler::GetGpuCalibration(RHI_Queue_Type queue)
    {
        static const RHI_TimestampCalibration unavailable;
        const size_t index = static_cast<size_t>(queue);
        return index < gpu_calibrations.size() ? gpu_calibrations[index] : unavailable;
    }
    void Profiler::SetContinuous(bool enabled) { continuous = enabled; }
    bool Profiler::IsContinuous() { return continuous; }
    bool Profiler::IsCpuWait(const char* name) { return is_cpu_wait(name); }
    uint64_t Profiler::GetCapturedFrameNumber() { return captured_engine_frame; }
    uint32_t Profiler::GetCapturedIncompleteCount() { return captured_incomplete; }
    uint32_t Profiler::GetCapturedDroppedTimestamps() { return captured_dropped; }

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
        // Count every completed frame, including pacing and stalls. Averaging
        // frame counts over elapsed time avoids bias from averaging reciprocal deltas.
        const double delta_sec = Timer::GetDeltaTimeSec();
        if (delta_sec > 0.0)
        {
            fps_elapsed_sec += delta_sec;
            fps_frame_count++;
            if (fps_elapsed_sec >= fps_update_interval_sec)
            {
                m_fps = static_cast<float>(fps_frame_count / fps_elapsed_sec);
                fps_elapsed_sec = 0.0;
                fps_frame_count = 0;
            }

            frame_history_ms[frame_history_head] = static_cast<float>(delta_sec * 1000.0);
            frame_history_head                   = (frame_history_head + 1) % frame_history_size;
            frame_history_count                  = min(frame_history_count + 1, frame_history_size);
        }

        // measure frame duration for timeline
        frame_duration_ms = GetCpuOffsetMs(RHI_Device::GetCpuTimestampMs());

        read_pending_captures();
        read_pending_timelines();

        // CPU capture is immediate; GPU samples resolve when their submission completes.
        const bool sampled_frame = poll;
        float profiler_readback_ms = 0.0f;
        if (sampled_frame)
        {
            poll = false;
            const auto readback_start =
                chrono::steady_clock::now();
            ReadTimeBlocks();
            if (capture_this_frame)
            {
                profiler_readback_ms =
                    static_cast<float>(
                        chrono::duration<double, milli>(
                            chrono::steady_clock::now() -
                            readback_start
                        ).count()
                    );
            }
        }

        // Consume each sample once. Reusing old blocks every render frame biases
        // averages and can pair a delayed GPU sample with a different CPU frame.
        if (metrics_revision != capture_revision)
        {
            metrics_revision = capture_revision;
            time_cpu_last     = 0.0f;
            float time_gpu_busy = 0.0f;
            bool gpu_present = false, gpu_calibrated = true;
            vector<pair<float, float>> cpu_wait_intervals;
            vector<pair<float, float>> gpu_busy_intervals;
            for (const TimeBlock& time_block : published_time_blocks)
            {
                if (!time_block.IsComplete() || !time_block.IsTimingValid())
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
                // Merge queue intervals below: overlapping graphics and compute
                // occupy the same wall time and must not be counted twice.
                if (
                    time_block.GetType() ==
                        TimeBlockType::Gpu
                )
                {
                    gpu_present = true;
                    gpu_calibrated &= time_block.IsGpuCalibrated();
                    gpu_busy_intervals.emplace_back(time_block.GetStartMs(), time_block.GetEndMs());
                }
            }

            sort(gpu_busy_intervals.begin(), gpu_busy_intervals.end());
            float gpu_end_ms = numeric_limits<float>::lowest();
            for (const auto& [start_ms, end_ms] : gpu_busy_intervals)
            {
                time_gpu_busy += max(0.0f, end_ms - max(start_ms, gpu_end_ms));
                gpu_end_ms = max(gpu_end_ms, end_ms);
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

            const bool has_gpu_sample = gpu_present && gpu_calibrated;
            time_gpu_last = has_gpu_sample ? time_gpu_busy : 0.0f;

            time_frame_last = captured_frame_duration_ms;
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

        if (capture_stop_pending && pending_captures.empty())
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
        vector<TimeBlock> blocks;
        blocks.reserve(m_time_blocks_write.size());
        incomplete_blocks_last = 0;
        for (const TimeBlock& block : m_time_blocks_write)
        {
            if (block.IsComplete()) blocks.push_back(block);
            else incomplete_blocks_last++;
        }
        m_time_blocks_write.clear();
        open_time_blocks.clear();
        m_time_block_index = -1;
        // Keep a separate published snapshot: the UI never sees unresolved GPU
        // placeholders, including while CSV recording is active.
        if (pending_timelines.size() >= 120) pending_timelines.pop_front();
        timeline_needs_wall = true;
        pending_timelines.push_back({blocks, frame_duration_ms, static_cast<float>(Timer::GetPacingTimeMs()),
            Renderer::GetFrameNumber(), incomplete_blocks_last, m_rhi_timestamps_dropped});
        if (capture_this_frame) m_time_blocks_read = move(blocks);
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
        open_time_blocks.push_back(m_time_block_index);
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
            const int index = static_cast<int>(time_block - m_time_blocks_write.data());
            time_block->End();
            open_time_blocks.erase(find(open_time_blocks.begin(), open_time_blocks.end(), index));
        }
    }

    void Profiler::ClearMetrics()
    {
        ClearRhiMetrics();

        m_fps           = 0.0f;
        fps_elapsed_sec = 0.0;
        fps_frame_count = 0;

        frame_history_head  = 0;
        frame_history_count = 0;

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
        // Published snapshot metadata remains paired with its blocks until replaced.
        timing_sample_count = 0;
    }

    uint64_t Profiler::GetCaptureRevision()
    {
        return capture_revision;
    }

    const vector<TimeBlock>& Profiler::GetTimeBlocks()
    {
        return published_time_blocks;
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
        // Completed sibling scopes cannot be parents. Searching the full frame
        // history made thousands of per-entity scopes quadratic in scene size.
        for (auto it = open_time_blocks.rbegin(); it != open_time_blocks.rend(); ++it)
        {
            TimeBlock& time_block = m_time_blocks_write[*it];

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

    namespace
    {
        // the overlay palette, one hue per meaning so a glance is enough
        namespace overlay_color
        {
            // matches color_accent_1 of the editor's default theme
            const Color accent  = Color(0.439f, 0.831f, 1.000f, 1.0f);
            const Color panel   = Color(0.031f, 0.037f, 0.047f, 0.86f);
            const Color border  = Color(1.0f, 1.0f, 1.0f, 0.06f);
            const Color divider = Color(1.0f, 1.0f, 1.0f, 0.07f);
            const Color track   = Color(1.0f, 1.0f, 1.0f, 0.07f);
            const Color text    = Color(0.94f, 0.95f, 0.97f, 1.0f);
            const Color muted   = Color(0.62f, 0.66f, 0.72f, 1.0f);
            const Color dim     = Color(0.43f, 0.47f, 0.53f, 1.0f);
            const Color good    = Color(0.36f, 0.86f, 0.55f, 1.0f);
            const Color warn    = Color(1.00f, 0.72f, 0.26f, 1.0f);
            const Color bad     = Color(1.00f, 0.37f, 0.37f, 1.0f);
            const Color cpu     = accent;
            const Color gpu     = Color(0.70f, 0.54f, 1.00f, 1.0f);
            const Color graph   = Color(accent.r, accent.g, accent.b, 0.80f);
            const Color memory  = Color(0.56f, 0.63f, 0.73f, 1.0f);
            const Color peak    = Color(1.0f, 1.0f, 1.0f, 0.65f);
            const Color budget  = Color(1.0f, 1.0f, 1.0f, 0.30f);
        }

        // numbers refresh a few times a second so they can be read, the graph moves every frame
        struct OverlaySnapshot
        {
            float fps             = 0.0f;
            float frame_ms        = 0.0f;
            float low_1_fps       = 0.0f;
            float low_01_fps      = 0.0f;
            float target_hz       = 60.0f;
            float budget_ms       = 1000.0f / 60.0f;
            float graph_max_ms    = 1000.0f / 30.0f;
            float cpu_ms          = 0.0f;
            float cpu_peak_ms     = 0.0f;
            float gpu_ms          = 0.0f;
            float gpu_peak_ms     = 0.0f;
            bool gpu_valid        = false;
            bool capped           = false;
            uint64_t frame        = 0;
            uint32_t draws        = 0;
            uint32_t instances    = 0;
            uint32_t barriers     = 0;
            uint32_t layouts      = 0;
            uint32_t pipelines    = 0;
            uint32_t pipeline_max = 0;
            uint32_t descriptors  = 0;
            uint32_t vertex_binds = 0;
            float vram_used_mb    = 0.0f;
            float vram_budget_mb  = 0.0f;
            float ram_used_mb     = 0.0f;
            float ram_total_mb    = 0.0f;
        };

        void format_count(char* out, const size_t size, const uint64_t value)
        {
            char digits[32];
            const int count = snprintf(digits, sizeof(digits), "%llu", static_cast<unsigned long long>(value));
            size_t length   = 0;
            for (int i = 0; i < count && length + 2 < size; i++)
            {
                if (i > 0 && (count - i) % 3 == 0)
                {
                    out[length++] = ',';
                }
                out[length++] = digits[i];
            }
            out[length] = '\0';
        }

        void format_memory(char* out, const size_t size, const float used_mb, const float total_mb)
        {
            if (total_mb >= 1024.0f)
            {
                snprintf(out, size, "%.1f / %.1f GB", used_mb / 1024.0f, total_mb / 1024.0f);
            }
            else
            {
                snprintf(out, size, "%.0f / %.0f MB", used_mb, total_mb);
            }
        }

        Color with_alpha(Color color, const float alpha)
        {
            color.a = alpha;
            return color;
        }

        // health follows what the player feels, 60 fps or the pacing target if that is lower is smooth, under 30 is not
        float comfort_ms(const float budget_ms)
        {
            return max(budget_ms, 1000.0f / 60.0f);
        }

        float struggle_ms(const float budget_ms)
        {
            return max(comfort_ms(budget_ms) * 1.5f, 1000.0f / 30.0f);
        }

        Color health_color(const float value_ms, const float budget_ms)
        {
            if (value_ms <= comfort_ms(budget_ms) * 1.05f)
            {
                return overlay_color::good;
            }

            return value_ms <= struggle_ms(budget_ms) ? overlay_color::warn : overlay_color::bad;
        }

        // brand strings carry trademarks and core counts that only cost width
        string clean_cpu_name(const char* name)
        {
            string cleaned = name;
            for (const char* noise : { "(R)", "(r)", "(TM)", "(tm)", " Processor", " CPU" })
            {
                for (size_t position = cleaned.find(noise); position != string::npos; position = cleaned.find(noise))
                {
                    cleaned.erase(position, strlen(noise));
                }
            }

            const size_t core = cleaned.find("-Core");
            if (core != string::npos)
            {
                size_t start = cleaned.rfind(' ', core);
                start        = start == string::npos ? 0 : start;
                cleaned.erase(start, core + strlen("-Core") - start);
            }

            string collapsed;
            for (const char character : cleaned)
            {
                if (character == ' ' && (collapsed.empty() || collapsed.back() == ' '))
                {
                    continue;
                }
                collapsed += character;
            }
            while (!collapsed.empty() && collapsed.back() == ' ')
            {
                collapsed.pop_back();
            }

            return collapsed;
        }

        // 616.92.0.0 reads as 616.92
        string clean_driver_version(const char* version)
        {
            string cleaned = version ? version : "";
            while (cleaned.size() > 2 && cleaned.compare(cleaned.size() - 2, 2, ".0") == 0)
            {
                cleaned.erase(cleaned.size() - 2);
            }

            return cleaned;
        }

        void refresh_snapshot(OverlaySnapshot& snapshot, const bool has_samples)
        {
            snapshot.fps      = m_fps;
            snapshot.frame_ms = m_fps > 0.0f ? 1000.0f / m_fps : 0.0f;
            snapshot.frame    = Renderer::GetFrameNumber();

            // the budget is whatever the frame is paced to, an unlocked frame is judged against the display
            const FpsLimitType limit_type = Timer::GetFpsLimitType();
            snapshot.target_hz            = limit_type == FpsLimitType::Unlocked ? Display::GetRefreshRate() : Timer::GetFpsLimit();
            if (snapshot.target_hz <= 0.0f)
            {
                snapshot.target_hz = 60.0f;
            }
            snapshot.budget_ms = 1000.0f / snapshot.target_hz;

            // percentile lows describe stutter far better than a min that is dominated by one hitch
            static vector<float> sorted;
            sorted.assign(frame_history_ms.begin(), frame_history_ms.begin() + frame_history_count);
            float p99 = 0.0f;
            if (!sorted.empty())
            {
                auto percentile = [](vector<float>& values, const float fraction)
                {
                    const size_t index = min(values.size() - 1, static_cast<size_t>(fraction * static_cast<float>(values.size() - 1) + 0.5f));
                    nth_element(values.begin(), values.begin() + index, values.end());
                    return values[index];
                };

                p99                 = percentile(sorted, 0.99f);
                const float p999    = percentile(sorted, 0.999f);
                snapshot.low_1_fps  = p99 > 0.0f ? 1000.0f / p99 : 0.0f;
                snapshot.low_01_fps = p999 > 0.0f ? 1000.0f / p999 : 0.0f;
            }

            // typical frames sit around half height, rare hitches clip at the top where they are already red
            const float graph_target = max({ snapshot.budget_ms * 2.0f, snapshot.frame_ms * 2.0f, p99 * 1.25f });
            snapshot.graph_max_ms    = graph_target > snapshot.graph_max_ms ? graph_target : lerp(snapshot.graph_max_ms, graph_target, 0.25f);

            // peak hold like an audio meter, a spike stays visible for a moment and then decays
            const float peak_decay = 0.85f;
            snapshot.cpu_ms        = time_cpu_avg;
            snapshot.cpu_peak_ms   = has_samples ? max(time_cpu_last, snapshot.cpu_peak_ms * peak_decay) : 0.0f;
            snapshot.gpu_valid     = Debugging::IsGpuTimingEnabled() && time_gpu_avg > 0.0f;
            snapshot.gpu_ms        = snapshot.gpu_valid ? time_gpu_avg : 0.0f;
            snapshot.gpu_peak_ms   = snapshot.gpu_valid && has_samples ? max(time_gpu_last, snapshot.gpu_peak_ms * peak_decay) : 0.0f;

            // the limiter or vsync sets the pace when the frame lands on the limit and neither processor fills it
            const bool on_limit = snapshot.frame_ms <= snapshot.budget_ms * 1.15f;
            snapshot.capped     = limit_type != FpsLimitType::Unlocked && on_limit && max(snapshot.cpu_ms, snapshot.gpu_ms) < snapshot.frame_ms * 0.8f;

            snapshot.draws        = Profiler::m_rhi_draw;
            snapshot.instances    = Profiler::m_rhi_instance_count;
            snapshot.barriers     = Profiler::m_rhi_pipeline_barriers;
            snapshot.layouts      = Profiler::m_rhi_layout_barriers;
            snapshot.pipelines    = Profiler::m_rhi_bindings_pipeline;
            snapshot.pipeline_max = static_cast<uint32_t>(RHI_Device::GetPipelineCount());
            snapshot.descriptors  = Profiler::m_rhi_descriptor_set_count;
            snapshot.vertex_binds = Profiler::m_rhi_bindings_buffer_vertex;

            snapshot.vram_used_mb   = static_cast<float>(RHI_Device::MemoryGetAllocatedMb());
            snapshot.vram_budget_mb = static_cast<float>(RHI_Device::MemoryGetAvailableMb());
            snapshot.ram_used_mb    = Allocator::GetMemoryProcessUsedMb();
            snapshot.ram_total_mb   = Allocator::GetMemoryTotalMb();
        }
    }

    void Profiler::DrawPerformanceMetrics()
    {
        using math::Vector2;
        namespace col = overlay_color;

        Font* font_shapes = Renderer::GetFont(Renderer_Font::Standard).get();
        Font* font_small  = Renderer::GetFont(Renderer_Font::OverlaySmall).get();
        Font* font_body   = Renderer::GetFont(Renderer_Font::Overlay).get();
        Font* font_large  = Renderer::GetFont(Renderer_Font::OverlayLarge).get();
        if (!font_shapes || !font_small || !font_body || !font_large)
        {
            return;
        }

        static OverlaySnapshot snapshot;
        static double time_since_refresh = fps_update_interval_sec;
        time_since_refresh += Timer::GetDeltaTimeSec();
        if (time_since_refresh >= fps_update_interval_sec)
        {
            time_since_refresh = 0.0;
            refresh_snapshot(snapshot, timing_sample_count > 0);
        }

        // 1 is the full panel, 2 keeps only the headline and the frame time graph
        const bool compact = cvar_performance_metrics.GetValueAs<float>() >= 2.0f;

        const float dpi = Window::GetDpiScale();
        auto px = [dpi](const float value)
        {
            return floor(value * dpi + 0.5f);
        };

        // shapes are collected and emitted after the panel background, all of them land below the text fonts
        struct OverlayRect
        {
            float x0, y0, x1, y1;
            Color color;
        };
        static vector<OverlayRect> rects;
        rects.clear();
        auto rect = [](const float x0, const float y0, const float x1, const float y1, const Color& color)
        {
            rects.push_back({ x0, y0, x1, y1, color });
        };
        auto text = [](Font* font, const char* value, const float x, const float y, const Color& color)
        {
            font->AddText(value, Vector2(x, y), color);
        };
        auto text_right = [](Font* font, const char* value, const float right, const float y, const Color& color)
        {
            font->AddText(value, Vector2(right - font->GetTextWidth(value), y), color);
        };
        // top of a line whose capitals are centered on mid, the ascent includes accents so caps sit a bit lower
        auto center_top = [](Font* font, const float mid)
        {
            return floor(mid - font->GetAscent() * 0.58f + 0.5f);
        };

        // system lines, left is the device name and right its details, built first because they decide the panel width
        struct SystemLine
        {
            string left;
            char right[128];
        };
        static array<SystemLine, 3> system_lines;
        const float system_gap = px(16.0f);
        float system_width     = 0.0f;
        if (!compact)
        {
            static const string cpu_display_name = clean_cpu_name(cpu_name);
            const RHI_PhysicalDevice* gpu        = RHI_Device::GetPrimaryPhysicalDevice();
            static const string driver_version   = clean_driver_version(gpu ? gpu->GetDriverVersion() : nullptr);

            system_lines[0].left = gpu ? gpu->GetName() : "Unknown GPU";
            snprintf(system_lines[0].right, sizeof(system_lines[0].right), "%s %s  |  %s", RHI_Context::api_type_str, RHI_Context::api_version_cstr ? RHI_Context::api_version_cstr : "", driver_version.c_str());

#ifdef __AVX2__
            const char* avx2 = "  |  AVX2";
#else
            const char* avx2 = "";
#endif
            system_lines[1].left = cpu_display_name;
            snprintf(system_lines[1].right, sizeof(system_lines[1].right), "%u threads%s", static_cast<uint32_t>(ThreadPool::GetThreadCount()), avx2);

            system_lines[2].left = Display::GetName();
            if (RHI_Device::GetSwapChain() && RHI_Device::GetSwapChain()->IsHdr())
            {
                snprintf(system_lines[2].right, sizeof(system_lines[2].right), "%.0f Hz  |  HDR %.0f nits", Display::GetRefreshRate(), Display::GetLuminanceMax());
            }
            else
            {
                snprintf(system_lines[2].right, sizeof(system_lines[2].right), "%.0f Hz  |  SDR", Display::GetRefreshRate());
            }

            for (SystemLine& line : system_lines)
            {
                system_width = max(system_width, font_small->GetTextWidth(line.left.c_str()) + system_gap + font_small->GetTextWidth(line.right));
            }
        }

        // the panel grows to fit the longest device name, so nothing is ever cut off
        const RHI_Viewport& viewport = Renderer::GetViewport();
        const float margin           = px(12.0f);
        const float padding          = px(14.0f);
        const float panel_width      = ceil(max(px(320.0f), system_width + padding * 2.0f));
        const float panel_x0         = max(margin, viewport.width - margin - panel_width);
        const float panel_x1         = panel_x0 + panel_width;
        const float panel_y0         = margin;
        const float left             = panel_x0 + padding;
        const float right            = panel_x1 - padding;
        const float inner_width      = right - left;
        const float hairline         = max(1.0f, floor(dpi));
        float y                      = panel_y0 + padding;

        char buffer[128];

        // headline, frame rate on the left, frame time and the limiting factor on the right
        const Color status = health_color(snapshot.frame_ms, snapshot.budget_ms);
        const bool healthy = snapshot.frame_ms <= comfort_ms(snapshot.budget_ms) * 1.05f;
        {
            // the number only takes a color when something is wrong
            snprintf(buffer, sizeof(buffer), "%.0f", snapshot.fps);
            text(font_large, buffer, left, y, healthy ? col::text : status);

            const float baseline = y + font_large->GetAscent();
            text(font_small, "FPS", left + font_large->GetTextWidth(buffer) + px(5.0f), baseline - font_small->GetAscent(), col::muted);

            snprintf(buffer, sizeof(buffer), "%.2f ms", snapshot.frame_ms);
            text_right(font_body, buffer, right, y, col::text);

            // the chip shares its hue with the meter of whatever limits the frame
            const char* verdict = nullptr;
            Color verdict_color = col::muted;
            if (snapshot.capped)
            {
                verdict = "FPS CAP";
            }
            else if (snapshot.gpu_valid)
            {
                const bool gpu_bound = snapshot.gpu_ms >= snapshot.cpu_ms;
                verdict              = gpu_bound ? "GPU BOUND" : "CPU BOUND";
                verdict_color        = gpu_bound ? col::gpu : col::cpu;
            }

            const float chip_height = font_small->GetLineHeight() + px(4.0f);
            if (verdict)
            {
                const float chip_width = font_small->GetTextWidth(verdict, false) + px(12.0f);
                const float chip_y0    = baseline - chip_height + px(2.0f);
                rect(right - chip_width, chip_y0, right, chip_y0 + chip_height, with_alpha(verdict_color, 0.14f));
                text(font_small, verdict, right - chip_width + px(6.0f), chip_y0 + px(2.0f), verdict_color);
            }

            y = baseline + px(8.0f);
        }

        // stutter summary
        {
            auto stat = [&](const char* label, const char* value, float x, const bool align_right)
            {
                const float gap   = px(5.0f);
                const float width = font_small->GetTextWidth(label, false) + gap + font_small->GetTextWidth(value);
                if (align_right)
                {
                    x -= width;
                }
                text(font_small, label, x, y, col::dim);
                text(font_small, value, x + font_small->GetTextWidth(label, false) + gap, y, col::text);
            };

            snprintf(buffer, sizeof(buffer), "%.0f", snapshot.low_1_fps);
            stat("1% LOW", buffer, left, false);
            snprintf(buffer, sizeof(buffer), "%.0f", snapshot.low_01_fps);
            stat("0.1% LOW", buffer, left + floor(inner_width * 0.36f), false);
            format_count(buffer, sizeof(buffer), snapshot.frame);
            stat("FRAME", buffer, right, true);

            y += font_small->GetLineHeight() + px(12.0f);
        }

        // frame time graph, newest frame on the right, bars are only colored when they miss the budget
        {
            text(font_small, "FRAME TIME", left, y, col::dim);
            snprintf(buffer, sizeof(buffer), "budget %.1f ms  |  %.0f Hz", snapshot.budget_ms, snapshot.target_hz);
            text_right(font_small, buffer, right, y, col::dim);
            y += font_small->GetLineHeight() + px(5.0f);

            const float graph_height = px(52.0f);
            const float graph_y1     = y + graph_height;
            rect(left, y, right, graph_y1, col::track);

            // a one pixel gap keeps individual frames readable instead of merging into a block
            const float pitch      = max(2.0f, px(2.0f));
            const uint32_t columns = min(static_cast<uint32_t>(inner_width / pitch), frame_history_count);
            const float warn_ms    = comfort_ms(snapshot.budget_ms) * 1.05f;
            const float bad_ms     = struggle_ms(snapshot.budget_ms);
            for (uint32_t i = 0; i < columns; i++)
            {
                const uint32_t index = (frame_history_head + frame_history_size - 1 - i) % frame_history_size;
                const float value_ms = frame_history_ms[index];
                const float height   = max(hairline, min(value_ms / snapshot.graph_max_ms, 1.0f) * graph_height);
                const float x1       = right - static_cast<float>(i) * pitch;

                Color color = col::graph;
                if (value_ms > warn_ms)
                {
                    color = value_ms > bad_ms ? col::bad : col::warn;
                }
                rect(x1 - pitch + 1.0f, graph_y1 - height, x1, graph_y1, color);
            }

            // dashed budget line
            if (snapshot.budget_ms < snapshot.graph_max_ms)
            {
                const float line_y = floor(graph_y1 - snapshot.budget_ms / snapshot.graph_max_ms * graph_height);
                const float dash   = px(4.0f);
                for (float x = left; x < right; x += dash + px(3.0f))
                {
                    rect(x, line_y, min(x + dash, right), line_y + hairline, col::budget);
                }
            }

            y = graph_y1 + px(14.0f);
        }

        if (!compact)
        {
            const float label_width = px(40.0f);
            const float value_width = px(92.0f);
            const float track_x0    = left + label_width;
            const float track_x1    = right - value_width;
            const float track_h     = px(6.0f);
            const float row_height  = max(font_body->GetLineHeight(), px(16.0f));

            // horizontal meter, full width is the budget or the capacity, the tick marks the peak
            auto meter = [&](const char* label, const float fraction, const float peak_fraction, const Color& fill, const char* value, const Color& value_color)
            {
                const float mid = y + row_height * 0.5f;
                text(font_small, label, left, center_top(font_small, mid), col::muted);

                const float ty0 = floor(mid - track_h * 0.5f);
                const float ty1 = ty0 + track_h;
                rect(track_x0, ty0, track_x1, ty1, col::track);
                if (fraction > 0.0f)
                {
                    rect(track_x0, ty0, track_x0 + (track_x1 - track_x0) * min(fraction, 1.0f), ty1, fill);
                }
                if (peak_fraction > 0.0f)
                {
                    const float peak_x = floor(track_x0 + (track_x1 - track_x0) * min(peak_fraction, 1.0f));
                    rect(peak_x - hairline, ty0 - px(3.0f), peak_x + hairline, ty1 + px(3.0f), col::peak);
                }

                text_right(font_body, value, right, center_top(font_body, mid), value_color);
                y += row_height;
            };

            auto divider = [&]()
            {
                y += px(9.0f);
                rect(left, y, right, y + hairline, col::divider);
                y += hairline + px(9.0f);
            };

            // workload, both on the scale of the frame so the longer bar is the bottleneck, with headroom for the peaks
            {
                const float scale = max(snapshot.frame_ms, snapshot.budget_ms) * 1.2f;
                snprintf(buffer, sizeof(buffer), "%.2f ms", snapshot.cpu_ms);
                meter("CPU", snapshot.cpu_ms / scale, snapshot.cpu_peak_ms / scale, col::cpu, buffer, col::text);
                y += px(5.0f);

                if (snapshot.gpu_valid)
                {
                    snprintf(buffer, sizeof(buffer), "%.2f ms", snapshot.gpu_ms);
                    meter("GPU", snapshot.gpu_ms / scale, snapshot.gpu_peak_ms / scale, col::gpu, buffer, col::text);
                }
                else
                {
                    meter("GPU", 0.0f, 0.0f, col::gpu, "timing off", col::dim);
                }
            }

            divider();

            // memory, colored as it approaches the budget
            {
                auto memory_color = [](const float fraction)
                {
                    if (fraction > 0.9f)
                    {
                        return col::bad;
                    }

                    return fraction > 0.75f ? col::warn : col::memory;
                };

                const float vram_fraction = snapshot.vram_budget_mb > 0.0f ? snapshot.vram_used_mb / snapshot.vram_budget_mb : 0.0f;
                format_memory(buffer, sizeof(buffer), snapshot.vram_used_mb, snapshot.vram_budget_mb);
                meter("VRAM", vram_fraction, 0.0f, memory_color(vram_fraction), buffer, col::text);
                y += px(5.0f);

                const float ram_fraction = snapshot.ram_total_mb > 0.0f ? snapshot.ram_used_mb / snapshot.ram_total_mb : 0.0f;
                format_memory(buffer, sizeof(buffer), snapshot.ram_used_mb, snapshot.ram_total_mb);
                meter("RAM", ram_fraction, 0.0f, memory_color(ram_fraction), buffer, col::text);
            }

            divider();

            // render statistics, two columns of label and right aligned value
            {
                const float column_gap   = px(20.0f);
                const float column_width = (inner_width - column_gap) * 0.5f;
                const float line_height  = font_body->GetLineHeight() + px(3.0f);

                auto cell = [&](const uint32_t column, const char* label, const char* value)
                {
                    const float x0  = left + static_cast<float>(column) * (column_width + column_gap);
                    const float mid = y + line_height * 0.5f;
                    text(font_small, label, x0, center_top(font_small, mid), col::muted);
                    text_right(font_body, value, x0 + column_width, center_top(font_body, mid), col::text);
                };

                format_count(buffer, sizeof(buffer), snapshot.draws);
                cell(0, "Draws", buffer);
                format_count(buffer, sizeof(buffer), snapshot.instances);
                cell(1, "Instances", buffer);
                y += line_height;

                format_count(buffer, sizeof(buffer), snapshot.barriers);
                cell(0, "Barriers", buffer);
                format_count(buffer, sizeof(buffer), snapshot.layouts);
                cell(1, "Layouts", buffer);
                y += line_height;

                format_count(buffer, sizeof(buffer), snapshot.pipeline_max);
                cell(0, "Pipelines", buffer);
                format_count(buffer, sizeof(buffer), snapshot.pipelines);
                cell(1, "Pipeline binds", buffer);
                y += line_height;

                format_count(buffer, sizeof(buffer), snapshot.vertex_binds);
                cell(0, "Vertex binds", buffer);
                snprintf(buffer, sizeof(buffer), "%u / %u", snapshot.descriptors, rhi_max_descriptor_set_count);
                cell(1, "Desc. sets", buffer);
                y += line_height + px(4.0f);

                const Vector2& render = Renderer::GetResolutionRender();
                const Vector2& output = Renderer::GetResolutionOutput();
                snprintf(buffer, sizeof(buffer), "%u x %u  %.0f%%", static_cast<uint32_t>(render.x), static_cast<uint32_t>(render.y), Renderer::GetResolutionScale() * 100.0f);
                text(font_small, "RENDER", left, y, col::dim);
                text(font_small, buffer, left + font_small->GetTextWidth("RENDER", false) + px(5.0f), y, col::text);
                snprintf(buffer, sizeof(buffer), "%u x %u", static_cast<uint32_t>(output.x), static_cast<uint32_t>(output.y));
                text_right(font_small, buffer, right, y, col::text);
                text_right(font_small, "OUTPUT", right - font_small->GetTextWidth(buffer) - px(5.0f), y, col::dim);
                y += font_small->GetLineHeight();
            }

            divider();

            // system, secondary information in small muted type
            {
                const float line_height = font_small->GetLineHeight() + px(2.0f);
                for (const SystemLine& line : system_lines)
                {
                    text(font_small, line.left.c_str(), left, y, col::muted);
                    text_right(font_small, line.right, right, y, col::dim);
                    y += line_height;
                }
                y -= px(2.0f);
            }
        }

        // panel, the leading edge carries the engine accent
        const float panel_y1 = y + padding - px(4.0f);
        font_shapes->AddRect(Vector2(panel_x0, panel_y0), Vector2(panel_x1, panel_y1), col::panel);
        font_shapes->AddRect(Vector2(panel_x0, panel_y0), Vector2(panel_x1, panel_y0 + hairline), col::border);
        font_shapes->AddRect(Vector2(panel_x0, panel_y1 - hairline), Vector2(panel_x1, panel_y1), col::border);
        font_shapes->AddRect(Vector2(panel_x1 - hairline, panel_y0 + hairline), Vector2(panel_x1, panel_y1 - hairline), col::border);
        font_shapes->AddRect(Vector2(panel_x0, panel_y0), Vector2(panel_x0 + px(3.0f), panel_y1), col::accent);
        for (const OverlayRect& shape : rects)
        {
            font_shapes->AddRect(Vector2(shape.x0, shape.y0), Vector2(shape.x1, shape.y1), shape.color);
        }
    }
}
