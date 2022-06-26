/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "RenderOptions.h"
#include "Rendering/Renderer.h"
#include "Core/Context.h"
#include "Core/Timer.h"
#include "Math/MathHelper.h"
#include "Rendering/Model.h"
#include "../ImGuiExtension.h"
#include "../ImGui/Source/imgui_internal.h"
#include "RHI/RHI_Device.h"
#include "Profiling/Profiler.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

namespace helper
{
    static Renderer* renderer;

    static const float k_width_input_numeric = 120.0f;
    static const float k_width_combo_box     = 120.0f;

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

    void RenderOptionValue(const char* label, Renderer::OptionValue render_option, const char* tooltip = nullptr, float step = 0.1f, float min = 0.0f, float max = numeric_limits<float>::max(), const char* format = "%.3f")
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
        helper::FirstColumn();
        ImGui::Text(label);
        helper::SecondColumn();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        ImGui::PushItemWidth(k_width_input_numeric);
        ImGui::InputInt("##shadow_resolution", &option, step);
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
}

RenderOptions::RenderOptions(Editor* editor) : Widget(editor)
{
    m_title        = "Renderer Options";
    m_flags        |= ImGuiWindowFlags_AlwaysAutoResize;
    m_visible      = false;
    m_renderer     = m_context->GetSubsystem<Renderer>();
    m_alpha        = 1.0f;
    m_position     = k_widget_position_screen_center;
    m_size_initial = Vector2(600.0f, 1000.0f);
}

