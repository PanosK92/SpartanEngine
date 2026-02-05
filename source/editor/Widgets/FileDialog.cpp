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

    void set_cursor_position_x(float pos_x)
    {
        ImGui::SetCursorPosX(pos_x);
        ImGui::Dummy(ImVec2(0, 0)); // imgui requirement to avoid assert
    }

    // options
    const float item_size_min         = 32.0f;
    const float item_size_max         = 256;
    const float item_background_alpha = 32.0f;


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



}

FileDialog::FileDialog(const bool standalone_window, const FileDialog_Type type, const FileDialog_Operation operation, const FileDialog_Filter filter)
{
    m_type                            = type;
    m_operation                       = operation;
    m_filter                          = filter;
    m_title                           = OPERATION_NAME;
    m_is_window                       = standalone_window;
    m_item_size                       = 150.0f;
    m_is_dirty                        = true;
    m_selection_made                  = false;
    m_callback_on_item_clicked        = nullptr;
    m_callback_on_item_double_clicked = nullptr;
    m_current_path                    = ResourceCache::GetProjectDirectory();
    m_root_path                       = ".."; // allow navigation to parent (repo root) for worlds folder access
    m_sort_column                     = Sort_Name;
    m_sort_ascending                  = true;
    m_view_mode                       = View_Grid;
    m_history_index                   = 0;
    m_history.push_back(m_current_path);
}

void FileDialog::SetOperation(const FileDialog_Operation operation)
{
    m_operation = operation;
    m_title     = OPERATION_NAME;
}

