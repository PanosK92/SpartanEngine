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

//= INCLUDES ==============================
#include "FileDialog.h"
#include "../ImGui/Source/imgui_internal.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "Rendering/Mesh.h"
#include "../Widgets/Viewport.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

namespace
{
    #define OPERATION_NAME (m_operation == FileDialog_Op_Open) ? "Open"      : (m_operation == FileDialog_Op_Load)   ? "Load"       : (m_operation == FileDialog_Op_Save) ? "Save" : "View"
    #define FILTER_NAME    (m_filter == FileDialog_Filter_All) ? "All (*.*)" : (m_filter == FileDialog_Filter_Model) ? "Model(*.*)" : "World (*.world)"

    void set_cursor_position_x(float pos_x)
    {
        ImGui::SetCursorPosX(pos_x);
        ImGui::Dummy(ImVec2(0, 0)); // imgui requirement to avoid assert
    }
}

FileDialog::FileDialog(const bool standalone_window, const FileDialog_Type type, const FileDialog_Operation operation, const FileDialog_Filter filter)
{
    m_type                            = type;
    m_operation                       = operation;
    m_filter                          = filter;
    m_type                            = type;
    m_title                           = OPERATION_NAME;
    m_is_window                       = standalone_window;
    m_item_size                       = Vector2(100.0f, 100.0f);
    m_is_dirty                        = true;
    m_selection_made                  = false;
    m_callback_on_item_clicked        = nullptr;
    m_callback_on_item_double_clicked = nullptr;
    m_current_path                    = ResourceCache::GetProjectDirectory();
}

void FileDialog::SetOperation(const FileDialog_Operation operation)
{
    m_operation = operation;
    m_title     = OPERATION_NAME;
}

bool FileDialog::Show(bool* is_visible, Editor* editor, string* directory /*= nullptr*/, string* file_path /*= nullptr*/)
{
    if (!(*is_visible))
    {
        m_is_dirty = true; // set as dirty as things can change till next time
        return false;
    }

    m_selection_made     = false;
    m_is_hovering_item   = false;
    m_is_hovering_window = false;

    ShowTop(is_visible, editor); // top menu
    ShowMiddle();                // contents of the current directory
    ShowBottom(is_visible);      // bottom menu

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
            (*file_path) = FileSystem::GetDirectoryFromFilePath(m_current_path) + "/" + string(m_input_box);
        }
    }

    EmptyAreaContextMenu();

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

    // Directory navigation buttons
    {
        // Go backwards
        if (ImGuiSp::button("<"))
        {
            m_current_path = FileSystem::GetParentDirectory(m_current_path);
            m_is_dirty     = true;
        }

        // Display current path
        ImGui::SameLine();
        ImGui::Text("%s", m_current_path.c_str());
    }

    // Size slider
    const float slider_width = 200.0f;
    ImGui::SameLine(ImGuiSp::GetWindowContentRegionWidth() - slider_width);
    ImGui::PushItemWidth(slider_width);
    const float previous_width = m_item_size.x;
    ImGui::SliderFloat("##FileDialogSlider", &m_item_size.x, m_item_size_min, m_item_size_max);
    m_item_size.y += m_item_size.x - previous_width;
    ImGui::PopItemWidth();

    // Search filter
    const float label_width = 37.0f * Spartan::Window::GetDpiScale();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12);
    m_search_filter.Draw("Filter", ImGui::GetContentRegionAvail().x - label_width);
    ImGui::PopStyleVar();

    ImGui::Separator();
}

