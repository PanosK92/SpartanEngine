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

//= INCLUDES ==============================
#include "pch.h"
#include "FileDialog.h"
#include "../ImGui/Source/imgui_internal.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "../ImGui/ImGui_Style.h"
#include "../Widgets/Viewport.h"
#include <Rendering/Material.h>

#include "World/Entity.h"
#include "World/Components/Script.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    #define OPERATION_NAME (m_operation == FileDialog_Op_Open) ? "Open"      : (m_operation == FileDialog_Op_Load)   ? "Load"        : (m_operation == FileDialog_Op_Save) ? "Save" : "View"
    #define FILTER_NAME    (m_filter == FileDialog_Filter_All) ? "All (*.*)" : (m_filter == FileDialog_Filter_Model) ? "Model (*.*)" : "World (*.world)"

    // visual configuration
    const float item_size_min       = 50.0f;
    const float item_size_max       = 200.0f;
    const float card_rounding       = 6.0f;
    const float toolbar_height      = 36.0f;
    const float breadcrumb_height   = 28.0f;
    const float search_bar_height   = 32.0f;
    const float grid_item_padding   = 8.0f;
    const float list_row_height     = 28.0f;
    const float icon_button_size    = 24.0f;
    const float status_bar_height   = 26.0f;
    const float bottom_panel_height = 44.0f;

    // colors - will be derived from style
    ImU32 col_card_bg;
    ImU32 col_card_bg_hover;
    ImU32 col_card_bg_selected;
    ImU32 col_card_border;
    ImU32 col_card_border_hover;
    ImU32 col_shadow;
    ImU32 col_accent;
    ImU32 col_text;
    ImU32 col_text_dim;
    ImU32 col_toolbar_bg;
    ImU32 col_separator;

    constexpr std::string_view NewLuaScriptContents = R"(

-- ================================================================
-- Spartan Lua Script Prelude
-- ================================================================
-- Lua is a lightweight scripting language for game logic.
-- The Lua API in Spartan mirrors the C++ API:
--   - Functions called in Lua have the same names and return types as C++.
--   - Component queries use enums, e.g.: self:GetComponent(ComponentTypes.Light)
--   - Colon syntax (:) automatically passes 'self'.
--   - Dot syntax (.) accesses fields or tables on self.
--
-- Lua reference: https://www.lua.org/manual/5.4/manual.html
--
-- This is a template script. All functions are empty.
-- ================================================================

-- Create the script table. Must be returned at the end.
MyScript = {}

-- ================================================================
-- Simulation lifecycle callbacks
-- ================================================================

-- Called once when the simulation starts.
function MyScript:Start()
    -- Place initialization logic here
end

-- Called once when the simulation stops.
function MyScript:Stop()
    -- Place shutdown logic here
end

-- Called when the script component is removed from the entity.
function MyScript:Remove()
    -- Cleanup logic here
end

-- ================================================================
-- Per-frame callbacks
-- ================================================================

-- Called every frame before Tick. Useful to reset temporary states.
function MyScript:PreTick()
    -- Pre-update logic here
end

-- Called every frame. Main update function.
function MyScript:Tick()
    -- Frame update logic here
end

-- ================================================================
-- Serialization callbacks
-- ================================================================

-- Called when the entity is being saved.
function MyScript:Save()
    -- Return a table with any custom data to save
end

-- Called when the entity is being loaded.
function MyScript:Load(data)
    -- Restore data from the table returned by Save
end

-- ================================================================
-- Return the script table to Spartan
-- ================================================================
return MyScript
)";

    void update_colors()
    {
        ImGuiStyle& style     = ImGui::GetStyle();
        col_card_bg           = ImGui::ColorConvertFloat4ToU32(ImVec4(0.12f, 0.12f, 0.13f, 1.0f));
        col_card_bg_hover     = ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.18f, 0.20f, 1.0f));
        col_card_bg_selected  = ImGui::ColorConvertFloat4ToU32(ImVec4(0.15f, 0.25f, 0.35f, 1.0f));
        col_card_border       = ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
        col_card_border_hover = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_CheckMark]);
        col_shadow            = IM_COL32(0, 0, 0, 50);
        col_accent            = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_CheckMark]);
        col_text              = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);
        col_text_dim          = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_TextDisabled]);
        col_toolbar_bg        = ImGui::ColorConvertFloat4ToU32(ImVec4(0.15f, 0.15f, 0.16f, 1.0f));
        col_separator         = ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
    }
}

FileDialog::FileDialog(const bool standalone_window, const FileDialog_Type type, const FileDialog_Operation operation, const FileDialog_Filter filter)
{
    m_type                            = type;
    m_operation                       = operation;
    m_filter                          = filter;
    m_title                           = OPERATION_NAME;
    m_is_window                       = standalone_window;
    m_item_size                       = 100.0f;
    m_is_dirty                        = true;
    m_selection_made                  = false;
    m_callback_on_item_clicked        = nullptr;
    m_callback_on_item_double_clicked = nullptr;
    m_current_path                    = ResourceCache::GetProjectDirectory();
    m_root_path                       = "..";
    m_sort_column                     = Sort_Name;
    m_sort_ascending                  = true;
    m_view_mode                       = View_Grid;
    m_history_index                   = 0;
    m_history.push_back(m_current_path);
    m_selected_item_id                = UINT32_MAX;
    m_hover_animation                 = 0.0f;
}

