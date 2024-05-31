/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "Core/Timer.h"
#include "RHI/RHI_Device.h"
#include "../ImGui/ImGuiExtension.h"
//==================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

namespace
{
    // table
    int column_count      = 2;
    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Resizable;

    // options sizes
    #define width_input_numeric 120.0f * Spartan::Window::GetDpiScale()
    #define width_combo_box     120.0f * Spartan::Window::GetDpiScale()

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
        bool value = Renderer::GetOption<bool>(render_option);
        ImGui::Checkbox("", &value);
        Renderer::SetOption(render_option, value);
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

    void option_value(const char* label, Renderer_Option render_option, const char* tooltip = nullptr, float step = 0.1f, float min = 0.0f, float max = numeric_limits<float>::max(), const char* format = "%.3f")
    {
        option_first_column();
        ImGui::Text(label);
        if (tooltip)
        {
            ImGuiSp::tooltip(tooltip);
        }

        option_second_column();
        {
            float value = Renderer::GetOption<float>(render_option);

            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY()));
            ImGui::PushItemWidth(width_input_numeric);
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
    m_size_initial = Vector2(720, 1080);
    m_visible      = false;
    m_alpha        = 1.0f;
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
    if (ImGui::BeginTable("##render_options", column_count, flags))
    {
        ImGui::TableSetupColumn("Option");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        if (option("Resolution"))
        {
            // render
            Vector2 resolution_render        = Renderer::GetResolutionRender();
            uint32_t resolution_render_index = get_display_mode_index(resolution_render);
            if (option_combo_box("Render resolution", display_modes_string, resolution_render_index))
            {
                Renderer::SetResolutionRender(display_modes[resolution_render_index].width, display_modes[resolution_render_index].height);
            }

            // output
            Vector2 resolution_output        = Renderer::GetResolutionOutput();
            uint32_t resolution_output_index = get_display_mode_index(resolution_output);
            if (option_combo_box("Output resolution", display_modes_string, resolution_output_index))
            {
                Renderer::SetResolutionOutput(display_modes[resolution_output_index].width, display_modes[resolution_output_index].height);
            }

            ImGui::BeginDisabled(!Spartan::RHI_Device::PropertyIsShadingRateSupported());
            { 
                option_check_box("Variable rate shading", Renderer_Option::VariableRateShading, "Improves performance by varying pixel shading detail");
            }
            ImGui::EndDisabled();
            option_check_box("Dynamic resolution", Renderer_Option::DynamicResolution, "GPU load driven resolution scale");
            ImGui::BeginDisabled(Renderer::GetOption<bool>(Renderer_Option::DynamicResolution));
            option_value("Resolution scale", Renderer_Option::ResolutionScale, "Adjusts the percentage of the render resolution", 0.01f);
            ImGui::EndDisabled();

            // upsampling
            {
                static vector<string> upsampling_modes =
                {
                    "Linear",
                    "FSR 2"
                };

                bool is_upsampling = resolution_render.x < resolution_output.x || resolution_render.y < resolution_output.y;
                ImGui::BeginDisabled(!is_upsampling);
                {
                    uint32_t upsampling_mode = Renderer::GetOption<uint32_t>(Renderer_Option::Upsampling);
                    if (option_combo_box("Upsampling", upsampling_modes, upsampling_mode))
                    {
                        Renderer::SetOption(Renderer_Option::Upsampling, static_cast<float>(upsampling_mode));
                    }
                }
                ImGui::EndDisabled();

                // sharpening
                bool use_rcas  = Renderer::GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling) == Renderer_Upsampling::Fsr2;
                string label   = use_rcas ? "Sharpness (RCAS)" : "Sharpness (CAS)";
                string tooltip = use_rcas ? "AMD FidelityFX Robust Contrast Adaptive Sharpening (RCAS)" : "AMD FidelityFX Contrast Adaptive Sharpening (CAS)";
                option_value(label.c_str(), Renderer_Option::Sharpness, tooltip.c_str(), 0.1f, 0.0f, 1.0f);
            }
        }

        if (option("Output"))
        {
            option_check_box("HDR", Renderer_Option::Hdr, "High dynamic range");

            bool hdr_enabled = Renderer::GetOption<bool>(Renderer_Option::Hdr);

            // white point
            ImGui::BeginDisabled(!hdr_enabled);
            option_value("White point (nits)", Renderer_Option::WhitePoint, nullptr, 1.0f);
            ImGui::EndDisabled();

            // tone mapping
            static vector<string> tonemapping_options = { "ACES", "Nautilus ACES", "Reinhard", "Uncharted 2", "Matrix", "Off" };
            ImGui::BeginDisabled(hdr_enabled);
            uint32_t selection_index = Renderer::GetOption<uint32_t>(Renderer_Option::Tonemapping);
            if (option_combo_box("Tonemapping", tonemapping_options, selection_index))
            {
                Renderer::SetOption(Renderer_Option::Tonemapping, static_cast<float>(selection_index));
            }
            ImGui::EndDisabled();
        }