void RenderOptions::TickVisible()
{
    // Reflect options from engine
    bool do_bloom                  = m_renderer->GetOption(Renderer::Option::Bloom);
    bool do_dof                    = m_renderer->GetOption(Renderer::Option::DepthOfField);
    bool do_volumetric_fog         = m_renderer->GetOption(Renderer::Option::VolumetricFog);
    bool do_ssao                   = m_renderer->GetOption(Renderer::Option::Ssao);
    bool do_ssao_gi                = m_renderer->GetOption(Renderer::Option::Ssao_Gi);
    bool do_sss                    = m_renderer->GetOption(Renderer::Option::ScreenSpaceShadows);
    bool do_ssr                    = m_renderer->GetOption(Renderer::Option::ScreenSpaceReflections);
    bool do_taa                    = m_renderer->GetOption(Renderer::Option::AntiAliasing_Taa);
    bool do_fxaa                   = m_renderer->GetOption(Renderer::Option::AntiAliasing_Fxaa);
    bool do_motion_blur            = m_renderer->GetOption(Renderer::Option::MotionBlur);
    bool do_film_grain             = m_renderer->GetOption(Renderer::Option::FilmGrain);
    bool do_sharperning            = m_renderer->GetOption(Renderer::Option::Sharpening_AMD_FidelityFX_CAS);
    bool do_chromatic_aberration   = m_renderer->GetOption(Renderer::Option::ChromaticAberration);
    bool do_debanding              = m_renderer->GetOption(Renderer::Option::Debanding);
    bool debug_physics             = m_renderer->GetOption(Renderer::Option::Debug_Physics);
    bool debug_aabb                = m_renderer->GetOption(Renderer::Option::Debug_Aabb);
    bool debug_light               = m_renderer->GetOption(Renderer::Option::Debug_Lights);
    bool debug_transform           = m_renderer->GetOption(Renderer::Option::Transform_Handle);
    bool debug_selection_outline   = m_renderer->GetOption(Renderer::Option::Debug_SelectionOutline);
    bool debug_picking_ray         = m_renderer->GetOption(Renderer::Option::Debug_PickingRay);
    bool debug_grid                = m_renderer->GetOption(Renderer::Option::Debug_Grid);
    bool debug_reflection_probes   = m_renderer->GetOption(Renderer::Option::Debug_ReflectionProbes);
    bool debug_performance_metrics = m_renderer->GetOption(Renderer::Option::Debug_PerformanceMetrics);
    bool debug_wireframe           = m_renderer->GetOption(Renderer::Option::Debug_Wireframe);
    bool do_depth_prepass          = m_renderer->GetOption(Renderer::Option::DepthPrepass);
    bool do_reverse_z              = m_renderer->GetOption(Renderer::Option::ReverseZ);
    bool do_upsample_taa           = m_renderer->GetOption(Renderer::Option::Upsample_TAA);
    bool do_upsample_amd           = m_renderer->GetOption(Renderer::Option::Upsample_AMD_FidelityFX_FSR_1_0);
    int resolution_shadow          = m_renderer->GetOptionValue<int>(Renderer::OptionValue::ShadowResolution);

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

        helper::Initialise(m_renderer);
        
        // Table
        if (ImGui::BeginTable("##render_options", column_count, flags, size))
        {
            ImGui::TableSetupColumn("Option");
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();

            if (helper::Option("Resolution"))
            {
                // Render
                Vector2 resolution_render = m_renderer->GetResolutionRender();
                uint32_t resolution_render_index = get_display_mode_index(resolution_render);
                if (helper::ComboBox("Render resolution", display_modes_string, resolution_render_index))
                {
                    m_renderer->SetResolutionRender(display_modes[resolution_render_index].width, display_modes[resolution_render_index].height);
                }

                // Output
                Vector2 resolution_output = m_renderer->GetResolutionOutput();
                uint32_t resolution_output_index = get_display_mode_index(resolution_output);
                if (helper::ComboBox("Output resolution", display_modes_string, resolution_output_index))
                {
                    m_renderer->SetResolutionOutput(display_modes[resolution_output_index].width, display_modes[resolution_output_index].height);
                }

                // Upsampling
                {
                    bool upsampling_allowed = resolution_render.x < resolution_output.x || resolution_render.y < resolution_output.y;

                    static vector<string> upsampling_modes = { "Linear", "TAA upsampling - WIP", "AMD FidelityFX Super Resolution" };
                    uint32_t upsampling_mode_index = do_upsample_taa ? 1 : (do_upsample_amd ? 2 : 0);

                    ImGui::BeginDisabled(!upsampling_allowed);

                    if (helper::ComboBox("Upsampling", upsampling_modes, upsampling_mode_index))
                    {
                        if (upsampling_mode_index == 0)
                        {
                            do_upsample_taa = false;
                            do_upsample_amd = false;
                        }
                        else if (upsampling_mode_index == 1)
                        {
                            do_upsample_taa = true;
                            do_upsample_amd = false;
                        }
                        else if (upsampling_mode_index == 2)
                        {
                            do_upsample_taa = false;
                            do_upsample_amd = true;
                        }
                    }

                    ImGui::EndDisabled();
                }
            }

            if (helper::Option("Screen space lighting"))
            {
                // SSR
                helper::CheckBox("SSR - Screen space reflections", do_ssr);

                // SSAO
                helper::CheckBox("SSAO - Screen space ambient occlusion", do_ssao);

                // SSAO + GI
                {
                    ImGui::BeginDisabled(!do_ssao);
                    helper::CheckBox("SSAO GI - Screen space global illumination", do_ssao_gi, "Use SSAO to compute diffuse global illumination");
                    ImGui::EndDisabled();
                }
            }

            if (helper::Option("Anti-Aliasing"))
            {
                // TAA
                helper::CheckBox("TAA - Temporal anti-aliasing", do_taa, "Used to improve many stochastic effects, you want this to always be enabled.");

                // FXAA
                helper::CheckBox("FXAA - Fast approximate anti-aliasing", do_fxaa);
            }

            if (helper::Option("Camera"))
            {
                // Tonemapping
                static vector<string> tonemapping_options = { "Off", "ACES", "Reinhard", "Uncharted 2", "Matrix"};
                uint32_t selection_index = m_renderer->GetOptionValue<uint32_t>(Renderer::OptionValue::Tonemapping);
                if (helper::ComboBox("Tonemapping", tonemapping_options, selection_index))
                {
                    m_renderer->SetOptionValue(Renderer::OptionValue::Tonemapping, static_cast<float>(selection_index));
                }

                // Gamma
                helper::RenderOptionValue("Gamma", Renderer::OptionValue::Gamma);

                // Bloom
                helper::CheckBox("Bloom", do_bloom);
                {
                    ImGui::BeginDisabled(!do_bloom);
                    helper::RenderOptionValue("Bloom intensity", Renderer::OptionValue::Bloom_Intensity, "", 0.001f);
                    ImGui::EndDisabled();
                }

                // Motion blur
                helper::CheckBox("Motion blur (controlled by the camera's shutter speed)", do_motion_blur);

                // Depth of Field
                helper::CheckBox("Depth of field (controlled by the camera's aperture)", do_dof);

                // Chromatic aberration
                helper::CheckBox("Chromatic aberration (controlled by the camera's aperture)", do_chromatic_aberration, "Emulates the inability of old cameras to focus all colors in the same focal point.");

                // Film grain
                helper::CheckBox("Film grain", do_film_grain);
            }

            if (helper::Option("Lights"))
            {
                // Volumetric fog
                helper::CheckBox("Volumetric fog", do_volumetric_fog, "Requires a light with shadows enabled.");
                {
                    // Density
                    ImGui::BeginDisabled(!do_volumetric_fog);
                    helper::RenderOptionValue("Volumetric fog density", Renderer::OptionValue::Fog, "", 0.01f, 0.0f, 16.0f, "%.2f");
                    ImGui::EndDisabled();
                }

                // Screen space shadows
                helper::CheckBox("Screen space shadows", do_sss);

                // Shadow resolution
                helper::Int("Shadow resolution", resolution_shadow);
            }

            if (helper::Option("Misc"))
            {
                // Dithering
                helper::CheckBox("Debanding", do_debanding, "Reduces color banding");

                // Sharpen
                helper::CheckBox("Sharpening (AMD FidelityFX CAS)", do_sharperning, "Contrast adaptive sharpening. Areas of the image that are already sharp are sharpened less, while areas that lack detail are sharpened more.");

                // Sharpen strength
                ImGui::BeginDisabled(!do_sharperning);
                helper::RenderOptionValue("Sharpening strength", Renderer::OptionValue::Sharpen_Strength, "", 0.1f, 0.0f, 1.0f);
                ImGui::EndDisabled();

                // FPS Limit
                {
                    Timer* timer = m_context->GetSubsystem<Timer>();
                    
                    helper::FirstColumn();
                    const FpsLimitType fps_limit_type = timer->GetFpsLimitType();
                    string label = "FPS Limit - " + string((fps_limit_type == FpsLimitType::FixedToMonitor) ? "Fixed to monitor" : (fps_limit_type == FpsLimitType::Unlocked ? "Unlocked" : "Fixed"));
                    ImGui::Text(label.c_str());

                    helper::SecondColumn();
                    {
                        double fps_target = timer->GetFpsLimit();
                        ImGui::PushItemWidth(helper::k_width_input_numeric);
                        ImGui::InputDouble("##fps_limit", &fps_target, 0.0, 0.0f, "%.1f");
                        ImGui::PopItemWidth();
                        timer->SetFpsLimit(fps_target);
                    }
                }

                // Depth-PrePass
                helper::CheckBox("Depth PrePass", do_depth_prepass);

                // Reverse-Z
                helper::CheckBox("Depth Reverse-Z", do_reverse_z);

                // Performance metrics
                if (helper::CheckBox("Performance Metrics", debug_performance_metrics) && !m_renderer->GetOption(Renderer::Option::Debug_PerformanceMetrics))
                {
                    // Reset metrics on activation
                    m_profiler->ResetMetrics();
                }
            }

            if (helper::Option("Editor", false))
            {
                helper::CheckBox("Transform", debug_transform);
                {
                    ImGui::BeginDisabled(!debug_transform);
                    helper::Float("Transform size",  m_context->GetSubsystem<World>()->m_gizmo_transform_size, 0.0025f);
                    ImGui::EndDisabled();
                }

                helper::CheckBox("Selection outline",                   debug_selection_outline);
                helper::CheckBox("Physics",                             debug_physics);
                helper::CheckBox("AABBs - Axis-aligned bounding boxes", debug_aabb);
                helper::CheckBox("Lights",                              debug_light);
                helper::CheckBox("Picking ray",                         debug_picking_ray);
                helper::CheckBox("Grid",                                debug_grid);
                helper::CheckBox("Reflection probes",                   debug_reflection_probes);
                helper::CheckBox("Wireframe",                           debug_wireframe);
            }

            ImGui::EndTable();
        }
        
        // Opacity
        ImGui::PushItemWidth(m_window->ContentSize.x - 60);
        ImGui::SliderFloat("Opacity", &m_alpha, 0.1f, 1.0f, "%.1f");
        ImGui::PopItemWidth();
    }

    // Map options to engine
    m_renderer->SetOption(Renderer::Option::Bloom,                                                do_bloom);
    m_renderer->SetOption(Renderer::Option::DepthOfField,                                         do_dof);
    m_renderer->SetOption(Renderer::Option::VolumetricFog,                                        do_volumetric_fog);
    m_renderer->SetOption(Renderer::Option::Ssao,                                                 do_ssao);
    m_renderer->SetOption(Renderer::Option::Ssao_Gi,                                              do_ssao_gi);
    m_renderer->SetOption(Renderer::Option::ScreenSpaceShadows,                                   do_sss);
    m_renderer->SetOption(Renderer::Option::ScreenSpaceReflections,                               do_ssr);
    m_renderer->SetOption(Renderer::Option::AntiAliasing_Taa,                                     do_taa);
    m_renderer->SetOption(Renderer::Option::AntiAliasing_Fxaa,                                    do_fxaa);
    m_renderer->SetOption(Renderer::Option::MotionBlur,                                           do_motion_blur);
    m_renderer->SetOption(Renderer::Option::FilmGrain,                                            do_film_grain);
    m_renderer->SetOption(Renderer::Option::Sharpening_AMD_FidelityFX_CAS, do_sharperning);
    m_renderer->SetOption(Renderer::Option::ChromaticAberration,                                  do_chromatic_aberration);
    m_renderer->SetOption(Renderer::Option::Debanding,                                            do_debanding);
    m_renderer->SetOption(Renderer::Option::Transform_Handle,                                     debug_transform);
    m_renderer->SetOption(Renderer::Option::Debug_SelectionOutline,                               debug_selection_outline);
    m_renderer->SetOption(Renderer::Option::Debug_Physics,                                        debug_physics);
    m_renderer->SetOption(Renderer::Option::Debug_Aabb,                                           debug_aabb);
    m_renderer->SetOption(Renderer::Option::Debug_Lights,                                         debug_light);
    m_renderer->SetOption(Renderer::Option::Debug_PickingRay,                                     debug_picking_ray);
    m_renderer->SetOption(Renderer::Option::Debug_Grid,                                           debug_grid);
    m_renderer->SetOption(Renderer::Option::Debug_ReflectionProbes,                               debug_reflection_probes);
    m_renderer->SetOption(Renderer::Option::Debug_PerformanceMetrics,                             debug_performance_metrics);
    m_renderer->SetOption(Renderer::Option::Debug_Wireframe,                                      debug_wireframe);
    m_renderer->SetOption(Renderer::Option::DepthPrepass,                                         do_depth_prepass);
    m_renderer->SetOption(Renderer::Option::ReverseZ,                                             do_reverse_z);
    m_renderer->SetOption(Renderer::Option::Upsample_TAA,                                         do_upsample_taa);
    m_renderer->SetOption(Renderer::Option::Upsample_AMD_FidelityFX_FSR_1_0,              do_upsample_amd);
    m_renderer->SetOptionValue(Renderer::OptionValue::ShadowResolution,                           static_cast<float>(resolution_shadow));
}