void FileDialog::SetOperation(const FileDialog_Operation operation)
{
    m_operation = operation;
    m_title     = OPERATION_NAME;
}

void FileDialog::SetCurrentPath(const string& path)
{
    if (FileSystem::IsFile(path))
    {
        m_current_path = FileSystem::GetDirectoryFromFilePath(path);
    }
    else if (FileSystem::IsDirectory(path))
    {
        m_current_path = path;
    }

    if (!m_current_path.empty())
    {
        m_is_dirty = true;
        m_history.push_back(m_current_path);
        m_history_index = m_history.size() - 1;
    }
}

bool FileDialog::Show(bool* is_visible, Editor* editor, string* directory /*= nullptr*/, string* file_path /*= nullptr*/)
{
    if (!(*is_visible))
    {
        m_is_dirty = true;
        return false;
    }

    update_colors();

    m_selection_made     = false;
    m_is_hovering_item   = false;
    m_is_hovering_window = false;
    
    // calculate bottom offset before rendering so ShowMiddle knows the available space
    if (m_type == FileDialog_Type_Browser)
    {
        m_offset_bottom = status_bar_height * spartan::Window::GetDpiScale();
    }
    else
    {
        m_offset_bottom = bottom_panel_height * spartan::Window::GetDpiScale();
    }

    ShowTop(is_visible, editor);
    ShowMiddle();
    ShowBottom(is_visible);

    if (m_is_window)
    {
        ImGui::End();
    }

    if (m_is_dirty)
    {
        if (FileSystem::IsFile(m_current_path))
        {
            DialogUpdateFromDirectory(FileSystem::GetDirectoryFromFilePath(m_current_path));
        }
        else
        {
            DialogUpdateFromDirectory(m_current_path);
        }
        m_is_dirty = false;
    }

    if (m_selection_made)
    {
        if (directory)
        {
            (*directory) = m_current_path;
        }
        if (file_path)
        {
            string dir = m_current_path;
            if (FileSystem::IsFile(m_current_path))
            {
                dir = FileSystem::GetDirectoryFromFilePath(m_current_path);
            }

            // ensure there's a separator between directory and filename
            if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
            {
                dir += "/";
            }
            (*file_path) = dir + m_input_box;
        }
    }

    EmptyAreaContextMenu();
    HandleKeyboardNavigation();

    return m_selection_made;
}

