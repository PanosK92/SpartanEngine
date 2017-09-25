/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ===================================
#include "PerformanceProfiler.h"
#include "../Core/Stopwatch.h"
#include "../Core/Timer.h"
#include "../Core/Scene.h"
#include "../Graphics/Material.h"
#include "../Graphics/Shaders/ShaderVariation.h"
#include "../Resource/ResourceManager.h"
#include "../EventSystem/EventSystem.h"
#include <iomanip>
#include <sstream>
//==============================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	Scene* PerformanceProfiler::m_scene;
	Timer* PerformanceProfiler::m_timer;
	ResourceManager* PerformanceProfiler::m_resourceManager;
	unique_ptr<Stopwatch> PerformanceProfiler::m_renderTimer;
	float PerformanceProfiler::m_renderTimeMs;
	int PerformanceProfiler::m_renderedMeshesCount;
	int PerformanceProfiler::m_renderedMeshesPerFrame;
	string PerformanceProfiler::m_metrics;
	float PerformanceProfiler::m_updateFrequencyMs;
	float PerformanceProfiler::m_timeSinceLastUpdate;

	void PerformanceProfiler::Initialize(Context* context)
	{
		// Dependencies
		m_scene = context->GetSubsystem<Scene>();
		m_timer = context->GetSubsystem<Timer>();
		m_resourceManager = context->GetSubsystem<ResourceManager>();

		// Metrics
		m_renderTimeMs = 0;
		m_renderedMeshesCount = 0;
		m_renderedMeshesPerFrame = 0;
		// Settings
		m_updateFrequencyMs = 200;

		// Misc
		m_renderTimer = make_unique<Stopwatch>();

		// Subscribe to update event
		EventSystem::Subscribe(EVENT_UPDATE, &PerformanceProfiler::UpdateMetrics);
	}

	void PerformanceProfiler::RenderingStarted()
	{
		m_renderTimer->Start();
		m_renderedMeshesCount = 0;
	}

	void PerformanceProfiler::RenderingMesh()
	{
		m_renderedMeshesCount++;
	}

	void PerformanceProfiler::RenderingStoped()
	{
		m_renderTimeMs = m_renderTimer->Stop();
		m_renderedMeshesPerFrame = m_renderedMeshesCount;
	}

	void PerformanceProfiler::UpdateMetrics()
	{
		float delta = m_timer->GetDeltaTimeMs();

		m_timeSinceLastUpdate += delta;
		if (m_timeSinceLastUpdate < m_updateFrequencyMs)
		{
			return;
		}

		float fps = m_scene->GetFPS();
		int materials = m_resourceManager->GetResourceCountByType<Material>();
		int shaders = m_resourceManager->GetResourceCountByType<ShaderVariation>();

		m_metrics =
			"FPS: " + To_String_Precision(fps, 2) + "\n"
			"Frame: " + To_String_Precision(delta, 2) + " ms\n"
			"Render: " + To_String_Precision(m_renderTimeMs, 2) + " ms\n"
			"Meshes Rendered: " + to_string(m_renderedMeshesPerFrame) + "\n"
			"Materials: " + to_string(materials) + "\n"
			"Shaders: " + to_string(shaders);

		m_timeSinceLastUpdate = 0;
	}

	string PerformanceProfiler::To_String_Precision(float value, int decimals)
	{
		ostringstream out;
		out << fixed << setprecision(decimals) << value;
		return out.str();
	}
}
