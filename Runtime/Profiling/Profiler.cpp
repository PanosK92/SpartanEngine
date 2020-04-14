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
#include "Profiler.h"
#include "../RHI/RHI_Device.h"
#include "../Rendering/Renderer.h"
#include "../RHI/RHI_CommandList.h"
#include "../Resource/ResourceCache.h"
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
		m_resource_manager	= m_context->GetSubsystem<ResourceCache>();
		m_renderer			= m_context->GetSubsystem<Renderer>();

		// Get available memory
		if (const PhysicalDevice* physical_device = m_renderer->GetRhiDevice()->GetPrimaryPhysicalDevice())
		{
			m_gpu_name				= physical_device->GetName();
			m_gpu_memory_available	= RHI_CommandList::Gpu_GetMemory(m_renderer->GetRhiDevice().get());
		}

		return true;
	}

    void Profiler::Tick(float delta_time)
    {
        if (!m_renderer || !m_renderer->GetRhiDevice()->GetContextRhi()->profiler)
            return;

        // End previous frame
        if (m_profile)
        {
            OnFrameEnd();
        }

        m_timer.Start();

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
            // Get GPU memory usage
            m_gpu_memory_used = RHI_CommandList::Gpu_GetMemoryUsed(m_renderer->GetRhiDevice().get());

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
            for (uint32_t i = 0; i < m_time_block_count; i++)
            {
                TimeBlock& time_block = m_time_blocks_write[i];

                if (time_block.IsComplete())
                {
                    // Must not happen when TimeBlockEnd() ends as D3D11 waits
                    // too much for the results to be ready, which increases CPU time.
                    time_block.ComputeDuration();

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

        // Compute cpu and gpu times
        {
            m_time_cpu_ms = 0;
            m_time_gpu_ms = 0;

            for (const TimeBlock& time_block : m_time_blocks_read)
            {
                if (!time_block.IsComplete())
                    continue;

                if (!time_block.GetParent() && time_block.GetType() == TimeBlock_Cpu)
                {
                    m_time_cpu_ms += time_block.GetDuration();
                }

                if (!time_block.GetParent() && time_block.GetType() == TimeBlock_Gpu)
                {
                    m_time_gpu_ms += time_block.GetDuration();
                }
            }

            m_time_frame_ms = Math::Min(m_timer.GetElapsedTimeMs(), m_time_cpu_ms + m_time_gpu_ms);
        }

        // Detect stutters
        {
            // Detect
            m_is_stuttering_cpu = m_time_cpu_ms > (m_cpu_avg_ms + m_stutter_delta_ms);
            m_is_stuttering_gpu = m_time_gpu_ms > (m_gpu_avg_ms + m_stutter_delta_ms);

            // Accumulate
            const double delta_feedback = 1.0 / m_frames_to_accumulate;
            m_cpu_avg_ms = m_cpu_avg_ms * (1.0 - delta_feedback) + m_time_cpu_ms * delta_feedback;
            m_gpu_avg_ms = m_gpu_avg_ms * (1.0 - delta_feedback) + m_time_gpu_ms * delta_feedback;
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

		if (auto time_block = GetNewTimeBlock())
		{
			time_block->Begin(func_name, type, time_block_parent, cmd_list, m_renderer->GetRhiDevice());
		}
	}

	void Profiler::TimeBlockEnd()
	{
		if (auto time_block = GetLastIncompleteTimeBlock())
		{
			time_block->End();
		}
	}

    TimeBlock* Profiler::GetNewTimeBlock()
	{
		// Grow capacity if needed
		if (m_time_block_count >= static_cast<uint32_t>(m_time_blocks_write.size()))
		{
            const uint32_t new_size = m_time_block_count + 100;
            m_time_blocks_read.reserve(new_size);
            m_time_blocks_read.resize(new_size);
			m_time_blocks_write.reserve(new_size);
			m_time_blocks_write.resize(new_size);
			LOG_WARNING("Time block list has grown to fit %d commands. Consider making the capacity larger to avoid re-allocations.", m_time_block_count + 1);
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
		m_frame_count++;
		m_time_passed += delta_time;
        m_fps = static_cast<float>(m_frame_count) / (m_time_passed / 1.0f);

		if (m_time_passed >= 1.0f)
		{
			m_frame_count = 0;
			m_time_passed = 0;
		}
	}

    void Profiler::UpdateRhiMetricsString()
	{
		const auto texture_count	= m_resource_manager->GetResourceCount(Resource_Texture) + m_resource_manager->GetResourceCount(Resource_Texture2d) + m_resource_manager->GetResourceCount(Resource_TextureCube);
		const auto material_count	= m_resource_manager->GetResourceCount(Resource_Material);

        static const char* text =
            // Performance
            "FPS:\t\t\t\t\t\t\t\t\t%.2f\n"
            "Frame time:\t\t\t\t\t\t\t%.2f\n"
            "CPU time:\t\t\t\t\t\t\t%.2f\n"
            "GPU time:\t\t\t\t\t\t\t%.2f\n"
            "GPU:\t\t\t\t\t\t\t\t\t%s\n"
            "VRAM:\t\t\t\t\t\t\t\t%d/%d MB\n"
            // Renderer
            "Resolution:\t\t\t\t\t\t\t%dx%d\n"
            "Meshes rendered:\t\t\t\t%d\n"
            "Textures:\t\t\t\t\t\t\t%d\n"
            "Materials:\t\t\t\t\t\t\t%d\n"
            // RHI
            "RHI Draw calls:\t\t\t\t\t\t%d\n"
            "RHI Index buffer bindings:\t\t%d\n"
            "RHI Vertex buffer bindings:\t%d\n"
            "RHI Constant buffer bindings:\t%d\n"
            "RHI Sampler bindings:\t\t\t%d\n"
            "RHI Texture bindings:\t\t\t%d\n"
            "RHI Vertex Shader bindings:\t%d\n"
            "RHI Pixel Shader bindings:\t\t%d\n"
            "RHI Compute Shader bindings:\t%d\n"
            "RHI Render Target bindings:\t%d\n"
            "RHI Pipeline bindings:\t\t\t%d\n"
            "RHI Descriptor Set bindings:\t%d";

		static char buffer[1024]; // real usage is around 800
		sprintf_s
		(
			buffer, text,

			// Performance
			m_fps,
			m_time_frame_ms,
			m_time_cpu_ms,
			m_time_gpu_ms,
			m_gpu_name.c_str(),
			m_gpu_memory_used,
			m_gpu_memory_available,

			// RendererFon
			static_cast<int>(m_renderer->GetResolution().x), static_cast<int>(m_renderer->GetResolution().y),
			m_renderer_meshes_rendered,
			texture_count,
			material_count,

			// RHI
			m_rhi_draw_calls,
			m_rhi_bindings_buffer_index,
			m_rhi_bindings_buffer_vertex,
			m_rhi_bindings_buffer_constant,
			m_rhi_bindings_sampler,
			m_rhi_bindings_texture,
			m_rhi_bindings_shader_vertex,
			m_rhi_bindings_shader_pixel,
            m_rhi_bindings_shader_compute,
			m_rhi_bindings_render_target,
            m_rhi_bindings_pipeline,
            m_rhi_bindings_descriptor_set
		);

		m_metrics = string(buffer);
	}
}