void FileDialog::ShowTop(bool* is_visible, Editor* editor)
{
    if (m_is_window)
    {
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(700, 500), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin(m_title.c_str(), is_visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);
        ImGui::PopStyleVar();
        ImGui::SetWindowFocus();
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float window_width    = ImGui::GetContentRegionAvail().x;

    // consistent style for toolbar
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

    // for standalone window, draw toolbar background and position from left
    if (m_is_window)
    {
        ImVec2 window_pos = ImGui::GetCursorScreenPos();
        draw_list->AddRectFilled(
            window_pos,
            ImVec2(window_pos.x + window_width, window_pos.y + toolbar_height),
            col_toolbar_bg
        );
        
        float button_height = ImGui::GetFrameHeight();
        float vertical_pad  = (toolbar_height - button_height) * 0.5f;
        ImGui::SetCursorPos(ImVec2(8, vertical_pad));
    }

    float button_height = ImGui::GetFrameHeight();

    // navigation buttons style
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.18f));

    // navigation: back button
    bool can_go_back = m_history_index > 0;
    ImGui::BeginDisabled(!can_go_back);
    if (ImGui::Button("Back"))
    {
        m_history_index--;
        m_current_path = m_history[m_history_index];
        m_is_dirty     = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("alt+left");
    ImGui::EndDisabled();
    ImGui::SameLine();

    // navigation: forward button
    bool can_go_forward = m_history_index < m_history.size() - 1;
    ImGui::BeginDisabled(!can_go_forward);
    if (ImGui::Button("Forward"))
    {
        m_history_index++;
        m_current_path = m_history[m_history_index];
        m_is_dirty     = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("alt+right");
    ImGui::EndDisabled();
    ImGui::SameLine();

    // navigation: up button
    if (ImGui::Button("Up"))
    {
        string parent = FileSystem::GetParentDirectory(m_current_path);
        if (!parent.empty() && parent != m_current_path)
        {
            m_current_path = parent;
            m_history.push_back(m_current_path);
            m_history_index = m_history.size() - 1;
            m_is_dirty      = true;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("alt+up");
    ImGui::SameLine();

    // navigation: refresh button
    if (ImGui::Button("Refresh"))
    {
        m_is_dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("f5");
    
    ImGui::SameLine(0, 12);

    // vertical separator
    {
        ImVec2 sep_pos = ImGui::GetCursorScreenPos();
        draw_list->AddLine(
            ImVec2(sep_pos.x, sep_pos.y + 2),
            ImVec2(sep_pos.x, sep_pos.y + button_height - 2),
            col_separator, 1.0f
        );
        ImGui::Dummy(ImVec2(1, button_height));
        ImGui::SameLine(0, 12);
    }

    // breadcrumb navigation
    {
        char accumulated_path[1024];
        accumulated_path[0] = '\0';

        char current_path[1024];
        strncpy_s(current_path, sizeof(current_path), m_current_path.c_str(), _TRUNCATE);

        const char* delimiters = "/\\";
        char* context          = nullptr;
        char* token            = strtok_s(current_path, delimiters, &context);
        bool first             = true;
        int segment_count      = 0;

        while (token)
        {
            if (strcmp(token, "..") == 0)
            {
                token = strtok_s(nullptr, delimiters, &context);
                continue;
            }

            if (first)
            {
                snprintf(accumulated_path, sizeof(accumulated_path), "%s/", token);
                first = false;
            }
            else
            {
                strncat_s(accumulated_path, sizeof(accumulated_path), token, _TRUNCATE);
                strncat_s(accumulated_path, sizeof(accumulated_path), "/", _TRUNCATE);
            }

            // chevron separator between breadcrumbs
            if (segment_count > 0)
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "/");
                ImGui::SameLine();
            }

            // breadcrumb button
            ImGui::PushID(segment_count);
            if (ImGui::Button(token))
            {
                m_current_path = accumulated_path;
                m_history.push_back(m_current_path);
                m_history_index = m_history.size() - 1;
                m_is_dirty      = true;
            }
            ImGui::PopID();
            ImGui::SameLine();

            segment_count++;
            token = strtok_s(nullptr, delimiters, &context);
        }
    }

    // right side: view toggle and size slider (snapped to right edge)
    {
        // save current mode before buttons (mode may change during button click)
        bool is_grid_mode = (m_view_mode == View_Grid);
        bool is_list_mode = (m_view_mode == View_List);
        
        // calculate button widths
        float grid_btn_w  = ImGui::CalcTextSize("Grid").x + ImGui::GetStyle().FramePadding.x * 2;
        float list_btn_w  = ImGui::CalcTextSize("List").x + ImGui::GetStyle().FramePadding.x * 2;
        float slider_width = is_grid_mode ? 80.0f : 0.0f;
        float slider_gap   = is_grid_mode ? 8.0f : 0.0f;
        float item_spacing = ImGui::GetStyle().ItemSpacing.x;
        float total_width  = grid_btn_w + item_spacing + list_btn_w + slider_gap + slider_width;
        
        // position from right edge of window
        float window_w = ImGui::GetWindowWidth();
        float right_x  = window_w - total_width - 8.0f;
        ImGui::SetCursorPosX(right_x);

        // grid view button
        if (is_grid_mode)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.15f));
        if (ImGui::Button("Grid"))
        {
            m_view_mode = View_Grid;
        }
        if (is_grid_mode)
            ImGui::PopStyleColor();
        ImGui::SameLine();

        // list view button
        if (is_list_mode)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.15f));
        if (ImGui::Button("List"))
        {
            m_view_mode = View_List;
        }
        if (is_list_mode)
            ImGui::PopStyleColor();

        // size slider (grid view only)
        if (is_grid_mode)
        {
            ImGui::SameLine(0, slider_gap);
            ImGui::SetNextItemWidth(slider_width);
            ImGui::SliderFloat("##size", &m_item_size.x, item_size_min, item_size_max, "");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("icon size: %.0f", m_item_size.x);
            }
        }
    }

    // pop toolbar styles (3 colors for buttons, 3 style vars)
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);

    // spacing after toolbar
    ImGui::Dummy(ImVec2(0, 4));
    
    // search bar row
    {
        ImGui::SetCursorPosX(8);
        
        // search input
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        
        float search_width = ImGui::GetContentRegionAvail().x - 16;
        if (m_type != FileDialog_Type_Browser)
        {
            search_width -= 120; // space for filter dropdown
        }
        
        ImGui::SetNextItemWidth(search_width);
        
        // custom search field styling
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
        
        // placeholder handling
        bool empty = m_search_filter.InputBuf[0] == '\0';
        if (empty)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }
        
        m_search_filter.Draw("##search");
        
        if (empty)
        {
            ImGui::PopStyleColor();
            // draw placeholder text
            ImVec2 pos = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(pos.x + 8, pos.y + 6),
                IM_COL32(128, 128, 128, 180),
                "search files..."
            );
        }
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        // filter dropdown (file selection mode only)
        if (m_type != FileDialog_Type_Browser)
        {
            ImGui::SameLine(0, 8);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::SetNextItemWidth(100);
            if (ImGui::BeginCombo("##filter", FILTER_NAME))
            {
                if (ImGui::Selectable("All (*.*)", m_filter == FileDialog_Filter_All))
                {
                    m_filter   = FileDialog_Filter_All;
                    m_is_dirty = true;
                }
                if (ImGui::Selectable("Model (*.*)", m_filter == FileDialog_Filter_Model))
                {
                    m_filter   = FileDialog_Filter_Model;
                    m_is_dirty = true;
                }
                if (ImGui::Selectable("World (*.world)", m_filter == FileDialog_Filter_World))
                {
                    m_filter   = FileDialog_Filter_World;
                    m_is_dirty = true;
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
        }
    }

    // spacing before separator
    ImGui::Dummy(ImVec2(0, 8));
    
    // separator line
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2 sep_pos  = ImGui::GetCursorScreenPos();
    float sep_width = ImGui::GetContentRegionAvail().x;
    dl->AddLine(sep_pos, ImVec2(sep_pos.x + sep_width, sep_pos.y), col_separator);
    ImGui::Dummy(ImVec2(0, 1));
}

