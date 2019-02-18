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

//= INCLUDES ==================
#include "Widget_Toolbar.h"
#include "../IconProvider.h"
#include "../EditorHelper.h"
#include "Rendering/Renderer.h"
#include "Widget_Profiler.h"
//=============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

namespace _Widget_Toolbar
{
	static float g_buttonSize				= 20.0f;
	static bool g_renderererOptions_visible	= false;
	static float g_rendererOptions_alpha	= 1.0f;
	static bool g_gizmo_physics				= true;
	static bool g_gizmo_aabb				= false;
	static bool g_gizmo_light				= true;
	static bool g_gizmo_transform			= true;
	static bool g_gizmo_pickingRay			= false;
	static bool g_gizmo_grid				= true;
	static bool g_gizmo_performanceMetrics	= false;
	static vector<string> gbufferTextures =
	{
		"None",
		"Albedo",
		"Normal",
		"Material",
		"Velocity",
		"Depth",
		"SSAO"
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

	m_renderer = context->GetSubsystem<Renderer>().get();
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
	ImGui::PushStyleColor(ImGuiCol_Button, _Widget_Toolbar::g_renderererOptions_visible ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Component_Options, _Widget_Toolbar::g_buttonSize))
	{
		_Widget_Toolbar::g_renderererOptions_visible = true;
	}
	ImGui::PopStyleColor();

	ImGui::PopStyleVar();

	// Visibility
	if (_Widget_Toolbar::g_renderererOptions_visible) ShowRendererOptions();
}

