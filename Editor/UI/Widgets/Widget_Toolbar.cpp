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

//= INCLUDES ==========================
#include "Widget_Toolbar.h"
#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui_internal.h"
#include "../IconProvider.h"
#include "../EditorHelper.h"
#include "Rendering/Renderer.h"
#include "Widget_Profiler.h"
//=====================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

namespace _Widget_Toolbar
{
	static float g_buttonSize			= 20.0f;
	static bool g_showRendererOptions	= false;
	static bool g_physics				= true;
	static bool g_aabb					= false;
	static bool g_gizmos				= true;
	static bool g_pickingRay			= false;
	static bool g_grid					= true;
	static bool g_performanceMetrics	= false;
	static vector<string> gbufferTextures =
	{
		"Default",
		"Albedo",
		"Normal",
		"Material",
		"Velocity",
		"Depth"
	};
	static int gbufferSelectedTextureIndex	= 0;
	static string gbufferSelectedTexture	= gbufferTextures[0];
}

Widget_Toolbar::Widget_Toolbar(Context* context) : Widget(context)
{
	m_title = "Toolbar";
	m_windowFlags = ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoTitleBar;

	m_renderer = context->GetSubsystem<Renderer>();
	Engine::EngineMode_Disable(Engine_Game);
}

bool Widget_Toolbar::Begin()
{
	ImGuiContext& g = *GImGui;
	g.NextWindowData.MenuBarOffsetMinVal = ImVec2(g.Style.DisplaySafeAreaPadding.x, ImMax(g.Style.DisplaySafeAreaPadding.y - g.Style.FramePadding.y, 0.0f));
	ImGui::SetNextWindowPos(ImVec2(g.Viewports[0]->Pos.x, g.Viewports[0]->Pos.y + 25.0f));
	ImGui::SetNextWindowSize(ImVec2(g.Viewports[0]->Size.x, g.NextWindowData.MenuBarOffsetMinVal.y + g.FontBaseSize + g.Style.FramePadding.y + 20.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 5));
	ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);

	return true;
}

void Widget_Toolbar::Tick(float deltaTime)
{
	// Play button
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, Engine::EngineMode_IsSet(Engine_Game) ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Button_Play, _Widget_Toolbar::g_buttonSize))
	{
		Engine::EngineMode_Toggle(Engine_Game);
	}
	ImGui::PopStyleColor();

	// Renderer options button
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, _Widget_Toolbar::g_showRendererOptions ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Component_Options, _Widget_Toolbar::g_buttonSize))
	{
		_Widget_Toolbar::g_showRendererOptions = true;
	}
	ImGui::PopStyleColor();

	ImGui::PopStyleVar();

	// Visibility
	if (_Widget_Toolbar::g_showRendererOptions) ShowRendererOptions();
}