void FileDialog::SetCurrentPath(const string& path)
{
    // if the path is a file, get its parent directory
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

    m_selection_made     = false;
    m_is_hovering_item   = false;
    m_is_hovering_window = false;

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
            // get the directory - if m_current_path is a file, get its parent directory
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
        ImGui::SetNextWindowSizeConstraints(ImVec2(800, 600), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::Begin(m_title.c_str(), is_visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);
        ImGui::SetWindowFocus();
    }

    // navigation buttons
    {
        // back button
        ImGui::BeginDisabled(m_history_index == 0);
        if (ImGuiSp::button("<"))
        {
            if (m_history_index > 0)
            {
                m_history_index--;
                m_current_path = m_history[m_history_index];
                m_is_dirty = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        // forward button
        ImGui::BeginDisabled(m_history_index == m_history.size() - 1);
        if (ImGuiSp::button(">"))
        {
            if (m_history_index < m_history.size() - 1)
            {
                m_history_index++;
                m_current_path = m_history[m_history_index];
                m_is_dirty = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        // breadcrumb navigation
        const char* root_path = m_root_path.c_str();

        char accumulated_path[1024];
        accumulated_path[0] = '\0';

        char current_path[1024];
        strncpy_s(current_path, sizeof(current_path), m_current_path.c_str(), _TRUNCATE);

        // show root directory button if not at root
        if (strcmp(m_current_path.c_str(), root_path) != 0)
        {
            if (ImGuiSp::button(".."))
            {
                m_current_path = root_path;
                m_history.push_back(m_current_path);
                m_history_index = m_history.size() - 1;
                m_is_dirty = true;
            }
            ImGui::SameLine();
            ImGui::Text(">");
            ImGui::SameLine();
        }

        // split manually
        const char* delimiters = "/\\";
        char* context = nullptr;
        char* token   = strtok_s(current_path, delimiters, &context);
        bool first    = true;

        while (token)
        {
            // skip ".." tokens in path display
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

            size_t root_len = strlen(root_path);
            size_t acc_len  = strlen(accumulated_path);
            if (acc_len == root_len + 1 && accumulated_path[root_len] == '/' && strncmp(accumulated_path, root_path, root_len) == 0)
            {
                token = strtok_s(nullptr, delimiters, &context);
                continue;
            }

            if (ImGuiSp::button(token))
            {
                m_current_path = accumulated_path;
                m_history.push_back(m_current_path);
                m_history_index = m_history.size() - 1;
                m_is_dirty = true;
            }
            ImGui::SameLine();
            ImGui::Text(">");
            ImGui::SameLine();

            token = strtok_s(nullptr, delimiters, &context);
        }
    }

    // size slider + view toggle
    {
        float button_width = ImGui::CalcTextSize(m_view_mode == View_Grid ? "List View" : "Grid View").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float slider_width = m_view_mode == View_Grid ? 150.0f : 0.0f;
        float total_width = button_width + slider_width + ImGui::GetStyle().ItemSpacing.x;
        float region_width = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + region_width - total_width);

        if (m_view_mode == View_Grid)
        {
            ImGui::SetNextItemWidth(slider_width);
            ImGui::SliderFloat("##FileDialogSlider", &m_item_size.x, item_size_min, item_size_max);
            ImGui::SameLine();
        }

        if (ImGuiSp::button(m_view_mode == View_Grid ? "List View" : "Grid View"))
        {
            m_view_mode = (m_view_mode == View_Grid) ? View_List : View_Grid;
            m_is_dirty = true;
        }
    }

    // search filter
    const float label_width = 37.0f * spartan::Window::GetDpiScale();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - label_width - 30.0f);
    m_search_filter.Draw("Filter", ImGui::GetContentRegionAvail().x - label_width - 30.0f);
    ImGui::PopItemWidth();

    // file filter dropdown
    if (m_type != FileDialog_Type_Browser)
    {
        ImGui::SameLine();
        if (ImGui::BeginCombo("##FileFilter", FILTER_NAME))
        {
            if (ImGui::Selectable("All (*.*)", m_filter == FileDialog_Filter_All))
            {
                m_filter = FileDialog_Filter_All;
                m_is_dirty = true;
            }
            if (ImGui::Selectable("Model (*.*)", m_filter == FileDialog_Filter_Model))
            {
                m_filter = FileDialog_Filter_Model;
                m_is_dirty = true;
            }
            if (ImGui::Selectable("World (*.world)", m_filter == FileDialog_Filter_World))
            {
                m_filter = FileDialog_Filter_World;
                m_is_dirty = true;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Separator();
}

void FileDialog::ShowMiddle()
{
    const auto content_width  = ImGui::GetContentRegionAvail().x;
    const auto content_height = ImGui::GetContentRegionAvail().y - m_offset_bottom;
    ImGuiContext& g           = *GImGui;
    ImGuiStyle& style         = ImGui::GetStyle();
    const float font_height   = g.FontSize;
    const float label_height  = font_height;
    const float text_offset   = 3.0f;
    float pen_x_min           = 0.0f;
    float pen_x               = 0.0f;
    bool new_line             = true;
    m_displayed_item_count    = 0;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    if (ImGui::BeginChild("##ContentRegion", ImVec2(content_width, content_height), true))
    {
        m_is_hovering_window = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (m_view_mode == View_List)
        {
            // list view with sortable columns
            if (ImGui::BeginTable("##FileTable", 3, ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders))
            {
                ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();

                // handle sorting
                if (ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs())
                {
                    if (sorts_specs->SpecsDirty)
                    {
                        m_sort_column           = sorts_specs->Specs[0].ColumnIndex == 0 ? Sort_Name : (sorts_specs->Specs[0].ColumnIndex == 1 ? Sort_Type : Sort_Modified);
                        m_sort_ascending        = sorts_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                        m_is_dirty              = true;
                        sorts_specs->SpecsDirty = false;
                    }
                }

                lock_guard lock(m_mutex_items);
                for (int i = 0; i < m_items.size(); i++)
                {
                    auto& item = m_items[i];
                    if (!m_search_filter.PassFilter(item.GetLabel().c_str()))
                        continue;

                    m_displayed_item_count++;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    RenderItem(&item, ImVec2(0, 0), true);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text(item.IsDirectory() ? "Folder" : FileSystem::GetExtensionFromFilePath(item.GetPath()).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text(FileSystem::GetLastWriteTime(item.GetPath()).c_str());
                }
                ImGui::EndTable();
            }
        }
        else
        {
            // grid view
            set_cursor_position_x(ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x);
            pen_x_min = ImGui::GetCursorPosX();
            lock_guard lock(m_mutex_items);
            for (int i = 0; i < m_items.size(); i++)
            {
                auto& item = m_items[i];
                if (!m_search_filter.PassFilter(item.GetLabel().c_str()))
                    continue;

                m_displayed_item_count++;
                if (new_line)
                {
                    ImGui::BeginGroup();
                    new_line = false;
                }

                ImGui::BeginGroup();
                ImRect rect_button, rect_label;
                {
                    rect_button = ImRect(
                        ImGui::GetCursorScreenPos().x,
                        ImGui::GetCursorScreenPos().y,
                        ImGui::GetCursorScreenPos().x + m_item_size.x,
                        ImGui::GetCursorScreenPos().y + m_item_size.y
                    );
                    rect_label = ImRect(
                        rect_button.Min.x,
                        rect_button.Max.y - label_height - style.FramePadding.y,
                        rect_button.Max.x,
                        rect_button.Max.y
                    );
                }

                RenderItem(&item, m_item_size, false);
                ImGui::EndGroup();

                pen_x += m_item_size.x + ImGui::GetStyle().ItemSpacing.x;
                if (pen_x >= content_width - m_item_size.x)
                {
                    ImGui::EndGroup();
                    pen_x = pen_x_min;
                    set_cursor_position_x(pen_x);
                    new_line = true;
                }
                else
                {
                    ImGui::SameLine();
                }
            }
            if (!new_line)
            {
                ImGui::EndGroup();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void FileDialog::ShowBottom(bool* is_visible)
{
    if (m_type == FileDialog_Type_Browser)
    {
        m_offset_bottom = 24.0f * spartan::Window::GetDpiScale();
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - m_offset_bottom);
        static char text[16];
        snprintf(text, sizeof(text), (m_displayed_item_count == 1) ? "%d item" : "%d items", m_displayed_item_count);
        ImGui::Text(text);
    }
    else
    {
        m_offset_bottom = 35.0f * spartan::Window::GetDpiScale();
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - m_offset_bottom);
        ImGui::PushItemWidth(ImGui::GetWindowSize().x - 235 * spartan::Window::GetDpiScale());
        ImGui::InputText("##InputBox", &m_input_box);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Text(FILTER_NAME);
        ImGui::SameLine();
        if (ImGuiSp::button(OPERATION_NAME))
        {
            m_selection_made = true;
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            m_selection_made = false;
            (*is_visible)    = false;
        }
    }
}

void FileDialog::RenderItem(FileDialogItem* item, const ImVec2& size, bool is_list_view)
{
    ImGui::PushID(item->GetId());
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    bool button_pressed = false;
    ImRect button_rect;
    if (is_list_view)
    {
        // list view: use selectable for click detection, spans the cell
        button_pressed = ImGui::Selectable("##selectable", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
        button_rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

        // drag source must be set up immediately after the item, before any other rendering
        ItemDrag(item);

        // render icon if available
        if (RHI_Texture* texture = item->GetIcon())
        {
            if (texture->GetResourceState() == ResourceState::PreparedForGpu)
            {
                ImVec2 image_size(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
                ImVec2 image_size_max(32.0f, 32.0f);
                float scale = min(image_size_max.x / image_size.x, image_size_max.y / image_size.y);
                image_size.x *= scale;
                image_size.y *= scale;
                // Adjust cursor to align icon vertically centered in the row
                float row_height = ImGui::GetCurrentTable()->RowMinHeight;
                float icon_y_offset = (row_height - image_size.y) * 0.5f;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + icon_y_offset);
                ImGuiSp::image(item->GetIcon(), image_size);
                ImGui::SameLine();
            }
        }
        // render label
        ImGui::TextUnformatted(item->GetLabel().c_str());
    }
    else
    {
        // grid view: sized invisible button
        button_pressed = ImGui::InvisibleButton("##dummy", size);
        button_rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

        // drag source must be set up immediately after the item, before any other rendering
        ItemDrag(item);

        // hover outline (grid view only)
        if (ImGui::IsItemHovered() && !is_list_view)
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRect(
                button_rect.Min,
                button_rect.Max,
                IM_COL32(100, 149, 237, 255),
                5.0f,
                0,
                1.0f);
        }
        // drop shadow (grid view only)
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(
            ImVec2(button_rect.Min.x - 2.0f, button_rect.Min.y - 2.0f),
            ImVec2(button_rect.Max.x + 2.0f, button_rect.Max.y + 2.0f),
            IM_COL32(0, 0, 0, item_background_alpha),
            5.0f);
        // render icon if available
        if (RHI_Texture* texture = item->GetIcon())
        {
            if (texture->GetResourceState() == ResourceState::PreparedForGpu)
            {
                ImVec2 image_size(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
                const float padding = ImGui::GetStyle().FramePadding.x;
                ImVec2 image_size_max(button_rect.GetWidth() - padding * 2.0f, button_rect.GetHeight() - padding * 2.0f - ImGui::GetFont()->FontSize - 5.0f);
                float scale = min(image_size_max.x / image_size.x, image_size_max.y / image_size.y);
                image_size.x *= scale;
                image_size.y *= scale;
                // grid view: center icon
                ImVec2 image_pos(button_rect.GetCenter().x - image_size.x * 0.5f, button_rect.Min.y + (button_rect.GetHeight() - image_size.y - ImGui::GetFont()->FontSize - 5.0f) * 0.5f);
                ImGui::SetCursorScreenPos(image_pos);
                ImGuiSp::image(item->GetIcon(), image_size);
            }
        }
        // render label
        const ImVec2 label_pos(button_rect.Min.x + ImGui::GetStyle().FramePadding.x, button_rect.Max.y - ImGui::GetFont()->FontSize - ImGui::GetStyle().FramePadding.y - 2.0f);
        ImGui::SetCursorScreenPos(label_pos);
        ImGui::RenderTextEllipsis(
            ImGui::GetWindowDrawList(),
            label_pos,
            button_rect.Max,
            button_rect.Max.x,
            button_rect.Max.x,
            item->GetLabel().c_str(),
            nullptr,
            nullptr
        );
    }
    if (button_pressed)
    {
        item->Clicked();
        const bool is_single_click = item->GetTimeSinceLastClickMs() > 500 || is_list_view;
        if (is_single_click)
        {
            m_input_box = item->GetLabel();
            if (m_callback_on_item_clicked) m_callback_on_item_clicked(item->GetPath());
        }
        else
        {
            m_current_path = item->GetPath();
            m_history.push_back(m_current_path);
            m_history_index  = m_history.size() - 1;
            m_is_dirty       = true;
            m_selection_made = !item->IsDirectory();
            if (m_type == FileDialog_Type_Browser && !item->IsDirectory())
            {
                FileSystem::OpenUrl(item->GetPath());
            }
            if (m_callback_on_item_double_clicked)
            {
                m_callback_on_item_double_clicked(m_current_path);
            }
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
    {
        m_is_hovering_item = true;
        m_hovered_item_path = item->GetPath();
    }
    ItemClick(item);
    ItemContextMenu(item);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void FileDialog::ItemDrag(FileDialogItem* item) const
{
    if (!item || m_type != FileDialog_Type_Browser)
        return;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        const auto set_payload = [this](const ImGuiSp::DragPayloadType type, const string& path)
        {
            m_drag_drop_payload.type = type;
            m_drag_drop_payload.data = path.c_str();
            ImGuiSp::create_drag_drop_payload(m_drag_drop_payload);
        };

        if (FileSystem::IsSupportedModelFile(item->GetPath())) { set_payload(ImGuiSp::DragPayloadType::Model,    item->GetPath()); }
        if (FileSystem::IsSupportedImageFile(item->GetPath())) { set_payload(ImGuiSp::DragPayloadType::Texture,  item->GetPath()); }
        if (FileSystem::IsSupportedAudioFile(item->GetPath())) { set_payload(ImGuiSp::DragPayloadType::Audio,    item->GetPath()); }
        if (FileSystem::IsEngineMaterialFile(item->GetPath())) { set_payload(ImGuiSp::DragPayloadType::Material, item->GetPath()); }
        if (FileSystem::IsEngineLuaFile(item->GetPath()))      { set_payload(ImGuiSp::DragPayloadType::Lua,      item->GetPath()); }

        ImGuiSp::image(item->GetIcon(), ImVec2(50, 50));
        ImGui::EndDragDropSource();
    }
}

void FileDialog::ItemClick(FileDialogItem* item) const
{
    if (!item || !m_is_hovering_window)
        return;
    if (ImGui::IsItemClicked(1))
    {
        m_context_menu_id = item->GetId();
        ImGui::OpenPopup("##FileDialogContextMenu");
    }
}

void FileDialog::ItemContextMenu(FileDialogItem* item)
{
    // ensure the context menu is for the correct item
    if (m_context_menu_id != item->GetId())
        return;

    // open and render the context menu
    if (ImGui::BeginPopup("##FileDialogContextMenu"))
    {
        if (ImGui::MenuItem("Rename"))
        {
            m_is_renaming    = true;
            m_rename_buffer  = item->GetLabel();
            m_rename_item_id = item->GetId();
            ImGui::OpenPopup("##RenameDialog"); // move OpenPopup here to ensure it's called in the same frame
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

        if (ImGui::MenuItem("Open in file explorer"))
        {
            FileSystem::OpenUrl(item->GetPath());
        }

        ImGui::EndPopup();
    }

    // handle renaming popup
    if (m_is_renaming && m_rename_item_id == item->GetId())
    {
        // ensure the popup is opened every frame while renaming
        ImGui::OpenPopup("##RenameDialog");

        // create a modal popup for renaming
        if (ImGui::BeginPopupModal("##RenameDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            // input text field for the new name
            ImGui::InputText("##RenameInput", &m_rename_buffer);

            // ok button to confirm renaming
            if (ImGui::Button("OK"))
            {
                string new_path = FileSystem::GetDirectoryFromFilePath(item->GetPath()) + m_rename_buffer;
                FileSystem::Rename(item->GetPath(), new_path);
                m_is_dirty    = true;
                m_is_renaming = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            // cancel button to abort renaming
            if (ImGui::Button("Cancel"))
            {
                m_is_renaming = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}
void FileDialog::DialogUpdateFromDirectory(const string& file_path)
{
    if (!FileSystem::IsDirectory(file_path))
    {
        SP_LOG_ERROR("Provided path doesn't point to a directory.");
        return;
    }

    lock_guard<mutex> lock(m_mutex_items);
    m_items.clear();
    auto directories = FileSystem::GetDirectoriesInDirectory(file_path);
    for (const string& directory : directories)
    {
        m_items.emplace_back(directory, spartan::ResourceCache::GetIcon(spartan::IconType::Folder));
    }

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
    if (ImGui::IsMouseClicked(1) && m_is_hovering_window && !m_is_hovering_item)
    {
        ImGui::OpenPopup("##Content_ContextMenu");
    }

    if (!ImGui::BeginPopup("##Content_ContextMenu"))
        return;

    if (ImGui::MenuItem("Create folder"))
    {
        FileSystem::CreateDirectory_(m_current_path + "/New folder");
        m_is_dirty = true;
    }

    if (ImGui::MenuItem("Create Lua Script"))
    {
        FileSystem::WriteFile(m_current_path + "/new_lua_script" + EXTENSION_LUA, NewLuaScriptContents);
        m_is_dirty = true;
    }

    if (ImGui::MenuItem("Create material"))
    {
        Material material      = Material();
        const string file_path = m_current_path + "/new_material" + EXTENSION_MATERIAL;
        material.SetResourceFilePath(file_path);
        material.SaveToFile(file_path);
        m_is_dirty = true;
    }

    if (ImGui::MenuItem("Open directory in explorer"))
    {
        FileSystem::OpenUrl(m_current_path);
    }

    ImGui::EndPopup();
}

void FileDialog::HandleKeyboardNavigation()
{
    if (!m_is_hovering_window || m_is_renaming)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        // todo
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        // todo
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !m_input_box.empty())
    {
        m_selection_made = true;
    }
}