void FileDialog::ShowMiddle()
{
    const float content_width  = ImGui::GetContentRegionAvail().x;
    const float content_height = ImGui::GetContentRegionAvail().y - m_offset_bottom;
    ImGuiStyle& style          = ImGui::GetStyle();
    m_displayed_item_count     = 0;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.09f, 0.10f, 1.0f));
    
    if (ImGui::BeginChild("##content", ImVec2(content_width, content_height), false))
    {
        m_is_hovering_window = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (m_view_mode == View_List)
        {
            RenderListView();
        }
        else
        {
            RenderGridView();
        }
    }
    ImGui::EndChild();
    
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void FileDialog::RenderGridView()
{
    const float content_width = ImGui::GetContentRegionAvail().x;
    const float icon_size     = m_item_size.x;
    const float label_height  = 20.0f;
    const float item_width    = icon_size + grid_item_padding * 2;
    const float item_height   = icon_size + label_height + grid_item_padding * 2;
    
    int columns = static_cast<int>((content_width - 16) / item_width);
    if (columns < 1) columns = 1;
    
    // initial padding
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(8.0f);
    
    lock_guard lock(m_mutex_items);
    int col = 0;
    bool first_in_row = true;
    
    for (size_t i = 0; i < m_items.size(); i++)
    {
        auto& item = m_items[i];
        if (!m_search_filter.PassFilter(item.GetLabel().c_str()))
            continue;
        
        m_displayed_item_count++;
        
        if (!first_in_row)
        {
            ImGui::SameLine(0, 4);
        }
        first_in_row = false;
        
        ImGui::PushID(static_cast<int>(i));
        
        ImVec2 screen_pos = ImGui::GetCursorScreenPos();
        
        // card dimensions
        ImVec2 card_min = screen_pos;
        ImVec2 card_max = ImVec2(screen_pos.x + item_width - 4, screen_pos.y + item_height - 4);
        
        // invisible button for interaction - this is the only item we submit
        ImGui::InvisibleButton("##card", ImVec2(item_width - 4, item_height - 4));
        bool is_hovered  = ImGui::IsItemHovered();
        bool is_selected = (m_selected_item_id == item.GetId());
        
        // handle drag
        ItemDrag(&item);
        
        // draw card using draw list (no cursor manipulation)
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        // shadow (subtle)
        if (is_hovered || is_selected)
        {
            draw_list->AddRectFilled(
                ImVec2(card_min.x + 2, card_min.y + 2),
                ImVec2(card_max.x + 2, card_max.y + 2),
                col_shadow,
                card_rounding
            );
        }
        
        // card background
        ImU32 bg_color = is_selected ? col_card_bg_selected : (is_hovered ? col_card_bg_hover : col_card_bg);
        draw_list->AddRectFilled(card_min, card_max, bg_color, card_rounding);
        
        // card border (on hover or selection)
        if (is_selected)
        {
            draw_list->AddRect(card_min, card_max, col_card_border_hover, card_rounding, 0, 2.0f);
        }
        else if (is_hovered)
        {
            draw_list->AddRect(card_min, card_max, col_card_border, card_rounding, 0, 1.0f);
        }
        
        // icon - draw directly to draw list
        float icon_area = icon_size - grid_item_padding;
        if (RHI_Texture* texture = item.GetIcon())
        {
            if (texture->GetResourceState() == ResourceState::PreparedForGpu)
            {
                ImVec2 img_size(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
                float scale = min(icon_area / img_size.x, icon_area / img_size.y);
                img_size.x *= scale;
                img_size.y *= scale;
                
                // center icon horizontally and vertically within icon area
                float img_x = card_min.x + (item_width - 4 - img_size.x) * 0.5f;
                float img_y = card_min.y + grid_item_padding + (icon_area - img_size.y) * 0.5f;
                
                draw_list->AddImage(
                    reinterpret_cast<ImTextureID>(texture),
                    ImVec2(img_x, img_y),
                    ImVec2(img_x + img_size.x, img_y + img_size.y)
                );
            }
        }
        
        // label - positioned below the icon area
        const string& label = item.GetLabel();
        ImVec2 text_size    = ImGui::CalcTextSize(label.c_str());
        float label_max_w   = item_width - grid_item_padding * 2;
        float label_x       = card_min.x + (item_width - 4 - min(text_size.x, label_max_w)) * 0.5f;
        float label_y       = card_min.y + grid_item_padding + icon_area + 4; // below icon
        
        // render with ellipsis if needed
        ImGui::RenderTextEllipsis(
            draw_list,
            ImVec2(label_x, label_y),
            ImVec2(card_max.x - grid_item_padding, card_max.y),
            card_max.x - grid_item_padding,
            card_max.x - grid_item_padding,
            label.c_str(),
            nullptr,
            nullptr
        );
        
        // tooltip for truncated labels
        if (is_hovered && text_size.x > label_max_w)
        {
            ImGui::SetTooltip("%s", label.c_str());
        }
        
        // handle click
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            item.Clicked();
            const bool is_single_click = item.GetTimeSinceLastClickMs() > 400;
            
            m_selected_item_id = item.GetId();
            m_input_box        = item.GetLabel();
            
            if (is_single_click)
            {
                if (m_callback_on_item_clicked)
                    m_callback_on_item_clicked(item.GetPath());
            }
            else
            {
                // double click
                m_current_path = item.GetPath();
                m_history.push_back(m_current_path);
                m_history_index  = m_history.size() - 1;
                m_is_dirty       = true;
                m_selection_made = !item.IsDirectory();
                
                if (m_type == FileDialog_Type_Browser && !item.IsDirectory())
                {
                    FileSystem::OpenUrl(item.GetPath());
                }
                if (m_callback_on_item_double_clicked)
                {
                    m_callback_on_item_double_clicked(m_current_path);
                }
            }
        }
        
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
        {
            m_is_hovering_item  = true;
            m_hovered_item_path = item.GetPath();
        }
        
        ItemClick(&item);
        ItemContextMenu(&item);
        
        ImGui::PopID();
        
        // layout: new row when columns are full
        col++;
        if (col >= columns)
        {
            col = 0;
            first_in_row = true;
        }
    }
    
    ImGui::Unindent(8.0f);
}

