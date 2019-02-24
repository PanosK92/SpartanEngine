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
#include "Core/Engine.h"
//=============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

namespace _Widget_Toolbar
{
	static float g_button_size					= 20.0f;
	static bool g_rendererer_options_visible	= false;
	static float g_renderer_options_alpha		= 1.0f;
	static bool g_gizmo_physics					= true;
	static bool g_gizmo_aabb					= false;
	static bool g_gizmo_light					= true;
	static bool g_gizmo_transform				= true;
	static bool g_gizmo_picking_ray				= false;
	static bool g_gizmo_grid					= true;
	static bool g_gizmo_performance_metrics		= false;
	static vector<string> gbuffer_textures =
	{
		"None",
		"Albedo",
		"Normal",
		"Material",
		"Velocity",
		"Depth",
		"SSAO"
	};
	static int gbuffer_selected_texture_index	= 0;
	static string gbuffer_selected_texture	= gbuffer_textures[0];
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
	auto& g = *GImGui;
	g.NextWindowData.MenuBarOffsetMinVal = ImVec2(g.Style.DisplaySafeAreaPadding.x, ImMax(g.Style.DisplaySafeAreaPadding.y - g.Style.FramePadding.y, 0.0f));
	ImGui::SetNextWindowPos(ImVec2(g.Viewports[0]->Pos.x, g.Viewports[0]->Pos.y + 25.0f));
	ImGui::SetNextWindowSize(ImVec2(g.Viewports[0]->Size.x, g.NextWindowData.MenuBarOffsetMinVal.y + g.FontBaseSize + g.Style.FramePadding.y + 20.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 5));
	ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);

	return true;
}

void Widget_Toolbar::Tick(float delta_time)
{
	// Play button
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, Engine::EngineMode_IsSet(Engine_Game) ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	if (ImGuiEx::ImageButton(Icon_Button_Play, _Widget_Toolbar::g_button_size))
	{
		Engine::EngineMode_Toggle(Engine_Game);
	}
	ImGui::PopStyleColor();

	// Renderer options button
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, _Widget_Toolbar::g_rendererer_options_visible ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	if (ImGuiEx::ImageButton(Icon_Component_Options, _Widget_Toolbar::g_button_size))
	{
		_Widget_Toolbar::g_rendererer_options_visible = true;
	}
	ImGui::PopStyleColor();

	ImGui::PopStyleVar();

	// Visibility
	if (_Widget_Toolbar::g_rendererer_options_visible) ShowRendererOptions();
}

