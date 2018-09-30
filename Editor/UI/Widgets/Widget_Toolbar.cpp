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

//= INCLUDES =================================
#include "Widget_Toolbar.h"
#include "../../ImGui/Source/imgui.h"
#include "../../ImGui/Source/imgui_internal.h"
#include "../IconProvider.h"
#include "../EditorHelper.h"
#include "Rendering/Renderer.h"
#include "Widget_Profiler.h"
//============================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

namespace Widget_Toolbar_Options
{
	static float g_buttonSize			= 20.0f;
	static bool g_showRendererOptions	= false;
	static bool g_physics				= true;
	static bool g_aabb					= false;
	static bool g_gizmos				= true;
	static bool g_pickingRay			= false;
	static bool g_grid					= true;
	static bool g_performanceMetrics	= false;
	const char* g_rendererViews[]	=
	{
		"Default",
		"Albedo",
		"Normal",
		"Specular",
		"Depth"
	};
	static int g_rendererViewInt = 0;
	static const char* g_rendererView = g_rendererViews[g_rendererViewInt];
}

Widget_Toolbar::Widget_Toolbar()
{
	
}

void Widget_Toolbar::Initialize(Context* context)
{
	Widget::Initialize(context);
	m_title = "Toolbar";
	m_windowFlags = ImGuiWindowFlags_NoCollapse | 
		ImGuiWindowFlags_NoResize				| 
		ImGuiWindowFlags_NoMove					| 
		ImGuiWindowFlags_NoSavedSettings		| 
		ImGuiWindowFlags_NoScrollbar			|
		ImGuiWindowFlags_NoTitleBar;

	Engine::EngineMode_Disable(Engine_Game);

	m_profiler = make_shared<Widget_Profiler>();
	m_profiler->Initialize(m_context);
}

void Widget_Toolbar::Begin()
{
	ImGuiContext& g = *GImGui;
	float width		= g.IO.DisplaySize.x;
	float height	= g.FontBaseSize + g.Style.FramePadding.y * 2.0f - 1.0f;
	ImGui::SetNextWindowPos(ImVec2(0.0f, height - 1.0f));
	ImGui::SetNextWindowSize(ImVec2(width, height + 16));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 5));
	ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);
}

void Widget_Toolbar::Tick(float deltaTime)
{
	// Play button
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, Engine::EngineMode_IsSet(Engine_Game) ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Button_Play, Widget_Toolbar_Options::g_buttonSize))
	{
		Engine::EngineMode_Toggle(Engine_Game);
	}
	ImGui::PopStyleColor();

	// Renderer options button
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, Widget_Toolbar_Options::g_showRendererOptions ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Component_Options, Widget_Toolbar_Options::g_buttonSize))
	{
		Widget_Toolbar_Options::g_showRendererOptions = true;
	}
	ImGui::PopStyleColor();

	// Profiler button
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, m_profiler->GetVisible() ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Profiler, Widget_Toolbar_Options::g_buttonSize))
	{
		m_profiler->SetVisible(true);
	}
	ImGui::PopStyleColor();

	ImGui::PopStyleVar();

	// Visibility
	if (Widget_Toolbar_Options::g_showRendererOptions) ShowRendererOptions();
	if (m_profiler->GetVisible()) m_profiler->Tick(deltaTime);
}