void Widget_Toolbar::ShowRendererOptions()
{
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, _Widget_Toolbar::g_rendererOptions_alpha);
	ImGui::Begin("Renderer Options", &_Widget_Toolbar::g_renderererOptions_visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking);

	ImGui::SliderFloat("Opacity", &_Widget_Toolbar::g_rendererOptions_alpha, 0.1f, 1.0f, "%.1f");

	if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen))
	{	
		// Read from engine
		static vector<char*> types	= { "Off", "ACES", "Reinhard", "Uncharted 2" };
		const char* typeCharPtr		= types[(int)m_renderer->m_tonemapping];

		bool do_bloom				= m_renderer->Flags_IsSet(Render_PostProcess_Bloom);
		bool do_fxaa				= m_renderer->Flags_IsSet(Render_PostProcess_FXAA);
		bool do_ssao				= m_renderer->Flags_IsSet(Render_PostProcess_SSAO);
		bool do_ssr					= m_renderer->Flags_IsSet(Render_PostProcess_SSR);
		bool do_taa					= m_renderer->Flags_IsSet(Render_PostProcess_TAA);
		bool do_motionBlur			= m_renderer->Flags_IsSet(Render_PostProcess_MotionBlur);
		bool do_sharperning			= m_renderer->Flags_IsSet(Render_PostProcess_Sharpening);
		bool do_chromaticAberration	= m_renderer->Flags_IsSet(Render_PostProcess_ChromaticAberration);
		bool do_dithering			= m_renderer->Flags_IsSet(Render_PostProcess_Dithering);
		
		// Display
		{
			auto tooltip = [this](const char* text) { if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::Text(text); ImGui::EndTooltip(); } };

			if (ImGui::BeginCombo("Tonemapping", typeCharPtr))
			{
				for (unsigned int i = 0; i < (unsigned int)types.size(); i++)
				{
					bool is_selected = (typeCharPtr == types[i]);
					if (ImGui::Selectable(types[i], is_selected))
					{
						typeCharPtr = types[i];
						m_renderer->m_tonemapping = (ToneMapping_Type)i;
					}
					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::InputFloat("Gamma", &m_renderer->m_gamma, 0.1f);
			ImGui::Checkbox("Bloom", &do_bloom);
			ImGui::InputFloat("Bloom Strength", &m_renderer->m_bloomIntensity, 0.1f);		
			ImGui::Checkbox("SSAO - Screen Space Ambient Occlusion", &do_ssao);
			ImGui::Checkbox("SSR - Screen Space Reflections", &do_ssr);
			ImGui::Checkbox("Motion Blur", &do_motionBlur);
			ImGui::InputFloat("Motion Blur Strength", &m_renderer->m_motionBlurStrength, 0.1f);
			ImGui::Checkbox("Chromatic Aberration", &do_chromaticAberration);							tooltip("Emulates the inability of old cameras to focus all colors in the same focal point");
			ImGui::Checkbox("TAA - Temporal Anti-Aliasing", &do_taa);
			ImGui::Checkbox("FXAA - Fast Approximate Anti-Aliasing", &do_fxaa);
			ImGui::InputFloat("FXAA Sub-Pixel", &m_renderer->m_fxaaSubPixel, 0.1f);						tooltip("The amount of sub-pixel aliasing removal");
			ImGui::InputFloat("FXAA Edge Threshold", &m_renderer->m_fxaaEdgeThreshold, 0.1f);			tooltip("The minimum amount of local contrast required to apply algorithm");
			ImGui::InputFloat("FXAA Edge Threshold Min", &m_renderer->m_fxaaEdgeThresholdMin, 0.1f);	tooltip("Trims the algorithm from processing darks");
			ImGui::Checkbox("Sharpen", &do_sharperning);
			ImGui::InputFloat("Sharpen Strength", &m_renderer->m_sharpenStrength, 0.1f);
			ImGui::InputFloat("Sharpen Clamp", &m_renderer->m_sharpenClamp, 0.1f);						tooltip("Limits maximum amount of sharpening a pixel receives");
			ImGui::Checkbox("Dithering", &do_dithering);												tooltip("Reduces color banding");
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
		#define SET_FLAG_IF(flag, value) value	? m_renderer->Flags_Enable(flag) : m_renderer->Flags_Disable(flag)
		SET_FLAG_IF(Render_PostProcess_Bloom, do_bloom);
		SET_FLAG_IF(Render_PostProcess_FXAA, do_fxaa);
		SET_FLAG_IF(Render_PostProcess_SSAO, do_ssao);
		SET_FLAG_IF(Render_PostProcess_SSR, do_ssr);
		SET_FLAG_IF(Render_PostProcess_TAA, do_taa);
		SET_FLAG_IF(Render_PostProcess_MotionBlur, do_motionBlur);
		SET_FLAG_IF(Render_PostProcess_Sharpening, do_sharperning);
		SET_FLAG_IF(Render_PostProcess_ChromaticAberration, do_chromaticAberration);
		SET_FLAG_IF(Render_PostProcess_Dithering, do_dithering);
	}

	if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_None))
	{
		if (ImGui::BeginCombo("Buffer", _Widget_Toolbar::gbufferSelectedTexture.c_str()))
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

		m_renderer->SetDebugBuffer((RendererDebug_Buffer)_Widget_Toolbar::gbufferSelectedTextureIndex);
	}

	if (ImGui::CollapsingHeader("Gizmos", ImGuiTreeNodeFlags_None))
	{
		// Transform
		ImGui::Checkbox("Transform", &_Widget_Toolbar::g_gizmo_transform); 
		ImGui::InputFloat("Size", &m_renderer->m_gizmo_transform_size, 0.0025f);
		ImGui::InputFloat("Speed", &m_renderer->m_gizmo_transform_speed, 1.0f);
		// Physics
		ImGui::Checkbox("Physics", &_Widget_Toolbar::g_gizmo_physics);
		// AABB
		ImGui::Checkbox("AABB", &_Widget_Toolbar::g_gizmo_aabb);
		// Lights
		ImGui::Checkbox("Lights", &_Widget_Toolbar::g_gizmo_light);	
		// Picking Ray
		ImGui::Checkbox("Picking Ray", &_Widget_Toolbar::g_gizmo_pickingRay);
		// Grid
		ImGui::Checkbox("Grid", &_Widget_Toolbar::g_gizmo_grid);
		// Performance metrics
		ImGui::Checkbox("Performance Metrics", &_Widget_Toolbar::g_gizmo_performanceMetrics);

		_Widget_Toolbar::g_gizmo_transform			? m_renderer->Flags_Enable(Render_Gizmo_Transform)			: m_renderer->Flags_Disable(Render_Gizmo_Transform);
		_Widget_Toolbar::g_gizmo_physics			? m_renderer->Flags_Enable(Render_Gizmo_Physics)			: m_renderer->Flags_Disable(Render_Gizmo_Physics);
		_Widget_Toolbar::g_gizmo_aabb				? m_renderer->Flags_Enable(Render_Gizmo_AABB)				: m_renderer->Flags_Disable(Render_Gizmo_AABB);
		_Widget_Toolbar::g_gizmo_light				? m_renderer->Flags_Enable(Render_Gizmo_Lights)				: m_renderer->Flags_Disable(Render_Gizmo_Lights);
		_Widget_Toolbar::g_gizmo_pickingRay			? m_renderer->Flags_Enable(Render_Gizmo_PickingRay)			: m_renderer->Flags_Disable(Render_Gizmo_PickingRay);
		_Widget_Toolbar::g_gizmo_grid				? m_renderer->Flags_Enable(Render_Gizmo_Grid)				: m_renderer->Flags_Disable(Render_Gizmo_Grid);
		_Widget_Toolbar::g_gizmo_performanceMetrics	? m_renderer->Flags_Enable(Render_Gizmo_PerformanceMetrics)	: m_renderer->Flags_Disable(Render_Gizmo_PerformanceMetrics);
	}

	ImGui::End();
	ImGui::PopStyleVar();
}