void FileDialog::ShowMiddle()
{
    // Compute some useful stuff
    const auto window         = ImGui::GetCurrentWindowRead();
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
    ImRect rect_button;
    ImRect rect_label;

    // Remove border
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

    // Make background slightly darker
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(
        static_cast<int>(m_content_background_color.x),
        static_cast<int>(m_content_background_color.y),
        static_cast<int>(m_content_background_color.z),
        static_cast<int>(m_content_background_color.w)));

    if (ImGui::BeginChild("##ContentRegion", ImVec2(content_width, content_height), true))
    {
        m_is_hovering_window = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ? true : m_is_hovering_window;

        // Set starting position
        {
            float offset = ImGui::GetStyle().ItemSpacing.x;
            pen_x_min    = ImGui::GetCursorPosX() + offset;
            set_cursor_position_x(pen_x_min);
        }

        // Go through all the items
        for (int i = 0; i < m_items.size(); i++)
        {
            // Get item to be displayed
            auto& item = m_items[i];

            // Apply search filter
            if (!m_search_filter.PassFilter(item.GetLabel().c_str()))
                continue;

            m_displayed_item_count++;

            // Start new line ?
            if (new_line)
            {
                ImGui::BeginGroup();
                new_line = false;
            }

            ImGui::BeginGroup();
            {
                // Compute rectangles for elements that make up an item
                {
                    rect_button = ImRect
                    (
                        ImGui::GetCursorScreenPos().x,
                        ImGui::GetCursorScreenPos().y,
                        ImGui::GetCursorScreenPos().x + m_item_size.x,
                        ImGui::GetCursorScreenPos().y + m_item_size.y
                    );

                    rect_label = ImRect
                    (
                        rect_button.Min.x,
                        rect_button.Max.y - label_height - style.FramePadding.y,
                        rect_button.Max.x,
                        rect_button.Max.y
                    );
                }

                // Drop shadow effect
                if (m_drop_shadow)
                {
                    static const float shadow_thickness = 2.0f;
                    ImVec4 color = {1.0f, 1.0f, 4.0f, 0.1f};
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        rect_button.Min,
                        ImVec2(rect_label.Max.x + shadow_thickness, rect_label.Max.y + shadow_thickness),
                        IM_COL32(color.x * 255, color.y * 255, color.z * 255, color.w * 255),
                        5.0f);
                }

                // THUMBNAIL
                {
                    ImGui::PushID(i);
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

                    if (ImGuiSp::button("##dummy", m_item_size))
                    {
                        // Determine type of click
                        item.Clicked();
                        const bool is_single_click = item.GetTimeSinceLastClickMs() > 500;

                        if (is_single_click)
                        {
                            // Updated input box
                            m_input_box = item.GetLabel();
                            // Callback
                            if (m_callback_on_item_clicked) m_callback_on_item_clicked(item.GetPath());
                        }
                        else // Double Click
                        {
                            m_current_path   = item.GetPath();
                            m_is_dirty       = true;
                            m_selection_made = !item.IsDirectory();

                            // When browsing files, open them on double click
                            if (m_type == FileDialog_Type_Browser)
                            {
                                if (!item.IsDirectory())
                                {
                                    FileSystem::OpenUrl(item.GetPath());
                                }
                            }

                            // Callback
                            if (m_callback_on_item_double_clicked)
                            {
                                m_callback_on_item_double_clicked(m_current_path);
                            }
                        }
                    }

                    // Item functionality
                    {
                        // Manually detect some useful states
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
                        {
                            m_is_hovering_item = true;
                            m_hovered_item_path = item.GetPath();
                        }

                        ItemClick(&item);
                        ItemContextMenu(&item);
                        ItemDrag(&item);
                    }

                    // Image
                    if (RHI_Texture* texture = item.GetTexture())
                    {
                        if (texture->IsReadyForUse()) // This is possible for when the editor is reading from drive
                        {
                            // Compute thumbnail size
                            ImVec2 image_size     = ImVec2(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
                            ImVec2 image_size_max = ImVec2(rect_button.Max.x - rect_button.Min.x - style.FramePadding.x * 2.0f, rect_button.Max.y - rect_button.Min.y - style.FramePadding.y - label_height - 5.0f);

                            // Scale the image size to fit the max available size while respecting its aspect ratio
                            {
                                float width_scale  = image_size_max.x / image_size.x;
                                float height_scale = image_size_max.y / image_size.y;
                                float scale        = (width_scale < height_scale) ? width_scale : height_scale;

                                image_size.x *= scale;
                                image_size.y *= scale;
                            }

                            // Calculate button center and image position
                            ImVec2 button_center = ImVec2(
                                rect_button.Min.x + (rect_button.Max.x - rect_button.Min.x) / 2,
                                rect_button.Min.y + (rect_button.Max.y - rect_button.Min.y - label_height - 5.0f) / 2
                            );

                            ImVec2 image_pos = ImVec2(
                                button_center.x - image_size.x / 2,
                                button_center.y - image_size.y / 2
                            );

                            // Position the image within the square border
                            ImGui::SetCursorScreenPos(image_pos);

                            // Draw the image
                            ImGuiSp::image(item.GetTexture(), image_size);
                        }
                    }

                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleVar(1);
                    ImGui::PopID();
                }

                // LABEL
                {
                    const char* label_text  = item.GetLabel().c_str();
                    const ImVec2 label_size = ImGui::CalcTextSize(label_text, nullptr, true);

                    // Draw text background
                    ImGui::GetWindowDrawList()->AddRectFilled(rect_label.Min, rect_label.Max, IM_COL32(51, 51, 51, 190));
                    //ImGui::GetWindowDrawList()->AddRect(rect_label.Min, rect_label.Max, IM_COL32(255, 0, 0, 255)); // debug

                    // Draw text
                    ImGui::SetCursorScreenPos(ImVec2(rect_label.Min.x + text_offset, rect_label.Min.y + text_offset));
                    if (label_size.x <= m_item_size.x && label_size.y <= m_item_size.y)
                    {
                        ImGui::TextUnformatted(label_text);
                    }
                    else
                    {
                        ImGui::RenderTextClipped(rect_label.Min, rect_label.Max, label_text, nullptr, &label_size, ImVec2(0, 0), &rect_label);
                    }
                }

                ImGui::EndGroup();
            }

            // Decide whether we should switch to the next column or switch row
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

    ImGui::EndChild(); // BeginChild() requires EndChild() to always be called
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void FileDialog::ShowBottom(bool* is_visible)
{
    if (m_type == FileDialog_Type_Browser)
    {
        // move to the bottom of the window
        m_offset_bottom = 24.0f * Spartan::Window::GetDpiScale();
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - m_offset_bottom);

        string text = (m_displayed_item_count == 1) ? "%d item" : "%d items";
        ImGui::Text(text.c_str(), m_displayed_item_count);
    }
    else
    {
        // move to the bottom of the window
        m_offset_bottom = 35.0f * Spartan::Window::GetDpiScale();
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - m_offset_bottom);

        ImGui::PushItemWidth(ImGui::GetWindowSize().x - 235 * Spartan::Window::GetDpiScale());
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
            (*is_visible) = false;
        }
    }
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
            ImGuiSp::create_drag_drop_paylod(m_drag_drop_payload);
        };

        if (FileSystem::IsSupportedModelFile(item->GetPath())) { set_payload(ImGuiSp::DragPayloadType::Model,    item->GetPath()); }
        if (FileSystem::IsSupportedImageFile(item->GetPath())) { set_payload(ImGuiSp::DragPayloadType::Texture,  item->GetPath()); }
        if (FileSystem::IsSupportedAudioFile(item->GetPath())) { set_payload(ImGuiSp::DragPayloadType::Audio,    item->GetPath()); }
        if (FileSystem::IsEngineMaterialFile(item->GetPath())) { set_payload(ImGuiSp::DragPayloadType::Material, item->GetPath()); }

        // Preview
        ImGuiSp::image(item->GetTexture(), 50);

        ImGui::EndDragDropSource();
    }
}

