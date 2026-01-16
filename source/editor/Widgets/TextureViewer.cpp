/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "TextureViewer.h"
#include "../ImGui/ImGui_Extension.h"
//===================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    spartan::RHI_Texture* texture_current = nullptr;
    uint32_t m_texture_index              = 0;
    int mip_level                         = 0;
    int array_level                       = 0;
    bool m_magnifying_glass               = false;
    bool m_channel_r                      = true;
    bool m_channel_g                      = true;
    bool m_channel_b                      = true;
    bool m_channel_a                      = true;
    bool m_gamma_correct                  = true;
    bool m_pack                           = false;
    bool m_boost                          = false;
    bool m_abs                            = false;
    bool m_point_sampling                 = false;
    float zoom_level                      = 1.0f;
    ImVec2 pan_offset                     = ImVec2(0.0f, 0.0f);
    uint32_t m_visualisation_flags        = 0;
    vector<string> render_target_names;
    vector<spartan::RHI_Texture*> render_targets;}

TextureViewer::TextureViewer(Editor* editor) : Widget(editor)
{
    m_title   = "Texture Viewer";
    m_visible = false;
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
    for (const shared_ptr<spartan::RHI_Texture>& render_target : Renderer::GetRenderTargets())
    {
        if (render_target)
        {
            render_target_names.emplace_back(render_target->GetObjectName());
            render_targets.emplace_back(render_target.get());
        }
    }
}

void TextureViewer::OnTickVisible()
{
    if (render_targets.empty())
        return;

    // two columns: left for preview, right for properties
    ImGui::Columns(2, "texture_viewer_columns", false);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.65f);

    //=====================================
    // preview (left)
    //=====================================
    {
        ImGui::BeginChild("texture_preview", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        ImVec2 child_pos  = ImGui::GetCursorScreenPos();
        ImVec2 child_size = ImGui::GetContentRegionAvail();
        
        // draw black border around the preview
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRect(child_pos, ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y), IM_COL32(0, 0, 0, 255), 0.0f, 0, 2.0f);
        
        if (spartan::RHI_Texture* texture = render_targets[m_texture_index])
        {
            texture_current = texture;
        
            float tex_w = static_cast<float>(texture->GetWidth());
            float tex_h = static_cast<float>(texture->GetHeight());
            float aspect = tex_w / tex_h;
        
            float avail_w = child_size.x;
            float avail_h = child_size.y;
        
            float fit_w = avail_w;
            float fit_h = avail_w / aspect;
            if (fit_h > avail_h)
            {
                fit_h = avail_h;
                fit_w = avail_h * aspect;
            }
        
            float draw_w = fit_w * zoom_level;
            float draw_h = fit_h * zoom_level;
        
            ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
            ImVec2 image_pos = ImVec2(cursor_pos.x + pan_offset.x, cursor_pos.y + pan_offset.y);
        
            ImGui::SetCursorScreenPos(image_pos);
            ImGuiSp::image(texture, Vector2(draw_w, draw_h), ImColor(255, 255, 255, 255), ImColor(40, 40, 40, 255));
            ImGui::SetCursorScreenPos(cursor_pos);
        
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 mouse_delta = io.MouseDelta;
        
            if (ImGui::IsWindowHovered())
            {
                if (io.MouseWheel != 0.0f)
                {
                    float prev_zoom = zoom_level;
                    zoom_level *= (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
                    zoom_level = clamp(zoom_level, 0.05f, 8.0f);
        
                    ImVec2 mouse_pos = io.MousePos;
                    ImVec2 rel = ImVec2(mouse_pos.x - image_pos.x, mouse_pos.y - image_pos.y);
                    pan_offset.x -= rel.x * (zoom_level / prev_zoom - 1.0f);
                    pan_offset.y -= rel.y * (zoom_level / prev_zoom - 1.0f);
                }
        
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                {
                    pan_offset.x += mouse_delta.x;
                    pan_offset.y += mouse_delta.y;
                }
        
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
                {
                    zoom_level = 1.0f;
                    pan_offset = ImVec2(0, 0);
                }
            }
        
            ImGui::Text("Zoom (Wheel): %.0f%%", zoom_level * 100.0f);
            ImGui::Text("Pan (Middle Click + Drag): %.0f, %.0f", pan_offset.x, pan_offset.y);
        }
        
        ImGui::EndChild();
    }

    ImGui::NextColumn();

    //=====================================
    // properties (right)
    //=====================================
    {
        ImGui::BeginChild("texture_properties", ImVec2(0, 0), true);

        // target selector
        ImGui::Text("Texture");
        ImGui::SameLine();
        ImGuiSp::combo_box("##texture", render_target_names, &m_texture_index);

        if (texture_current)
        {
            // info
            if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Name: %s", texture_current->GetObjectName().c_str());
                ImGui::Text("Size: %dx%d", texture_current->GetWidth(), texture_current->GetHeight());
                ImGui::Text("Channels: %d", texture_current->GetChannelCount());
                ImGui::Text("Format: %s", rhi_format_to_string(texture_current->GetFormat()));
                ImGui::Text("Mips: %d", texture_current->GetMipCount());
                ImGui::Text("Array: %d", texture_current->GetDepth());
            }

            // mip and array sliders
            if (texture_current->GetMipCount() > 1)
            {
                ImGui::SliderInt("Mip Level", &mip_level, 0, static_cast<int>(texture_current->GetMipCount()) - 1);
            }
            if (texture_current->GetDepth() > 1)
            {
                ImGui::SliderInt("Array Level", &array_level, 0, static_cast<int>(texture_current->GetDepth()) - 1);
            }

            // channels
            if (ImGui::CollapsingHeader("Channels", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Red",   &m_channel_r);
                ImGui::Checkbox("Green", &m_channel_g);
                ImGui::Checkbox("Blue",  &m_channel_b);
                ImGui::Checkbox("Alpha", &m_channel_a);
            }

            // visualisation
            if (ImGui::CollapsingHeader("Visualization", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Gamma Correct", &m_gamma_correct);
                ImGui::Checkbox("Pack from [-1, 1] to [0, 1]", &m_pack);
                ImGui::Checkbox("Boost", &m_boost);
                ImGui::Checkbox("Abs", &m_abs);
                ImGui::Checkbox("Point Sampling", &m_point_sampling);
            }
        }

        ImGui::EndChild();
    }

    ImGui::Columns(1);

    // update flags
    m_visualisation_flags = 0;
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
