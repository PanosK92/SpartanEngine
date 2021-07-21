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

//= INCLUDES ==============================
#include "Widget_RenderOptions.h"
#include "Rendering/Renderer.h"
#include "Core/Context.h"
#include "Core/Timer.h"
#include "Math/MathHelper.h"
#include "Rendering/Model.h"
#include "../ImGui_Extension.h"
#include "../ImGui/Source/imgui_internal.h"
#include "RHI/RHI_Device.h"
#include "Profiling/Profiler.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

namespace WidgetHelper
{
    static Renderer* renderer;

    static const float k_width_input_numeric    = 120.0f;
    static const float k_width_combo_box        = 120.0f;

    void Initialise(Renderer* _renderer)
    {
        renderer = _renderer;
    }

    bool Option(const char* title, bool default_open = true)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        return ImGuiEx::CollapsingHeader(title, default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
    }

    void FirstColumn()
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
    }

    void SecondColumn()
    {
        ImGui::TableSetColumnIndex(1);
    }

    bool CheckBox(const char* label, bool& option, const char* tooltip = nullptr)
    {
        FirstColumn();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiEx::Tooltip(tooltip);
        }

        SecondColumn();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        ImGui::Checkbox("", &option);
        ImGui::PopID();

        return option;
    }

    bool ComboBox(const char* label, const std::vector<std::string>& options, uint32_t& selection_index, const char* tooltip = nullptr)
    {
        FirstColumn();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiEx::Tooltip(tooltip);
        }

        SecondColumn();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        ImGui::PushItemWidth(k_width_combo_box);
        bool result = ImGuiEx::ComboBox("", options, &selection_index);
        ImGui::PopItemWidth();
        ImGui::PopID();
        return result;
    }

    void RenderOptionValue(const char* label, Renderer_Option_Value render_option, const char* tooltip = nullptr, float step = 0.1f, float min = 0.0f, float max = numeric_limits<float>::max(), const char* format = "%.3f")
    {
        FirstColumn();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiEx::Tooltip(tooltip);
        }

        SecondColumn();
        {
            float value = renderer->GetOptionValue<float>(render_option);

            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
            ImGui::PushItemWidth(k_width_input_numeric);
            ImGui::InputFloat("", &value, step, 0.0f, format);
            ImGui::PopItemWidth();
            ImGui::PopID();
            value = Math::Helper::Clamp(value, min, max);

            // Only update if changed
            if (renderer->GetOptionValue<float>(render_option) != value)
            {
                renderer->SetOptionValue(render_option, value);
            }
        }
    }

    void Float(const char* label, float& option, float step = 0.1f, const char* format = "%.3f")
    {
        FirstColumn();
        ImGui::Text(label);

        SecondColumn();
        {
            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
            ImGui::PushItemWidth(k_width_input_numeric);
            ImGui::InputFloat("", &option, step, 0.0f, format);
            ImGui::PopItemWidth();
            ImGui::PopID();
        }
    }

    void Int(const char* label, int& option, int step = 1)
    {
        WidgetHelper::FirstColumn();
        ImGui::Text(label);
        WidgetHelper::SecondColumn();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        ImGui::PushItemWidth(k_width_input_numeric);
        ImGui::InputInt("##shadow_resolution", &option, step);
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
}

Widget_RenderOptions::Widget_RenderOptions(Editor* editor) : Widget(editor)
{
    m_title         = "Renderer Options";
    m_flags         |= ImGuiWindowFlags_AlwaysAutoResize;
    m_is_visible    = false;
    m_renderer      = m_context->GetSubsystem<Renderer>();
    m_alpha         = 1.0f;
    m_position      = k_widget_position_screen_center;
    m_size          = Vector2(600.0f, 1000.0f);
}