void FileDialog::RenderListView()
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 4));
    
    if (ImGui::BeginTable("##files", 3, ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        
        // handle sorting
        if (ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs())
        {
            if (sorts_specs->SpecsDirty)
            {
                m_sort_column = sorts_specs->Specs[0].ColumnIndex == 0 ? Sort_Name :
                               (sorts_specs->Specs[0].ColumnIndex == 1 ? Sort_Type : Sort_Modified);
                m_sort_ascending        = sorts_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                m_is_dirty              = true;
                sorts_specs->SpecsDirty = false;
            }
        }
        
        lock_guard lock(m_mutex_items);
        for (size_t i = 0; i < m_items.size(); i++)
        {
            auto& item = m_items[i];
            if (!m_search_filter.PassFilter(item.GetLabel().c_str()))
                continue;
            
            m_displayed_item_count++;
            
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            
            ImGui::PushID(static_cast<int>(i));
            
            bool is_selected = (m_selected_item_id == item.GetId());
            
            // selectable for the entire row
            if (ImGui::Selectable("##row", is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, list_row_height)))
            {
                item.Clicked();
                const bool is_single_click = item.GetTimeSinceLastClickMs() > 400;
                
                m_selected_item_id = item.GetId();
                m_input_box        = item.GetLabel();
                
                if (is_single_click)
                {
                    if (m_callback_on_item_clicked)
                        m_callback_on_item_clicked(item.GetPath());
                }
                else
                {
                    m_current_path = item.GetPath();
                    m_history.push_back(m_current_path);
                    m_history_index  = m_history.size() - 1;
                    m_is_dirty       = true;
                    m_selection_made = !item.IsDirectory();
                    
                    if (m_type == FileDialog_Type_Browser && !item.IsDirectory())
                    {
                        FileSystem::OpenUrl(item.GetPath());
                    }
                    if (m_callback_on_item_double_clicked)
                    {
                        m_callback_on_item_double_clicked(m_current_path);
                    }
                }
            }
            
            // drag source
            ItemDrag(&item);
            
            // hover state tracking
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
            {
                m_is_hovering_item  = true;
                m_hovered_item_path = item.GetPath();
            }
            
            ItemClick(&item);
            ItemContextMenu(&item);
            
            // icon
            ImGui::SameLine(0, 0);
            if (RHI_Texture* texture = item.GetIcon())
            {
                if (texture->GetResourceState() == ResourceState::PreparedForGpu)
                {
                    ImVec2 icon_size(20.0f, 20.0f);
                    ImGuiSp::image(texture, icon_size);
                    ImGui::SameLine(0, 8);
                }
            }
            
            // name
            ImGui::TextUnformatted(item.GetLabel().c_str());
            
            // type column
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
                item.IsDirectory() ? "Folder" : FileSystem::GetExtensionFromFilePath(item.GetPath()).c_str());
            
            // modified column
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
                FileSystem::GetLastWriteTime(item.GetPath()).c_str());
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
    
    ImGui::PopStyleVar();
}