void Widget_Toolbar::ShowRendererOptions()
{
	ImGui::Begin("Renderer Options", &_Widget_Toolbar::g_showRendererOptions, ImGuiWindowFlags_AlwaysAutoResize);

	if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen))
	{	
		// Read from engine
		bool bloom					= m_renderer->Flags_IsSet(Render_Bloom);
		bool tonemapping			= m_renderer->Flags_IsSet(Render_ToneMapping);
		bool fxaa					= m_renderer->Flags_IsSet(Render_FXAA);
		bool ssao					= m_renderer->Flags_IsSet(Render_SSAO);
		bool ssr					= m_renderer->Flags_IsSet(Render_SSR);
		bool taa					= m_renderer->Flags_IsSet(Render_TAA);
		bool motionBlur				= m_renderer->Flags_IsSet(Render_MotionBlur);
		bool sharpening				= m_renderer->Flags_IsSet(Render_Sharpening);
		bool chromaticAberration	= m_renderer->Flags_IsSet(Render_ChromaticAberration);
		
		// Display
		{
			auto tooltip = [this](const char* text) { if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::Text(text); ImGui::EndTooltip(); } };

			ImGui::Checkbox("Tone-mapping", &tonemapping); tooltip("ACES Filmic");
			ImGui::Checkbox("Bloom", &bloom);
			ImGui::InputFloat("Intensity", &m_renderer->m_bloomIntensity, 0.1f);		
			ImGui::Checkbox("SSAO - Screen Space Ambient Occlusion", &ssao);
			ImGui::Checkbox("SSR - Screen Space Reflections", &ssr);
			ImGui::Checkbox("Motion Blur", &motionBlur);
			ImGui::InputFloat("Strength", &m_renderer->m_motionBlurStrength, 0.1f);
			ImGui::Checkbox("Chromatic Aberration", &chromaticAberration);
			ImGui::Checkbox("TAA - Temporal Anti-Aliasing - DEVELOPMENT", &taa);
			ImGui::Checkbox("FXAA - Fast Approximate Anti-Aliasing", &fxaa);
			ImGui::InputFloat("Sub-Pixel", &m_renderer->m_fxaaSubPixel, 0.1f);					tooltip("The amount of sub-pixel aliasing removal");
			ImGui::InputFloat("Edge Threshold", &m_renderer->m_fxaaEdgeThreshold, 0.1f);		tooltip("The minimum amount of local contrast required to apply algorithm");
			ImGui::InputFloat("Edge Threshold Min", &m_renderer->m_fxaaEdgeThresholdMin, 0.1f); tooltip("Trims the algorithm from processing darks");
			ImGui::Checkbox("Sharpening", &sharpening);
			ImGui::InputFloat("Strength", &m_renderer->m_sharpenStrength, 0.1f);
			ImGui::InputFloat("Clamp", &m_renderer->m_sharpenClamp, 0.1f); tooltip("Limits maximum amount of sharpening a pixel receives");
			ImGui::Separator();
		}

		// Filter input
		m_renderer->m_bloomIntensity		= Abs(m_renderer->m_bloomIntensity);
		m_renderer->m_fxaaSubPixel			= Abs(m_renderer->m_fxaaSubPixel);
		m_renderer->m_fxaaEdgeThreshold		= Abs(m_renderer->m_fxaaEdgeThreshold);
		m_renderer->m_fxaaEdgeThresholdMin	= Abs(m_renderer->m_fxaaEdgeThresholdMin);
		m_renderer->m_sharpenStrength		= Abs(m_renderer->m_sharpenStrength);
		m_renderer->m_sharpenClamp			= Abs(m_renderer->m_sharpenClamp);
		m_renderer->m_motionBlurStrength	= Abs(m_renderer->m_motionBlurStrength);

		// Map back to engine
		bloom				? m_renderer->Flags_Enable(Render_Bloom)				: m_renderer->Flags_Disable(Render_Bloom);
		tonemapping			? m_renderer->Flags_Enable(Render_ToneMapping)			: m_renderer->Flags_Disable(Render_ToneMapping);
		fxaa				? m_renderer->Flags_Enable(Render_FXAA)					: m_renderer->Flags_Disable(Render_FXAA);
		ssao				? m_renderer->Flags_Enable(Render_SSAO)					: m_renderer->Flags_Disable(Render_SSAO);
		ssr					? m_renderer->Flags_Enable(Render_SSR)					: m_renderer->Flags_Disable(Render_SSR);
		taa					? m_renderer->Flags_Enable(Render_TAA)					: m_renderer->Flags_Disable(Render_TAA);
		motionBlur			? m_renderer->Flags_Enable(Render_MotionBlur)			: m_renderer->Flags_Disable(Render_MotionBlur);
		sharpening			? m_renderer->Flags_Enable(Render_Sharpening)			: m_renderer->Flags_Disable(Render_Sharpening);
		chromaticAberration	? m_renderer->Flags_Enable(Render_ChromaticAberration)	: m_renderer->Flags_Disable(Render_ChromaticAberration);	
	}

	if (ImGui::CollapsingHeader("Debug Visualization", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginCombo("G-Buffer", _Widget_Toolbar::gbufferSelectedTexture.c_str()))
		{
			for (int i = 0; i < _Widget_Toolbar::gbufferTextures.size(); i++)
			{
				bool is_selected = (_Widget_Toolbar::gbufferSelectedTexture == _Widget_Toolbar::gbufferTextures[i]);
				if (ImGui::Selectable(_Widget_Toolbar::gbufferTextures[i].c_str(), is_selected))
				{
					_Widget_Toolbar::gbufferSelectedTexture = _Widget_Toolbar::gbufferTextures[i].data();
					_Widget_Toolbar::gbufferSelectedTextureIndex = i;
				}
				if (is_selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (_Widget_Toolbar::gbufferSelectedTextureIndex == 0) // Combined
		{
			m_renderer->Flags_Disable(Render_Albedo);
			m_renderer->Flags_Disable(Render_Normal);
			m_renderer->Flags_Disable(Render_Material);
			m_renderer->Flags_Disable(Render_Velocity);
			m_renderer->Flags_Disable(Render_Depth);
		}
		else if (_Widget_Toolbar::gbufferSelectedTextureIndex == 1) // Albedo
		{
			m_renderer->Flags_Enable(Render_Albedo);
			m_renderer->Flags_Disable(Render_Normal);
			m_renderer->Flags_Disable(Render_Material);
			m_renderer->Flags_Disable(Render_Velocity);
			m_renderer->Flags_Disable(Render_Depth);
		}
		else if (_Widget_Toolbar::gbufferSelectedTextureIndex == 2) // Normal
		{
			m_renderer->Flags_Disable(Render_Albedo);
			m_renderer->Flags_Enable(Render_Normal);
			m_renderer->Flags_Disable(Render_Material);
			m_renderer->Flags_Disable(Render_Velocity);
			m_renderer->Flags_Disable(Render_Depth);
		}
		else if (_Widget_Toolbar::gbufferSelectedTextureIndex == 3) // Material
		{
			m_renderer->Flags_Disable(Render_Albedo);
			m_renderer->Flags_Disable(Render_Normal);
			m_renderer->Flags_Enable(Render_Material);
			m_renderer->Flags_Disable(Render_Velocity);
			m_renderer->Flags_Disable(Render_Depth);
		}
		else if (_Widget_Toolbar::gbufferSelectedTextureIndex == 4) // Velocity
		{
			m_renderer->Flags_Disable(Render_Albedo);
			m_renderer->Flags_Disable(Render_Normal);
			m_renderer->Flags_Disable(Render_Material);
			m_renderer->Flags_Enable(Render_Velocity);
			m_renderer->Flags_Disable(Render_Depth);
		}
		else if (_Widget_Toolbar::gbufferSelectedTextureIndex == 5) // Depth
		{
			m_renderer->Flags_Disable(Render_Albedo);
			m_renderer->Flags_Disable(Render_Normal);
			m_renderer->Flags_Disable(Render_Material);
			m_renderer->Flags_Disable(Render_Velocity);
			m_renderer->Flags_Enable(Render_Depth);
		}

		ImGui::Checkbox("Physics", &_Widget_Toolbar::g_physics);
		ImGui::Checkbox("AABB", &_Widget_Toolbar::g_aabb);
		ImGui::Checkbox("Gizmos", &_Widget_Toolbar::g_gizmos);
		ImGui::Checkbox("Picking Ray", &_Widget_Toolbar::g_pickingRay);
		ImGui::Checkbox("Scene Grid", &_Widget_Toolbar::g_grid);
		ImGui::Checkbox("Performance Metrics", &_Widget_Toolbar::g_performanceMetrics);

		_Widget_Toolbar::g_physics				? m_renderer->Flags_Enable(Render_Physics)				: m_renderer->Flags_Disable(Render_Physics);
		_Widget_Toolbar::g_aabb					? m_renderer->Flags_Enable(Render_AABB)					: m_renderer->Flags_Disable(Render_AABB);
		_Widget_Toolbar::g_gizmos				? m_renderer->Flags_Enable(Render_Light)				: m_renderer->Flags_Disable(Render_Light);
		_Widget_Toolbar::g_pickingRay			? m_renderer->Flags_Enable(Render_PickingRay)			: m_renderer->Flags_Disable(Render_PickingRay);
		_Widget_Toolbar::g_grid					? m_renderer->Flags_Enable(Render_SceneGrid)			: m_renderer->Flags_Disable(Render_SceneGrid);
		_Widget_Toolbar::g_performanceMetrics	? m_renderer->Flags_Enable(Render_PerformanceMetrics)	: m_renderer->Flags_Disable(Render_PerformanceMetrics);
	}

	ImGui::End();
}