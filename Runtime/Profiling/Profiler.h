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

#pragma once

//= INCLUDES ==================
#include <string>
#include <vector>
#include "TimeBlock.h"
#include "../Core/EngineDefs.h"
#include "../Core/ISubsystem.h"
//=============================

#define TIME_BLOCK_START_MULTI(profiler)	        profiler->TimeBlockStart(__FUNCTION__, true, true);
#define TIME_BLOCK_START_CPU_NAMED(profiler, name)  profiler->TimeBlockStart(name, true, false);
#define TIME_BLOCK_START_CPU(profiler)		        profiler->TimeBlockStart(__FUNCTION__, true, false);
#define TIME_BLOCK_START_GPU(profiler)		        profiler->TimeBlockStart(__FUNCTION__, false, true);
#define TIME_BLOCK_END(profiler)			        profiler->TimeBlockEnd();

namespace Spartan
{
	class Context;
	class Timer;
	class ResourceCache;
	class Renderer;
    class Variant;

	class SPARTAN_CLASS Profiler : public ISubsystem
	{
	public:
		Profiler(Context* context);
        ~Profiler();

		//= Subsystem =======================
        bool Initialize() override;
        void Tick(float delta_time) override;
		//===================================

        // Events
        void OnFrameStart(float delta_time);
        void OnFrameEnd();

		// Time block
		bool TimeBlockStart(const std::string& func_name, bool profile_cpu = true, bool profile_gpu = false);
		bool TimeBlockEnd();

        // Stutter detection
        void DetectStutter();

        // Properties
		void SetProfilingEnabledCpu(const bool enabled)	{ m_profile_cpu_enabled = enabled; }
		void SetProfilingEnabledGpu(const bool enabled)	{ m_profile_gpu_enabled = enabled; }
		const auto& GetMetrics() const			        { return m_metrics; }
		const auto& GetTimeBlocks() const				{ return m_time_blocks_read; }
		auto GetTimeCpu() const						    { return m_time_cpu_ms; }
		auto GetTimeGpu() const						    { return m_time_gpu_ms; }
		auto GetTimeFrame() const						{ return m_time_frame_ms; }
		auto GetFps() const							    { return m_fps; }
		auto GetUpdateInterval()						{ return m_profiling_interval_sec; }
		void SetUpdateInterval(float internval)			{ m_profiling_interval_sec = internval; }
		const auto& GpuGetName()					    { return m_gpu_name; }
        auto GpuGetMemoryAvailable()			        { return m_gpu_memory_available; }
        auto GpuGetMemoryUsed()					        { return m_gpu_memory_used; }
        bool IsCpuStuttering()                          { return m_is_stuttering_cpu; }
        bool IsGpuStuttering()                          { return m_is_stuttering_gpu; }
		
		// Metrics - RHI
		uint32_t m_rhi_draw_calls				= 0;
		uint32_t m_rhi_bindings_buffer_index	= 0;
		uint32_t m_rhi_bindings_buffer_vertex	= 0;
		uint32_t m_rhi_bindings_buffer_constant = 0;
		uint32_t m_rhi_bindings_sampler			= 0;
		uint32_t m_rhi_bindings_texture			= 0;
		uint32_t m_rhi_bindings_shader_vertex	= 0;
		uint32_t m_rhi_bindings_shader_pixel	= 0;
        uint32_t m_rhi_bindings_shader_compute  = 0;
		uint32_t m_rhi_bindings_render_target	= 0;

		// Metrics - Renderer
		uint32_t m_renderer_meshes_rendered = 0;

		// Metrics - Time
		float m_time_frame_ms	= 0.0f;
		float m_time_cpu_ms		= 0.0f;
		float m_time_gpu_ms		= 0.0f;

	private:
        void ClearRhiMetrics()
        {
            m_rhi_draw_calls                = 0;
            m_renderer_meshes_rendered      = 0;
            m_rhi_bindings_buffer_index     = 0;
            m_rhi_bindings_buffer_vertex    = 0;
            m_rhi_bindings_buffer_constant  = 0;
            m_rhi_bindings_sampler          = 0;
            m_rhi_bindings_texture          = 0;
            m_rhi_bindings_shader_vertex    = 0;
            m_rhi_bindings_shader_pixel     = 0;
            m_rhi_bindings_shader_compute   = 0;
            m_rhi_bindings_render_target    = 0;
        }

		TimeBlock* GetNextTimeBlock();
		TimeBlock* GetLastIncompleteTimeBlock();
		TimeBlock* GetSecondLastIncompleteTimeBlock();
		void ComputeFps(float delta_time);
		void UpdateRhiMetricsString();

		// Profiling options
		bool m_profile_cpu_enabled			= true; // cheap
		bool m_profile_gpu_enabled			= true; // expensive
		float m_profiling_interval_sec		= 0.3f;
		float m_time_since_profiling_sec	= m_profiling_interval_sec;

		// Time blocks
		uint32_t m_time_block_capacity	= 200;
		uint32_t m_time_block_count		= 0;
		std::vector<TimeBlock> m_time_blocks;
        std::vector<TimeBlock> m_time_blocks_read;

		// FPS
        float m_delta_time      = 0.0f;
		float m_fps				= 0.0f;
		float m_time_passed		= 0.0f;
		uint32_t m_frame_count	= 0;

		// Hardware - GPU
		std::string m_gpu_name			= "N/A";
		uint32_t m_gpu_memory_available	= 0;
		uint32_t m_gpu_memory_used		= 0;

        // Stutter detection
        double m_cpu_avg_ms             = 0.0;
        double m_gpu_avg_ms             = 0.0;
        double m_stutter_delta_ms       = 0.5;
        double m_frames_to_accumulate   = 5;
        bool m_is_stuttering_cpu        = false;
        bool m_is_stuttering_gpu        = false;

		// Misc
		std::string m_metrics;
		bool m_profile = false;
	
		// Dependencies
		ResourceCache* m_resource_manager	= nullptr;
		Renderer* m_renderer				= nullptr;
	};
}
