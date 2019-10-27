/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "../Core/EventSystem.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	Profiler::Profiler(Context* context) : ISubsystem(context)
	{
		m_time_blocks.reserve(m_time_block_capacity);
		m_time_blocks.resize(m_time_block_capacity);
	}

    Profiler::~Profiler()
    {
        if (m_profile) OnFrameEnd();
        m_time_blocks.clear();
        m_time_blocks_read.clear();
        ClearRhiMetrics();
    }

    bool Profiler::Initialize()
	{
		m_resource_manager	= m_context->GetSubsystem<ResourceCache>().get();
		m_renderer			= m_context->GetSubsystem<Renderer>().get();

		// Get available memory
		if (const DisplayAdapter* adapter = m_renderer->GetRhiDevice()->GetPrimaryAdapter())
		{
			m_gpu_name				= adapter->name;
			m_gpu_memory_available	= m_renderer->GetRhiDevice()->ProfilingGetGpuMemory();
		}

		return true;
	}

    void Profiler::Tick(float delta_time)
    {
        // End previous frame
        if (m_profile)
        {
            OnFrameEnd();
        }

        // Compute some stuff
        m_time_cpu_ms   = m_time_blocks[0].GetDurationCpu(); // This assumes that that first time block is the frame time
        m_time_gpu_ms   = m_time_blocks[0].GetDurationGpu(); // This assumes that that first time block is the frame time
        m_time_frame_ms = m_time_cpu_ms + m_time_gpu_ms;
        ComputeFps(delta_time);
        
        // Check whether we should profile or not
        m_profile                   = false;
        m_time_since_profiling_sec  += delta_time;   
        if (m_time_since_profiling_sec >= m_profiling_interval_sec)
        {
            m_time_since_profiling_sec  = 0.0f;
            m_profile                   = true;
        }

        // Updating every m_profiling_interval_sec
        if (m_profile)
        {
            // Get GPU memory usage
            m_gpu_memory_used = m_renderer->GetRhiDevice()->ProfilingGetGpuMemoryUsage();

            // Create a string version of the rhi metrics
            if (m_renderer->GetOptions() & Render_Debug_PerformanceMetrics)
            {
                UpdateRhiMetricsString();
            }

            // Start a new frame
            OnFrameStart(delta_time);
        }

        ClearRhiMetrics();
    }

    void Profiler::OnFrameStart(float delta_time)
    {
        // Discard previous frame data
        for (uint32_t i = 0; i < m_time_block_count; i++)
        {
            TimeBlock& time_block = m_time_blocks[i];
            if (!time_block.IsComplete())
            {
                LOG_WARNING("Ensure that TimeBlockEnd() is called for %s", time_block.GetName().c_str());
            }
            time_block.Clear();
        }

        m_time_block_count = 0;

        // Start frame time block
        TimeBlockStart("Frame", true, true);
    }

    void Profiler::OnFrameEnd()
    {
        TimeBlockEnd();

        for (auto& time_block : m_time_blocks)
        {
            if (!time_block.IsProfilingGpu())
                continue;

            time_block.OnFrameEnd(m_renderer->GetRhiDevice());
        }

        m_time_blocks_read = m_time_blocks;
        DetectStutter();
    }

    bool Profiler::TimeBlockStart(const string& func_name, bool profile_cpu /*= true*/, bool profile_gpu /*= false*/)
	{
		if (!m_profile)
			return false;

		bool can_profile_cpu = profile_cpu && m_profile_cpu_enabled;
		bool can_profile_gpu = profile_gpu && m_profile_gpu_enabled;

		if (!can_profile_cpu && !can_profile_gpu)
			return false;

		if (auto time_block = GetNextTimeBlock())
		{
			auto time_block_parent = GetSecondLastIncompleteTimeBlock();
			time_block->Begin(func_name, can_profile_cpu, can_profile_gpu, time_block_parent, m_renderer->GetRhiDevice());
		}

		return true;
	}

	bool Profiler::TimeBlockEnd()
	{
		if (!m_profile || m_time_block_count == 0)
			return false;

		if (auto time_block = GetLastIncompleteTimeBlock())
		{
			time_block->End(m_renderer->GetRhiDevice());
		}

		return true;
	}

    void Profiler::DetectStutter()
    {
        // Detect
        m_is_stuttering_cpu = m_time_cpu_ms > (m_cpu_avg_ms + m_stutter_delta_ms);
        m_is_stuttering_gpu = m_time_gpu_ms > (m_gpu_avg_ms + m_stutter_delta_ms);

        // Accumulate
        double delta_feedback = 1.0 / m_frames_to_accumulate;
        m_cpu_avg_ms = m_cpu_avg_ms * (1.0 - delta_feedback) + m_time_cpu_ms * delta_feedback;
        m_gpu_avg_ms = m_gpu_avg_ms * (1.0 - delta_feedback) + m_time_gpu_ms * delta_feedback;
    }

    TimeBlock* Profiler::GetNextTimeBlock()
	{
		// Grow capacity if needed
		if (m_time_block_count >= static_cast<uint32_t>(m_time_blocks.size()))
		{
			uint32_t new_size = m_time_block_count + 100;
			m_time_blocks.reserve(new_size);
			m_time_blocks.resize(new_size);
			LOG_WARNING("Time block list has grown to fit %d commands. Consider making the capacity larger to avoid re-allocations.", m_time_block_count + 1);
		}

		// Return a time block
		m_time_block_count++;
		return &m_time_blocks[m_time_block_count - 1];
	}

	TimeBlock* Profiler::GetLastIncompleteTimeBlock()
	{
		for (int i = m_time_block_count - 1; i >= 0; i--)
		{
			TimeBlock& time_block = m_time_blocks[i];
			if (!time_block.IsComplete())
				return &time_block;
		}

		return nullptr;
	}

	TimeBlock* Profiler::GetSecondLastIncompleteTimeBlock()
	{
		bool first_found = false;
		for (int i = m_time_block_count - 1; i >= 0; i--)
		{
			TimeBlock& time_block = m_time_blocks[i];
			if (!time_block.IsComplete())
			{
				if (first_found)
				{
					return &time_block;
				}
				first_found = true;		
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

		static char buffer[1000]; // real usage is around 700
		sprintf_s
		(
			buffer,

			// Performance
			"FPS:\t\t\t\t\t\t\t%.2f\n"
			"Frame time:\t\t\t\t\t%.2f\n"
			"CPU time:\t\t\t\t\t%.2f\n"
			"GPU time:\t\t\t\t\t%.2f\n"
			"GPU:\t\t\t\t\t\t\t%s\n"
			"VRAM:\t\t\t\t\t\t%d/%d MB\n"
			// Renderer
			"Resolution:\t\t\t\t\t%dx%d\n"
			"Meshes rendered:\t\t\t\t%d\n"
			"Textures:\t\t\t\t\t%d\n"
			"Materials:\t\t\t\t\t%d\n"
			// RHI
			"RHI Draw calls:\t\t\t\t%d\n"
			"RHI Index buffer bindings:\t\t%d\n"
			"RHI Vertex buffer bindings:\t\t%d\n"
			"RHI Constant buffer bindings:\t%d\n"
			"RHI Sampler bindings:\t\t\t%d\n"
			"RHI Texture bindings:\t\t\t%d\n"
			"RHI Vertex Shader bindings:\t\t%d\n"
			"RHI Pixel Shader bindings:\t\t%d\n"
            "RHI Compute Shader bindings:\t%d\n"
			"RHI Render Target bindings:\t\t%d",
			
			// Performance
			m_fps,
			m_time_frame_ms,
			m_time_cpu_ms,
			m_time_gpu_ms,
			m_gpu_name.c_str(),
			m_gpu_memory_used,
			m_gpu_memory_available,
			// Renderer
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
			m_rhi_bindings_render_target
		);

		m_metrics = string(buffer);
	}
}