void Widget_Toolbar::ShowRendererOptions()
{
	ImGui::Begin("Renderer Options", &Widget_Toolbar_Options::g_showRendererOptions, ImGuiWindowFlags_AlwaysAutoResize);

	// G-Buffer Visualization
	{
		if (ImGui::BeginCombo("G-Buffer", Widget_Toolbar_Options::g_rendererView))
		{
			for (int i = 0; i < IM_ARRAYSIZE(Widget_Toolbar_Options::g_rendererViews); i++)
			{
				bool is_selected = (Widget_Toolbar_Options::g_rendererView == Widget_Toolbar_Options::g_rendererViews[i]);
				if (ImGui::Selectable(Widget_Toolbar_Options::g_rendererViews[i], is_selected))
				{
					Widget_Toolbar_Options::g_rendererView		= Widget_Toolbar_Options::g_rendererViews[i];
					Widget_Toolbar_Options::g_rendererViewInt	= i;
				}
				if (is_selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (Widget_Toolbar_Options::g_rendererViewInt == 0) // Combined
		{
			Renderer::RenderFlags_Disable(Render_Albedo);
			Renderer::RenderFlags_Disable(Render_Normal);
			Renderer::RenderFlags_Disable(Render_Specular);
			Renderer::RenderFlags_Disable(Render_Depth);
		}
		else if (Widget_Toolbar_Options::g_rendererViewInt == 1) // Albedo
		{
			Renderer::RenderFlags_Enable(Render_Albedo);
			Renderer::RenderFlags_Disable(Render_Normal);
			Renderer::RenderFlags_Disable(Render_Specular);
			Renderer::RenderFlags_Disable(Render_Depth);
		}
		else if (Widget_Toolbar_Options::g_rendererViewInt == 2) // Normal
		{
			Renderer::RenderFlags_Disable(Render_Albedo);
			Renderer::RenderFlags_Enable(Render_Normal);
			Renderer::RenderFlags_Disable(Render_Specular);
			Renderer::RenderFlags_Disable(Render_Depth);
		}
		else if (Widget_Toolbar_Options::g_rendererViewInt == 3) // Specular
		{
			Renderer::RenderFlags_Disable(Render_Albedo);
			Renderer::RenderFlags_Disable(Render_Normal);
			Renderer::RenderFlags_Enable(Render_Specular);
			Renderer::RenderFlags_Disable(Render_Depth);
		}
		else if (Widget_Toolbar_Options::g_rendererViewInt == 4) // Depth
		{
			Renderer::RenderFlags_Disable(Render_Albedo);
			Renderer::RenderFlags_Disable(Render_Normal);
			Renderer::RenderFlags_Disable(Render_Specular);
			Renderer::RenderFlags_Enable(Render_Depth);
		}
	}

	ImGui::Separator();

	// Effects
	{	
		bool bloom					= Renderer::RenderFlags_IsSet(Render_Bloom);
		bool correction				= Renderer::RenderFlags_IsSet(Render_Correction);
		bool fxaa					= Renderer::RenderFlags_IsSet(Render_FXAA);
		bool sharpening				= Renderer::RenderFlags_IsSet(Render_Sharpening);
		bool chromaticAberration	= Renderer::RenderFlags_IsSet(Render_ChromaticAberration);
		
		ImGui::Checkbox("Bloom", &bloom);
		ImGui::Checkbox("Tone-mapping & Gamma correction", &correction);
		ImGui::Checkbox("FXAA", &fxaa);
		ImGui::Checkbox("Chromatic Aberration", &chromaticAberration);
		ImGui::Checkbox("Sharpening", &sharpening);
			
		bloom				? Renderer::RenderFlags_Enable(Render_Bloom)				: Renderer::RenderFlags_Disable(Render_Bloom);
		correction			? Renderer::RenderFlags_Enable(Render_Correction)			: Renderer::RenderFlags_Disable(Render_Correction);
		fxaa				? Renderer::RenderFlags_Enable(Render_FXAA)					: Renderer::RenderFlags_Disable(Render_FXAA);
		sharpening			? Renderer::RenderFlags_Enable(Render_Sharpening)			: Renderer::RenderFlags_Disable(Render_Sharpening);
		chromaticAberration	? Renderer::RenderFlags_Enable(Render_ChromaticAberration)	: Renderer::RenderFlags_Disable(Render_ChromaticAberration);	
	}

	ImGui::Separator();

	// Misc
	{
		ImGui::Checkbox("Physics", &Widget_Toolbar_Options::g_physics);
		ImGui::Checkbox("AABB", &Widget_Toolbar_Options::g_aabb);
		ImGui::Checkbox("Gizmos", &Widget_Toolbar_Options::g_gizmos);
		ImGui::Checkbox("Picking Ray", &Widget_Toolbar_Options::g_pickingRay);
		ImGui::Checkbox("Scene Grid", &Widget_Toolbar_Options::g_grid);
		ImGui::Checkbox("Performance Metrics", &Widget_Toolbar_Options::g_performanceMetrics);

		Widget_Toolbar_Options::g_physics				? Renderer::RenderFlags_Enable(Render_Physics)				: Renderer::RenderFlags_Disable(Render_Physics);
		Widget_Toolbar_Options::g_aabb					? Renderer::RenderFlags_Enable(Render_AABB)					: Renderer::RenderFlags_Disable(Render_AABB);
		Widget_Toolbar_Options::g_gizmos				? Renderer::RenderFlags_Enable(Render_Light)				: Renderer::RenderFlags_Disable(Render_Light);
		Widget_Toolbar_Options::g_pickingRay			? Renderer::RenderFlags_Enable(Render_PickingRay)			: Renderer::RenderFlags_Disable(Render_PickingRay);
		Widget_Toolbar_Options::g_grid					? Renderer::RenderFlags_Enable(Render_SceneGrid)			: Renderer::RenderFlags_Disable(Render_SceneGrid);
		Widget_Toolbar_Options::g_performanceMetrics	? Renderer::RenderFlags_Enable(Render_PerformanceMetrics)	: Renderer::RenderFlags_Disable(Render_PerformanceMetrics);
	}

	ImGui::End();
}