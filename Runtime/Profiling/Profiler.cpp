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

//= INCLUDES =====================
#include "Profiler.h"
#include "../Core/Timer.h"
#include "../Core/Settings.h"
#include "../Core/EventSystem.h"
#include "../Scene/Scene.h"
#include "../Rendering/Renderer.h"
#include <iomanip>
#include <sstream>
#include "../RHI/RHI_Device.h"
//================================

//= NAMESPACES =============
using namespace std;
using namespace std::chrono;
//==========================

namespace Directus
{
	Profiler::Profiler()
	{
		m_metrics					= NOT_ASSIGNED;
		m_scene						= nullptr;
		m_timer						= nullptr;
		m_resourceManager			= nullptr;
		m_gpuProfiling				= true;	// expensive
		m_cpuProfiling				= true;	// cheap
		m_profilingFrequencyMs		= 0;
		m_profilingLastUpdateTime	= 0;
	}

	void Profiler::Initialize(Context* context)
	{
		m_scene						= context->GetSubsystem<Scene>();
		m_timer						= context->GetSubsystem<Timer>();
		m_resourceManager			= context->GetSubsystem<ResourceManager>();
		m_rhiDevice					= context->GetSubsystem<Renderer>()->GetRHIDevice();
		m_profilingFrequencyMs		= 500;
		m_profilingLastUpdateTime	= m_profilingFrequencyMs;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, EVENT_HANDLER(OnUpdate));
		SUBSCRIBE_TO_EVENT(EVENT_FRAME_END, EVENT_HANDLER(OnFrameEnd));
	}

	void Profiler::TimeBlockStart_CPU(const char* funcName)
	{
		if (!m_cpuProfiling || !m_shouldUpdate)
			return;

		m_timeBlocks_cpu[funcName].start = high_resolution_clock::now();
	}

	void Profiler::TimeBlockEnd_CPU(const char* funcName)
	{
		if (!m_cpuProfiling || !m_shouldUpdate)
			return;

		auto timeBlock = &m_timeBlocks_cpu[funcName];

		timeBlock->end				= high_resolution_clock::now();
		duration<double, milli> ms	= timeBlock->end - timeBlock->start;
		timeBlock->duration			= (float)ms.count();
	}

	void Profiler::TimeBlockStart_GPU(const char* funcName)
	{
		if (!m_gpuProfiling || !m_shouldUpdate)
			return;

		auto timeBlock = &m_timeBlocks_gpu[funcName];

		if (!timeBlock->initialized)
		{
			m_rhiDevice->Profiling_CreateQuery(&timeBlock->query,		Query_Timestamp_Disjoint);
			m_rhiDevice->Profiling_CreateQuery(&timeBlock->time_start,	Query_Timestamp);
			m_rhiDevice->Profiling_CreateQuery(&timeBlock->time_end,	Query_Timestamp);
			timeBlock->initialized = true;
		}

		m_rhiDevice->Profiling_QueryStart(timeBlock->query);
		m_rhiDevice->Profiling_GetTimeStamp(timeBlock->time_start);
		timeBlock->started = true;
	}

	void Profiler::TimeBlockEnd_GPU(const char* funcName)
	{
		if (!m_gpuProfiling || !m_shouldUpdate)
			return;

		auto timeBlock = &m_timeBlocks_gpu[funcName];

		m_rhiDevice->Profiling_GetTimeStamp(timeBlock->time_end);
		m_rhiDevice->Profiling_QueryEnd(timeBlock->query);
	}

	void Profiler::TimeBlockStart_Multi(const char* funcName)
	{
		TimeBlockStart_CPU(funcName);
		TimeBlockStart_GPU(funcName);
	}

	void Profiler::TimeBlockEnd_Multi(const char* funcName)
	{
		TimeBlockEnd_CPU(funcName);
		TimeBlockEnd_GPU(funcName);
	}

	void Profiler::OnUpdate()
	{
		m_profilingLastUpdateTime += m_timer->GetDeltaTimeMs();
		if (m_profilingLastUpdateTime < m_profilingFrequencyMs)
			return;

		UpdateMetrics();
		m_shouldUpdate				= true;
		m_profilingLastUpdateTime	= 0.0f;
	}

	void Profiler::OnFrameEnd()
	{
		if (!m_shouldUpdate)
			return;

		for (auto& entry : m_timeBlocks_gpu)
		{
			auto& timeBlock = entry.second;

			if (timeBlock.started)
			{
				timeBlock.duration = m_rhiDevice->Profiling_GetDuration(timeBlock.query, timeBlock.time_start, timeBlock.time_end);
			}
			timeBlock.started = false;
		}

		m_shouldUpdate = false;
	}

	void Profiler::UpdateMetrics()
	{
		float fps		= m_scene->GetFPS();
		int textures	= m_resourceManager->GetResourceCountByType(Resource_Texture);
		int materials	= m_resourceManager->GetResourceCountByType(Resource_Material);
		int shaders		= m_resourceManager->GetResourceCountByType(Resource_Shader);

		m_metrics =
			"FPS:\t\t\t\t\t\t\t"				+ to_string_precision(fps, 2) + "\n"
			"CPU:\t\t\t\t\t\t\t"				+ to_string_precision(m_timer->GetDeltaTimeMs(), 2) + " ms\n"
			"GPU:\t\t\t\t\t\t\t"				+ to_string_precision(GetTimeBlockMs_GPU("Directus::Renderer::Render"), 2) + " ms\n"
			"Resolution:\t\t\t\t\t"				+ to_string(int(Settings::Get().GetResolutionWidth())) + "x" + to_string(int(Settings::Get().GetResolutionHeight())) + "\n"
			"Meshes rendered:\t\t\t\t"			+ to_string(m_meshesRendered) + "\n"
			"Textures:\t\t\t\t\t\t"				+ to_string(textures) + "\n"
			"Materials:\t\t\t\t\t\t"			+ to_string(materials) + "\n"
			"Shaders:\t\t\t\t\t\t"				+ to_string(shaders) + "\n"
			"RHI Draw calls:\t\t\t\t\t"			+ to_string(m_drawCalls) + "\n"
			"RHI Index buffer bindings:\t\t"	+ to_string(m_bindBufferIndexCount) + "\n"
			"RHI Vertex buffer bindings:\t"		+ to_string(m_bindBufferVertexCount) + "\n"
			"RHI Constant buffer bindings:\t"	+ to_string(m_bindConstantBufferCount) + "\n"
			"RHI Sampler bindings:\t\t\t"		+ to_string(m_bindSamplerCount) + "\n"
			"RHI Texture bindings:\t\t\t"		+ to_string(m_bindTextureCount) + "\n"
			"RHI Vertex Shader bindings:\t"		+ to_string(m_bindVertexShaderCount) + "\n"
			"RHI Pixel Shader bindings:\t\t"	+ to_string(m_bindPixelShaderCount);
	}

	string Profiler::to_string_precision(float value, int decimals)
	{
		ostringstream out;
		out << fixed << setprecision(decimals) << value;
		return out.str();
	}
}
