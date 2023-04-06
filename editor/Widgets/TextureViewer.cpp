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
#include "TextureViewer.h"
#include "RHI/RHI_Texture.h"
#include "../ImGui/ImGuiExtension.h"
//==================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

namespace
{
    static string name;
    static RHI_Format rhi_format    = RHI_Format::Undefined;
    static uint32_t m_texture_index = 1;
    static int mip_level            = 0;
    static int  mip_count           = 0;
    static bool m_magnifying_glass  = false;
    static bool m_channel_r         = true;
    static bool m_channel_g         = true;
    static bool m_channel_b         = true;
    static bool m_channel_a         = true;
    static bool m_gamma_correct     = false;
    static bool m_pack              = false;
    static bool m_boost             = false;
    static bool m_abs               = false;
    static bool m_point_sampling    = false;
    static uint32_t width           = 0;
    static uint32_t height          = 0;
    static uint32_t channel_count   = 0;
    static uint64_t texture_id    = 0;

    // When editing this, make sure that the bit shifts in common_buffer.hlsl are also updated.
    enum VisualisationOptions
    {
        Visualise_Pack         = 1U << 0,
        Visualise_GammaCorrect = 1U << 1,
        Visualise_Boost        = 1U << 2,
        Visualise_Abs          = 1U << 3,
        Visualise_Channel_R    = 1U << 4,
        Visualise_Channel_G    = 1U << 5,
        Visualise_Channel_B    = 1U << 6,
        Visualise_Channel_A    = 1U << 7,
        Visualise_Sample_Point = 1U << 8,
    };
    static uint32_t m_visualisation_flags = 0;
}

TextureViewer::TextureViewer(Editor* editor) : Widget(editor)
{
    m_title    = "Texture Viewer";
    m_visible  = false;
    m_position = k_widget_position_screen_center;
    m_size_min = Vector2(720, 576);
}

void TextureViewer::TickAlways()
{
    m_visualisation_flags = 0;
    texture_id          = 0;
}

void TextureViewer::TickVisible()
{
    // Get render targets
    static vector<string> render_target_options;
    if (render_target_options.empty())
    {
        render_target_options.emplace_back("None");
        for (const shared_ptr<RHI_Texture>& render_target : Renderer::GetRenderTargets())
        {
            if (render_target)
            {
                render_target_options.emplace_back(render_target->GetName());
            }
        }
    }

    // Display the selected texture
    if (shared_ptr<RHI_Texture> texture = Renderer::GetRenderTarget(static_cast<RendererTexture>(m_texture_index)))
    {
        // Calculate a percentage that once multiplied with the texture dimensions, the texture will always be displayed within the window.
        float bottom_padding              = 200.0f; // to fit the information text
        float texture_shrink_percentage_x = ImGui::GetWindowWidth() / static_cast<float>(texture->GetWidth()) * 0.95f; // 0.95 to avoid not be hidden by the scroll bar
        float texture_shrink_percentage_y = ImGui::GetWindowHeight() / static_cast<float>(texture->GetHeight() + bottom_padding);
        float texture_shrink_percentage   = min(texture_shrink_percentage_x, texture_shrink_percentage_y);

        // Texture
        float virtual_width = static_cast<float>(texture->GetWidth()) * texture_shrink_percentage;
        float virtual_height = static_cast<float>(texture->GetHeight()) * texture_shrink_percentage;
        ImGui_SP::image(texture.get(), Vector2(virtual_width, virtual_height), ImColor(255, 255, 255, 255), ImColor(0, 0, 0, 255));

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
                region_x = clamp(region_x, 0.0f, virtual_width - region_sz);
                region_y = clamp(region_y, 0.0f, virtual_height - region_sz);

                ImVec2 uv0 = ImVec2(region_x / virtual_width, region_y / virtual_height);
                ImVec2 uv1 = ImVec2((region_x + region_sz) / virtual_width, (region_y + region_sz) / virtual_height);
                ImGui::Image(static_cast<ImTextureID>(texture.get()), ImVec2(region_sz * zoom, region_sz * zoom), uv0, uv1, tint_col, border_col);
            }
            ImGui::EndTooltip();
        }

        // Disable for now as it's buggy.
        //ImGui::Checkbox("Magnifying glass", &m_magnifying_glass);

        name          = texture->GetName();
        width         = texture->GetWidth();
        height        = texture->GetHeight();
        mip_count     = texture->GetMipCount();
        channel_count = texture->GetChannelCount();
        rhi_format    = texture->GetFormat();
        texture_id    = texture->GetObjectId();
    }

    // Render target
    ImGui::Text("Render target");
    ImGui::SameLine();
    ImGui_SP::combo_box("##render_target", render_target_options, &m_texture_index);

    // Mip level
    ImGui::SameLine();
    ImGui::PushItemWidth(100);
    ImGui::InputInt("Mip", &mip_level);
    mip_level = Math::Helper::Clamp(mip_level, 0, static_cast<int>(mip_count) - 1);
    ImGui::PopItemWidth();

    // Properties
    ImGui::BeginGroup();
    {
        // Information
        ImGui::BeginGroup();
        ImGui::Text("Name: %s",          name.c_str());
        ImGui::Text("Dimensions: %dx%d", width, height);
        ImGui::Text("Channels: %d",      channel_count);
        ImGui::Text("Format: %s",        rhi_format_to_string(rhi_format));
        ImGui::Text("Mips: %d",          mip_count);
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
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Checkbox("Abs", &m_abs);
        ImGui::Checkbox("Point sampling", &m_point_sampling);
        ImGui::EndGroup();
    }
    ImGui::EndGroup();
    
    // Map changes
    m_visualisation_flags |=  m_channel_r      ? Visualise_Channel_R    : 0;
    m_visualisation_flags |=  m_channel_g      ? Visualise_Channel_G    : 0;
    m_visualisation_flags |=  m_channel_b      ? Visualise_Channel_B    : 0;
    m_visualisation_flags |=  m_channel_a      ? Visualise_Channel_A    : 0;
    m_visualisation_flags |=  m_gamma_correct  ? Visualise_GammaCorrect : 0;
    m_visualisation_flags |=  m_pack           ? Visualise_Pack         : 0;
    m_visualisation_flags |=  m_boost          ? Visualise_Boost        : 0;
    m_visualisation_flags |=  m_abs            ? Visualise_Abs          : 0;
    m_visualisation_flags |=  m_point_sampling ? Visualise_Sample_Point : 0;

}

uint32_t TextureViewer::GetVisualisationFlags()
{
    return m_visualisation_flags;
}

int TextureViewer::GetMipLevel()
{
    return mip_level;
}

uint64_t TextureViewer::GetVisualisedTextureId()
{
    return texture_id;
}
