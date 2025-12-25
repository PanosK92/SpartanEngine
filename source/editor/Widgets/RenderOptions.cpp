/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ========================
#include "pch.h"
#include "RenderOptions.h"
#include "Core/Timer.h"
#include "RHI/RHI_Device.h"
#include "../ImGui/ImGui_Extension.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    // table
    int column_count      = 2;
    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Resizable;

    // options sizes
    #define width_input_numeric 120.0f * spartan::Window::GetDpiScale()
    #define width_combo_box     120.0f * spartan::Window::GetDpiScale()

    // misc
    vector<DisplayMode> display_modes;
    vector<string> display_modes_string;

    // helper functions
    bool option(const char* title, bool default_open = true)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        return ImGuiSp::collapsing_header(title, default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
    }

    void option_first_column()
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
    }

    void option_second_column()
    {
        ImGui::TableSetColumnIndex(1);
    }

    void option_check_box(const char* label, const Renderer_Option render_option, const char* tooltip = nullptr)
    {
        option_first_column();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiSp::tooltip(tooltip);
        }

        option_second_column();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        bool value = Renderer::GetOption<bool>(render_option, true);
        ImGui::Checkbox("", &value);
        Renderer::SetOption(render_option, value, true);
        ImGui::PopID();
    }

    void option_check_box(const char* label, bool& value, const char* tooltip = nullptr)
    {
        option_first_column();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiSp::tooltip(tooltip);
        }

        option_second_column();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        ImGui::Checkbox("", &value);
        ImGui::PopID();
    }

    bool option_combo_box(const char* label, const vector<string>& options, uint32_t& selection_index, const char* tooltip = nullptr)
    {
        option_first_column();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiSp::tooltip(tooltip);
        }

        option_second_column();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        ImGui::PushItemWidth(width_combo_box);
        bool result = ImGuiSp::combo_box("", options, &selection_index);
        ImGui::PopItemWidth();
        ImGui::PopID();
        return result;
    }

    bool option_value(const char* label, Renderer_Option render_option, const char* tooltip = nullptr, float step = 0.1f, float min = 0.0f, float max = numeric_limits<float>::max(), const char* format = "%.3f")
    {
        option_first_column();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiSp::tooltip(tooltip);
        }

        bool changed = false;
        option_second_column();
        {
            float value = Renderer::GetOption<float>(render_option, true);

            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
            ImGui::PushItemWidth(width_input_numeric);
            changed = ImGui::InputFloat("", &value, step, 0.0f, format);
            ImGui::PopItemWidth();
            ImGui::PopID();
            value = clamp(value, min, max);

            // Only update if changed
            if (changed)
            {
                Renderer::SetOption(render_option, value, true);
            }
        }

        return changed;
    }

    void option_float(const char* label, float& option, float step = 0.1f, const char* format = "%.3f")
    {
        option_first_column();
        ImGui::Text(label);

        option_second_column();
        {
            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
            ImGui::PushItemWidth(width_input_numeric);
            ImGui::InputFloat("", &option, step, 0.0f, format);
            ImGui::PopItemWidth();
            ImGui::PopID();
        }
    }

    void option_int(const char* label, int& option, int step = 1)
    {
        option_first_column();
        ImGui::Text(label);
        option_second_column();
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
        ImGui::PushItemWidth(width_input_numeric);
        ImGui::InputInt("##shadow_resolution", &option, step);
        ImGui::PopItemWidth();
        ImGui::PopID();
    }

    uint32_t get_display_mode_index(const Vector2& resolution)
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
}

RenderOptions::RenderOptions(Editor* editor) : Widget(editor)
{
    m_title        = "Renderer Options";
    m_visible      = false;
    m_alpha        = 1.0f;
    m_size_initial = Vector2(Display::GetWidth() * 0.25f, Display::GetHeight() * 0.5f);
}

void RenderOptions::OnVisible()
{
    // get display modes
    {
        display_modes.clear();
        display_modes_string.clear();

        for (const DisplayMode& display_mode : Display::GetDisplayModes())
        {
            if (display_mode.hz == Display::GetRefreshRate())
            {
                display_modes.emplace_back(display_mode);
                display_modes_string.emplace_back(to_string(display_mode.width) + "x" + to_string(display_mode.height));
            }
        }
    }
}

