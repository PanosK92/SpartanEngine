/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Rendering/Model.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

#define OPERATION_NAME    (m_operation == FileDialog_Op_Open)    ? "Open"        : (m_operation == FileDialog_Op_Load)    ? "Load"        : (m_operation == FileDialog_Op_Save) ? "Save" : "View"
#define FILTER_NAME        (m_filter == FileDialog_Filter_All)    ? "All (*.*)"    : (m_filter == FileDialog_Filter_Model)    ? "Model(*.*)"    : "World (*.world)"

FileDialog::FileDialog(Context* context, const bool standalone_window, const FileDialog_Type type, const FileDialog_Operation operation, const FileDialog_Filter filter)
{
    m_context                            = context;
    m_type                                = type;
    m_operation                            = operation;
    m_filter                            = filter;
    m_type                                = type;
    m_title                                = OPERATION_NAME;
    m_is_window                            = standalone_window;
    m_item_size                         = Vector2(100.0f, 100.0f);
    m_is_dirty                            = true;
    m_selection_made                    = false;
    m_callback_on_item_clicked            = nullptr;
    m_callback_on_item_double_clicked    = nullptr;
    m_navigation.Navigate(m_context->GetSubsystem<ResourceCache>()->GetProjectDirectory());
}

void FileDialog::SetOperation(const FileDialog_Operation operation)
{
    m_operation = operation;
    m_title        = OPERATION_NAME;
}

bool FileDialog::Show(bool* is_visible, string* directory /*= nullptr*/, string* file_path /*= nullptr*/)
{
    if (!(*is_visible))
    {
        m_is_dirty = true; // set as dirty as things can change till next time
        return false;
    }

    m_selection_made        = false;
    m_is_hovering_item        = false;
    m_is_hovering_window    = false;
    
    ShowTop(is_visible);    // Top menu    
    ShowMiddle();            // Contents of the current directory
    ShowBottom(is_visible); // Bottom menu

    if (m_is_window)
    {
        ImGui::End();
    }

    if (m_is_dirty)
    {
        DialogUpdateFromDirectory(m_navigation.m_path_current);
        m_is_dirty = false;
    }

    if (m_selection_made)
    {
        if (directory)
        {
            (*directory) = m_navigation.m_path_current;
        }

        if (file_path)
        {
            (*file_path) = m_navigation.m_path_current + "/" + string(m_input_box);
        }
    }

    EmptyAreaContextMenu();

    return m_selection_made;
}

void FileDialog::ShowTop(bool* is_visible)
{
    if (m_is_window)
    {
        ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(350, 250), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::Begin(m_title.c_str(), is_visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking);
        ImGui::SetWindowFocus();
    }
    
    // Directory navigation buttons
    {
        // Backwards
        if (ImGui::Button("<"))
        {
            m_is_dirty = m_navigation.Backward();
        }

        // Forwards
        ImGui::SameLine();
        if (ImGui::Button(">"))
        {
            m_is_dirty = m_navigation.Forward();
        }

        // Individual directories buttons
        for (uint32_t i = 0; i < m_navigation.m_path_hierarchy.size(); i++)
        {
            ImGui::SameLine();
            if (ImGui::Button(m_navigation.m_path_hierarchy_labels[i].c_str()))
            {
                m_is_dirty = m_navigation.Navigate(m_navigation.m_path_hierarchy[i]);
            }
        }
    }

    // Size slider
    const float slider_width = 200.0f;
    ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - slider_width);
    ImGui::PushItemWidth(slider_width);
    const float previous_width = m_item_size.x;
    ImGui::SliderFloat("##FileDialogSlider", &m_item_size.x, m_item_size_min, m_item_size_max);
    m_item_size.y += m_item_size.x - previous_width;
    ImGui::PopItemWidth();

    // Search filter
    const float label_width = 37.0f; //ImGui::CalcTextSize("Filter", nullptr, true).x;
    m_search_filter.Draw("Filter", ImGui::GetContentRegionAvail().x - label_width);

    ImGui::Separator();
}

