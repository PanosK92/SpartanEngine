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
    static bool g_debug_physics             = true;
    static bool g_debug_aabb                = false;
    static bool g_debug_light               = true;
    static bool g_debug_transform           = true;
    static bool g_debug_picking_ray         = false;
    static bool g_debug_grid                = true;
    static bool g_debug_performance_metrics = false;
    static bool g_debug_wireframe           = false;
    static vector<string> debug_textures =
    {
        "None",
        "Albedo",
        "Normal",
        "Material",
        "Diffuse",
        "Specular",
        "Velocity",
        "Depth",
        "SSAO",
        "SSR",
        "Bloom",
        "Volumetric Lighting",
        "Shadows"
    };
    static int debug_texture_selected_index = 0;
    static string debug_texture_selected = debug_textures[0];
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

        auto do_bloom                   = m_renderer->IsFlagSet(Render_PostProcess_Bloom);
        auto do_volumetric_lighting     = m_renderer->IsFlagSet(Render_PostProcess_VolumetricLighting);
        auto do_fxaa                    = m_renderer->IsFlagSet(Render_PostProcess_FXAA);
        auto do_ssao                    = m_renderer->IsFlagSet(Render_PostProcess_SSAO);
        auto do_sscs                    = m_renderer->IsFlagSet(Render_PostProcess_SSCS);
        auto do_ssr                     = m_renderer->IsFlagSet(Render_PostProcess_SSR);
        auto do_taa                     = m_renderer->IsFlagSet(Render_PostProcess_TAA);
        auto do_motion_blur             = m_renderer->IsFlagSet(Render_PostProcess_MotionBlur);
        auto do_sharperning             = m_renderer->IsFlagSet(Render_PostProcess_Sharpening);
        auto do_chromatic_aberration    = m_renderer->IsFlagSet(Render_PostProcess_ChromaticAberration);
        auto do_dithering               = m_renderer->IsFlagSet(Render_PostProcess_Dithering);
        auto resolution_shadow          = static_cast<int>(m_renderer->GetShadowResolution());

        // Display
        {
            const auto tooltip = [](const char* text) { if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::Text(text); ImGui::EndTooltip(); } };
            const auto input_float = [](const char* id, const char* text, float* value, float step) { ImGui::PushID(id); ImGui::PushItemWidth(120); ImGui::InputFloat(text, value, step); ImGui::PopItemWidth(); ImGui::PopID(); };

            // Tonemapping
            if (ImGui::BeginCombo("Tonemapping", type_char_ptr))
            {
                for (unsigned int i = 0; i < static_cast<unsigned int>(types.size()); i++)
                {
                    const auto is_selected = (type_char_ptr == types[i]);
                    if (ImGui::Selectable(types[i], is_selected))
                    {
                        type_char_ptr = types[i];
                        m_renderer->m_tonemapping = static_cast<Renderer_ToneMapping_Type>(i);
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); input_float("##tonemapping_option_1", "Exposure", &m_renderer->m_exposure, 0.1f);
            ImGui::SameLine(); input_float("##tonemapping_option_2", "Gamma", &m_renderer->m_gamma, 0.1f);
            ImGui::Separator();

            // Bloom
            ImGui::Checkbox("Bloom", &do_bloom); ImGui::SameLine();
            input_float("##bloom_option_1", "Intensity", &m_renderer->m_bloom_intensity, 0.001f);
            ImGui::Separator();

            // Volumetric lighting
            ImGui::Checkbox("Volumetric lighting", &do_volumetric_lighting); tooltip("Requires a light with shadows");
            ImGui::Separator();

            // Screen space contact shadows
            ImGui::Checkbox("SSCS - Screen Space Contact Shadows", &do_sscs); tooltip("Requires a light with shadows");
            ImGui::Separator();

            // Screen space ambient occlusion
            ImGui::Checkbox("SSAO - Screen Space Ambient Occlusion", &do_ssao);
            ImGui::Separator();

            // Screen space reflections
            ImGui::Checkbox("SSR - Screen Space Reflections", &do_ssr);
            ImGui::Separator();

            // Motion blur
            ImGui::Checkbox("Motion Blur", &do_motion_blur); ImGui::SameLine();
            input_float("##motion_blur_option_1", "Intensity", &m_renderer->m_motion_blur_intensity, 0.1f);
            ImGui::Separator();

            // Chromatic aberration
            ImGui::Checkbox("Chromatic Aberration", &do_chromatic_aberration); tooltip("Emulates the inability of old cameras to focus all colors in the same focal point");
            ImGui::Separator();

            // Temporal anti-aliasing
            ImGui::Checkbox("TAA - Temporal Anti-Aliasing", &do_taa);
            ImGui::Separator();

            // FXAA
            ImGui::Checkbox("FXAA - Fast Approximate Anti-Aliasing",   &do_fxaa);
            ImGui::SameLine(); input_float("##fxaa_option_1", "Sub-Pixel",          &m_renderer->m_fxaa_sub_pixel, 0.1f);			tooltip("The amount of sub-pixel aliasing removal");
            ImGui::SameLine(); input_float("##fxaa_option_2", "Edge Threshold",     &m_renderer->m_fxaa_edge_threshold, 0.1f);		tooltip("The minimum amount of local contrast required to apply algorithm");
            ImGui::SameLine(); input_float("##fxaa_option_3", "Edge Threshold Min", &m_renderer->m_fxaa_edge_threshold_min, 0.1f);	tooltip("Trims the algorithm from processing darks");
            ImGui::Separator();

            // Sharpen
            ImGui::Checkbox("Sharpen", &do_sharperning);
            ImGui::SameLine(); input_float("##sharpen_option_1", "Strength", &m_renderer->m_sharpen_strength, 0.1f);
            ImGui::SameLine(); input_float("##sharpen_option_2", "Clamp", &m_renderer->m_sharpen_clamp, 0.1f); tooltip("Limits maximum amount of sharpening a pixel receives");
            ImGui::Separator();

            // Dithering
            ImGui::Checkbox("Dithering", &do_dithering); tooltip("Reduces color banding");
            ImGui::Separator();

            // Shadow resolution
            ImGui::InputInt("Shadow Resolution", &resolution_shadow, 1);
        }

        // Filter input
        m_renderer->m_exposure                  = Abs(m_renderer->m_exposure);
        m_renderer->m_bloom_intensity           = Abs(m_renderer->m_bloom_intensity);
        m_renderer->m_fxaa_sub_pixel            = Abs(m_renderer->m_fxaa_sub_pixel);
        m_renderer->m_fxaa_edge_threshold       = Abs(m_renderer->m_fxaa_edge_threshold);
        m_renderer->m_fxaa_edge_threshold_min   = Abs(m_renderer->m_fxaa_edge_threshold_min);
        m_renderer->m_sharpen_strength          = Abs(m_renderer->m_sharpen_strength);
        m_renderer->m_sharpen_clamp             = Abs(m_renderer->m_sharpen_clamp);
        m_renderer->m_motion_blur_intensity     = Abs(m_renderer->m_motion_blur_intensity);
        m_renderer->SetShadowResolution(static_cast<uint32_t>(resolution_shadow));
       
        #define set_flag_if(flag, value) value? m_renderer->SetFlag(flag) : m_renderer->UnsetFlag(flag)

        // Map back to engine
        set_flag_if(Render_PostProcess_Bloom,               do_bloom);
        set_flag_if(Render_PostProcess_VolumetricLighting,  do_volumetric_lighting);
        set_flag_if(Render_PostProcess_FXAA,                do_fxaa);
        set_flag_if(Render_PostProcess_SSAO,                do_ssao);
        set_flag_if(Render_PostProcess_SSCS,                do_sscs);
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
            if (ImGui::BeginCombo("Buffer", _RenderOptions::debug_texture_selected.c_str()))
            {
                for (auto i = 0; i < _RenderOptions::debug_textures.size(); i++)
                {
                    const auto is_selected = (_RenderOptions::debug_texture_selected == _RenderOptions::debug_textures[i]);
                    if (ImGui::Selectable(_RenderOptions::debug_textures[i].c_str(), is_selected))
                    {
                        _RenderOptions::debug_texture_selected = _RenderOptions::debug_textures[i];
                        _RenderOptions::debug_texture_selected_index = i;
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            m_renderer->SetDebugBuffer(static_cast<Renderer_Buffer_Type>(_RenderOptions::debug_texture_selected_index));
        }
        ImGui::Separator();

        // FPS
        {
            auto& timer = m_context->GetSubsystem<Timer>();
            auto fps_target = timer->GetTargetFps();

            ImGui::InputDouble("Target FPS", &fps_target);
            timer->SetTargetFps(fps_target);
            const auto fps_policy = timer->GetFpsPolicy();
            ImGui::SameLine(); ImGui::Text(fps_policy == Fps_FixedMonitor ? "Fixed (Monitor)" : fps_target == Fps_Unlocked ? "Unlocked" : "Fixed");
        }
        ImGui::Separator();

        ImGui::Checkbox("Transform", &_RenderOptions::g_debug_transform);
        ImGui::SameLine(); ImGui::InputFloat("Size",    &m_renderer->m_gizmo_transform_size, 0.0025f);
        ImGui::SameLine(); ImGui::InputFloat("Speed",   &m_renderer->m_gizmo_transform_speed, 1.0f);
        ImGui::Checkbox("Physics",              &_RenderOptions::g_debug_physics);
        ImGui::Checkbox("AABB",                 &_RenderOptions::g_debug_aabb);
        ImGui::Checkbox("Lights",               &_RenderOptions::g_debug_light);
        ImGui::Checkbox("Picking Ray",          &_RenderOptions::g_debug_picking_ray);
        ImGui::Checkbox("Grid",                 &_RenderOptions::g_debug_grid);
        ImGui::Checkbox("Performance Metrics",  &_RenderOptions::g_debug_performance_metrics);
        ImGui::Checkbox("Wireframe",            &_RenderOptions::g_debug_wireframe);

        set_flag_if(Render_Debug_Transform,             _RenderOptions::g_debug_transform);
        set_flag_if(Render_Debug_Physics,               _RenderOptions::g_debug_physics); 
        set_flag_if(Render_Debug_AABB,                  _RenderOptions::g_debug_aabb);
        set_flag_if(Render_Debug_Lights,                _RenderOptions::g_debug_light);
        set_flag_if(Render_Debug_PickingRay,            _RenderOptions::g_debug_picking_ray);
        set_flag_if(Render_Debug_Grid,                  _RenderOptions::g_debug_grid);
        set_flag_if(Render_Debug_PerformanceMetrics,    _RenderOptions::g_debug_performance_metrics);
        set_flag_if(Render_Debug_Wireframe,             _RenderOptions::g_debug_wireframe);
    }
}
