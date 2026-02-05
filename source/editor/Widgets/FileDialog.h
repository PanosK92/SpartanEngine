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

#pragma once
//= INCLUDES ========================
#include "FileSystem/FileSystem.h"
#include "../ImGui/ImGui_Extension.h"
#include <chrono>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
//===================================

enum FileDialog_Type
{
    FileDialog_Type_Browser,
    FileDialog_Type_FileSelection
};

enum FileDialog_Operation
{
    FileDialog_Op_Open,
    FileDialog_Op_Load,
    FileDialog_Op_Save
};

enum FileDialog_Filter
{
    FileDialog_Filter_All,
    FileDialog_Filter_World,
    FileDialog_Filter_Model
};

enum FileDialog_SortColumn
{
    Sort_Name,
    Sort_Type,
    Sort_Modified
};

enum FileDialog_ViewMode
{
    View_Grid,
    View_List
};

class FileDialogItem
{
public:
    FileDialogItem(const std::string& path, spartan::RHI_Texture* icon)
    {
        m_path          = path;
        m_path_relative = spartan::FileSystem::GetRelativePath(path);
        m_icon          = icon;
        static uint32_t id = 0;
        m_id = id++;
        m_isDirectory = spartan::FileSystem::IsDirectory(path);
        m_label = spartan::FileSystem::GetFileNameFromFilePath(path);
    }
    const auto& GetPath() const { return m_path; }
    const auto& GetPathRelative() const { return m_path_relative; }
    const auto& GetLabel() const { return m_label; }
    uint32_t GetId() const { return m_id; }
    spartan::RHI_Texture* GetIcon() const { return m_icon; }
    auto IsDirectory() const { return m_isDirectory; }
    auto GetTimeSinceLastClickMs() const { return static_cast<float>(m_time_since_last_click.count()); }
    void Clicked()
    {
        const auto now = std::chrono::high_resolution_clock::now();
        m_time_since_last_click = now - m_last_click_time;
        m_last_click_time = now;
    }

private:
    spartan::RHI_Texture* m_icon;
    uint32_t m_id;
    std::string m_path;
    std::string m_path_relative;
    std::string m_label;
    bool m_isDirectory;
    std::chrono::duration<double, std::milli> m_time_since_last_click;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_last_click_time;
};

class FileDialog
{
public:
    FileDialog(bool standalone_window, FileDialog_Type type, FileDialog_Operation operation, FileDialog_Filter filter);

    // type & fFilter
    auto GetType() const { return m_type; }
    auto GetFilter() const { return m_filter; }

    // operation
    auto GetOperation() const { return m_operation; }
    void SetOperation(FileDialog_Operation operation);

    // path
    void SetCurrentPath(const std::string& path);

    // shows the dialog and returns true if a selection was made
    bool Show(bool* is_visible, Editor* editor, std::string* directory = nullptr, std::string* file_path = nullptr);
    void SetCallbackOnItemClicked(const std::function<void(const std::string&)>& callback) { m_callback_on_item_clicked = callback; }
    void SetCallbackOnItemDoubleClicked(const std::function<void(const std::string&)>& callback) { m_callback_on_item_double_clicked = callback; }

private:
    void ShowTop(bool* is_visible, Editor* editor);
    void ShowMiddle();
    void ShowBottom(bool* is_visible);
    void RenderItem(FileDialogItem* item, const ImVec2& size, bool is_list_view);

    // item functionality handling
    void ItemDrag(FileDialogItem* item) const;
    void ItemClick(FileDialogItem* item) const;
    void ItemContextMenu(FileDialogItem* item);

    // misc
    void DialogUpdateFromDirectory(const std::string& path);
    void EmptyAreaContextMenu();
    void HandleKeyboardNavigation();

    // flags
    bool m_is_window;
    bool m_selection_made;
    bool m_is_dirty;
    bool m_is_hovering_item;
    bool m_is_hovering_window;
    std::string m_title;
    std::string m_input_box;
    std::string m_hovered_item_path;
    uint32_t m_displayed_item_count;

    // internal
    mutable uint64_t m_context_menu_id;
    mutable ImGuiSp::DragDropPayload m_drag_drop_payload;
    float m_offset_bottom = 0.0f;
    FileDialog_Type m_type;
    FileDialog_Operation m_operation;
    FileDialog_Filter m_filter;
    std::vector<FileDialogItem> m_items;
    spartan::math::Vector2 m_item_size;
    ImGuiTextFilter m_search_filter;
    std::string m_current_path;
    std::string m_root_path;
    std::mutex m_mutex_items;

    // navigation history
    std::vector<std::string> m_history;
    size_t m_history_index;

    // view and sorting
    FileDialog_ViewMode m_view_mode;
    FileDialog_SortColumn m_sort_column;
    bool m_sort_ascending;

    // renaming
    bool m_is_renaming;
    std::string m_rename_buffer;
    uint32_t m_rename_item_id;

    // callbacks
    std::function<void(const std::string&)> m_callback_on_item_clicked;
    std::function<void(const std::string&)> m_callback_on_item_double_clicked;
};
