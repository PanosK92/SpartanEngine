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
#include "TextureViewer.h"
#include "../ImGui/ImGuiExtension.h"
#include "World/Components/Light.h"
#include "World/Entity.h"
//==================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

namespace
{
    RHI_Texture* texture_current   = nullptr;
    uint32_t m_texture_index       = 0;
    int mip_level                  = 0;
    int array_level                = 0;
    bool m_magnifying_glass        = false;
    bool m_channel_r               = true;
    bool m_channel_g               = true;
    bool m_channel_b               = true;
    bool m_channel_a               = true;
    bool m_gamma_correct           = true;
    bool m_pack                    = false;
    bool m_boost                   = false;
    bool m_abs                     = false;
    bool m_point_sampling          = false;
    uint32_t m_visualisation_flags = 0;
    vector<string> render_target_names;
    vector<RHI_Texture*> render_targets;

    void show_texture(RHI_Texture* texture)
    {
        // calculate a percentage that once multiplied with the texture dimensions, the texture will always be displayed within the window.
        float bottom_padding              = 200.0f * Spartan::Window::GetDpiScale(); // to fit the information text
        float texture_shrink_percentage_x = ImGui::GetWindowWidth() / static_cast<float>(texture->GetWidth()) * 0.95f; // 0.95 to avoid not be hidden by the scroll bar
        float texture_shrink_percentage_y = ImGui::GetWindowHeight() / static_cast<float>(texture->GetHeight() + bottom_padding);
        float texture_shrink_percentage   = min(texture_shrink_percentage_x, texture_shrink_percentage_y);
        
        // texture
        float virtual_width  = static_cast<float>(texture->GetWidth()) * texture_shrink_percentage;
        float virtual_height = static_cast<float>(texture->GetHeight()) * texture_shrink_percentage;
        ImGuiSp::image(texture, Vector2(virtual_width, virtual_height), ImColor(255, 255, 255, 255), ImColor(0, 0, 0, 255));
        
        // magnifying glass
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
                ImGui::Image(static_cast<ImTextureID>(texture), ImVec2(region_sz * zoom, region_sz * zoom), uv0, uv1, tint_col, border_col);
            }
            ImGui::EndTooltip();
        }
        
        // disable for now as it's buggy
        //ImGui::Checkbox("Magnifying glass", &m_magnifying_glass);

        texture_current = texture;
    }
}

TextureViewer::TextureViewer(Editor* editor) : Widget(editor)
{
    m_title    = "Texture Viewer";
    m_visible  = false;
    m_size_min = Vector2(720, 576);
}

void TextureViewer::OnTick()
{
    m_visualisation_flags = 0;
    texture_current       = nullptr;
}

void TextureViewer::OnVisible()
{
    render_target_names.clear();
    render_targets.clear();

    // get render targets
    {
        // renderer
        for (const shared_ptr<RHI_Texture>& render_target : Renderer::GetRenderTargets())
        {
            if (render_target)
            {
                render_target_names.emplace_back(render_target->GetObjectName());
                render_targets.emplace_back(render_target.get());
            }
        }

        // lights
        for (shared_ptr<Entity>& entity : Renderer::GetEntitiesLights())
        {
            if (shared_ptr<Light> light = entity->GetComponent<Light>())
            {
                if (RHI_Texture* depth_map = light->GetDepthTexture())
                {
                    render_target_names.emplace_back(depth_map->GetObjectName() + "_" + to_string(depth_map->GetObjectId()));
                    render_targets.emplace_back(depth_map);
                }
            }
        }
    }
}

void TextureViewer::OnTickVisible()
{
    // texture
    ImGui::BeginGroup();
    {
        if (RHI_Texture* texture = render_targets[m_texture_index])
        {
            show_texture(texture);
        }
    }
    ImGui::EndGroup();

    // properties
    ImGui::BeginGroup();
    {
        // render target
        ImGui::Text("Render target");
        ImGui::SameLine();
        ImGuiSp::combo_box("##render_target", render_target_names, &m_texture_index);

        // mip level control
        if (texture_current->GetMipCount() > 1)
        {
            ImGui::SameLine();
            ImGui::PushItemWidth(85 * Spartan::Window::GetDpiScale());
            ImGui::InputInt("Mip", &mip_level);
            ImGui::PopItemWidth();
            mip_level = Math::Helper::Clamp(mip_level, 0, static_cast<int>(texture_current->GetMipCount()) - 1);
        }

        // array level control
        if (texture_current->GetArrayLength() > 1)
        {
            ImGui::SameLine();
            ImGui::PushItemWidth(85 * Spartan::Window::GetDpiScale());
            ImGui::InputInt("Array", &array_level);
            ImGui::PopItemWidth();
            array_level = Math::Helper::Clamp(array_level, 0, static_cast<int>(texture_current->GetArrayLength()) - 1);
        }

        ImGui::BeginGroup();
        {
            // information
            ImGui::BeginGroup();
            ImGui::Text("Name: %s",          texture_current->GetObjectName().c_str());
            ImGui::Text("Dimensions: %dx%d", texture_current->GetWidth(), texture_current->GetHeight());
            ImGui::Text("Channels: %d",      texture_current->GetChannelCount());
            ImGui::Text("Format: %s",        rhi_format_to_string(texture_current->GetFormat()));
            ImGui::Text("Mips: %d",          texture_current->GetMipCount());
            ImGui::Text("Array: %d",         texture_current->GetArrayLength());
            ImGui::EndGroup();
        
            // channels
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("Channels");
            ImGui::Checkbox("R", &m_channel_r);
            ImGui::Checkbox("G", &m_channel_g);
            ImGui::Checkbox("B", &m_channel_b);
            ImGui::Checkbox("A", &m_channel_a);
            ImGui::EndGroup();
        
            // misc
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
    }
    ImGui::EndGroup();
    
    // map changes
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

int TextureViewer::GetArrayLevel()
{
    return array_level;
}

uint64_t TextureViewer::GetVisualisedTextureId()
{
    return texture_current ? texture_current->GetObjectId() : 0;
}