        if (option("Screen space lighting"))
        {
            // ssr
            option_check_box("SSR - Screen space reflections", Renderer_Option::ScreenSpaceReflections);

            // ssgi
            option_check_box("SSGI - Screen space global illumination", Renderer_Option::ScreenSpaceGlobalIllumination, "SSAO with a diffuse light bounce");
        }

        if (option("Anti-Aliasing"))
        {
            // reflect
            Renderer_Antialiasing antialiasing = Renderer::GetOption<Renderer_Antialiasing>(Renderer_Option::Antialiasing);

            // taa
            bool taa_enabled = antialiasing == Renderer_Antialiasing::Taa || antialiasing == Renderer_Antialiasing::TaaFxaa;
            option_check_box("TAA - Temporal anti-aliasing", taa_enabled, "Used to improve many stochastic effects, you want this to always be enabled");

            // fxaa
            bool fxaa_enabled = antialiasing == Renderer_Antialiasing::Fxaa || antialiasing == Renderer_Antialiasing::TaaFxaa;
            option_check_box("FXAA - Fast approximate anti-aliasing", fxaa_enabled);

            // map
            if (taa_enabled && fxaa_enabled)
            {
                Renderer::SetOption(Renderer_Option::Antialiasing, static_cast<float>(Renderer_Antialiasing::TaaFxaa));
            }
            else if (taa_enabled)
            {
                Renderer::SetOption(Renderer_Option::Antialiasing, static_cast<float>(Renderer_Antialiasing::Taa));
            }
            else if (fxaa_enabled)
            {
                Renderer::SetOption(Renderer_Option::Antialiasing, static_cast<float>(Renderer_Antialiasing::Fxaa));
            }
            else
            {
                Renderer::SetOption(Renderer_Option::Antialiasing, static_cast<float>(Renderer_Antialiasing::Disabled));
            }
        }

        if (option("Camera"))
        {
            // bloom
            option_value("Bloom", Renderer_Option::Bloom, "Controls the blend factor. If zero, then bloom is disabled", 0.01f);

            // motion blur
            option_check_box("Motion blur (controlled by the camera's shutter speed)", Renderer_Option::MotionBlur);

            // depth of field
            option_check_box("Depth of field (controlled by the camera's aperture)", Renderer_Option::DepthOfField);

            // chromatic aberration
            option_check_box("Chromatic aberration (controlled by the camera's aperture)", Renderer_Option::ChromaticAberration, "Emulates the inability of old cameras to focus all colors in the same focal point");

            // film grain
            option_check_box("Film grain", Renderer_Option::FilmGrain);
        }

        if (option("Lights"))
        {
            // volumetric fog
            option_check_box("Volumetric fog", Renderer_Option::FogVolumetric, "Requires a light with shadows enabled");

            // screen space shadows
            option_check_box("Screen space shadows", Renderer_Option::ScreenSpaceShadows, "Requires a light with shadows enabled");

            // shadow resolution
            int resolution_shadow = Renderer::GetOption<int>(Renderer_Option::ShadowResolution);
            option_int("Shadow resolution", resolution_shadow);
            Renderer::SetOption(Renderer_Option::ShadowResolution, static_cast<float>(resolution_shadow));
        }

        if (option("Misc"))
        {
            option_value("Fog",      Renderer_Option::Fog, "Controls the density of the fog", 0.1f);
            option_value("Exposure", Renderer_Option::Exposure);

            // vsync
            option_check_box("VSync", Renderer_Option::Vsync, "Vertical Synchronization");

            // fps Limit
            {
                option_first_column();
                const FpsLimitType fps_limit_type = Timer::GetFpsLimitType();
                string label = "FPS Limit - " + string((fps_limit_type == FpsLimitType::FixedToMonitor) ? "Fixed to monitor" : (fps_limit_type == FpsLimitType::Unlocked ? "Unlocked" : "Fixed"));
                ImGui::Text(label.c_str());

                option_second_column();
                {
                    float fps_target = Timer::GetFpsLimit();
                    ImGui::PushItemWidth(width_input_numeric);
                    ImGui::InputFloat("##fps_limit", &fps_target, 0.0, 0.0f, "%.1f");
                    ImGui::PopItemWidth();
                    Timer::SetFpsLimit(fps_target);
                }
            }

            option_check_box("Performance Metrics",     Renderer_Option::PerformanceMetrics);
            option_check_box("Transform",               Renderer_Option::TransformHandle);
            option_check_box("Selection outline",       Renderer_Option::SelectionOutline);
            option_check_box("Lights",                  Renderer_Option::Lights);
            option_check_box("Grid",                    Renderer_Option::Grid);
            option_check_box("Picking ray",             Renderer_Option::PickingRay);
            option_check_box("Physics",                 Renderer_Option::Physics);
            option_check_box("AABBs",                   Renderer_Option::Aabb);
            option_check_box("Wireframe",               Renderer_Option::Wireframe);
            option_check_box("Occlusion Culling (WIP)", Renderer_Option::OcclusionCulling);
        }

        ImGui::EndTable();
    }
}
