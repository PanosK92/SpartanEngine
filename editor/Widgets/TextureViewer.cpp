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

//= INCLUDES =======================
#include "TextureViewer.h"
#include "Rendering/Renderer.h"
#include "RHI/RHI_Texture.h"
#include "../ImGui/ImGuiExtension.h"
//==================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

TextureViewer::TextureViewer(Editor* editor) : Widget(editor)
{
    m_title    = "Texture Viewer";
    m_visible  = false;
    m_position = k_widget_position_screen_center;
    m_size_min = Vector2(720, 576);
}

void TextureViewer::TickVisible()
{
    m_renderer = m_context->GetSystem<Renderer>();

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

    // Display them in a combo box.
    ImGui::Text("Render target");
    ImGui::SameLine();
    imgui_extension::combo_box("##render_target", render_target_options, &m_texture_index);

    // Display the selected texture
    if (shared_ptr<RHI_Texture> texture = m_renderer->GetRenderTarget(static_cast<RendererTexture>(m_texture_index)))
    {
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise);

        // Calculate a percentage that once multiplied with the texture dimensions, the texture will always be displayed within the window.
        float bottom_padding              = 200.0f; // to fit the information text
        float texture_shrink_percentage_x = ImGui::GetWindowWidth() / static_cast<float>(texture->GetWidth());
        float texture_shrink_percentage_y = ImGui::GetWindowHeight() / static_cast<float>(texture->GetHeight() + bottom_padding);
        float texture_shrink_percentage   = min(texture_shrink_percentage_x, texture_shrink_percentage_y);

        // Texture
        float width  = static_cast<float>(texture->GetWidth()) * texture_shrink_percentage;
        float height = static_cast<float>(texture->GetHeight()) * texture_shrink_percentage;
        bool border  = true;
        imgui_extension::image(texture.get(), Vector2(width, height), border);

        // Magnifying glass
        if (m_magnifying_glass && ImGui::IsItemHovered())
        {
            const float region_sz   = 32.0f;
            const float zoom        = 16.0f;
            const ImVec4 tint_col   = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // No tint
            const ImVec4 border_col = ImVec4(1.0f, 1.0f, 1.0f, 0.5f); // 50% opaque white

            ImVec2 pos     = ImGui::GetCursorScreenPos();
            ImGuiIO& io    = ImGui::GetIO();
            float region_x = io.MousePos.x - pos.x - region_sz * 0.5f;
            float region_y = io.MousePos.y - pos.y - region_sz * 0.5f;

            ImGui::BeginTooltip();
            {
                region_x = clamp(region_x, 0.0f, width - region_sz);
                region_y = clamp(region_y, 0.0f, height - region_sz);

                ImVec2 uv0 = ImVec2(region_x / width, region_y / height);
                ImVec2 uv1 = ImVec2((region_x + region_sz) / width, (region_y + region_sz) / height);
                ImGui::Image(static_cast<ImTextureID>(texture.get()), ImVec2(region_sz * zoom, region_sz * zoom), uv0, uv1, tint_col, border_col);
            }
            ImGui::EndTooltip();
        }

        // Disable for now as it's buggy.
        //ImGui::Checkbox("Magnifying glass", &m_magnifying_glass);

        // Properties
        ImGui::BeginGroup();
        {
            // Information
            ImGui::BeginGroup();
            ImGui::Text("Name: %s",          texture->GetName().c_str());
            ImGui::Text("Dimensions: %dx%d", texture->GetWidth(), texture->GetHeight());
            ImGui::Text("Channels: %d",      texture->GetChannelCount());
            ImGui::Text("Format: %s",        string(rhi_format_to_string(texture->GetFormat())).c_str());
            ImGui::EndGroup();

            // Channels
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("Channels");
            ImGui::Checkbox("R", &m_channel_r);
            ImGui::Checkbox("G", &m_channel_g);
            ImGui::Checkbox("B", &m_channel_b);
            ImGui::Checkbox("A", &m_channel_a);
            ImGui::EndGroup();

            // Misc
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Checkbox("Gamma correct", &m_gamma_correct);
            ImGui::Checkbox("Pack", &m_pack);
            ImGui::Checkbox("Boost", &m_boost);
            ImGui::Checkbox("Abs", &m_abs);
            ImGui::Checkbox("Point sampling", &m_point_sampling);
            ImGui::EndGroup();
        }
        ImGui::EndGroup();

        // Map changes to texture
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Channel_R,    m_channel_r);
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Channel_G,    m_channel_g);
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Channel_B,    m_channel_b);
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Channel_A,    m_channel_a);
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_GammaCorrect, m_gamma_correct);
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Pack,         m_pack);
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Boost,        m_boost);
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Abs,          m_abs);
        texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Sample_Point, m_point_sampling);
    }
}
