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
    m_position      = k_widget_position_screen_center;
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
        bool allow_taa_upsampling       = m_renderer->GetOptionValue<bool>(Renderer_Option_Value::Taa_AllowUpsampling);

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
                // Get display modes
                static vector<DisplayMode> display_modes;
                static vector<string> display_modes_string;
                const DisplayMode& display_mode_active = Display::GetActiveDisplayMode();
                if (display_modes.empty())
                {
                    for (const DisplayMode& display_mode : Display::GetDisplayModes())
                    {
                        if (display_mode.hz == display_mode_active.hz)
                        {
                            display_modes.emplace_back(display_mode);
                            display_modes_string.emplace_back(to_string(display_mode.width) + "x" + to_string(display_mode.height));
                        }
                    }
                }

                auto get_display_mode_index = [](const Vector2& resolution)
                {
                    uint32_t index = 0;

                    for (uint32_t i = 0; i < static_cast<uint32_t>(display_modes.size()); i++)
                    {
                        const DisplayMode& display_mode = display_modes[i];

                        if (display_mode.width == resolution.x && display_mode.height == resolution.y)
                        {
                            index = i;
                            break;
                        }
                    }

                    return index;
                };

                // Render resolution
                Vector2 resolution_render = m_renderer->GetResolutionRender();
                uint32_t render_index = get_display_mode_index(resolution_render);
                if (ImGuiEx::ComboBox("Render resolution", display_modes_string, &render_index))
                {
                    m_renderer->SetResolutionRender(display_modes[render_index].width, display_modes[render_index].height);
                }

                // Output resolution
                Vector2 resolution_output = m_renderer->GetResolutionOutput();
                uint32_t output_index = get_display_mode_index(resolution_output);
                if (ImGuiEx::ComboBox("Output resolution", display_modes_string, &output_index))
                {
                    m_renderer->SetResolutionOutput(display_modes[output_index].width, display_modes[output_index].height);
                }

                ImGui::Separator();
            }

            // Tonemapping & Gamma
            {
                static vector<string> tonemapping_options   = { "Off", "ACES", "Reinhard", "Uncharted 2" };
                uint32_t selection_index                    = m_renderer->GetOptionValue<uint32_t>(Renderer_Option_Value::Tonemapping);
                if (ImGuiEx::ComboBox("Tonemapping", tonemapping_options, &selection_index))
                {
                    m_renderer->SetOptionValue(Renderer_Option_Value::Tonemapping, static_cast<float>(selection_index));
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
            ImGui::SameLine();
            ImGui::Checkbox("Allow upsampling (not ready)", &allow_taa_upsampling);
            ImGuiEx::Tooltip("If the output resolution is bigger than the render resolution, TAA will be used to reconstruct the image.");
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
            ImGui::Separator();

            // FPS Limit
            Timer* timer = m_context->GetSubsystem<Timer>();
            double fps_target = timer->GetTargetFps();
            ImGui::InputDouble("FPS Limit", &fps_target);
            timer->SetTargetFps(fps_target);
            const FpsLimitType fps_limit_type = timer->GetFpsLimitType();
            ImGui::SameLine(); ImGui::Text(fps_limit_type == FpsLimitType::FixedToMonitor ? "Fixed to monitor" : (fps_limit_type == FpsLimitType::Unlocked ? "Unlocked" : "Fixed"));
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
        m_renderer->SetOptionValue(Renderer_Option_Value::Taa_AllowUpsampling,     static_cast<float>(allow_taa_upsampling));
    }

    if (ImGui::CollapsingHeader("Widgets", ImGuiTreeNodeFlags_None))
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

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_None))
    {
        // Reflect from engine
        bool do_depth_prepass   = m_renderer->GetOption(Render_DepthPrepass);
        bool do_reverse_z       = m_renderer->GetOption(Render_ReverseZ);

        {
            // Render target
            {
                // Get render targets
                static vector<string> render_target_options;
                if (render_target_options.empty())
                {
                    render_target_options.emplace_back("None");
                    for (const shared_ptr<RHI_Texture>& render_target : m_renderer->GetRenderTargets())
                    {
                        if (render_target)
                        {
                            render_target_options.emplace_back(render_target->GetName());
                        }
                    }
                }

                // Combo box
                uint32_t selection_index = static_cast<uint32_t>(m_renderer->GetRenderTargetDebug());
                if (ImGuiEx::ComboBox("Render Target", render_target_options, &selection_index))
                {
                    m_renderer->SetRenderTargetDebug(static_cast<RendererRt>(selection_index));
                }
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
