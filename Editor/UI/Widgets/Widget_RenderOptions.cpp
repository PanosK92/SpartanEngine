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

//= INCLUDES ====================
#include "Widget_RenderOptions.h"
#include "Rendering/Renderer.h"
#include "Core/Context.h"
#include "Math/MathHelper.h"
#include <Core\Timer.h>
//===============================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

namespace _RenderOptions
{
    static bool g_gizmo_physics             = true;
    static bool g_gizmo_aabb                = false;
    static bool g_gizmo_light               = true;
    static bool g_gizmo_transform           = true;
    static bool g_gizmo_picking_ray         = false;
    static bool g_gizmo_grid                = true;
    static bool g_gizmo_performance_metrics = false;
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
    static int gbuffer_selected_texture_index = 0;
    static string gbuffer_selected_texture = gbuffer_textures[0];
}


Widget_RenderOptions::Widget_RenderOptions(Context* context) : Widget(context)
{
    m_title         = "Renderer Options";
    m_flags         |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
    m_is_visible	= false;
    m_renderer      = context->GetSubsystem<Renderer>().get();
    m_alpha         = 1.0f;
}

void Widget_RenderOptions::Tick()
{
    ImGui::SliderFloat("Opacity", &m_alpha, 0.1f, 1.0f, "%.1f");

    if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Read from engine
        static vector<char*> types = { "Off", "ACES", "Reinhard", "Uncharted 2" };
        const char* type_char_ptr = types[static_cast<unsigned int>(m_renderer->m_tonemapping)];

        auto do_bloom                   = m_renderer->FlagEnabled(Render_PostProcess_Bloom);
        auto do_fxaa                    = m_renderer->FlagEnabled(Render_PostProcess_FXAA);
        auto do_ssao                    = m_renderer->FlagEnabled(Render_PostProcess_SSAO);
        auto do_ssr                     = m_renderer->FlagEnabled(Render_PostProcess_SSR);
        auto do_taa                     = m_renderer->FlagEnabled(Render_PostProcess_TAA);
        auto do_motion_blur             = m_renderer->FlagEnabled(Render_PostProcess_MotionBlur);
        auto do_sharperning             = m_renderer->FlagEnabled(Render_PostProcess_Sharpening);
        auto do_chromatic_aberration    = m_renderer->FlagEnabled(Render_PostProcess_ChromaticAberration);
        auto do_dithering               = m_renderer->FlagEnabled(Render_PostProcess_Dithering);
        auto resolution_shadow          = static_cast<int>(m_renderer->GetShadowResolution());

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
            ImGui::InputFloat("Exposure",                               &m_renderer->m_exposure, 0.1f);
            ImGui::InputFloat("Gamma",                                  &m_renderer->m_gamma, 0.1f);
            ImGui::Separator();

            ImGui::Checkbox("Bloom",                                    &do_bloom);
            ImGui::InputFloat("Bloom Strength",                         &m_renderer->m_bloom_intensity, 0.1f);
            ImGui::Separator();

            ImGui::Checkbox("SSAO - Screen Space Ambient Occlusion",    &do_ssao);
            ImGui::Separator();

            ImGui::Checkbox("SSR - Screen Space Reflections",           &do_ssr);
            ImGui::Separator();

            ImGui::Checkbox("Motion Blur",                              &do_motion_blur);
            ImGui::InputFloat("Motion Blur Strength",                   &m_renderer->m_motion_blur_strength, 0.1f);
            ImGui::Separator();

            ImGui::Checkbox("Chromatic Aberration",                     &do_chromatic_aberration);						tooltip("Emulates the inability of old cameras to focus all colors in the same focal point");
            ImGui::Separator();

            ImGui::Checkbox("TAA - Temporal Anti-Aliasing",             &do_taa);
            ImGui::Checkbox("FXAA - Fast Approximate Anti-Aliasing",    &do_fxaa);
            ImGui::InputFloat("FXAA Sub-Pixel",                         &m_renderer->m_fxaa_sub_pixel, 0.1f);			tooltip("The amount of sub-pixel aliasing removal");
            ImGui::InputFloat("FXAA Edge Threshold",                    &m_renderer->m_fxaa_edge_threshold, 0.1f);		tooltip("The minimum amount of local contrast required to apply algorithm");
            ImGui::InputFloat("FXAA Edge Threshold Min",                &m_renderer->m_fxaa_edge_threshold_min, 0.1f);	tooltip("Trims the algorithm from processing darks");
            ImGui::Separator();

            ImGui::Checkbox("Sharpen",                                  &do_sharperning);
            ImGui::InputFloat("Sharpen Strength",                       &m_renderer->m_sharpen_strength, 0.1f);
            ImGui::InputFloat("Sharpen Clamp",                          &m_renderer->m_sharpen_clamp, 0.1f);		    tooltip("Limits maximum amount of sharpening a pixel receives");
            ImGui::Separator();

            ImGui::InputInt("Shadow Resolution",                        &resolution_shadow, 1);
            ImGui::Separator();

            ImGui::Checkbox("Dithering",                                &do_dithering);									tooltip("Reduces color banding");
        }

        // Filter input
        m_renderer->m_exposure                  = Abs(m_renderer->m_exposure);
        m_renderer->m_bloom_intensity           = Abs(m_renderer->m_bloom_intensity);
        m_renderer->m_fxaa_sub_pixel            = Abs(m_renderer->m_fxaa_sub_pixel);
        m_renderer->m_fxaa_edge_threshold       = Abs(m_renderer->m_fxaa_edge_threshold);
        m_renderer->m_fxaa_edge_threshold_min   = Abs(m_renderer->m_fxaa_edge_threshold_min);
        m_renderer->m_sharpen_strength          = Abs(m_renderer->m_sharpen_strength);
        m_renderer->m_sharpen_clamp             = Abs(m_renderer->m_sharpen_clamp);
        m_renderer->m_motion_blur_strength      = Abs(m_renderer->m_motion_blur_strength);
        m_renderer->SetShadowResolution(static_cast<uint32_t>(resolution_shadow));
       
        #define set_flag_if(flag, value) value? m_renderer->EnableFlag(flag) : m_renderer->DisableFlag(flag)

        // Map back to engine
        set_flag_if(Render_PostProcess_Bloom,               do_bloom);
        set_flag_if(Render_PostProcess_FXAA,                do_fxaa);
        set_flag_if(Render_PostProcess_SSAO,                do_ssao);
        set_flag_if(Render_PostProcess_SSR,                 do_ssr);
        set_flag_if(Render_PostProcess_TAA,                 do_taa);
        set_flag_if(Render_PostProcess_MotionBlur,          do_motion_blur);
        set_flag_if(Render_PostProcess_Sharpening,          do_sharperning);
        set_flag_if(Render_PostProcess_ChromaticAberration, do_chromatic_aberration);
        set_flag_if(Render_PostProcess_Dithering,           do_dithering);
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_None))
    {
        // Buffer
        {
            if (ImGui::BeginCombo("Buffer", _RenderOptions::gbuffer_selected_texture.c_str()))
            {
                for (auto i = 0; i < _RenderOptions::gbuffer_textures.size(); i++)
                {
                    const auto is_selected = (_RenderOptions::gbuffer_selected_texture == _RenderOptions::gbuffer_textures[i]);
                    if (ImGui::Selectable(_RenderOptions::gbuffer_textures[i].c_str(), is_selected))
                    {
                        _RenderOptions::gbuffer_selected_texture = _RenderOptions::gbuffer_textures[i];
                        _RenderOptions::gbuffer_selected_texture_index = i;
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            m_renderer->SetDebugBuffer(static_cast<RendererDebug_Buffer>(_RenderOptions::gbuffer_selected_texture_index));
        }
        ImGui::Separator();

        // FPS
        {
            auto& timer = m_context->GetSubsystem<Timer>();
            auto fps_target = timer->GetTargetFps();

            ImGui::InputDouble("Target FPS", &fps_target);
            timer->SetTargetFps(fps_target);
            const auto fps_policy = timer->GetFpsPolicy();
            ImGui::Text(fps_policy == Fps_FixedMonitor ? "Fixed (Monitor)" : fps_target == Fps_Unlocked ? "Unlocked" : "Fixed");
        }
        ImGui::Separator();

        // Transform
        ImGui::Checkbox("Transform", &_RenderOptions::g_gizmo_transform);
        ImGui::InputFloat("Size", &m_renderer->m_gizmo_transform_size, 0.0025f);
        ImGui::InputFloat("Speed", &m_renderer->m_gizmo_transform_speed, 1.0f);
        // Physics
        ImGui::Checkbox("Physics", &_RenderOptions::g_gizmo_physics);
        // AABB
        ImGui::Checkbox("AABB", &_RenderOptions::g_gizmo_aabb);
        // Lights
        ImGui::Checkbox("Lights", &_RenderOptions::g_gizmo_light);
        // Picking Ray
        ImGui::Checkbox("Picking Ray", &_RenderOptions::g_gizmo_picking_ray);
        // Grid
        ImGui::Checkbox("Grid", &_RenderOptions::g_gizmo_grid);
        // Performance metrics
        ImGui::Checkbox("Performance Metrics", &_RenderOptions::g_gizmo_performance_metrics);

        set_flag_if(Render_Gizmo_Transform,             _RenderOptions::g_gizmo_transform);
        set_flag_if(Render_Gizmo_Physics,               _RenderOptions::g_gizmo_physics); 
        set_flag_if(Render_Gizmo_AABB,                  _RenderOptions::g_gizmo_aabb);
        set_flag_if(Render_Gizmo_Lights,                _RenderOptions::g_gizmo_light);
        set_flag_if(Render_Gizmo_PickingRay,            _RenderOptions::g_gizmo_picking_ray);
        set_flag_if(Render_Gizmo_Grid,                  _RenderOptions::g_gizmo_grid);
        set_flag_if(Render_Gizmo_PerformanceMetrics,    _RenderOptions::g_gizmo_performance_metrics);
    }
}
