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
#include "Profiler.h"
#include "../Core/Stopwatch.h"
#include "../Core/Timer.h"
#include "../Core/Settings.h"
#include "../Core/EventSystem.h"
#include "../Scene/Scene.h"
#include "../Rendering/Material.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceManager.h"
#include <iomanip>
#include <sstream>
//======================================

//= NAMESPACES =============
using namespace std;
using namespace std::chrono;
//==========================

namespace Directus
{
	Profiler::Profiler()
	{
		m_updateFrequencyMs			= 0;
		m_timeSinceLastUpdate		= 0;
		m_metrics					= NOT_ASSIGNED;
		m_scene						= nullptr;
		m_timer						= nullptr;
		m_resourceManager			= nullptr;
		m_renderer					= nullptr;
	}

	void Profiler::Initialize(Context* context)
	{
		m_scene				= context->GetSubsystem<Scene>();
		m_timer				= context->GetSubsystem<Timer>();
		m_resourceManager	= context->GetSubsystem<ResourceManager>();
		m_renderer			= context->GetSubsystem<Renderer>();
		m_updateFrequencyMs = 200;

		// Subscribe to update event
		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, EVENT_HANDLER(UpdateMetrics));
	}

	void Profiler::BeginBlock(const char* funcName)
	{
		m_timeBlocks[funcName].start = high_resolution_clock::now();
	}

	void Profiler::EndBlock(const char* funcName)
	{
		m_timeBlocks[funcName].end			= high_resolution_clock::now();
		duration<double, milli> ms				= m_timeBlocks[funcName].end - m_timeBlocks[funcName].start;
		m_timeBlocks[funcName].duration	= (float)ms.count();
	}

	void Profiler::UpdateMetrics()
	{
		float delta = m_timer->GetDeltaTimeMs();

		m_timeSinceLastUpdate += delta;
		if (m_timeSinceLastUpdate < m_updateFrequencyMs)
		{
			return;
		}

		float fps		= m_scene->GetFPS();	
		int textures	= m_resourceManager->GetResourceCountByType(Resource_Texture);	
		int materials	= m_resourceManager->GetResourceCountByType(Resource_Material);
		int shaders		= m_resourceManager->GetResourceCountByType(Resource_Shader);

		m_metrics =
			"FPS:\t\t\t\t" + to_string_precision(fps, 2) + "\n"
			"Frame:\t\t\t\t" + to_string_precision(delta, 2) + " ms\n"
			//"Update:\t\t\t" + to_string_precision(delta - m_renderTimeMs, 2) + " ms\n" Must compute this properly
			"Render:\t\t\t" + to_string_precision(GetBlockTimeMs("Directus::Renderer::Render"), 2) + " ms\n"
			"Resolution:\t\t" + to_string(int(RESOLUTION_WIDTH)) + "x" + to_string(int(RESOLUTION_HEIGHT)) + "\n" 
			"Meshes Rendered:\t" + to_string(m_renderer->GetRendereredMeshes()) + "\n"
			"Textures:\t\t\t" + to_string(textures) + "\n"
			"Materials:\t\t\t" + to_string(materials) + "\n"
			"Shaders:\t\t\t" + to_string(shaders);

		m_timeSinceLastUpdate = 0;
	}

	string Profiler::to_string_precision(float value, int decimals)
	{
		ostringstream out;
		out << fixed << setprecision(decimals) << value;
		return out.str();
	}
}