void FileDialog::ShowBottom(bool* is_visible)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 window_pos     = ImGui::GetWindowPos();
    ImVec2 window_size    = ImGui::GetWindowSize();
    float bar_y           = window_size.y - m_offset_bottom;
    
    // draw background bar
    ImVec2 bar_min = ImVec2(window_pos.x, window_pos.y + bar_y);
    ImVec2 bar_max = ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y);
    draw_list->AddRectFilled(bar_min, bar_max, col_toolbar_bg);
    draw_list->AddLine(bar_min, ImVec2(bar_max.x, bar_min.y), col_separator);
    
    if (m_type == FileDialog_Type_Browser)
    {
        // status bar: item count
        ImGui::SetCursorPos(ImVec2(12, bar_y + 5));
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
            m_displayed_item_count == 1 ? "%d item" : "%d items", m_displayed_item_count);
    }
    else
    {
        // action bar: filename input, filter text, and buttons
        // calculate layout: [input field] [filter text] [Cancel] [Action]
        float frame_pad_x    = 16.0f;
        float button_spacing = 8.0f;
        float cancel_width   = ImGui::CalcTextSize("Cancel").x + frame_pad_x * 2;
        float action_width   = ImGui::CalcTextSize(OPERATION_NAME).x + frame_pad_x * 2;
        float buttons_total  = cancel_width + button_spacing + action_width + 12; // buttons + spacing + right margin
        float filter_width   = ImGui::CalcTextSize(FILTER_NAME).x + 16;
        float input_width    = window_size.x - buttons_total - filter_width - 24; // left margin + gaps
        
        ImGui::SetCursorPos(ImVec2(12, bar_y + 8));
        
        // filename input
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::SetNextItemWidth(input_width);
        ImGui::InputText("##filename", &m_input_box);
        ImGui::PopStyleVar(2);
        
        ImGui::SameLine(0, 8);
        
        // filter display
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), FILTER_NAME);
        
        ImGui::SameLine(0, 8);
        
        // action buttons (auto-sized)
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 6));
        
        // cancel button
        if (ImGui::Button("Cancel"))
        {
            m_selection_made = false;
            (*is_visible)    = false;
        }
        
        ImGui::SameLine(0, button_spacing);
        
        // primary action button (styled)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.7f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.6f, 0.8f, 1.0f));
        
        if (ImGui::Button(OPERATION_NAME))
        {
            m_selection_made = true;
        }
        
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }
}

void FileDialog::RenderItem(FileDialogItem* item, const ImVec2& size, bool is_list_view)
{
    // legacy function kept for compatibility - actual rendering is now in RenderGridView/RenderListView
}

void FileDialog::ItemDrag(FileDialogItem* item) const
{
    if (!item || m_type != FileDialog_Type_Browser)
        return;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        const auto set_payload = [this](const ImGuiSp::DragPayloadType type, const string& path_full, const string& path_relative)
        {
            m_drag_drop_payload.type          = type;
            m_drag_drop_payload.data          = path_full.c_str();
            m_drag_drop_payload.path_relative = path_relative.c_str();
            ImGuiSp::create_drag_drop_paylod(m_drag_drop_payload);
        };

        const string& path_full     = item->GetPath();
        const string& path_relative = item->GetPathRelative();

        if (FileSystem::IsSupportedModelFile(path_full)) { set_payload(ImGuiSp::DragPayloadType::Model,    path_full, path_relative); }
        if (FileSystem::IsSupportedImageFile(path_full)) { set_payload(ImGuiSp::DragPayloadType::Texture,  path_full, path_relative); }
        if (FileSystem::IsSupportedAudioFile(path_full)) { set_payload(ImGuiSp::DragPayloadType::Audio,    path_full, path_relative); }
        if (FileSystem::IsEngineMaterialFile(path_full)) { set_payload(ImGuiSp::DragPayloadType::Material, path_full, path_relative); }
        if (FileSystem::IsEngineLuaFile(path_full))      { set_payload(ImGuiSp::DragPayloadType::Lua,      path_full, path_relative); }

        // drag preview
        ImGui::BeginTooltip();
        ImGuiSp::image(item->GetIcon(), ImVec2(48, 48));
        ImGui::SameLine();
        ImGui::Text("%s", item->GetLabel().c_str());
        ImGui::EndTooltip();
        
        ImGui::EndDragDropSource();
    }
}

