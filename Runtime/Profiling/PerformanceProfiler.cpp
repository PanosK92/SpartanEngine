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

//= INCLUDES ===========================
#include "PerformanceProfiler.h"
#include "../Core/Stopwatch.h"
#include "../Core/Timer.h"
#include "../Scene/Scene.h"
#include "../Graphics/Material.h"
#include "../Resource/ResourceManager.h"
#include "../EventSystem/EventSystem.h"
#include <iomanip>
#include <sstream>
#include "../Core/Settings.h"
//======================================

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
		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, EVENT_HANDLER_STATIC(UpdateMetrics));
	}

	void PerformanceProfiler::RenderingStarted()
	{
		if (m_renderTimer)
		{
			m_renderTimer->Start();
		}

		m_renderedMeshesCount = 0;
	}

	void PerformanceProfiler::RenderingMesh()
	{
		m_renderedMeshesCount++;
	}

	void PerformanceProfiler::RenderingFinished()
	{
		if (m_renderTimer)
		{
			m_renderTimeMs = m_renderTimer->GetElapsedTimeMs();
		}
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
		int textures = m_resourceManager->GetResourceCountByType(Resource_Texture);	
		int materials = m_resourceManager->GetResourceCountByType(Resource_Material);
		int shaders = m_resourceManager->GetResourceCountByType(Resource_Shader);
		float meshesMB = (float)m_resourceManager->GetMemoryUsage(Resource_Mesh) / 1000.0f / 1000.0f;
		float textureMemoryMB = (float)m_resourceManager->GetMemoryUsage(Resource_Texture) / 1000.0f / 1000.0f;

		m_metrics =
			"FPS:\t\t\t\t" + to_string_precision(fps, 2) + "\n"
			"Frame:\t\t\t\t" + to_string_precision(delta, 2) + " ms\n"
			"Update:\t\t\t" + to_string_precision(delta - m_renderTimeMs, 2) + " ms\n"
			"Render:\t\t\t" + to_string_precision(m_renderTimeMs, 2) + " ms\n"
			"Resolution:\t\t" + to_string(int(RESOLUTION_WIDTH)) + "x" + to_string(int(RESOLUTION_HEIGHT)) + "\n" 
			"Meshes Rendered:\t" + to_string(m_renderedMeshesPerFrame) + "\n"
			"Meshes:\t\t\t" + to_string_precision(meshesMB, 1) + " MB\n"
			"Textures:\t\t\t" + to_string(textures) + " (" + to_string_precision(textureMemoryMB, 1) + " MB)\n"
			"Materials:\t\t\t" + to_string(materials) + "\n"
			"Shaders:\t\t\t" + to_string(shaders);

		m_timeSinceLastUpdate = 0;
	}

	string PerformanceProfiler::to_string_precision(float value, int decimals)
	{
		ostringstream out;
		out << fixed << setprecision(decimals) << value;
		return out.str();
	}
}