void Widget_Toolbar::ShowRendererOptions()
{
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, _Widget_Toolbar::g_renderer_options_alpha);
	ImGui::Begin("Renderer Options", &_Widget_Toolbar::g_rendererer_options_visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking);

	ImGui::SliderFloat("Opacity", &_Widget_Toolbar::g_renderer_options_alpha, 0.1f, 1.0f, "%.1f");

	if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen))
	{	
		// Read from engine
		static vector<char*> types	= { "Off", "ACES", "Reinhard", "Uncharted 2" };
		const char* type_char_ptr	= types[static_cast<unsigned int>(m_renderer->m_tonemapping)];

		auto do_bloom					= m_renderer->Flags_IsSet(Render_PostProcess_Bloom);
		auto do_fxaa					= m_renderer->Flags_IsSet(Render_PostProcess_FXAA);
		auto do_ssao					= m_renderer->Flags_IsSet(Render_PostProcess_SSAO);
		auto do_ssr						= m_renderer->Flags_IsSet(Render_PostProcess_SSR);
		auto do_taa						= m_renderer->Flags_IsSet(Render_PostProcess_TAA);
		auto do_motion_blur				= m_renderer->Flags_IsSet(Render_PostProcess_MotionBlur);
		auto do_sharperning				= m_renderer->Flags_IsSet(Render_PostProcess_Sharpening);
		auto do_chromatic_aberration	= m_renderer->Flags_IsSet(Render_PostProcess_ChromaticAberration);
		auto do_dithering				= m_renderer->Flags_IsSet(Render_PostProcess_Dithering);
		
		// Display
		{
			const auto tooltip = [](const char* text) { if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::Text(text); ImGui::EndTooltip(); } };

			if (ImGui::BeginCombo("Tonemapping", type_char_ptr))
			{
				for (unsigned int i = 0; i < static_cast<unsigned int>(types.size()); i++)
				{
					const auto is_selected = (type_char_ptr == types[i]);
					if (ImGui::Selectable(types[i], is_selected))
					{
						type_char_ptr = types[i];
						m_renderer->m_tonemapping = static_cast<ToneMapping_Type>(i);
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
			ImGui::InputFloat("Bloom Strength", &m_renderer->m_bloom_intensity, 0.1f);		
			ImGui::Checkbox("SSAO - Screen Space Ambient Occlusion", &do_ssao);
			ImGui::Checkbox("SSR - Screen Space Reflections", &do_ssr);
			ImGui::Checkbox("Motion Blur", &do_motion_blur);
			ImGui::InputFloat("Motion Blur Strength", &m_renderer->m_motion_blur_strength, 0.1f);
			ImGui::Checkbox("Chromatic Aberration", &do_chromatic_aberration);							tooltip("Emulates the inability of old cameras to focus all colors in the same focal point");
			ImGui::Checkbox("TAA - Temporal Anti-Aliasing", &do_taa);
			ImGui::Checkbox("FXAA - Fast Approximate Anti-Aliasing", &do_fxaa);
			ImGui::InputFloat("FXAA Sub-Pixel", &m_renderer->m_fxaa_sub_pixel, 0.1f);					tooltip("The amount of sub-pixel aliasing removal");
			ImGui::InputFloat("FXAA Edge Threshold", &m_renderer->m_fxaa_edge_threshold, 0.1f);			tooltip("The minimum amount of local contrast required to apply algorithm");
			ImGui::InputFloat("FXAA Edge Threshold Min", &m_renderer->m_fxaa_edge_threshold_min, 0.1f);	tooltip("Trims the algorithm from processing darks");
			ImGui::Checkbox("Sharpen", &do_sharperning);
			ImGui::InputFloat("Sharpen Strength", &m_renderer->m_sharpen_strength, 0.1f);
			ImGui::InputFloat("Sharpen Clamp", &m_renderer->m_sharpen_clamp, 0.1f);						tooltip("Limits maximum amount of sharpening a pixel receives");
			ImGui::Checkbox("Dithering", &do_dithering);												tooltip("Reduces color banding");
		}

		// Filter input
		m_renderer->m_bloom_intensity			= Abs(m_renderer->m_bloom_intensity);
		m_renderer->m_fxaa_sub_pixel			= Abs(m_renderer->m_fxaa_sub_pixel);
		m_renderer->m_fxaa_edge_threshold		= Abs(m_renderer->m_fxaa_edge_threshold);
		m_renderer->m_fxaa_edge_threshold_min	= Abs(m_renderer->m_fxaa_edge_threshold_min);
		m_renderer->m_sharpen_strength			= Abs(m_renderer->m_sharpen_strength);
		m_renderer->m_sharpen_clamp				= Abs(m_renderer->m_sharpen_clamp);
		m_renderer->m_motion_blur_strength		= Abs(m_renderer->m_motion_blur_strength);

		// Map back to engine
		#define SET_FLAG_IF(flag, value) value	? m_renderer->Flags_Enable(flag) : m_renderer->Flags_Disable(flag)
		SET_FLAG_IF(Render_PostProcess_Bloom, do_bloom);
		SET_FLAG_IF(Render_PostProcess_FXAA, do_fxaa);
		SET_FLAG_IF(Render_PostProcess_SSAO, do_ssao);
		SET_FLAG_IF(Render_PostProcess_SSR, do_ssr);
		SET_FLAG_IF(Render_PostProcess_TAA, do_taa);
		SET_FLAG_IF(Render_PostProcess_MotionBlur, do_motion_blur);
		SET_FLAG_IF(Render_PostProcess_Sharpening, do_sharperning);
		SET_FLAG_IF(Render_PostProcess_ChromaticAberration, do_chromatic_aberration);
		SET_FLAG_IF(Render_PostProcess_Dithering, do_dithering);
	}

	if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_None))
	{
		if (ImGui::BeginCombo("Buffer", _Widget_Toolbar::gbuffer_selected_texture.c_str()))
		{
			for (auto i = 0; i < _Widget_Toolbar::gbuffer_textures.size(); i++)
			{
				const auto is_selected = (_Widget_Toolbar::gbuffer_selected_texture == _Widget_Toolbar::gbuffer_textures[i]);
				if (ImGui::Selectable(_Widget_Toolbar::gbuffer_textures[i].c_str(), is_selected))
				{
					_Widget_Toolbar::gbuffer_selected_texture = _Widget_Toolbar::gbuffer_textures[i];
					_Widget_Toolbar::gbuffer_selected_texture_index = i;
				}
				if (is_selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		m_renderer->SetDebugBuffer(static_cast<RendererDebug_Buffer>(_Widget_Toolbar::gbuffer_selected_texture_index));
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
		ImGui::Checkbox("Picking Ray", &_Widget_Toolbar::g_gizmo_picking_ray);
		// Grid
		ImGui::Checkbox("Grid", &_Widget_Toolbar::g_gizmo_grid);
		// Performance metrics
		ImGui::Checkbox("Performance Metrics", &_Widget_Toolbar::g_gizmo_performance_metrics);

		_Widget_Toolbar::g_gizmo_transform				? m_renderer->Flags_Enable(Render_Gizmo_Transform)			: m_renderer->Flags_Disable(Render_Gizmo_Transform);
		_Widget_Toolbar::g_gizmo_physics				? m_renderer->Flags_Enable(Render_Gizmo_Physics)			: m_renderer->Flags_Disable(Render_Gizmo_Physics);
		_Widget_Toolbar::g_gizmo_aabb					? m_renderer->Flags_Enable(Render_Gizmo_AABB)				: m_renderer->Flags_Disable(Render_Gizmo_AABB);
		_Widget_Toolbar::g_gizmo_light					? m_renderer->Flags_Enable(Render_Gizmo_Lights)				: m_renderer->Flags_Disable(Render_Gizmo_Lights);
		_Widget_Toolbar::g_gizmo_picking_ray			? m_renderer->Flags_Enable(Render_Gizmo_PickingRay)			: m_renderer->Flags_Disable(Render_Gizmo_PickingRay);
		_Widget_Toolbar::g_gizmo_grid					? m_renderer->Flags_Enable(Render_Gizmo_Grid)				: m_renderer->Flags_Disable(Render_Gizmo_Grid);
		_Widget_Toolbar::g_gizmo_performance_metrics	? m_renderer->Flags_Enable(Render_Gizmo_PerformanceMetrics)	: m_renderer->Flags_Disable(Render_Gizmo_PerformanceMetrics);
	}

	ImGui::End();
	ImGui::PopStyleVar();
}