void RenderOptions::OnTickVisible()
{
    if (ImGui::BeginTabBar("##renderer_options_tabs"))
    {
        // ------------------------------------------------------------------------
        // RENDERING TAB
        // ------------------------------------------------------------------------
        if (ImGui::BeginTabItem("Rendering"))
        {
            if (ImGui::BeginTable("##rendering", column_count, flags))
            {
                ImGui::TableSetupColumn("Option");
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();

                if (option("Resolution"))
                {
                    // render resolution
                    Vector2 res_render = Renderer::GetResolutionRender();
                    uint32_t res_render_index = get_display_mode_index(res_render);
                    if (option_combo_box("Render resolution", display_modes_string, res_render_index))
                    {
                        Renderer::SetResolutionRender(display_modes[res_render_index].width, display_modes[res_render_index].height);
                    }

                    // output resolution
                    Vector2 res_output = Renderer::GetResolutionOutput();
                    uint32_t res_output_index = get_display_mode_index(res_output);
                    if (option_combo_box("Output resolution", display_modes_string, res_output_index))
                    {
                        Renderer::SetResolutionOutput(display_modes[res_output_index].width, display_modes[res_output_index].height);
                    }

                    option_check_box("Variable rate shading", Renderer_Option::VariableRateShading, "Improves performance by varying shading detail per pixel");
                    option_check_box("Dynamic resolution", Renderer_Option::DynamicResolution, "Scales render resolution automatically based on GPU load");

                    ImGui::BeginDisabled(Renderer::GetOption<bool>(Renderer_Option::DynamicResolution, true));
                    option_value("Resolution scale", Renderer_Option::ResolutionScale, "Adjusts the percentage of the render resolution", 0.01f);
                    ImGui::EndDisabled();
                }

                if (option("Anti-Aliasing & Upscaling"))
                {
                    static vector<string> upsamplers =
                    {
                        "Off",   // AA_Off_Upscale_Linear
                        "FXAA",  // AA_Fxaa_Upscale_Linear
                        "FSR 3", // AA_Fsr_Upscale_Fsr
                        "XeSS 2" // AA_Xess_Upscale_Xess
                    };

                    Vector2 res_render = Renderer::GetResolutionRender();
                    Vector2 res_output = Renderer::GetResolutionOutput();
                    uint32_t mode      = Renderer::GetOption<uint32_t>(Renderer_Option::AntiAliasing_Upsampling, true);
                    if (option_combo_box("Upsampling method", upsamplers, mode))
                    {
                        Renderer::SetOption(Renderer_Option::AntiAliasing_Upsampling, static_cast<float>(mode), true);
                    }

                    bool use_rcas = Renderer::GetOption<Renderer_AntiAliasing_Upsampling>(Renderer_Option::AntiAliasing_Upsampling, true) == Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr;
                    string label = use_rcas ? "Sharpness (RCAS)" : "Sharpness (CAS)";
                    string tooltip = use_rcas ? "AMD FidelityFX Robust Contrast Adaptive Sharpening" : "AMD FidelityFX Contrast Adaptive Sharpening";
                    option_value(label.c_str(), Renderer_Option::Sharpness, tooltip.c_str(), 0.1f, 0.0f, 1.0f);
                }

                if (option("Ray-traced Effects"))
                {
                    ImGui::BeginDisabled(!RHI_Device::IsSupportedRayTracing());
                    option_check_box("Reflections (WIP)", Renderer_Option::RayTracedReflections);
                    ImGui::EndDisabled();
                }

                if (option("Screen-space Effects"))
                {
                    option_check_box("Reflections (SSR)", Renderer_Option::ScreenSpaceReflections);
                    option_check_box("Ambient Occlusion (SSAO)", Renderer_Option::ScreenSpaceAmbientOcclusion);
                }

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // ------------------------------------------------------------------------
        // OUTPUT TAB
        // ------------------------------------------------------------------------
        if (ImGui::BeginTabItem("Output"))
        {
            if (ImGui::BeginTable("##output", column_count, flags))
            {
                ImGui::TableSetupColumn("Option");
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();

                if (option("Display"))
                {
                    option_check_box("HDR", Renderer_Option::Hdr, "Enable high dynamic range output");
                    ImGui::BeginDisabled(Renderer::GetOption<bool>(Renderer_Option::Hdr, true));
                    option_value("Gamma", Renderer_Option::Gamma);
                    ImGui::EndDisabled();
                    option_value("Exposure adaptation speed", Renderer_Option::AutoExposureAdaptationSpeed, "Negative value disables adaptation");

                    bool hdr_enabled = Renderer::GetOption<bool>(Renderer_Option::Hdr, true);
                    ImGui::BeginDisabled(!hdr_enabled);
                    option_value("White point (nits)", Renderer_Option::WhitePoint, "Target luminance of peak white", 1.0f);
                    ImGui::EndDisabled();
                }

                if (option("Tone Mapping"))
                {
                    static vector<string> tonemapping = { "ACES", "AgX", "Reinhard", "ACES Nautilus", "Off" };
                    uint32_t index = Renderer::GetOption<uint32_t>(Renderer_Option::Tonemapping, true);
                    if (option_combo_box("Algorithm", tonemapping, index))
                    {
                        Renderer::SetOption(Renderer_Option::Tonemapping, static_cast<float>(index), true);
                    }
                }

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // ------------------------------------------------------------------------
        // CAMERA TAB
        // ------------------------------------------------------------------------
        if (ImGui::BeginTabItem("Camera"))
        {
            if (ImGui::BeginTable("##camera", column_count, flags))
            {
                ImGui::TableSetupColumn("Effect");
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();

                option_value("Bloom intensity", Renderer_Option::Bloom, "Blend factor, set to 0 to disable", 0.01f);
                option_check_box("Motion blur", Renderer_Option::MotionBlur, "Controlled by camera shutter speed");
                option_check_box("Depth of field", Renderer_Option::DepthOfField, "Controlled by camera aperture");
                option_check_box("Film grain", Renderer_Option::FilmGrain, "Simulates old film camera noise");
                option_check_box("Chromatic aberration", Renderer_Option::ChromaticAberration, "Lens color fringing effect");
                option_check_box("VHS effect", Renderer_Option::Vhs, "Retro VHS look");
                option_check_box("Dithering", Renderer_Option::Dithering, "Reduces color banding in gradients");

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // ------------------------------------------------------------------------
        // WORLD TAB
        // ------------------------------------------------------------------------
        if (ImGui::BeginTabItem("World"))
        {
            if (ImGui::BeginTable("##world", column_count, flags))
            {
                ImGui::TableSetupColumn("Option");
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();

                option_value("Fog density", Renderer_Option::Fog, "Controls atmospheric fog strength", 0.1f);

                if (option("Wind"))
                {
                    Vector3 wind = Renderer::GetWind();
                    float strength = wind.Length();
                    float direction = atan2f(wind.x, wind.z) * (180.0f / 3.14159f);

                    bool changed = false;
                    changed |= ImGui::SliderFloat("Strength", &strength, 0.1f, 10.0f, "%.1f");
                    changed |= ImGui::SliderFloat("Direction (deg)", &direction, 0.0f, 360.0f, "%.1f");

                    if (changed)
                    {
                        float radians = direction * (3.14159f / 180.0f);
                        wind.x = sinf(radians) * strength;
                        wind.z = cosf(radians) * strength;
                        Renderer::SetWind(wind);
                    }
                }

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // ------------------------------------------------------------------------
        // DEBUG TAB
        // ------------------------------------------------------------------------
        if (ImGui::BeginTabItem("Debug"))
        {
            if (ImGui::BeginTable("##debug", column_count, flags))
            {
                ImGui::TableSetupColumn("Option");
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();

                if (option("Performance"))
                {
                    option_check_box("VSync", Renderer_Option::Vsync, "Synchronize frame updates with monitor refresh");
                    option_first_column();
                    string fps_label = "FPS Limit (" + string(
                        Timer::GetFpsLimitType() == FpsLimitType::FixedToMonitor ? "Fixed to monitor" :
                        Timer::GetFpsLimitType() == FpsLimitType::Unlocked ? "Unlocked" : "Fixed") + ")";
                    ImGui::Text(fps_label.c_str());
                    option_second_column();
                    {
                        float fps_target = Timer::GetFpsLimit();
                        ImGui::PushItemWidth(width_input_numeric);
                        ImGui::InputFloat("##fps_limit", &fps_target, 0.0, 0.0f, "%.1f");
                        ImGui::PopItemWidth();
                        Timer::SetFpsLimit(fps_target);
                    }
                    option_check_box("Show performance metrics", Renderer_Option::PerformanceMetrics);
                }

                if (option("Debug Visuals"))
                {
                    option_check_box("Transform handles", Renderer_Option::TransformHandle);
                    option_check_box("Selection outline", Renderer_Option::SelectionOutline);
                    option_check_box("Lights", Renderer_Option::Lights);
                    option_check_box("Audio sources", Renderer_Option::AudioSources);
                    option_check_box("Grid", Renderer_Option::Grid);
                    option_check_box("Picking ray", Renderer_Option::PickingRay);
                    option_check_box("Physics", Renderer_Option::Physics);
                    option_check_box("AABBs", Renderer_Option::Aabb);
                    option_check_box("Wireframe", Renderer_Option::Wireframe);
                    option_check_box("Occlusion culling", Renderer_Option::OcclusionCulling, "For development purposes");
                }

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