void FileDialog::ShowMiddle()
{
    // Compute some useful stuff
    const auto window           = ImGui::GetCurrentWindowRead();
    const auto content_width    = ImGui::GetContentRegionAvail().x;
    const auto content_height   = ImGui::GetContentRegionAvail().y - m_offset_bottom;
    ImGuiContext& g             = *GImGui;
    ImGuiStyle& style           = ImGui::GetStyle();
    const float font_height     = g.FontSize;
    const float label_height    = font_height;
    const float text_offset     = 3.0f;
    float pen_x_min             = 0.0f;
    float pen_x                 = 0.0f;
    bool new_line               = true;
    m_displayed_item_count      = 0;
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
            float offset    = ImGui::GetStyle().ItemSpacing.x;
            pen_x_min       = ImGui::GetCursorPosX() + offset;
            ImGui::SetCursorPosX(pen_x_min);
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
                    ImVec4 color = ImGui::GetStyle().Colors[ImGuiCol_BorderShadow];
                    ImGui::GetWindowDrawList()->AddRectFilled(rect_button.Min, ImVec2(rect_label.Max.x + shadow_thickness, rect_label.Max.y + shadow_thickness), IM_COL32(color.x * 255, color.y * 255, color.z * 255, color.w * 255));
                }

                // THUMBNAIL
                {
                    ImGui::PushID(i);
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.25f));

                    if (ImGui::Button("##dummy", m_item_size))
                    {
                        // Determine type of click
                        item.Clicked();
                        const auto is_single_click = item.GetTimeSinceLastClickMs() > 500;

                        if (is_single_click)
                        {
                            // Updated input box
                            m_input_box = item.GetLabel();
                            // Callback
                            if (m_callback_on_item_clicked) m_callback_on_item_clicked(item.GetPath());
                        }
                        else // Double Click
                        {
                            m_is_dirty = m_navigation.Navigate(item.GetPath());
                            m_selection_made = !item.IsDirectory();

                            // When browsing files, open them on double click
                            if (m_type == FileDialog_Type_Browser)
                            {
                                if (!item.IsDirectory())
                                {
                                    FileSystem::OpenDirectoryWindow(item.GetPath());
                                }
                            }

                            // Callback
                            if (m_callback_on_item_double_clicked) m_callback_on_item_double_clicked(m_navigation.m_path_current);
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

                    ImGui::SetCursorScreenPos(ImVec2(rect_button.Min.x + style.FramePadding.x, rect_button.Min.y + style.FramePadding.y));
                    ImGui::Image(item.GetTexture(), ImVec2(
                        rect_button.Max.x - rect_button.Min.x - style.FramePadding.x * 2.0f,
                        rect_button.Max.y - rect_button.Min.y - style.FramePadding.y - label_height - 5.0f)
                    );

                    ImGui::PopStyleColor(2);
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
                ImGui::SetCursorPosX(pen_x);
                new_line = true;
            }
            else
            {
                ImGui::SameLine();
            }
        }

        if (!new_line)
            ImGui::EndGroup();
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
        m_offset_bottom = 20.0f;
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - m_offset_bottom);

        const char* text = (m_displayed_item_count == 1) ? "%d item" : "%d items";
        ImGui::Text(text, m_displayed_item_count);
    }
    else
    {
        // move to the bottom of the window
        m_offset_bottom = 35.0f;
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - m_offset_bottom);

        ImGui::PushItemWidth(ImGui::GetWindowSize().x - 235);
        ImGui::InputText("##InputBox", &m_input_box);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::Text(FILTER_NAME);

        ImGui::SameLine();
        if (ImGui::Button(OPERATION_NAME))
        {
            m_selection_made = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
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
        const auto set_payload = [this](const ImGuiEx::DragPayloadType type, const string& path)
        {
            m_drag_drop_payload.type = type;
            m_drag_drop_payload.data = path.c_str();
            ImGuiEx::CreateDragPayload(m_drag_drop_payload);
        };

        if (FileSystem::IsSupportedModelFile(item->GetPath()))    { set_payload(ImGuiEx::DragPayload_Model,        item->GetPath()); }
        if (FileSystem::IsSupportedImageFile(item->GetPath()))    { set_payload(ImGuiEx::DragPayload_Texture,        item->GetPath()); }
        if (FileSystem::IsSupportedAudioFile(item->GetPath()))    { set_payload(ImGuiEx::DragPayload_Audio,        item->GetPath()); }
        if (FileSystem::IsEngineScriptFile(item->GetPath()))    { set_payload(ImGuiEx::DragPayload_Script,        item->GetPath()); }
        if (FileSystem::IsEngineMaterialFile(item->GetPath()))  { set_payload(ImGuiEx::DragPayload_Material,    item->GetPath()); }

        // Preview
        ImGuiEx::Image(item->GetTexture(), 50);

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
        FileSystem::OpenDirectoryWindow(item->GetPath());
    }

    ImGui::EndPopup();
}

