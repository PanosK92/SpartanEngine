/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "RHI/RHI_Device.h"
#include "Profiling/Profiler.h"
//===============================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

Widget_RenderOptions::Widget_RenderOptions(Editor* editor) : Widget(editor)
{
    m_title         = "Renderer Options";
    m_flags         |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
    m_is_visible    = false;
    m_renderer      = m_context->GetSubsystem<Renderer>();
    m_alpha         = 1.0f;
}

void Widget_RenderOptions::TickVisible()
{
    ImGui::SliderFloat("Opacity", &m_alpha, 0.1f, 1.0f, "%.1f");

    if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Reflect
        bool do_bloom                   = m_renderer->GetOption(Render_Bloom);
        bool do_dof                     = m_renderer->GetOption(Render_DepthOfField);
        bool do_volumetric_fog          = m_renderer->GetOption(Render_VolumetricFog);
        bool do_ssao                    = m_renderer->GetOption(Render_Ssao);
        bool do_sss                     = m_renderer->GetOption(Render_ScreenSpaceShadows);
        bool do_ssr                     = m_renderer->GetOption(Render_ScreenSpaceReflections);
        bool do_taa                     = m_renderer->GetOption(Render_AntiAliasing_Taa);
        bool do_fxaa                    = m_renderer->GetOption(Render_AntiAliasing_Fxaa);
        bool do_motion_blur             = m_renderer->GetOption(Render_MotionBlur);
        bool do_film_grain              = m_renderer->GetOption(Render_FilmGrain);
        bool do_sharperning             = m_renderer->GetOption(Render_Sharpening_LumaSharpen);
        bool do_chromatic_aberration    = m_renderer->GetOption(Render_ChromaticAberration);
        bool do_dithering               = m_renderer->GetOption(Render_Dithering);
        bool do_ssgi                    = m_renderer->GetOption(Render_Ssgi);
        int resolution_shadow           = m_renderer->GetOptionValue<int>(Renderer_Option_Value::ShadowResolution);
        float fog_density               = m_renderer->GetOptionValue<float>(Renderer_Option_Value::Fog);

        // Show
        {
            const auto render_option_float = [this](const char* id, const char* text, Renderer_Option_Value render_option, const string& tooltip = "", float step = 0.1f, float min = 0.0f, float max = numeric_limits<float>::max())
            {
                float value = m_renderer->GetOptionValue<float>(render_option);

                ImGui::PushID(id);
                ImGui::PushItemWidth(120);
                ImGui::InputFloat(text, &value, step);
                ImGui::PopItemWidth();
                ImGui::PopID();
                value = Helper::Clamp(value, min, max);

                // Only update if changed
                if (m_renderer->GetOptionValue<float>(render_option) != value)
                {
                    m_renderer->SetOptionValue(render_option, value);
                }

                if (!tooltip.empty())
                {
                    ImGuiEx::Tooltip(tooltip.c_str());
                }
            };

            // Resolution
            {
                const auto display_mode_to_str = [](const DisplayMode& display_mode)
                {
                    return to_string(display_mode.width) + "x" + to_string(display_mode.height);
                };

                static vector<DisplayMode> display_modes;
                static uint32_t display_mode_index      = 0;
                const DisplayMode& display_mode_active  = Display::GetActiveDisplayMode();
                string display_mode_str                 = display_mode_to_str(display_mode_active);
                double display_mode_rhz                 = display_mode_active.hz;

                // Get display modes
                if (display_modes.empty())
                {
                    for (const DisplayMode& display_mode : Display::GetDisplayModes())
                    {
                        if (display_mode.hz == display_mode_rhz)
                        {
                            display_modes.emplace_back(display_mode);
                        }
                    }
                }
                
                if (ImGui::BeginCombo("Resolution", display_mode_str.c_str()))
                {
                    for (uint32_t i = 0; i < static_cast<uint32_t>(display_modes.size()); i++)
                    {
                        const bool entry_selected   = display_mode_index == i;
                        const string entry_str      = display_mode_to_str(display_modes[i]);

                        if (ImGui::Selectable(entry_str.c_str(), entry_selected))
                        {
                            display_mode_index  = i;
                            display_mode_str    = entry_str;

                            m_renderer->SetResolution(display_modes[i].width, display_modes[i].height);
                        }

                        if (entry_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::Separator();
            }

            // Tonemapping
            {
                // Reflect from engine
                static array<string, 4> tonemapping_options = { "Off", "ACES", "Reinhard", "Uncharted 2" };
                static string tonemapping_selection         = tonemapping_options[m_renderer->GetOptionValue<uint32_t>(Renderer_Option_Value::Tonemapping)];

                if (ImGui::BeginCombo("Tonemapping", tonemapping_selection.c_str()))
                {
                    for (uint32_t i = 0; i < static_cast<uint32_t>(tonemapping_options.size()); i++)
                    {
                        const auto is_selected = (tonemapping_selection == tonemapping_options[i]);
                        if (ImGui::Selectable(tonemapping_options[i].c_str(), is_selected))
                        {
                            tonemapping_selection = tonemapping_options[i].c_str();
                            m_renderer->SetOptionValue(Renderer_Option_Value::Tonemapping, static_cast<float>(i));
                        }
                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine(); render_option_float("##tonemapping_option_1", "Gamma", Renderer_Option_Value::Gamma);
                ImGui::Separator();
            }

            // Bloom
            ImGui::Checkbox("Bloom", &do_bloom); ImGui::SameLine();
            render_option_float("##bloom_option_1", "Intensity", Renderer_Option_Value::Intensity, "", 0.001f);
            ImGui::Separator();

            // Depth of Field
            ImGui::Checkbox("Depth of Field", &do_dof);
            ImGuiEx::Tooltip("Controlled by the camera's aperture.");
            ImGui::Separator();

            // Motion blur
            ImGui::Checkbox("Motion Blur", &do_motion_blur);
            ImGuiEx::Tooltip("Controlled by the camera's shutter speed.");
            ImGui::Separator();

            // Volumetric fog
            ImGui::Checkbox("Volumetric Fog", &do_volumetric_fog);
            ImGuiEx::Tooltip("Requires a light with shadows enabled.");
            ImGui::SameLine();
            ImGuiEx::DragFloatWrap("Density", &fog_density, 0.01f, 0.0f, 16.0f, "%.2f");
            ImGui::Separator();

            // Screen space shadows
            ImGui::Checkbox("SSS - Screen Space Shadows", &do_sss);
            ImGui::Separator();

            // Screen space ambient occlusion
            ImGui::Checkbox("SSAO - Screen Space Ambient Occlusion", &do_ssao);
            ImGui::Separator();

            // Screen Space Global Illumination
            ImGui::Checkbox("SSGI - Screen Space Global Illumination", &do_ssgi);
            ImGuiEx::Tooltip("Computes one bounce of indirect diffuse light.");
            ImGui::Separator();

            // Screen space reflections
            ImGui::Checkbox("SSR - Screen Space Reflections", &do_ssr);
            ImGui::Separator();

            // Chromatic aberration
            ImGui::Checkbox("Chromatic Aberration", &do_chromatic_aberration);
            ImGuiEx::Tooltip("Emulates the inability of old cameras to focus all colors in the same focal point.");
            ImGui::Separator();

            // Temporal anti-aliasing
            ImGui::Checkbox("TAA - Temporal Anti-Aliasing", &do_taa);
            ImGuiEx::Tooltip("Used to improve many stochastic effects, you want this to always be enabled.");
            ImGui::Separator();

            // FXAA
            ImGui::Checkbox("FXAA - Fast Approximate Anti-Aliasing", &do_fxaa);
            ImGui::Separator();

            // Film grain
            ImGui::Checkbox("Film Grain", &do_film_grain);
            ImGui::Separator();

            // Sharpen
            ImGui::Checkbox("Sharpening (AMD FidelityFX CAS)", &do_sharperning);
            ImGuiEx::Tooltip("Contrast adaptive sharpening. Areas of the image that are already sharp are sharpened less, while areas that lack detail are sharpened more.");
            ImGui::SameLine(); render_option_float("##sharpen_option", "Strength", Renderer_Option_Value::Sharpen_Strength, "", 0.1f, 0.0f, 1.0f);
            ImGui::Separator();

            // Dithering
            ImGui::Checkbox("Dithering", &do_dithering);
            ImGuiEx::Tooltip("Reduces color banding");
            ImGui::Separator();

            // Shadow resolution
            ImGui::InputInt("Shadow Resolution", &resolution_shadow, 1);
        }

        // Map
        m_renderer->SetOption(Render_Bloom,                                     do_bloom);
        m_renderer->SetOption(Render_DepthOfField,                              do_dof);
        m_renderer->SetOption(Render_VolumetricFog,                             do_volumetric_fog);
        m_renderer->SetOption(Render_Ssao,                                      do_ssao);
        m_renderer->SetOption(Render_ScreenSpaceShadows,                        do_sss);
        m_renderer->SetOption(Render_ScreenSpaceReflections,                    do_ssr);
        m_renderer->SetOption(Render_Ssgi,                                      do_ssgi);
        m_renderer->SetOption(Render_AntiAliasing_Taa,                          do_taa);
        m_renderer->SetOption(Render_AntiAliasing_Fxaa,                         do_fxaa);
        m_renderer->SetOption(Render_MotionBlur,                                do_motion_blur);
        m_renderer->SetOption(Render_FilmGrain,                                 do_film_grain);
        m_renderer->SetOption(Render_Sharpening_LumaSharpen,                    do_sharperning);
        m_renderer->SetOption(Render_ChromaticAberration,                       do_chromatic_aberration);
        m_renderer->SetOption(Render_Dithering,                                 do_dithering);
        m_renderer->SetOptionValue(Renderer_Option_Value::ShadowResolution,     static_cast<float>(resolution_shadow));
        m_renderer->SetOptionValue(Renderer_Option_Value::Fog,                  fog_density);
    }

    if (ImGui::CollapsingHeader("Widgets", ImGuiTreeNodeFlags_None))
    {
        // FPS
        {
            auto timer = m_context->GetSubsystem<Timer>();
            auto fps_target = timer->GetTargetFps();

            ImGui::InputDouble("Target FPS", &fps_target);
            timer->SetTargetFps(fps_target);
            const auto fps_policy = timer->GetFpsPolicy();
            ImGui::SameLine(); ImGui::Text(fps_policy == Fps_FixedMonitor ? "Fixed (Monitor)" : fps_target == Fps_Unlocked ? "Unlocked" : "Fixed");
        }
        ImGui::Separator();

        {
            bool debug_physics               = m_renderer->GetOption(Render_Debug_Physics);
            bool debug_aabb                  = m_renderer->GetOption(Render_Debug_Aabb);
            bool debug_light                 = m_renderer->GetOption(Render_Debug_Lights);
            bool debug_transform             = m_renderer->GetOption(Render_Debug_Transform);
            bool debug_selection_outline     = m_renderer->GetOption(Render_Debug_SelectionOutline);
            bool debug_picking_ray           = m_renderer->GetOption(Render_Debug_PickingRay);
            bool debug_grid                  = m_renderer->GetOption(Render_Debug_Grid);
            bool debug_performance_metrics   = m_renderer->GetOption(Render_Debug_PerformanceMetrics);
            bool debug_wireframe             = m_renderer->GetOption(Render_Debug_Wireframe);

            ImGui::Checkbox("Transform", &debug_transform);
            {
                ImGui::SameLine(); ImGui::InputFloat("Size", &m_renderer->m_gizmo_transform_size, 0.0025f);
                ImGui::SameLine(); ImGui::InputFloat("Speed", &m_renderer->m_gizmo_transform_speed, 1.0f);
            }
            ImGui::Checkbox("Selection Outline",    &debug_selection_outline);
            ImGui::Checkbox("Physics",              &debug_physics);
            ImGui::Checkbox("AABB",                 &debug_aabb);
            ImGui::Checkbox("Lights",               &debug_light);
            ImGui::Checkbox("Picking Ray",          &debug_picking_ray);
            ImGui::Checkbox("Grid",                 &debug_grid);
            ImGui::Checkbox("Performance Metrics",  &debug_performance_metrics);
            ImGui::Checkbox("Wireframe",            &debug_wireframe);

            // Reset metrics on activation
            if (debug_performance_metrics && !m_renderer->GetOption(Render_Debug_PerformanceMetrics))
            {
                m_profiler->ResetMetrics();
            }

            m_renderer->SetOption(Render_Debug_Transform,           debug_transform);
            m_renderer->SetOption(Render_Debug_SelectionOutline,    debug_selection_outline);
            m_renderer->SetOption(Render_Debug_Physics,             debug_physics);
            m_renderer->SetOption(Render_Debug_Aabb,                debug_aabb);
            m_renderer->SetOption(Render_Debug_Lights,              debug_light);
            m_renderer->SetOption(Render_Debug_PickingRay,          debug_picking_ray);
            m_renderer->SetOption(Render_Debug_Grid,                debug_grid);
            m_renderer->SetOption(Render_Debug_PerformanceMetrics,  debug_performance_metrics);
            m_renderer->SetOption(Render_Debug_Wireframe,           debug_wireframe);
        }
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_None))
    {
        // Reflect from engine
        auto do_depth_prepass   = m_renderer->GetOption(Render_DepthPrepass);
        auto do_reverse_z       = m_renderer->GetOption(Render_ReverseZ);

        {
            // Buffer
            {
                static array<string, 24> render_target_debug =
                {
                    "None",
                    "Gbuffer_Albedo",
                    "Gbuffer_Normal",
                    "Gbuffer_Material",
                    "Gbuffer_Velocity",
                    "Gbuffer_Depth",
                    "Brdf_Prefiltered_Environment",
                    "Brdf_Specular_Lut",
                    "Light_Diffuse",
                    "Light_Diffuse_Transparent",
                    "Light_Specular",
                    "Light_Specular_Transparent",
                    "Light_Volumetric",
                    "Frame_Hdr",
                    "Frame_Hdr_2",
                    "Frame_Ldr",
                    "Frame_Ldr_2",
                    "Dof_Half",
                    "Dof_Half_2",
                    "Bloom",
                    "Hbao_Noisy",
                    "Hbao",
                    "Ssgi",
                    "Ssr"
                };
                static int selection_int = 0;
                static string selection_str = render_target_debug[0];

                if (ImGui::BeginCombo("Render Target", selection_str.c_str()))
                {
                    for (auto i = 0; i < render_target_debug.size(); i++)
                    {
                        const auto is_selected = (selection_str == render_target_debug[i]);
                        if (ImGui::Selectable(render_target_debug[i].c_str(), is_selected))
                        {
                            selection_str = render_target_debug[i];
                            selection_int = i;
                        }
                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                m_renderer->SetRenderTargetDebug(static_cast<RendererRt>(selection_int));
            }
            ImGui::Separator();

            // Depth-PrePass
            ImGui::Checkbox("Depth-PrePass", &do_depth_prepass);

            // Reverse-Z
            ImGui::Checkbox("Reverse-Z", &do_reverse_z);
        }

        // Map back to engine
        m_renderer->SetOption(Render_DepthPrepass, do_depth_prepass);
        m_renderer->SetOption(Render_ReverseZ, do_reverse_z);
    }
}
