/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../Core/EngineDefs.h"
#include <string>
#include <map>
#include <chrono>
#include <memory>
//=============================

#define PROFILE_BEGIN_CPU() Directus::Profiler::Get().BeginBlock_CPU(__FUNCTION__);
#define PROFILE_END_CPU()	Directus::Profiler::Get().EndBlock_CPU(__FUNCTION__);
#define PROFILE_BEGIN()		Directus::Profiler::Get().BeginBlock(__FUNCTION__);
#define PROFILE_END()		Directus::Profiler::Get().EndBlock(__FUNCTION__);

namespace Directus
{
	class Context;
	class Scene;
	class Timer;
	class ResourceManager;
	class RHI_Device;

	struct TimeBlock_CPU
	{
		std::chrono::steady_clock::time_point start;
		std::chrono::steady_clock::time_point end;
		float duration = 0.0f;
	};

	struct TimeBlock_GPU
	{
		void* query;
		void* time_start;
		void* time_end;
		float duration		= 0.0f;
		bool initialized	= false;
		bool started		= false;
	};

	class ENGINE_CLASS Profiler
	{
	public:
		static Profiler& Get()
		{
			static Profiler instance;
			return instance;
		}

		Profiler();

		void Initialize(Context* context);

		// Multi-timing
		void BeginBlock(const char* funcName);
		void EndBlock(const char* funcName);

		// CPU timing
		void BeginBlock_CPU(const char* funcName);
		void EndBlock_CPU(const char* funcName);

		// GPU timing
		void BeginBlock_GPU(const char* funcName);
		void EndBlock_GPU(const char* funcName);

		void OnUpdate();
		void OnFrameEnd();

		void SetProfilingEnabled_CPU(bool enabled)		{ m_cpuProfiling = enabled; }
		void SetProfilingEnabled_GPU(bool enabled)		{ m_gpuProfiling = enabled; }
		const std::string& GetMetrics()					{ return m_metrics; }
		float GetTimeBlockMs_CPU(const char* funcName)	{ return m_timeBlocks_cpu[funcName].duration; }
		float GetTimeBlockMs_GPU(const char* funcName)	{ return m_timeBlocks_gpu[funcName].duration; }
		const auto& GetTimeBlocks_CPU()					{ return m_timeBlocks_cpu; }
		const auto& GetTimeBlocks_GPU()					{ return m_timeBlocks_gpu; }
		
		void Reset()
		{
			m_drawCalls					= 0;
			m_meshesRendered			= 0;
			m_bindBufferIndexCount		= 0;
			m_bindBufferVertexCount		= 0;
			m_bindConstantBufferCount	= 0;
			m_bindSamplerCount			= 0;
			m_bindTextureCount			= 0;
			m_bindVertexShaderCount		= 0;
			m_bindPixelShaderCount		= 0;		
		}

		unsigned int m_drawCalls;
		unsigned int m_meshesRendered;
		unsigned int m_bindBufferIndexCount;
		unsigned int m_bindBufferVertexCount;
		unsigned int m_bindConstantBufferCount;
		unsigned int m_bindSamplerCount;
		unsigned int m_bindTextureCount;
		unsigned int m_bindVertexShaderCount;
		unsigned int m_bindPixelShaderCount;
		
	private:
		void UpdateMetrics();
		// Converts float to string with specified precision
		std::string to_string_precision(float value, int decimals);

		// Profiling options
		bool m_gpuProfiling;
		bool m_cpuProfiling;
		float m_profilingFrequencyMs;
		float m_profilingLastUpdateTime;

		// Time blocks
		std::map<const char*, TimeBlock_CPU> m_timeBlocks_cpu;
		std::map<const char*, TimeBlock_GPU> m_timeBlocks_gpu;

		// Misc
		std::string m_metrics;
		bool m_shouldUpdate;

		// Dependencies
		Scene* m_scene;
		Timer* m_timer;
		ResourceManager* m_resourceManager;
		std::shared_ptr<RHI_Device> m_rhiDevice;
	};
}
