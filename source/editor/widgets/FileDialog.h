/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
//= INCLUDES ========================
#include "file_system/FileSystem.h"
#include "../imgui/ImGui_Extension.h"
#include <vector>
#include <string>
#include <functional>
#include <filesystem>
//===================================
// chrono and mutex are provided via pch.h to keep this header lean

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
    FileDialog_Filter_Model,
    FileDialog_Filter_Image
};

enum FileDialog_SortColumn
{
    Sort_Name,
    Sort_Type,
    Sort_Size,
    Sort_Modified
};

// what an entry is to the user, drives its tint, tag, status hint and the type chips
enum FileDialog_Kind
{
    Kind_Folder,
    Kind_Model,
    Kind_Texture,
    Kind_Material,
    Kind_Prefab,
    Kind_World,
    Kind_Script,
    Kind_Audio,
    Kind_Font,
    Kind_Archive,
    Kind_Other,
    Kind_Count
};

enum FileDialog_ViewMode
{
    View_Grid,
    View_List
};

class FileDialogItem
{
public:
    // atlas type icon, carries the shared atlas texture plus the icon's uv sub rect
    FileDialogItem(const std::string& path, const spartan::Icon& icon)
    {
        m_icon = icon;
        Init(path);
    }

    // standalone image thumbnail, samples the whole texture
    FileDialogItem(const std::string& path, spartan::RHI_Texture* thumbnail)
    {
        m_icon.texture = thumbnail;
        m_icon.uv_min  = spartan::math::Vector2(0.0f, 0.0f);
        m_icon.uv_max  = spartan::math::Vector2(1.0f, 1.0f);
        Init(path);
    }

    const auto& GetPath() const { return m_path; }
    const auto& GetPathRelative() const { return m_path_relative; }
    const auto& GetLabel() const { return m_label; }
    uint32_t GetId() const { return m_id; }
    const spartan::Icon& GetIcon() const { return m_icon; }
    auto IsDirectory() const { return m_is_directory; }
    FileDialog_Kind GetKind() const { return m_kind; }
    const std::string& GetExtension() const { return m_extension; }
    uint64_t GetSizeBytes() const { return m_size_bytes; }
    uint32_t GetChildCount() const { return m_child_count; }
    const std::filesystem::file_time_type& GetModified() const { return m_modified; }
    auto GetTimeSinceLastClickMs() const { return static_cast<float>(m_time_since_last_click.count()); }
    void Clicked()
    {
        const auto now = std::chrono::high_resolution_clock::now();
        m_time_since_last_click = now - m_last_click_time;
        m_last_click_time = now;
    }

private:
    void Init(const std::string& path)
    {
        m_path          = path;
        m_path_relative = spartan::FileSystem::GetRelativePath(path);
        static uint32_t id = 0;
        m_id          = id++;
        m_is_directory = spartan::FileSystem::IsDirectory(path);
        m_label       = spartan::FileSystem::GetFileNameFromFilePath(path);
        InitDetails();
    }

    // size, age and child count are read once here, the list view used to hit the disk for every row every frame
    void InitDetails();

    spartan::Icon m_icon;
    uint32_t m_id;
    std::string m_path;
    std::string m_path_relative;
    std::string m_label;
    std::string m_extension;
    bool m_is_directory;
    FileDialog_Kind m_kind   = Kind_Other;
    uint64_t m_size_bytes    = 0;
    uint32_t m_child_count   = 0;
    std::filesystem::file_time_type m_modified{};
    std::chrono::duration<double, std::milli> m_time_since_last_click;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_last_click_time;
};

class FileDialog
{
public:
    FileDialog(bool standalone_window, FileDialog_Type type, FileDialog_Operation operation, FileDialog_Filter filter);
    ~FileDialog();

    // type & filter
    auto GetType() const { return m_type; }
    auto GetFilter() const { return m_filter; }

