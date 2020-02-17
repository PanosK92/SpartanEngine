/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Core/Timer.h"
#include "Math/MathHelper.h"
#include "Rendering/Model.h"
#include "../ImGui_Extension.h"
//===============================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

Widget_RenderOptions::Widget_RenderOptions(Context* context) : Widget(context)
{
    m_title         = "Renderer Options";
    m_flags         |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
    m_is_visible    = false;
    m_renderer      = context->GetSubsystem<Renderer>().get();
    m_alpha         = 1.0f;
}

void Widget_RenderOptions::Tick()
{
    ImGui::SliderFloat("Opacity", &m_alpha, 0.1f, 1.0f, "%.1f");

    if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Reflect from engine
        static vector<char*> tonemapping_options    = { "Off", "ACES", "Reinhard", "Uncharted 2" };
        const char* tonemapping_selection           = tonemapping_options[m_renderer->GetOptionValue<uint32_t>(Option_Value_Tonemapping)];

        auto do_bloom                   = m_renderer->GetOptionValue(Render_Bloom);
        auto do_volumetric_lighting     = m_renderer->GetOptionValue(Render_VolumetricLighting);
        auto do_fxaa                    = m_renderer->GetOptionValue(Render_AntiAliasing_FXAA);
        auto do_ssao                    = m_renderer->GetOptionValue(Render_ScreenSpaceAmbientOcclusion);
        auto do_sss                     = m_renderer->GetOptionValue(Render_ScreenSpaceShadows);
        auto do_ssr                     = m_renderer->GetOptionValue(Render_ScreenSpaceReflections);
        auto do_taa                     = m_renderer->GetOptionValue(Render_AntiAliasing_TAA);
        auto do_motion_blur             = m_renderer->GetOptionValue(Render_MotionBlur);
        auto do_sharperning             = m_renderer->GetOptionValue(Render_Sharpening_LumaSharpen);
        auto do_chromatic_aberration    = m_renderer->GetOptionValue(Render_ChromaticAberration);
        auto do_dithering               = m_renderer->GetOptionValue(Render_Dithering);  
        auto resolution_shadow          = m_renderer->GetOptionValue<int>(Option_Value_ShadowResolution);

        // Display
        {
            const auto render_option_float = [this](const char* id, const char* text, Renderer_Option_Value render_option, char* tooltip = nullptr, float step = 0.1f)
            {
                float value = m_renderer->GetOptionValue<float>(render_option);

                ImGui::PushID(id);
                ImGui::PushItemWidth(120);
                ImGui::InputFloat(text, &value, step);
                ImGui::PopItemWidth();
                ImGui::PopID();
                value = Abs(value);

                // Only update if changed
                if (m_renderer->GetOptionValue<float>(render_option) != value)
                {
                    m_renderer->SetOptionValue(render_option, value);
                }

                ImGuiEx::Tooltip(tooltip);
            };

            // Tonemapping
            if (ImGui::BeginCombo("Tonemapping", tonemapping_selection))
            {
                for (unsigned int i = 0; i < static_cast<unsigned int>(tonemapping_options.size()); i++)
                {
                    const auto is_selected = (tonemapping_selection == tonemapping_options[i]);
                    if (ImGui::Selectable(tonemapping_options[i], is_selected))
                    {
                        tonemapping_selection = tonemapping_options[i];
                        m_renderer->SetOptionValue(Option_Value_Tonemapping, static_cast<float>(i));
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); render_option_float("##tonemapping_option_1", "Exposure", Option_Value_Exposure);
            ImGui::SameLine(); render_option_float("##tonemapping_option_2", "Gamma", Option_Value_Gamma);
            ImGui::Separator();

            // Bloom
            ImGui::Checkbox("Bloom", &do_bloom); ImGui::SameLine();
            render_option_float("##bloom_option_1", "Intensity", Option_Value_Bloom_Intensity, nullptr, 0.001f);
            ImGui::Separator();

            // Volumetric lighting
            ImGui::Checkbox("Volumetric lighting", &do_volumetric_lighting);
            ImGuiEx::Tooltip("Requires a light with shadows enabled");
            ImGui::Separator();

            // Screen space shadows
            ImGui::Checkbox("SSS - Screen Space Shadows", &do_sss);
            ImGuiEx::Tooltip("Requires a light with shadows enabled");
            ImGui::Separator();

            // Screen space ambient occlusion
            ImGui::Checkbox("SSAO - Screen Space Ambient Occlusion", &do_ssao);
            ImGui::SameLine(); render_option_float("##ssao_option_1", "Scale", Option_Value_Ssao_Scale);
            ImGui::Separator();

            // Screen space reflections
            ImGui::Checkbox("SSR - Screen Space Reflections", &do_ssr);
            ImGui::Separator();

            // Motion blur
            ImGui::Checkbox("Motion Blur", &do_motion_blur); ImGui::SameLine();
            render_option_float("##motion_blur_option_1", "Intensity", Option_Value_Motion_Blur_Intensity);
            ImGui::Separator();

            // Chromatic aberration
            ImGui::Checkbox("Chromatic Aberration", &do_chromatic_aberration);
            ImGuiEx::Tooltip("Emulates the inability of old cameras to focus all colors in the same focal point");
            ImGui::Separator();

            // Temporal anti-aliasing
            ImGui::Checkbox("TAA - Temporal Anti-Aliasing", &do_taa);
            ImGui::Separator();

            // FXAA
            ImGui::Checkbox("FXAA - Fast Approximate Anti-Aliasing", &do_fxaa);
            ImGui::Separator();

            // Sharpen
            ImGui::Checkbox("Sharpen", &do_sharperning);
            ImGui::SameLine(); render_option_float("##sharpen_option_1", "Strength",    Option_Value_Sharpen_Strength);
            ImGui::SameLine(); render_option_float("##sharpen_option_2", "Clamp",       Option_Value_Sharpen_Clamp, "Limits maximum amount of sharpening a pixel receives");
            ImGui::Separator();

            // Dithering
            ImGui::Checkbox("Dithering", &do_dithering);
            ImGuiEx::Tooltip("Reduces color banding");
            ImGui::Separator();

            // Shadow resolution
            ImGui::InputInt("Shadow Resolution", &resolution_shadow, 1);
        }

        // Map back to engine
        m_renderer->SetShadowResolution(static_cast<uint32_t>(resolution_shadow));
        m_renderer->SetOptionValue(Render_Bloom,                        do_bloom);
        m_renderer->SetOptionValue(Render_VolumetricLighting,           do_volumetric_lighting);
        m_renderer->SetOptionValue(Render_AntiAliasing_FXAA,            do_fxaa);
        m_renderer->SetOptionValue(Render_ScreenSpaceAmbientOcclusion,  do_ssao);
        m_renderer->SetOptionValue(Render_ScreenSpaceShadows,           do_sss);
        m_renderer->SetOptionValue(Render_ScreenSpaceReflections,       do_ssr);
        m_renderer->SetOptionValue(Render_AntiAliasing_TAA,             do_taa);
        m_renderer->SetOptionValue(Render_MotionBlur,                   do_motion_blur);
        m_renderer->SetOptionValue(Render_Sharpening_LumaSharpen,       do_sharperning);
        m_renderer->SetOptionValue(Render_ChromaticAberration,          do_chromatic_aberration);
        m_renderer->SetOptionValue(Render_Dithering,                    do_dithering);
    }

    if (ImGui::CollapsingHeader("Widgets", ImGuiTreeNodeFlags_None))
    {
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

        {
            bool debug_physics               = m_renderer->GetOptionValue(Render_Debug_Physics);
            bool debug_aabb                  = m_renderer->GetOptionValue(Render_Debug_AABB);
            bool debug_light                 = m_renderer->GetOptionValue(Render_Debug_Lights);
            bool debug_transform             = m_renderer->GetOptionValue(Render_Debug_Transform);
            bool debug_picking_ray           = m_renderer->GetOptionValue(Render_Debug_PickingRay);
            bool debug_grid                  = m_renderer->GetOptionValue(Render_Debug_Grid);
            bool debug_performance_metrics   = m_renderer->GetOptionValue(Render_Debug_PerformanceMetrics);
            bool debug_wireframe             = m_renderer->GetOptionValue(Render_Debug_Wireframe);

            ImGui::Checkbox("Transform", &debug_transform);
            {
                ImGui::SameLine(); ImGui::InputFloat("Size", &m_renderer->m_gizmo_transform_size, 0.0025f);
                ImGui::SameLine(); ImGui::InputFloat("Speed", &m_renderer->m_gizmo_transform_speed, 1.0f);
            }
            ImGui::Checkbox("Physics",              &debug_physics);
            ImGui::Checkbox("AABB",                 &debug_aabb);
            ImGui::Checkbox("Lights",               &debug_light);
            ImGui::Checkbox("Picking Ray",          &debug_picking_ray);
            ImGui::Checkbox("Grid",                 &debug_grid);
            ImGui::Checkbox("Performance Metrics",  &debug_performance_metrics);
            ImGui::Checkbox("Wireframe",            &debug_wireframe);

            m_renderer->SetOptionValue(Render_Debug_Transform,             debug_transform);
            m_renderer->SetOptionValue(Render_Debug_Physics,               debug_physics);
            m_renderer->SetOptionValue(Render_Debug_AABB,                  debug_aabb);
            m_renderer->SetOptionValue(Render_Debug_Lights,                debug_light);
            m_renderer->SetOptionValue(Render_Debug_PickingRay,            debug_picking_ray);
            m_renderer->SetOptionValue(Render_Debug_Grid,                  debug_grid);
            m_renderer->SetOptionValue(Render_Debug_PerformanceMetrics,    debug_performance_metrics);
            m_renderer->SetOptionValue(Render_Debug_Wireframe,             debug_wireframe);
        }
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_None))
    {
        // Reflect from engine
        auto do_depth_prepass   = m_renderer->GetOptionValue(Render_DepthPrepass);
        auto do_reverse_z       = m_renderer->GetOptionValue(Render_ReverseZ);

        {
            // Buffer
            {
                static vector<string> buffer_options =
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
                    "Volumetric Lighting"
                };
                static int buffer_selection = 0;
                static string buffer_selection_str = buffer_options[0];

                if (ImGui::BeginCombo("Buffer", buffer_selection_str.c_str()))
                {
                    for (auto i = 0; i < buffer_options.size(); i++)
                    {
                        const auto is_selected = (buffer_selection_str == buffer_options[i]);
                        if (ImGui::Selectable(buffer_options[i].c_str(), is_selected))
                        {
                            buffer_selection_str = buffer_options[i];
                            buffer_selection = i;
                        }
                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                m_renderer->SetDebugBuffer(static_cast<Renderer_Buffer_Type>(buffer_selection));
            }
            ImGui::Separator();

            // Depth-PrePass
            ImGui::Checkbox("Depth-PrePass", &do_depth_prepass);

            // Reverse-Z
            ImGui::Checkbox("Reverse-Z", &do_reverse_z);
        }

        // Map back to engine
        m_renderer->SetOptionValue(Render_DepthPrepass, do_depth_prepass);
        m_renderer->SetOptionValue(Render_ReverseZ, do_reverse_z);
    }
}
