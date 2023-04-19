/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =======================
#include "RenderOptions.h"
#include "Rendering/Renderer.h"
#include "Core/Timer.h"
#include "Math/MathHelper.h"
#include "RHI/RHI_Device.h"
#include "Profiling/Profiler.h"
#include "../ImGui/ImGuiExtension.h"
#include "Rendering/Mesh.h"
//==================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

namespace helper
{
    static const float k_width_input_numeric = 120.0f;
    static const float k_width_combo_box     = 120.0f;

    bool Option(const char* title, bool default_open = true)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        return ImGuiSp::collapsing_header(title, default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
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
            ImGuiSp::tooltip(tooltip);
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
            ImGuiSp::tooltip(tooltip);
        }

        SecondColumn();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        ImGui::PushItemWidth(k_width_combo_box);
        bool result = ImGuiSp::combo_box("", options, &selection_index);
        ImGui::PopItemWidth();
        ImGui::PopID();
        return result;
    }

    void RenderOptionValue(const char* label, RendererOption render_option, const char* tooltip = nullptr, float step = 0.1f, float min = 0.0f, float max = numeric_limits<float>::max(), const char* format = "%.3f")
    {
        FirstColumn();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiSp::tooltip(tooltip);
        }

        SecondColumn();
        {
            float value = Renderer::GetOption<float>(render_option);

            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
            ImGui::PushItemWidth(k_width_input_numeric);
            ImGui::InputFloat("", &value, step, 0.0f, format);
            ImGui::PopItemWidth();
            ImGui::PopID();
            value = Math::Helper::Clamp(value, min, max);

            // Only update if changed
            if (Renderer::GetOption<float>(render_option) != value)
            {
                Renderer::SetOption(render_option, value);
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
    m_alpha        = 1.0f;
    m_position     = k_widget_position_screen_center;
    m_size_initial = Vector2(600.0f, 1000.0f);
}

void RenderOptions::TickVisible()
{
    // Reflect options from engine
    bool do_dof                    = Renderer::GetOption<bool>(RendererOption::DepthOfField);
    bool do_volumetric_fog         = Renderer::GetOption<bool>(RendererOption::VolumetricFog);
    bool do_ssao                   = Renderer::GetOption<bool>(RendererOption::Ssao);
    bool do_ssao_gi                = Renderer::GetOption<bool>(RendererOption::Ssao_Gi);
    bool do_sss                    = Renderer::GetOption<bool>(RendererOption::ScreenSpaceShadows);
    bool do_ssr                    = Renderer::GetOption<bool>(RendererOption::ScreenSpaceReflections);
    bool do_motion_blur            = Renderer::GetOption<bool>(RendererOption::MotionBlur);
    bool do_film_grain             = Renderer::GetOption<bool>(RendererOption::FilmGrain);
    bool do_chromatic_aberration   = Renderer::GetOption<bool>(RendererOption::ChromaticAberration); 
    bool do_debanding              = Renderer::GetOption<bool>(RendererOption::Debanding);
    bool do_hdr                    = Renderer::GetOption<bool>(RendererOption::Hdr);
    bool do_vsync                  = Renderer::GetOption<bool>(RendererOption::Vsync);
    bool debug_physics             = Renderer::GetOption<bool>(RendererOption::Debug_Physics);
    bool debug_aabb                = Renderer::GetOption<bool>(RendererOption::Debug_Aabb);
    bool debug_light               = Renderer::GetOption<bool>(RendererOption::Debug_Lights);
    bool debug_transform           = Renderer::GetOption<bool>(RendererOption::Debug_TransformHandle);
    bool debug_selection_outline   = Renderer::GetOption<bool>(RendererOption::Debug_SelectionOutline);
    bool debug_picking_ray         = Renderer::GetOption<bool>(RendererOption::Debug_PickingRay);
    bool debug_grid                = Renderer::GetOption<bool>(RendererOption::Debug_Grid);
    bool debug_reflection_probes   = Renderer::GetOption<bool>(RendererOption::Debug_ReflectionProbes);
    bool performance_metrics       = Renderer::GetOption<bool>(RendererOption::Debug_PerformanceMetrics);
    bool debug_wireframe           = Renderer::GetOption<bool>(RendererOption::Debug_Wireframe);
    bool do_depth_prepass          = Renderer::GetOption<bool>(RendererOption::DepthPrepass);
    int resolution_shadow          = Renderer::GetOption<int>(RendererOption::ShadowResolution);

    // Present options (with a table)
    {
        // Get display modes
        vector<DisplayMode> display_modes;
        vector<string> display_modes_string;
        if (display_modes.empty())
        {
            for (const DisplayMode& display_mode : Display::GetDisplayModes())
            {
                if (display_mode.hz == Display::GetRefreshRate())
                {
                    display_modes.emplace_back(display_mode);
                    display_modes_string.emplace_back(to_string(display_mode.width) + "x" + to_string(display_mode.height));
                }
            }
        }

        const auto get_display_mode_index = [&display_modes](const Vector2& resolution)
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

        static ImVec2 size           = ImVec2(0.0f);
        static int column_count      = 2;
        static ImGuiTableFlags flags =
            ImGuiTableFlags_NoHostExtendX   |   // Make outer width auto-fit to columns, overriding outer_size.x value. Only available when ScrollX/ScrollY are disabled and Stretch columns are not used.
            ImGuiTableFlags_BordersInnerV   |   // Draw vertical borders between columns.
            ImGuiTableFlags_SizingFixedFit;     // Columns default to _WidthFixed or _WidthAuto (if resizable or not resizable), matching contents width.

        // Table
        if (ImGui::BeginTable("##render_options", column_count, flags, size))
        {
            ImGui::TableSetupColumn("Option");
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();

            if (helper::Option("Resolution"))
            {
                // Render
                Vector2 resolution_render = Renderer::GetResolutionRender();
                uint32_t resolution_render_index = get_display_mode_index(resolution_render);
                if (helper::ComboBox("Render resolution", display_modes_string, resolution_render_index))
                {
                    Renderer::SetResolutionRender(display_modes[resolution_render_index].width, display_modes[resolution_render_index].height);
                }

                // Output
                Vector2 resolution_output = Renderer::GetResolutionOutput();
                uint32_t resolution_output_index = get_display_mode_index(resolution_output);
                if (helper::ComboBox("Output resolution", display_modes_string, resolution_output_index))
                {
                    Renderer::SetResolutionOutput(display_modes[resolution_output_index].width, display_modes[resolution_output_index].height);
                }

                // Upsampling
                {
                    static vector<string> upsampling_modes =
                    {
                        "Linear",
                        "FSR 2"
                    };

                    bool upsampling_allowed = resolution_render.x < resolution_output.x || resolution_render.y < resolution_output.y;
                    ImGui::BeginDisabled(!upsampling_allowed);

                    uint32_t upsampling_mode = Renderer::GetOption<uint32_t>(RendererOption::Upsampling);
                    if (helper::ComboBox("Upsampling", upsampling_modes, upsampling_mode))
                    {
                        Renderer::SetOption(RendererOption::Upsampling, static_cast<float>(upsampling_mode));
                    }

                    ImGui::EndDisabled();
                }

                // Sharpening
                {
                    // FidelityFX FSR 2.0 sharpening overrides FidelityFX CAS
                    bool fsr_enabled = Renderer::GetOption<UpsamplingMode>(RendererOption::Upsampling) == UpsamplingMode::FSR2;
                    string label = fsr_enabled ? "Sharpness (AMD FidelityFX FSR 2.0)" : "Sharpness (AMD FidelityFX CAS)";

                    helper::RenderOptionValue(label.c_str(), RendererOption::Sharpness, "", 0.1f, 0.0f, 1.0f);
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
                // Reflect
                AntialiasingMode antialiasing = Renderer::GetOption<AntialiasingMode>(RendererOption::Antialiasing);

                // TAA
                bool taa_enabled = antialiasing == AntialiasingMode::Taa || antialiasing == AntialiasingMode::TaaFxaa;
                helper::CheckBox("TAA - Temporal anti-aliasing", taa_enabled, "Used to improve many stochastic effects, you want this to always be enabled.");

                // FXAA
                bool fxaa_enabled = antialiasing == AntialiasingMode::Fxaa || antialiasing == AntialiasingMode::TaaFxaa;
                helper::CheckBox("FXAA - Fast approximate anti-aliasing", fxaa_enabled);

                // Map
                if (taa_enabled && fxaa_enabled)
                {
                    Renderer::SetOption(RendererOption::Antialiasing, static_cast<float>(AntialiasingMode::TaaFxaa));
                }
                else if (taa_enabled)
                {
                    Renderer::SetOption(RendererOption::Antialiasing, static_cast<float>(AntialiasingMode::Taa));
                }
                else if (fxaa_enabled)
                {
                    Renderer::SetOption(RendererOption::Antialiasing, static_cast<float>(AntialiasingMode::Fxaa));
                }
                else
                {
                    Renderer::SetOption(RendererOption::Antialiasing, static_cast<float>(AntialiasingMode::Disabled));
                }
            }

            if (helper::Option("Camera"))
            {
                // Tonemapping
                static vector<string> tonemapping_options = { "AMD", "ACES", "Reinhard", "Uncharted 2", "Matrix", "Off" };
                uint32_t selection_index = Renderer::GetOption<uint32_t>(RendererOption::Tonemapping);
                if (helper::ComboBox("Tonemapping", tonemapping_options, selection_index))
                {
                    Renderer::SetOption(RendererOption::Tonemapping, static_cast<float>(selection_index));
                }

                // Gamma
                helper::RenderOptionValue("Gamma", RendererOption::Gamma);

                // Bloom
                helper::RenderOptionValue("Bloom", RendererOption::Bloom, "Controls the blend factor. If zero, then bloom is disabled.", 0.01f);

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
                    helper::RenderOptionValue("Volumetric fog density", RendererOption::Fog, "", 0.01f, 0.0f, 16.0f, "%.2f");
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

                // HDR
                helper::CheckBox("HDR", do_hdr, "High dynamic range");

                // VSync
                helper::CheckBox("VSync", do_vsync, "Vertical Synchronization");

                // FPS Limit
                {
                    helper::FirstColumn();
                    const FpsLimitType fps_limit_type = Timer::GetFpsLimitType();
                    string label = "FPS Limit - " + string((fps_limit_type == FpsLimitType::FixedToMonitor) ? "Fixed to monitor" : (fps_limit_type == FpsLimitType::Unlocked ? "Unlocked" : "Fixed"));
                    ImGui::Text(label.c_str());

                    helper::SecondColumn();
                    {
                        float fps_target = Timer::GetFpsLimit();
                        ImGui::PushItemWidth(helper::k_width_input_numeric);
                        ImGui::InputFloat("##fps_limit", &fps_target, 0.0, 0.0f, "%.1f");
                        ImGui::PopItemWidth();
                        Timer::SetFpsLimit(fps_target);
                    }
                }

                // Depth-PrePass
                helper::CheckBox("Depth PrePass", do_depth_prepass);

                // Performance metrics
                {
                    bool performance_metrics_previous = performance_metrics;
                    helper::CheckBox("Performance Metrics", performance_metrics);

                    // Reset metrics on activation
                    if (performance_metrics_previous == false && performance_metrics == true)
                    {
                        Profiler::ClearMetrics();
                    }
                }
            }

            if (helper::Option("Gizmos", false))
            {
                helper::CheckBox("Transform",                           debug_transform);
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
    Renderer::SetOption(RendererOption::ShadowResolution,         static_cast<float>(resolution_shadow));
    Renderer::SetOption(RendererOption::DepthOfField,             do_dof);
    Renderer::SetOption(RendererOption::VolumetricFog,            do_volumetric_fog);
    Renderer::SetOption(RendererOption::Ssao,                     do_ssao);
    Renderer::SetOption(RendererOption::Ssao_Gi,                  do_ssao_gi);
    Renderer::SetOption(RendererOption::ScreenSpaceShadows,       do_sss);
    Renderer::SetOption(RendererOption::ScreenSpaceReflections,   do_ssr);
    Renderer::SetOption(RendererOption::MotionBlur,               do_motion_blur);
    Renderer::SetOption(RendererOption::FilmGrain,                do_film_grain);
    Renderer::SetOption(RendererOption::ChromaticAberration,      do_chromatic_aberration);
    Renderer::SetOption(RendererOption::Debanding,                do_debanding);
    Renderer::SetOption(RendererOption::Hdr,                      do_hdr);
    Renderer::SetOption(RendererOption::Vsync,                    do_vsync);
    Renderer::SetOption(RendererOption::Debug_TransformHandle,    debug_transform);
    Renderer::SetOption(RendererOption::Debug_SelectionOutline,   debug_selection_outline);
    Renderer::SetOption(RendererOption::Debug_Physics,            debug_physics);
    Renderer::SetOption(RendererOption::Debug_Aabb,               debug_aabb);
    Renderer::SetOption(RendererOption::Debug_Lights,             debug_light);
    Renderer::SetOption(RendererOption::Debug_PickingRay,         debug_picking_ray);
    Renderer::SetOption(RendererOption::Debug_Grid,               debug_grid);
    Renderer::SetOption(RendererOption::Debug_ReflectionProbes,   debug_reflection_probes);
    Renderer::SetOption(RendererOption::Debug_PerformanceMetrics, performance_metrics);
    Renderer::SetOption(RendererOption::Debug_Wireframe,          debug_wireframe);
    Renderer::SetOption(RendererOption::DepthPrepass,             do_depth_prepass);
}