void FileDialog::ItemClick(FileDialogItem* item) const
{
    if (!item || !m_is_hovering_window)
        return;

    // Context menu on right click
    if (ImGui::IsItemClicked(1))
    {
        m_context_menu_id = item->GetId();
        ImGui::OpenPopup("##FileDialogContextMenu");
    }
}

void FileDialog::ItemContextMenu(FileDialogItem* item)
{
    if (m_context_menu_id != item->GetId())
        return;

    if (!ImGui::BeginPopup("##FileDialogContextMenu"))
        return;

    if (ImGui::MenuItem("Delete"))
    {
        if (item->IsDirectory())
        {
            FileSystem::Delete(item->GetPath());
            m_is_dirty = true;
        }
        else
        {
            FileSystem::Delete(item->GetPath());
            m_is_dirty = true;
        }
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Open in file explorer"))
    {
        FileSystem::OpenUrl(item->GetPath());
    }

    ImGui::EndPopup();
}

bool FileDialog::DialogUpdateFromDirectory(const string& file_path)
{
    if (!FileSystem::IsDirectory(file_path))
    {
        SP_LOG_ERROR("Provided path doesn't point to a directory.");
        return false;
    }

    m_items.clear();
    m_items.shrink_to_fit();

    // Get directories
    auto directories = FileSystem::GetDirectoriesInDirectory(file_path);
    for (const string& directory : directories)
    {
        m_items.emplace_back(directory, IconLoader::LoadFromFile(directory, IconType::Directory_Folder));
    }

    // Get files (based on filter)
    if (m_filter == FileDialog_Filter_All)
    {
        vector<string> paths_anything = FileSystem::GetFilesInDirectory(file_path);
        for (const string& anything : paths_anything)
        {
            if (!FileSystem::IsEngineTextureFile(anything) && !FileSystem::IsEngineModelFile(anything))
            {
                m_items.emplace_back(anything, IconLoader::LoadFromFile(anything, IconType::Undefined));
            }
        }
    }
    else if (m_filter == FileDialog_Filter_World)
    {
        vector<string> paths_world = FileSystem::GetSupportedSceneFilesInDirectory(file_path);
        for (const string& world : paths_world)
        {
            m_items.emplace_back(world, IconLoader::LoadFromFile(world, IconType::Directory_File_World));
        }
    }
    else if (m_filter == FileDialog_Filter_Model)
    {
        vector<string> paths_models = FileSystem::GetSupportedModelFilesInDirectory(file_path);
        for (const string& model : paths_models)
        {
            m_items.emplace_back(model, IconLoader::LoadFromFile(model, IconType::Directory_File_Model));
        }
    }

    return true;
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
        FileSystem::CreateDirectory(m_current_path + "/New folder");
        m_is_dirty = true;
    }

    if (ImGui::MenuItem("Create material"))
    {
        Material material = Material();
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