bool FileDialog::DialogUpdateFromDirectory(const std::string& path)
{
    if (!FileSystem::IsDirectory(path))
    {
        LOG_ERROR_INVALID_PARAMETER();
        return false;
    }

    m_items.clear();
    m_items.shrink_to_fit();

    // Get directories
    auto child_directories = FileSystem::GetDirectoriesInDirectory(path);
    for (const auto& child_dir : child_directories)
    {
        m_items.emplace_back(child_dir, IconProvider::Get().Thumbnail_Load(child_dir, Thumbnail_Folder, static_cast<int>(m_item_size.x)));
    }

    // Get files (based on filter)
    vector<string> child_files;
    if (m_filter == FileDialog_Filter_All)
    {
        child_files = FileSystem::GetFilesInDirectory(path);
        for (const auto& child_file : child_files)
        {
            if (!FileSystem::IsEngineTextureFile(child_file) && !FileSystem::IsEngineModelFile(child_file))
            {
                m_items.emplace_back(child_file, IconProvider::Get().Thumbnail_Load(child_file, Thumbnail_Custom, static_cast<int>(m_item_size.x)));
            }
        }
    }
    else if (m_filter == FileDialog_Filter_Scene)
    {
        child_files = FileSystem::GetSupportedSceneFilesInDirectory(path);
        for (const auto& child_file : child_files)
        {
            m_items.emplace_back(child_file, IconProvider::Get().Thumbnail_Load(child_file, Thumbnail_File_Scene, static_cast<int>(m_item_size.x)));
        }
    }
    else if (m_filter == FileDialog_Filter_Model)
    {
        child_files = FileSystem::GetSupportedModelFilesInDirectory(path);
        for (const auto& child_file : child_files)
        {
            m_items.emplace_back(child_file, IconProvider::Get().Thumbnail_Load(child_file, Thumbnail_File_Model, static_cast<int>(m_item_size.x)));
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
        FileSystem::CreateDirectory_(m_navigation.m_path_current + "/New folder");
        m_is_dirty = true;
    }

    if (ImGui::MenuItem("Create script"))
    {
        const string file_path = m_navigation.m_path_current + "/NewScript" + EXTENSION_SCRIPT;
        const string text =
            "using System;\n"
            "using Spartan;\n"
            "\n"
            "public class NewScript\n"
            "{\n"
            "\tpublic NewScript()\n"
            "\t{\n"
            "\n"
            "\t}\n"
            "\n"
            "\t// Start is called before the first frame update\n"
            "\tpublic void Start()\n"
            "\t{\n"
            "\n"
            "\t}\n"
            "\n"
            "\t// Update is called once per frame\n"
            "\tpublic void Update(float delta_time)\n"
            "\t{\n"
            "\n"
            "\t}\n"
            "}\n";

        FileSystem::CreateTextFile(file_path, text);

        m_is_dirty = true;
    }

    if (ImGui::MenuItem("Create material"))
    {
        Material material = Material(m_context);
        const string file_path = m_navigation.m_path_current + "/new_material" + EXTENSION_MATERIAL;
        material.SetResourceFilePath(file_path);
        material.SaveToFile(file_path);
        m_is_dirty = true;
    }

    if (ImGui::MenuItem("Open directory in explorer"))
    {
        FileSystem::OpenDirectoryWindow(m_navigation.m_path_current);
    }

    ImGui::EndPopup();
}