void Widget_RenderOptions::TickVisible()
{
    // Reflect options from engine
    bool do_bloom                   = m_renderer->GetOption(Render_Bloom);
    bool do_dof                     = m_renderer->GetOption(Render_DepthOfField);
    bool do_volumetric_fog          = m_renderer->GetOption(Render_VolumetricFog);
    bool do_ssao                    = m_renderer->GetOption(Render_Ssao);
    bool ssao_gi                    = m_renderer->GetOptionValue<bool>(Renderer_Option_Value::Ssao_Gi);
    bool do_sss                     = m_renderer->GetOption(Render_ScreenSpaceShadows);
    bool do_ssr                     = m_renderer->GetOption(Render_ScreenSpaceReflections);
    bool do_taa                     = m_renderer->GetOption(Render_AntiAliasing_Taa);
    bool do_fxaa                    = m_renderer->GetOption(Render_AntiAliasing_Fxaa);
    bool do_motion_blur             = m_renderer->GetOption(Render_MotionBlur);
    bool do_film_grain              = m_renderer->GetOption(Render_FilmGrain);
    bool do_sharperning             = m_renderer->GetOption(Render_Sharpening);
    bool do_chromatic_aberration    = m_renderer->GetOption(Render_ChromaticAberration);
    bool do_dithering               = m_renderer->GetOption(Render_Dithering);
    int resolution_shadow           = m_renderer->GetOptionValue<int>(Renderer_Option_Value::ShadowResolution);
    bool allow_taa_upsampling       = m_renderer->GetOptionValue<bool>(Renderer_Option_Value::Taa_AllowUpsampling);
    bool debug_physics              = m_renderer->GetOption(Render_Debug_Physics);
    bool debug_aabb                 = m_renderer->GetOption(Render_Debug_Aabb);
    bool debug_light                = m_renderer->GetOption(Render_Debug_Lights);
    bool debug_transform            = m_renderer->GetOption(Render_Debug_Transform);
    bool debug_selection_outline    = m_renderer->GetOption(Render_Debug_SelectionOutline);
    bool debug_picking_ray          = m_renderer->GetOption(Render_Debug_PickingRay);
    bool debug_grid                 = m_renderer->GetOption(Render_Debug_Grid);
    bool debug_performance_metrics  = m_renderer->GetOption(Render_Debug_PerformanceMetrics);
    bool debug_wireframe            = m_renderer->GetOption(Render_Debug_Wireframe);
    bool do_depth_prepass           = m_renderer->GetOption(Render_DepthPrepass);
    bool do_reverse_z               = m_renderer->GetOption(Render_ReverseZ);

    // Present options (with a table)
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

        const auto get_display_mode_index = [](const Vector2& resolution)
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

        static ImVec2 size              = ImVec2(0.0f);
        static int column_count         = 2;
        static ImGuiTableFlags flags    =
            ImGuiTableFlags_NoHostExtendX   |   // Make outer width auto-fit to columns, overriding outer_size.x value. Only available when ScrollX/ScrollY are disabled and Stretch columns are not used.
            ImGuiTableFlags_BordersInnerV   |   // Draw vertical borders between columns.
            ImGuiTableFlags_SizingFixedFit;     // Columns default to _WidthFixed or _WidthAuto (if resizable or not resizable), matching contents width.

        WidgetHelper::Initialise(m_renderer);

        // Table
        if (ImGui::BeginTable("##render_options", column_count, flags, size))
        {
            ImGui::TableSetupColumn("Option");
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();

            if (WidgetHelper::Option("Resolution"))
            {
                // Render
                Vector2 resolution_render = m_renderer->GetResolutionRender();
                uint32_t render_index = get_display_mode_index(resolution_render);
                if (WidgetHelper::ComboBox("Render resolution", display_modes_string, render_index))
                {
                    m_renderer->SetResolutionRender(display_modes[render_index].width, display_modes[render_index].height);
                }

                // Output
                Vector2 resolution_output = m_renderer->GetResolutionOutput();
                uint32_t output_index = get_display_mode_index(resolution_output);
                if (WidgetHelper::ComboBox("Output resolution", display_modes_string, output_index))
                {
                    m_renderer->SetResolutionOutput(display_modes[output_index].width, display_modes[output_index].height);
                }
            }

            if (WidgetHelper::Option("Screen space lighting"))
            {
                // SSR
                WidgetHelper::CheckBox("SSR - Screen space reflections", do_ssr);

                // SSAO
                WidgetHelper::CheckBox("SSAO - Screen space ambient occlusion", do_ssao);

                // SSAO + GI
                if (do_ssao)
                {
                    WidgetHelper::CheckBox("SSAO GI - Screen space global illumination", ssao_gi, "Use SSAO to compute diffuse global illumination");
                }
            }

            // Anti-aliasing
            if (WidgetHelper::Option("Anti-Aliasing"))
            {
                // TAA
                if (WidgetHelper::CheckBox("TAA - Temporal anti-aliasing", do_taa, "Used to improve many stochastic effects, you want this to always be enabled."))
                {
                    // Upsampling
                    WidgetHelper::CheckBox("TAA upsampling - WIP", allow_taa_upsampling, "If the output resolution is bigger than the render resolution, TAA will be used to reconstruct the image.");
                }

                // FXAA
                WidgetHelper::CheckBox("FXAA - Fast approximate anti-aliasing", do_fxaa);
            }

            if (WidgetHelper::Option("Camera"))
            {
                // Tonemapping
                static vector<string> tonemapping_options = { "Off", "ACES", "Reinhard", "Uncharted 2" };
                uint32_t selection_index = m_renderer->GetOptionValue<uint32_t>(Renderer_Option_Value::Tonemapping);
                if (WidgetHelper::ComboBox("Tonemapping", tonemapping_options, selection_index))
                {
                    m_renderer->SetOptionValue(Renderer_Option_Value::Tonemapping, static_cast<float>(selection_index));
                }

                // Gamma
                WidgetHelper::RenderOptionValue("Gamma", Renderer_Option_Value::Gamma);

                // Bloom
                if (WidgetHelper::CheckBox("Bloom", do_bloom))
                {
                    WidgetHelper::RenderOptionValue("Bloom intensity", Renderer_Option_Value::Bloom_Intensity, "", 0.001f);
                }

                // Motion blur
                WidgetHelper::CheckBox("Motion blur (controlled by the camera's shutter speed)", do_motion_blur);

                // Depth of Field
                WidgetHelper::CheckBox("Depth of field (controlled by the camera's aperture) - WIP", do_dof);

                // Chromatic aberration
                WidgetHelper::CheckBox("Chromatic aberration", do_chromatic_aberration, "Emulates the inability of old cameras to focus all colors in the same focal point.");

                // Film grain
                WidgetHelper::CheckBox("Film grain", do_film_grain);
            }

            if (WidgetHelper::Option("Lights"))
            {
                // Volumetric fog
                if (WidgetHelper::CheckBox("Volumetric fog", do_volumetric_fog, "Requires a light with shadows enabled."))
                {
                    // Density
                    WidgetHelper::RenderOptionValue("Volumetric fog density", Renderer_Option_Value::Fog, "", 0.01f, 0.0f, 16.0f, "%.2f");
                }

                // Screen space shadows
                WidgetHelper::CheckBox("Screen space shadows", do_sss);

                // Shadow resolution
                WidgetHelper::Int("Shadow resolution", resolution_shadow);
            }

            if (WidgetHelper::Option("Misc"))
            {
                // Dithering
                WidgetHelper::CheckBox("Dithering", do_dithering, "Reduces color banding");

                // Sharpen
                WidgetHelper::CheckBox("Sharpening (AMD FidelityFX CAS)", do_sharperning, "Contrast adaptive sharpening. Areas of the image that are already sharp are sharpened less, while areas that lack detail are sharpened more.");
                // Sharpen strength
                WidgetHelper::RenderOptionValue("Sharpening strength", Renderer_Option_Value::Sharpen_Strength, "", 0.1f, 0.0f, 1.0f);

                // FPS Limit
                {
                    Timer* timer = m_context->GetSubsystem<Timer>();
                    
                    WidgetHelper::FirstColumn();
                    const FpsLimitType fps_limit_type = timer->GetFpsLimitType();
                    string label = "FPS Limit - " + (fps_limit_type == FpsLimitType::FixedToMonitor) ? "Fixed to monitor" : (fps_limit_type == FpsLimitType::Unlocked ? "Unlocked" : "Fixed");
                    ImGui::Text(label.c_str());

                    WidgetHelper::SecondColumn();
                    {
                        double fps_target = timer->GetTargetFps();
                        ImGui::PushItemWidth(WidgetHelper::k_width_input_numeric);
                        ImGui::InputDouble("##fps_target", &fps_target, 0.0, 0.0f, "%.1f");
                        ImGui::PopItemWidth();
                        timer->SetTargetFps(fps_target);
                    }
                }

                // Depth-PrePass
                WidgetHelper::CheckBox("Depth PrePass - WIP", do_depth_prepass);

                // Reverse-Z
                WidgetHelper::CheckBox("Depth Reverse-Z", do_reverse_z);
            }

            if (WidgetHelper::Option("Editor", false))
            {
                if (WidgetHelper::CheckBox("Transform", debug_transform))
                {
                    WidgetHelper::Float("Transform size", m_renderer->m_gizmo_transform_size, 0.0025f);
                    WidgetHelper::Float("Transform speed", m_renderer->m_gizmo_transform_speed, 1.0f);
                }

                WidgetHelper::CheckBox("Selection outline", debug_selection_outline);
                WidgetHelper::CheckBox("Physics", debug_physics);
                WidgetHelper::CheckBox("AABBs - Axis-aligned bounding boxes", debug_aabb);
                WidgetHelper::CheckBox("Lights", debug_light);
                WidgetHelper::CheckBox("Picking ray", debug_picking_ray);
                WidgetHelper::CheckBox("Grid", debug_grid);  
                WidgetHelper::CheckBox("Wireframe", debug_wireframe);
            }

            if (WidgetHelper::Option("Debug", false))
            {
                // Performance metrics
                if (WidgetHelper::CheckBox("Performance Metrics", debug_performance_metrics) && !m_renderer->GetOption(Render_Debug_PerformanceMetrics))
                {
                    // Reset metrics on activation
                    m_profiler->ResetMetrics();
                }

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
                                render_target_options.emplace_back(render_target->GetObjectName());
                            }
                        }
                    }

                    // Combo box
                    uint32_t selection_index = static_cast<uint32_t>(m_renderer->GetRenderTargetDebug());
                    if (WidgetHelper::ComboBox("Render target", render_target_options, selection_index))
                    {
                        m_renderer->SetRenderTargetDebug(static_cast<RendererRt>(selection_index));
                    }
                }
            }

            ImGui::EndTable();
        }

        // Opacity
        ImGui::PushItemWidth(m_window->ContentSize.x - 60);
        ImGui::SliderFloat("Opacity", &m_alpha, 0.1f, 1.0f, "%.1f");
        ImGui::PopItemWidth();
    }

    // Map options to engine
    m_renderer->SetOption(Render_Bloom, do_bloom);
    m_renderer->SetOption(Render_DepthOfField, do_dof);
    m_renderer->SetOption(Render_VolumetricFog, do_volumetric_fog);
    m_renderer->SetOption(Render_Ssao, do_ssao);
    m_renderer->SetOption(Render_ScreenSpaceShadows, do_sss);
    m_renderer->SetOption(Render_ScreenSpaceReflections, do_ssr);
    m_renderer->SetOptionValue(Renderer_Option_Value::Ssao_Gi, static_cast<float>(ssao_gi));
    m_renderer->SetOption(Render_AntiAliasing_Taa, do_taa);
    m_renderer->SetOption(Render_AntiAliasing_Fxaa, do_fxaa);
    m_renderer->SetOption(Render_MotionBlur, do_motion_blur);
    m_renderer->SetOption(Render_FilmGrain, do_film_grain);
    m_renderer->SetOption(Render_Sharpening, do_sharperning);
    m_renderer->SetOption(Render_ChromaticAberration, do_chromatic_aberration);
    m_renderer->SetOption(Render_Dithering, do_dithering);
    m_renderer->SetOptionValue(Renderer_Option_Value::ShadowResolution, static_cast<float>(resolution_shadow));
    m_renderer->SetOptionValue(Renderer_Option_Value::Taa_AllowUpsampling, static_cast<float>(allow_taa_upsampling));
    m_renderer->SetOption(Render_Debug_Transform, debug_transform);
    m_renderer->SetOption(Render_Debug_SelectionOutline, debug_selection_outline);
    m_renderer->SetOption(Render_Debug_Physics, debug_physics);
    m_renderer->SetOption(Render_Debug_Aabb, debug_aabb);
    m_renderer->SetOption(Render_Debug_Lights, debug_light);
    m_renderer->SetOption(Render_Debug_PickingRay, debug_picking_ray);
    m_renderer->SetOption(Render_Debug_Grid, debug_grid);
    m_renderer->SetOption(Render_Debug_PerformanceMetrics, debug_performance_metrics);
    m_renderer->SetOption(Render_Debug_Wireframe, debug_wireframe);
    m_renderer->SetOption(Render_DepthPrepass, do_depth_prepass);
    m_renderer->SetOption(Render_ReverseZ, do_reverse_z);
}