void FileDialog::ItemClick(FileDialogItem* item) const
{
    if (!item || !m_is_hovering_window)
        return;
    
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        m_context_menu_id = item->GetId();
        ImGui::OpenPopup("##context_menu");
    }
}

void FileDialog::ItemContextMenu(FileDialogItem* item)
{
    if (m_context_menu_id != item->GetId())
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
    
    if (ImGui::BeginPopup("##context_menu"))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        
        if (ImGui::MenuItem("Rename"))
        {
            m_is_renaming    = true;
            m_rename_buffer  = item->GetLabel();
            m_rename_item_id = item->GetId();
        }

        if (FileSystem::IsEngineLuaFile(item->GetPath()))
        {
            if (ImGui::MenuItem("Reload Script"))
            {
                for (Entity* entity : World::GetEntities())
                {
                    if (Script* script = entity->GetComponent<Script>())
                    {
                        if (script->file_path == item->GetPath())
                        {
                            script->LoadScriptFile(item->GetPath());
                        }
                    }
                }
            }
        }

        if (ImGui::MenuItem("Delete"))
        {
            FileSystem::Delete(item->GetPath());
            m_is_dirty = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Open in explorer"))
        {
            FileSystem::OpenUrl(item->GetPath());
        }
        
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    
    ImGui::PopStyleVar(2);

    // rename dialog
    if (m_is_renaming && m_rename_item_id == item->GetId())
    {
        ImGui::OpenPopup("##rename_dialog");

        ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        
        if (ImGui::BeginPopupModal("##rename_dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::Text("Rename");
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##rename_input", &m_rename_buffer);
            ImGui::PopStyleVar();
            
            ImGui::Spacing();
            ImGui::Spacing();
            
            float button_width = 80.0f;
            float buttons_x    = ImGui::GetContentRegionAvail().x - button_width * 2 - 8;
            ImGui::SetCursorPosX(buttons_x);
            
            if (ImGui::Button("Cancel", ImVec2(button_width, 0)))
            {
                m_is_renaming = false;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine(0, 8);
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
            if (ImGui::Button("Rename", ImVec2(button_width, 0)))
            {
                string new_path = FileSystem::GetDirectoryFromFilePath(item->GetPath()) + m_rename_buffer;
                FileSystem::Rename(item->GetPath(), new_path);
                m_is_dirty    = true;
                m_is_renaming = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();

            ImGui::EndPopup();
        }
        
        ImGui::PopStyleVar(2);
    }
}

void FileDialog::DialogUpdateFromDirectory(const string& file_path)
{
    if (!FileSystem::IsDirectory(file_path))
    {
        SP_LOG_ERROR("provided path doesn't point to a directory.");
        return;
    }

    lock_guard<mutex> lock(m_mutex_items);
    m_items.clear();
    m_selected_item_id = UINT32_MAX;
    
    // directories first
    auto directories = FileSystem::GetDirectoriesInDirectory(file_path);
    for (const string& directory : directories)
    {
        m_items.emplace_back(directory, spartan::ResourceCache::GetIcon(spartan::IconType::Folder));
    }

    // then files based on filter
    vector<string> paths_anything = FileSystem::GetFilesInDirectory(file_path);
    
    if (m_filter == FileDialog_Filter_All)
    {
        for (const string& file_path : paths_anything)
        {
            if (FileSystem::IsSupportedImageFile(file_path))
            {
                ThreadPool::AddTask([this, file_path]()
                {
                    auto texture = spartan::ResourceCache::Load<RHI_Texture>(file_path);
                    if (texture)
                    {
                        texture->PrepareForGpu();
                    }
                    lock_guard<mutex> lock(m_mutex_items);
                    m_items.emplace_back(file_path, texture.get());
                });
            }
            else if (FileSystem::IsSupportedAudioFile(file_path))
            {
                m_items.emplace_back(file_path, spartan::ResourceCache::GetIcon(spartan::IconType::Audio));
            }
            else if (FileSystem::IsSupportedModelFile(file_path))
            {
                m_items.emplace_back(file_path, spartan::ResourceCache::GetIcon(spartan::IconType::Model));
            }
            else if (FileSystem::IsSupportedFontFile(file_path))
            {
                m_items.emplace_back(file_path, spartan::ResourceCache::GetIcon(spartan::IconType::Font));
            }
            else if (FileSystem::IsEngineMaterialFile(file_path))
            {
                m_items.emplace_back(file_path, spartan::ResourceCache::GetIcon(spartan::IconType::Material));
            }
            else if (FileSystem::IsEngineWorldFile(file_path))
            {
                m_items.emplace_back(file_path, spartan::ResourceCache::GetIcon(spartan::IconType::World));
            }
            else if (FileSystem::GetExtensionFromFilePath(file_path) == ".7z")
            {
                m_items.emplace_back(file_path, spartan::ResourceCache::GetIcon(spartan::IconType::Compressed));
            }
            else
            {
                m_items.emplace_back(file_path, spartan::ResourceCache::GetIcon(spartan::IconType::Undefined));
            }
        }
    }
    else if (m_filter == FileDialog_Filter_World)
    {
        for (const string& anything : paths_anything)
        {
            if (FileSystem::GetExtensionFromFilePath(anything) == EXTENSION_WORLD)
            {
                m_items.emplace_back(anything, spartan::ResourceCache::GetIcon(spartan::IconType::World));
            }
        }
    }
    else if (m_filter == FileDialog_Filter_Model)
    {
        for (const string& anything : paths_anything)
        {
            if (FileSystem::IsSupportedModelFile(anything))
            {
                m_items.emplace_back(anything, spartan::ResourceCache::GetIcon(spartan::IconType::Model));
            }
        }
    }

    // sort items
    sort(m_items.begin(), m_items.end(), [this](const FileDialogItem& a, const FileDialogItem& b)
    {
        bool a_is_dir = FileSystem::IsDirectory(a.GetPath());
        bool b_is_dir = FileSystem::IsDirectory(b.GetPath());

        // directories always first
        if (a_is_dir != b_is_dir)
            return a_is_dir;

        if (m_sort_column == Sort_Name)
            return m_sort_ascending ? a.GetLabel() < b.GetLabel() : a.GetLabel() > b.GetLabel();

        if (m_sort_column == Sort_Type)
            return m_sort_ascending ? FileSystem::GetExtensionFromFilePath(a.GetPath()) < FileSystem::GetExtensionFromFilePath(b.GetPath()) :
                                      FileSystem::GetExtensionFromFilePath(a.GetPath()) > FileSystem::GetExtensionFromFilePath(b.GetPath());

        if (m_sort_column == Sort_Modified)
            return m_sort_ascending ? FileSystem::GetLastWriteTime(a.GetPath()) < FileSystem::GetLastWriteTime(b.GetPath()) :
                                      FileSystem::GetLastWriteTime(a.GetPath()) > FileSystem::GetLastWriteTime(b.GetPath());

        return false;
    });
}

void FileDialog::EmptyAreaContextMenu()
{
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m_is_hovering_window && !m_is_hovering_item)
    {
        ImGui::OpenPopup("##empty_context_menu");
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
    
    if (ImGui::BeginPopup("##empty_context_menu"))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        
        if (ImGui::MenuItem("New folder"))
        {
            FileSystem::CreateDirectory_(m_current_path + "/New folder");
            m_is_dirty = true;
        }

        if (ImGui::MenuItem("New Lua script"))
        {
            FileSystem::WriteFile(m_current_path + "/new_lua_script" + EXTENSION_LUA, NewLuaScriptContents);
            m_is_dirty = true;
        }

        if (ImGui::MenuItem("New material"))
        {
            Material material      = Material();
            const string file_path = m_current_path + "/new_material" + EXTENSION_MATERIAL;
            material.SetResourceFilePath(file_path);
            material.SaveToFile(file_path);
            m_is_dirty = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Open in explorer"))
        {
            FileSystem::OpenUrl(m_current_path);
        }
        
        if (ImGui::MenuItem("Refresh"))
        {
            m_is_dirty = true;
        }
        
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    
    ImGui::PopStyleVar(2);
}

void FileDialog::HandleKeyboardNavigation()
{
    if (!m_is_hovering_window || m_is_renaming)
        return;

    // enter to confirm selection
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !m_input_box.empty())
    {
        m_selection_made = true;
    }
    
    // escape to close (file selection mode only)
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && m_type == FileDialog_Type_FileSelection)
    {
        // handled by parent
    }
    
    // f5 to refresh
    if (ImGui::IsKeyPressed(ImGuiKey_F5))
    {
        m_is_dirty = true;
    }
    
    // alt+left for back
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && ImGui::GetIO().KeyAlt && m_history_index > 0)
    {
        m_history_index--;
        m_current_path = m_history[m_history_index];
        m_is_dirty     = true;
    }
    
    // alt+right for forward
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && ImGui::GetIO().KeyAlt && m_history_index < m_history.size() - 1)
    {
        m_history_index++;
        m_current_path = m_history[m_history_index];
        m_is_dirty     = true;
    }
    
    // alt+up for parent directory
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && ImGui::GetIO().KeyAlt)
    {
        string parent = FileSystem::GetParentDirectory(m_current_path);
        if (!parent.empty() && parent != m_current_path)
        {
            m_current_path = parent;
            m_history.push_back(m_current_path);
            m_history_index = m_history.size() - 1;
            m_is_dirty      = true;
        }
    }
    
    // backspace for parent directory
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !ImGui::GetIO().WantTextInput)
    {
        string parent = FileSystem::GetParentDirectory(m_current_path);
        if (!parent.empty() && parent != m_current_path)
        {
            m_current_path = parent;
            m_history.push_back(m_current_path);
            m_history_index = m_history.size() - 1;
            m_is_dirty      = true;
        }
    }
}