    // operation
    auto GetOperation() const { return m_operation; }
    void SetOperation(FileDialog_Operation operation);

    // path
    const std::string& GetCurrentPath() const { return m_current_path; }
    void SetCurrentPath(const std::string& path);
    void SetDirty() { m_is_dirty = true; }

    // shows the dialog and returns true if a selection was made
    bool Show(bool* is_visible, Editor* editor, std::string* directory = nullptr, std::string* file_path = nullptr);
    void SetCallbackOnItemClicked(const std::function<void(const std::string&)>& callback) { m_callback_on_item_clicked = callback; }
    void SetCallbackOnItemDoubleClicked(const std::function<void(const std::string&)>& callback) { m_callback_on_item_double_clicked = callback; }
    void SetToolbarAction(const std::string& label, const std::function<void()>& callback) { m_toolbar_action_label = label; m_toolbar_action = callback; }

    // view state, also driven by the mcp so the browser can be reviewed without mouse input
    FileDialog_ViewMode GetViewMode() const { return m_view_mode; }
    void SetViewMode(const FileDialog_ViewMode mode) { m_view_mode = mode; }
    float GetItemSize() const { return m_item_size.x; }
    void SetItemSize(float size);
    void SetSearch(const std::string& text);
    int GetKindFilter() const { return m_kind_filter; }
    void SetKindFilter(const int kind) { m_kind_filter = kind; }
    bool SelectItem(const std::string& label);
    std::vector<std::string> GetVisibleLabels();
    std::string GetSelectedLabel();
    static const char* GetKindName(int kind);
    static const char* GetKindPlural(int kind);

private:
    void ShowTop(bool* is_visible, Editor* editor);
    void ShowMiddle();
    void ShowBottom(bool* is_visible);
    void ShowBreadcrumbs(float right_edge);
    void ShowKindChips(float width);
    void ShowEmptyState();

    // view rendering
    void RenderGridView();
    void RenderListView();

    // item functionality handling
    bool IsItemVisible(const FileDialogItem& item) const;
    void ItemReleased(FileDialogItem* item);
    void ItemDrag(FileDialogItem* item);
    void ItemClick(FileDialogItem* item) const;
    void ItemContextMenu(FileDialogItem* item);
    void NavigateTo(const std::string& path);

    // misc
    void DialogUpdateFromDirectory(const std::string& path);
    void EmptyAreaContextMenu();
    void HandleKeyboardNavigation();
    void ShowOverwriteDialog(std::string* directory, std::string* file_path);
    void WatchDirectory();
    void RenameItemInline(FileDialogItem* item, float width);

    // flags
    bool m_is_window;
    bool m_selection_made;
    bool m_is_dirty;
    bool m_is_hovering_item;
    bool m_is_hovering_window;
    std::string m_title;
    std::string m_input_box;
    std::string m_file_path_pending_overwrite;
    std::string m_hovered_item_path;
    uint32_t m_hovered_item_id = UINT32_MAX;
    uint32_t m_displayed_item_count;
    uint32_t m_kind_counts[Kind_Count] = {};
    int m_kind_filter                  = -1;

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

    // selection
    uint32_t m_selected_item_id;
    float m_hover_animation;
    bool m_was_dragging = false;

    // renaming
    bool m_is_renaming;
    bool m_rename_request_focus;
    bool m_rename_select_pending;
    std::string m_rename_buffer;
    uint32_t m_rename_item_id;

    // callbacks
    std::function<void(const std::string&)> m_callback_on_item_clicked;
    std::function<void(const std::string&)> m_callback_on_item_double_clicked;
    std::string m_toolbar_action_label;
    std::function<void()> m_toolbar_action;

    // directory watching for auto refresh on external changes
    std::filesystem::file_time_type m_watch_dir_time{};
    std::chrono::steady_clock::time_point m_watch_last_check{};
    std::string m_watch_path;
    uint64_t m_world_unloading_handle = 0;
};
