/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==============================
#include "pch.h"
#include <chrono>
#include <filesystem>
#include "FileDialog.h"
#include "../imgui/source/imgui_internal.h"
#include "../imgui/source/imgui_stdlib.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Style.h"
#include "../widgets/Viewport.h"
#include "../AssetThumbnails.h"
#include <rendering/Material.h>
#include "world/Entity.h"
#include "world/Prefab.h"
#include "world/World.h"
#include "world/components/Script.h"
#include "core/ThreadPool.h"
#include "core/Event.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    #define OPERATION_NAME (m_operation == FileDialog_Op_Open) ? "Open"      : (m_operation == FileDialog_Op_Load)   ? "Load"        : (m_operation == FileDialog_Op_Save) ? "Save" : "View"
    #define FILTER_NAME    (m_filter == FileDialog_Filter_All) ? "All (*.*)" : (m_filter == FileDialog_Filter_Model) ? "Model (*.*)" : (m_filter == FileDialog_Filter_Image) ? "Image (*.png, *.jpg)" : "World (*.world)"

    bool is_prompt_reference_image(const string& path)
    {
        const string extension = FileSystem::ConvertToUppercase(
            FileSystem::GetExtensionFromFilePath(path)
        );
        return
            extension == ".PNG" ||
            extension == ".JPG" ||
            extension == ".JPEG" ||
            extension == ".GIF" ||
            extension == ".WEBP";
    }

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
    ImU32 col_content_bg;
    ImU32 col_separator;

    constexpr std::string_view NewLuaScriptContents = R"(

-- Scripting Wiki: https://github.com/PanosK92/SpartanEngine/wiki/Scripting

MyScript = {
    -- Values here will be exposed to component details.
    -- bMyValue = true,
    -- MyString = "Hello, World!",
}

-- Called once when the simulation starts.
function MyScript:Start(Entity)
    -- Place initialization logic here
end

-- Called once when the simulation stops.
function MyScript:Stop(Entity)
    -- Place shutdown logic here
end

-- Called when the script component is removed from the entity.
function MyScript:Remove(Entity)
    -- Cleanup logic here
end

-- Called every frame before Tick. Useful to reset temporary states.
function MyScript:PreTick(Entity)
    -- Pre-update logic here
end

-- Called every frame. Main update function.
function MyScript:Tick(Entity)
    -- Frame update logic here
end

-- Called when the entity is being saved.
function MyScript:Save(Entity)
    -- Return a table with any custom data to save
end

-- Called when the entity is being loaded.
function MyScript:Load(Entity)
    -- Restore data from the table returned by Save
end

return MyScript
)";

    void update_colors()
    {
        ImGuiStyle& style      = ImGui::GetStyle();
        const ImVec4 bg        = ImGui::Style::bg_color_1;
        const ImVec4 surface   = ImGui::Style::bg_color_2;
        const ImVec4 accent    = ImGui::Style::color_accent_1;
        col_card_bg            = ImGui::ColorConvertFloat4ToU32(ImGui::Style::lerp(bg, surface, 0.24f));
        col_card_bg_hover      = ImGui::ColorConvertFloat4ToU32(ImGui::Style::lerp(bg, surface, 0.42f));
        col_card_bg_selected   = ImGui::ColorConvertFloat4ToU32(ImGui::Style::lerp(bg, accent, 0.20f));
        col_card_border        = ImGui::ColorConvertFloat4ToU32(ImGui::Style::lerp(bg, surface, 0.72f));
        col_card_border_hover  = ImGui::ColorConvertFloat4ToU32(accent);
        col_shadow            = IM_COL32(0, 0, 0, 50);
        col_accent            = ImGui::ColorConvertFloat4ToU32(accent);
        col_text              = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);
        col_text_dim          = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_TextDisabled]);
        col_toolbar_bg        = ImGui::ColorConvertFloat4ToU32(ImGui::Style::lerp(bg, surface, 0.18f));
        col_content_bg        = ImGui::ColorConvertFloat4ToU32(ImGui::Style::lerp(bg, surface, 0.08f));
        col_separator         = ImGui::ColorConvertFloat4ToU32(ImGui::Style::lerp(bg, surface, 0.62f));
    }

    struct kind_info
    {
        const char* name;
        const char* plural;
        ImVec4 tint;
        const char* hint;
    };

    // the tints match the inspector accents of the component each asset feeds, so a material reads coral in both places
    const kind_info& kind_of(const int kind)
    {
        static const kind_info table[Kind_Count] =
        {
            { "Folder",   "Folders",   ImVec4(0.55f, 0.72f, 0.86f, 1.0f), "Double-click to open" },
            { "Model",    "Models",    ImVec4(0.66f, 0.52f, 1.00f, 1.0f), "Drag into the viewport to import and place it" },
            { "Texture",  "Textures",  ImVec4(0.25f, 0.70f, 1.00f, 1.0f), "Drag onto a texture slot of a material" },
            { "Material", "Materials", ImVec4(1.00f, 0.52f, 0.42f, 1.0f), "Click to inspect, drag onto an object or a Render material slot to apply" },
            { "Prefab",   "Prefabs",   ImVec4(0.62f, 0.78f, 1.00f, 1.0f), "Drag into the viewport or the World panel to place it" },
            { "World",    "Worlds",    ImVec4(0.55f, 0.85f, 0.35f, 1.0f), "A saved world, open it from the File menu" },
            { "Script",   "Scripts",   ImVec4(0.80f, 0.90f, 0.40f, 1.0f), "Drag onto a Script component, right-click to reload it" },
            { "Audio",    "Audio",     ImVec4(1.00f, 0.42f, 0.66f, 1.0f), "Drag onto the clip of an Audio Source" },
            { "Font",     "Fonts",     ImVec4(0.82f, 0.55f, 1.00f, 1.0f), "A font for 3D Text and the interface" },
            { "Archive",  "Archives",  ImVec4(1.00f, 0.78f, 0.30f, 1.0f), "Double-click to open with the system" },
            { "File",     "Other",     ImVec4(0.60f, 0.62f, 0.66f, 1.0f), "Double-click to open with the system" }
        };
        return table[(kind >= 0 && kind < Kind_Count) ? kind : Kind_Other];
    }

    string format_size(const uint64_t bytes)
    {
        char buffer[32];
        if (bytes < 1024)
        {
            snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
        }
        else if (bytes < 1024ull * 1024)
        {
            snprintf(buffer, sizeof(buffer), "%.1f KB", bytes / 1024.0);
        }
        else if (bytes < 1024ull * 1024 * 1024)
        {
            snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024.0));
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
        }
        return buffer;
    }

    // how long ago, which is what people scan for, the exact date only once it is over a month old
    string format_age(const filesystem::file_time_type& time)
    {
        if (time == filesystem::file_time_type{})
        {
            return "";
        }
        const auto seconds = chrono::duration_cast<chrono::seconds>(filesystem::file_time_type::clock::now() - time).count();
        char buffer[32];
        if (seconds < 60)
        {
            return "just now";
        }
        if (seconds < 3600)
        {
            snprintf(buffer, sizeof(buffer), "%lld min ago", static_cast<long long>(seconds / 60));
            return buffer;
        }
        if (seconds < 86400)
        {
            snprintf(buffer, sizeof(buffer), "%lld h ago", static_cast<long long>(seconds / 3600));
            return buffer;
        }
        if (seconds < 2 * 86400)
        {
            return "yesterday";
        }
        if (seconds < 30 * 86400)
        {
            snprintf(buffer, sizeof(buffer), "%lld days ago", static_cast<long long>(seconds / 86400));
            return buffer;
        }
        const time_t stamp = chrono::system_clock::to_time_t(chrono::clock_cast<chrono::system_clock>(time));
        tm local = {};
        localtime_s(&local, &stamp);
        strftime(buffer, sizeof(buffer), "%d %b %Y", &local);
        return buffer;
    }

    // the longest prefix of [begin, end) that fits in width
    const char* fit_prefix(const char* begin, const char* end, const float width)
    {
        const char* low  = begin;
        const char* high = end;
        while (low < high)
        {
            const char* middle = low + (high - low + 1) / 2;
            if (ImGui::CalcTextSize(begin, middle).x <= width)
            {
                low = middle;
            }
            else
            {
                high = middle - 1;
            }
        }
        return low;
    }

    // splits a name over two lines, breaking after _ - . or a space so "car_playground_resources" reads as words,
    // returns where the second line starts, or the end when the name fits on one line
    const char* wrap_break(const char* begin, const char* end, const float width)
    {
        const char* fit = fit_prefix(begin, end, width);
        if (fit >= end)
        {
            return end;
        }
        // a break that leaves the first line mostly empty wastes the room the second line needs, below 40% cut at the edge instead
        const char* earliest = begin + max<ptrdiff_t>(1, (fit - begin) * 2 / 5);
        for (const char* c = fit; c > earliest; c--)
        {
            const char previous = *(c - 1);
            if (previous == '_' || previous == '-' || previous == '.' || previous == ' ')
            {
                return c;
            }
        }
        return fit > begin ? fit : begin + 1;
    }
}

void FileDialogItem::InitDetails()
{
    error_code error;
    const filesystem::path fs_path(m_path);
    m_modified = filesystem::last_write_time(fs_path, error);
    if (error)
    {
        m_modified = {};
    }

    if (m_is_directory)
    {
        m_kind = Kind_Folder;
        uint32_t count = 0;
        for (filesystem::directory_iterator it(fs_path, error), end; !error && it != end && count < 9999; it.increment(error))
        {
            count++;
        }
        m_child_count = count;
        return;
    }

    m_size_bytes = filesystem::file_size(fs_path, error);
    if (error)
    {
        m_size_bytes = 0;
    }

    string extension = FileSystem::GetExtensionFromFilePath(m_path);
    if (!extension.empty() && extension[0] == '.')
    {
        extension.erase(0, 1);
    }
    m_extension = FileSystem::ConvertToUppercase(extension);

    if (FileSystem::IsSupportedModelFile(m_path))        m_kind = Kind_Model;
    else if (FileSystem::IsSupportedImageFile(m_path))   m_kind = Kind_Texture;
    else if (FileSystem::IsEngineTextureFile(m_path))    m_kind = Kind_Texture;
    else if (FileSystem::IsEngineMaterialFile(m_path))   m_kind = Kind_Material;
    else if (FileSystem::IsEnginePrefabFile(m_path))     m_kind = Kind_Prefab;
    else if (FileSystem::IsEngineWorldFile(m_path))      m_kind = Kind_World;
    else if (FileSystem::IsEngineLuaFile(m_path))        m_kind = Kind_Script;
    else if (FileSystem::IsSupportedAudioFile(m_path))   m_kind = Kind_Audio;
    else if (FileSystem::IsSupportedFontFile(m_path))    m_kind = Kind_Font;
    else if (m_extension == "7Z" || m_extension == "ZIP") m_kind = Kind_Archive;
    else                                                  m_kind = Kind_Other;
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
    m_is_renaming                     = false;
    m_rename_request_focus            = false;
    m_rename_select_pending           = false;
    m_rename_item_id                  = UINT32_MAX;
    m_context_menu_id                 = 0;
    m_world_unloading_handle          = SP_SUBSCRIBE_TO_EVENT(
        EventType::WorldUnloading,
        SP_EVENT_HANDLER_EXPRESSION(
            {
                lock_guard<mutex> lock(m_mutex_items);
                m_items.clear();
                m_is_dirty = true;
            }
        )
    );
}

FileDialog::~FileDialog()
{
    if (m_world_unloading_handle != 0)
    {
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldUnloading, m_world_unloading_handle);
        m_world_unloading_handle = 0;
    }
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
        m_input_box    = FileSystem::GetFileNameFromFilePath(path);
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

void FileDialog::NavigateTo(const string& path)
{
    if (path.empty() || path == m_current_path)
    {
        return;
    }
    // drop the forward history, like any browser, going somewhere new ends the old branch
    if (m_history_index + 1 < m_history.size())
    {
        m_history.resize(m_history_index + 1);
    }
    m_current_path = path;
    m_history.push_back(m_current_path);
    m_history_index = m_history.size() - 1;
    m_kind_filter   = -1;
    m_is_dirty      = true;
}

void FileDialog::SetItemSize(const float size)
{
    m_item_size.x = clamp(size, item_size_min, item_size_max);
}

void FileDialog::SetSearch(const string& text)
{
    strncpy_s(m_search_filter.InputBuf, sizeof(m_search_filter.InputBuf), text.c_str(), _TRUNCATE);
    m_search_filter.Build();
}

bool FileDialog::SelectItem(const string& label)
{
    lock_guard<mutex> lock(m_mutex_items);
    for (const FileDialogItem& item : m_items)
    {
        if (item.GetLabel() == label)
        {
            m_selected_item_id = item.GetId();
            return true;
        }
    }
    return false;
}

vector<string> FileDialog::GetVisibleLabels()
{
    vector<string> labels;
    lock_guard<mutex> lock(m_mutex_items);
    for (const FileDialogItem& item : m_items)
    {
        if (IsItemVisible(item))
        {
            labels.push_back(item.GetLabel());
        }
    }
    return labels;
}

string FileDialog::GetSelectedLabel()
{
    lock_guard<mutex> lock(m_mutex_items);
    for (const FileDialogItem& item : m_items)
    {
        if (item.GetId() == m_selected_item_id)
        {
            return item.GetLabel();
        }
    }
    return "";
}

const char* FileDialog::GetKindName(const int kind)
{
    return kind_of(kind).name;
}

const char* FileDialog::GetKindPlural(const int kind)
{
    return kind_of(kind).plural;
}

bool FileDialog::IsItemVisible(const FileDialogItem& item) const
{
    if (m_kind_filter >= 0 && item.GetKind() != m_kind_filter)
    {
        return false;
    }
    return m_search_filter.PassFilter(item.GetLabel().c_str());
}

bool FileDialog::Show(bool* is_visible, Editor* editor, string* directory /*= nullptr*/, string* file_path /*= nullptr*/)
{
    if (!(*is_visible))
    {
        m_is_dirty = true;
        m_file_path_pending_overwrite.clear();
        return false;
    }

    update_colors();

    WatchDirectory();

    m_selection_made     = false;
    m_is_hovering_item   = false;
    m_is_hovering_window = false;
    m_hovered_item_path.clear();
    m_hovered_item_id    = UINT32_MAX;

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

    EmptyAreaContextMenu();
    HandleKeyboardNavigation();

    if (m_selection_made && file_path && m_input_box.empty())
    {
        m_selection_made = false;
    }

    if (m_selection_made && file_path)
    {
        string dir = m_current_path;
        if (FileSystem::IsFile(dir))
        {
            dir = FileSystem::GetDirectoryFromFilePath(dir);
        }

        // ensure trailing separator between directory and filename
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        {
            dir += "/";
        }

        const string selected_file_path = dir + m_input_box;
        if (m_operation == FileDialog_Op_Save && FileSystem::IsFile(selected_file_path))
        {
            m_file_path_pending_overwrite = selected_file_path;
            ImGui::OpenPopup("##overwrite_dialog");
            m_selection_made = false;
        }
        else
        {
            if (directory)
            {
                (*directory) = dir;
            }
            (*file_path) = selected_file_path;
        }
    }

    ShowOverwriteDialog(directory, file_path);

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

    return m_selection_made;
}

void FileDialog::ShowOverwriteDialog(string* directory, string* file_path)
{
    // center the popup on the main viewport so it doesn't appear at the top left of the monitor
    ImVec2 viewport_center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(viewport_center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    // suppress the dim overlay that imgui draws behind modal popups so the rest of the editor stays visible
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0));

    if (ImGui::BeginPopupModal("##overwrite_dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::Text("Overwrite existing file?");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("The selected file already exists:");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_file_path_pending_overwrite.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        float button_width = 90.0f;
        float buttons_x    = ImGui::GetContentRegionAvail().x - button_width * 2 - 8;
        ImGui::SetCursorPosX(buttons_x);

        if (ImGui::Button("Cancel", ImVec2(button_width, 0)))
        {
            m_file_path_pending_overwrite.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine(0, 8);

        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
        if (ImGui::Button("Overwrite", ImVec2(button_width, 0)))
        {
            if (directory)
            {
                (*directory) = m_current_path;
            }

            if (file_path)
            {
                (*file_path) = m_file_path_pending_overwrite;
            }

            m_selection_made = true;
            m_file_path_pending_overwrite.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
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
    }

    // what is in this folder, per kind, the chips and the status bar both read it
    {
        lock_guard<mutex> lock(m_mutex_items);
        memset(m_kind_counts, 0, sizeof(m_kind_counts));
        for (const FileDialogItem& item : m_items)
        {
            m_kind_counts[item.GetKind()]++;
        }
    }
    if (m_kind_filter >= 0 && (m_kind_filter >= Kind_Count || m_kind_counts[m_kind_filter] == 0))
    {
        m_kind_filter = -1;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float dpi       = spartan::Window::GetDpiScale();
    float window_width    = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

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

    const float button_height = ImGui::GetFrameHeight();
    const bool is_grid_mode   = m_view_mode == View_Grid;
    const float grid_btn_w    = ImGui::CalcTextSize("Grid").x + ImGui::GetStyle().FramePadding.x * 2;
    const float list_btn_w    = ImGui::CalcTextSize("List").x + ImGui::GetStyle().FramePadding.x * 2;
    const float slider_width  = is_grid_mode && ImGui::GetWindowWidth() >= 520.0f * dpi ? 80.0f * dpi : 0.0f;
    const float slider_gap    = slider_width > 0.0f ? 8.0f : 0.0f;
    const float action_width  = m_toolbar_action ? ImGuiSp::command_button_width(m_toolbar_action_label.c_str()) : 0.0f;
    const float action_gap    = m_toolbar_action ? 10.0f : 0.0f;
    const float toggle_width  = grid_btn_w + list_btn_w;
    const float controls_width = toggle_width + slider_gap + slider_width + action_gap + action_width;
    const float controls_x     = ImGui::GetWindowWidth() - controls_width - 8.0f;
    const bool controls_on_new_line = controls_x < 220.0f * dpi;

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.14f));

    const spartan::math::Vector2 nav_icon_size(ImGui::GetFontSize(), ImGui::GetFontSize());

    // refresh is gone, the folder is watched and refreshes itself, f5 and the right-click menu remain for the rare manual case
    const bool can_go_back = m_history_index > 0;
    ImGui::BeginDisabled(!can_go_back);
    if (ImGuiSp::image_button(spartan::IconType::ArrowLeft, nav_icon_size, false))
    {
        m_history_index--;
        m_current_path = m_history[m_history_index];
        m_kind_filter  = -1;
        m_is_dirty     = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Back  (Alt+Left)");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    const bool can_go_forward = m_history_index + 1 < m_history.size();
    ImGui::BeginDisabled(!can_go_forward);
    if (ImGuiSp::image_button(spartan::IconType::ArrowRight, nav_icon_size, false))
    {
        m_history_index++;
        m_current_path = m_history[m_history_index];
        m_kind_filter  = -1;
        m_is_dirty     = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Forward  (Alt+Right)");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    const string parent_path = FileSystem::GetParentDirectory(m_current_path);
    const bool can_go_up     = !parent_path.empty() && parent_path != m_current_path;
    ImGui::BeginDisabled(!can_go_up);
    if (ImGuiSp::image_button(spartan::IconType::ArrowUp, nav_icon_size, false))
    {
        NavigateTo(parent_path);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Up one folder  (Alt+Up or Backspace)");
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0, 10);
    {
        ImVec2 sep_pos = ImGui::GetCursorScreenPos();
        draw_list->AddLine(ImVec2(sep_pos.x, sep_pos.y + 4), ImVec2(sep_pos.x, sep_pos.y + button_height - 4), col_separator, 1.0f);
        ImGui::Dummy(ImVec2(1, button_height));
        ImGui::SameLine(0, 8);
    }

    const float breadcrumb_end = controls_on_new_line ? ImGui::GetWindowWidth() - 8.0f : controls_x - 12.0f;
    ShowBreadcrumbs(ImGui::GetWindowPos().x + breadcrumb_end);

    // right side, the view toggle and the size belong together, the one primary action sits at the far edge where the eye ends
    {
        if (controls_on_new_line)
        {
            ImGui::NewLine();
        }
        else
        {
            ImGui::SameLine();
        }
        ImGui::SetCursorPosX(max(8.0f, controls_x));

        // a segmented control, two halves of one frame, so it reads as a choice rather than two unrelated buttons
        {
            const ImVec2 toggle_min = ImGui::GetCursorScreenPos();
            const ImVec2 toggle_max = ImVec2(toggle_min.x + toggle_width, toggle_min.y + button_height);
            draw_list->AddRectFilled(toggle_min, toggle_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.04f)), 4.0f);
            draw_list->AddRect(toggle_min, toggle_max, ImGui::EditorUi::color(ImGui::Style::color_border), 4.0f);

            auto view_button = [](const char* label, const bool active, const char* tooltip)
            {
                const ImVec4 accent = ImGui::Style::color_accent_1;
                ImGui::PushStyleColor(ImGuiCol_Button, active ? ImGui::EditorUi::alpha(accent, 0.18f) : ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? ImGui::EditorUi::alpha(accent, 0.24f) : ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.08f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? ImGui::EditorUi::alpha(accent, 0.32f) : ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.14f));
                ImGui::PushStyleColor(ImGuiCol_Text, active ? ImGui::Style::color_accent_hi : ImGui::Style::color_text_muted);
                const bool pressed = ImGui::Button(label);
                ImGui::PopStyleColor(4);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", tooltip);
                }
                return pressed;
            };

            if (view_button("Grid", is_grid_mode, "Thumbnails, best for textures and materials"))
            {
                m_view_mode = View_Grid;
            }
            ImGui::SameLine(0, 0);
            if (view_button("List", !is_grid_mode, "Details, sort by name, kind, size or date"))
            {
                m_view_mode = View_List;
            }
        }

        if (slider_width > 0.0f)
        {
            ImGui::SameLine(0, slider_gap);
            ImGui::SetNextItemWidth(slider_width);
            ImGui::SliderFloat("##size", &m_item_size.x, item_size_min, item_size_max, "");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Thumbnail size, names and tags appear from medium size up");
            }
        }

        if (m_toolbar_action)
        {
            ImGui::SameLine(0, action_gap);
            if (ImGuiSp::command_button(m_toolbar_action_label.c_str(), ImVec2(action_width, button_height), true))
            {
                m_toolbar_action();
            }
        }
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);

    ImGui::Dummy(ImVec2(0, 2));

    // search and type filter share a row, they answer the same question, what am I looking for
    ImVec2 search_min;
    ImVec2 search_max;
    {
        ImGui::SetCursorPosX(8);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));

        const bool is_browser = m_type == FileDialog_Type_Browser;
        int kinds_present     = 0;
        for (uint32_t count : m_kind_counts)
        {
            kinds_present += count > 0 ? 1 : 0;
        }
        const bool show_chips = is_browser && kinds_present > 1;

        const float row_width = ImGui::GetContentRegionAvail().x - 8.0f;
        float search_width    = row_width;
        if (!is_browser)
        {
            search_width -= 120.0f * dpi;
        }
        else if (show_chips)
        {
            search_width = clamp(row_width * 0.32f, min(row_width, 150.0f * dpi), 300.0f * dpi);
        }

        ImGui::SetNextItemWidth(search_width);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(col_card_bg));
        ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
        if (ImGui::InputTextWithHint("##search", "Search this folder", m_search_filter.InputBuf, IM_ARRAYSIZE(m_search_filter.InputBuf), ImGuiInputTextFlags_EscapeClearsAll))
        {
            m_search_filter.Build();
        }
        search_min = ImGui::GetItemRectMin();
        search_max = ImGui::GetItemRectMax();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        if (show_chips)
        {
            ImGui::SameLine(0, 10);
            ShowKindChips(ImGui::GetContentRegionAvail().x - 8.0f);
        }

        if (!is_browser)
        {
            ImGui::SameLine(0, 8);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::SetNextItemWidth(100 * dpi);
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
                if (ImGui::Selectable("Image (*.png, *.jpg)", m_filter == FileDialog_Filter_Image))
                {
                    m_filter   = FileDialog_Filter_Image;
                    m_is_dirty = true;
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
        }
    }

    // counted after the search field so a keystroke shows up in the same frame
    {
        lock_guard<mutex> lock(m_mutex_items);
        m_displayed_item_count = 0;
        for (const FileDialogItem& item : m_items)
        {
            m_displayed_item_count += IsItemVisible(item) ? 1 : 0;
        }
    }

    // the result count lives inside the field, where the eyes are while typing
    if (m_search_filter.IsActive())
    {
        char matches[32];
        snprintf(matches, sizeof(matches), m_displayed_item_count == 1 ? "%u match" : "%u matches", m_displayed_item_count);
        const ImVec2 size = ImGui::CalcTextSize(matches);
        const float x     = search_max.x - size.x - 8.0f;
        if (x > search_min.x + ImGui::CalcTextSize(m_search_filter.InputBuf).x + 24.0f)
        {
            const ImVec4 tint = m_displayed_item_count == 0 ? ImVec4(1.0f, 0.55f, 0.45f, 1.0f) : ImGui::Style::color_text_muted;
            draw_list->AddText(ImVec2(x, (search_min.y + search_max.y - size.y) * 0.5f), ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, 0.8f)), matches);
        }
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImVec2 sep_pos  = ImGui::GetCursorScreenPos();
    float sep_width = ImGui::GetContentRegionAvail().x;
    draw_list->AddLine(sep_pos, ImVec2(sep_pos.x + sep_width, sep_pos.y), col_separator);
    ImGui::Dummy(ImVec2(0, 1));
}

void FileDialog::ShowBreadcrumbs(const float right_edge)
{
    struct crumb
    {
        string label;
        string path;
    };
    vector<crumb> crumbs;
    {
        char current_path[1024];
        strncpy_s(current_path, sizeof(current_path), m_current_path.c_str(), _TRUNCATE);

        string accumulated;
        char* context = nullptr;
        for (char* token = strtok_s(current_path, "/\\", &context); token; token = strtok_s(nullptr, "/\\", &context))
        {
            if (strcmp(token, "..") == 0)
            {
                continue;
            }
            accumulated += token;
            accumulated += "/";
            crumbs.push_back({ token, accumulated });
        }
    }
    if (crumbs.empty())
    {
        return;
    }

    ImDrawList* draw_list     = ImGui::GetWindowDrawList();
    const float button_height = ImGui::GetFrameHeight();
    const float pad_x         = ImGui::GetStyle().FramePadding.x;
    const float chevron_width = ImGui::GetFontSize() * 0.9f;
    const float available     = right_edge - ImGui::GetCursorScreenPos().x;

    auto crumb_width = [&](const string& label)
    {
        return ImGui::CalcTextSize(label.c_str()).x + pad_x * 2.0f;
    };

    // when the path is deep, the leading folders collapse into one button so the folder you are in is never the one cut off
    const float ellipsis_width = crumb_width("...") + chevron_width;
    size_t first               = crumbs.size() - 1;
    float used                 = crumb_width(crumbs.back().label);
    while (first > 0)
    {
        const float next     = crumb_width(crumbs[first - 1].label) + chevron_width;
        const float reserved = first - 1 > 0 ? ellipsis_width : 0.0f;
        if (used + next + reserved > available)
        {
            break;
        }
        used += next;
        first--;
    }

    const ImVec2 clip_min = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(clip_min, ImVec2(max(clip_min.x, right_edge), clip_min.y + button_height), true);

    auto chevron = [&]()
    {
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(chevron_width, button_height));
        ImGui::EditorUi::draw_chevron(
            draw_list,
            ImVec2(position.x + chevron_width * 0.5f, position.y + button_height * 0.5f),
            ImGui::GetFontSize() * 0.32f,
            0.0f,
            ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.7f)
        );
        ImGui::SameLine(0, 0);
    };

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (first > 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        if (ImGui::Button("...##crumbs_hidden"))
        {
            NavigateTo(crumbs[first - 1].path);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", crumbs[first - 1].path.c_str());
        }
        ImGui::SameLine(0, 0);
    }

    for (size_t i = first; i < crumbs.size(); i++)
    {
        if (i > first || first > 0)
        {
            chevron();
        }

        // the folder you are in reads as a title, the parents as quiet links back up
        const bool is_current = i + 1 == crumbs.size();
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, is_current ? ImGui::Style::color_text : ImGui::Style::color_text_muted);
        if (ImGui::Button(crumbs[i].label.c_str()) && !is_current)
        {
            NavigateTo(crumbs[i].path);
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::SameLine(0, 0);
    }
    ImGui::PopStyleColor();

    ImGui::PopClipRect();
}

void FileDialog::ShowKindChips(const float width)
{
    struct chip
    {
        int kind;
        string label;
    };
    vector<chip> chips;
    uint32_t total = 0;
    for (uint32_t count : m_kind_counts)
    {
        total += count;
    }
    chips.push_back({ -1, "All " + to_string(total) });
    for (int kind = 0; kind < Kind_Count; kind++)
    {
        if (m_kind_counts[kind] > 0)
        {
            chips.push_back({ kind, string(kind_of(kind).plural) + " " + to_string(m_kind_counts[kind]) });
        }
    }

    const float height = ImGui::GetFrameHeight();
    const float pad_x  = ImGui::EditorUi::scaled(9.0f);
    const float dot    = ImGui::EditorUi::scaled(3.0f);
    const float gap    = ImGui::EditorUi::scaled(4.0f);
    auto chip_width = [&](const chip& c)
    {
        return ImGui::CalcTextSize(c.label.c_str()).x + pad_x * 2.0f + (c.kind >= 0 ? dot * 2.0f + gap + 2.0f : 0.0f);
    };

    float total_width = 0.0f;
    for (const chip& c : chips)
    {
        total_width += chip_width(c) + gap;
    }

    // too narrow for the chips, the same choices collapse into a dropdown rather than wrapping onto a third row
    if (total_width - gap > width)
    {
        const string preview = m_kind_filter >= 0 ? string(kind_of(m_kind_filter).plural) : string("All types");
        ImGui::SetNextItemWidth(min(width, ImGui::EditorUi::scaled(150.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::BeginCombo("##kind_filter", preview.c_str()))
        {
            for (const chip& c : chips)
            {
                if (ImGui::Selectable(c.label.c_str(), m_kind_filter == c.kind))
                {
                    m_kind_filter = c.kind;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < chips.size(); i++)
    {
        const chip& c = chips[i];
        if (i > 0)
        {
            ImGui::SameLine(0, gap);
        }

        ImGui::PushID(c.kind + 1);
        const bool pressed = ImGui::InvisibleButton("##chip", ImVec2(chip_width(c), height));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        const bool active = m_kind_filter == c.kind;
        if (pressed)
        {
            // clicking the active chip again clears it, no need to hunt for "All"
            m_kind_filter = (active && c.kind >= 0) ? -1 : c.kind;
        }

        const ImVec2 min_pos = ImGui::GetItemRectMin();
        const ImVec2 max_pos = ImGui::GetItemRectMax();
        const ImVec4 tint    = c.kind >= 0 ? kind_of(c.kind).tint : ImGui::Style::color_accent_1;
        const float rounding = height * 0.5f;
        const ImVec4 fill    = active ? ImGui::EditorUi::alpha(tint, 0.16f) : (hovered ? ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.06f) : ImVec4(0, 0, 0, 0));
        draw_list->AddRectFilled(min_pos, max_pos, ImGui::EditorUi::color(fill), rounding);
        draw_list->AddRect(min_pos, max_pos, ImGui::EditorUi::color(active ? ImGui::EditorUi::alpha(tint, 0.6f) : ImGui::Style::color_border), rounding);

        float x = min_pos.x + pad_x;
        if (c.kind >= 0)
        {
            draw_list->AddCircleFilled(ImVec2(x + dot, (min_pos.y + max_pos.y) * 0.5f), dot, ImGui::EditorUi::color(tint), 12);
            x += dot * 2.0f + gap + 2.0f;
        }
        const ImVec4 text = active || hovered ? ImGui::Style::color_text : ImGui::Style::color_text_muted;
        draw_list->AddText(ImVec2(x, min_pos.y + (height - ImGui::GetFontSize()) * 0.5f), ImGui::EditorUi::color(text), c.label.c_str());

        if (hovered && c.kind >= 0)
        {
            if (active)
            {
                ImGui::SetTooltip("Click again to show everything");
            }
            else
            {
                ImGui::SetTooltip("Show only %s", kind_of(c.kind).plural);
            }
        }
    }
}

void FileDialog::ShowEmptyState()
{
    const bool filtered   = m_search_filter.IsActive() || m_kind_filter >= 0;
    const char* title     = filtered ? "Nothing here matches" : "This folder is empty";
    const char* detail    = filtered ? "Try another word, or clear the search and the type filter." :
                            (m_type == FileDialog_Type_Browser ? "Right-click for a new folder, script or material, or drag an entity from the World panel here to save it as a prefab." :
                                                                 "Nothing in this folder can be opened by this dialog.");
    const float width     = ImGui::GetContentRegionAvail().x;
    const float wrap      = min(width - 32.0f, ImGui::EditorUi::scaled(420.0f));
    const float origin_x  = ImGui::GetCursorPosX();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + max(16.0f, ImGui::GetContentRegionAvail().y * 0.28f));

    const float title_width = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(origin_x + max(0.0f, (width - title_width) * 0.5f));
    ImGui::TextColored(ImGui::Style::color_text, "%s", title);

    const ImVec2 detail_size = ImGui::CalcTextSize(detail, nullptr, false, wrap);
    const float detail_x     = origin_x + max(0.0f, (width - detail_size.x) * 0.5f);
    ImGui::SetCursorPosX(detail_x);
    ImGui::PushTextWrapPos(detail_x + wrap);
    ImGui::TextColored(ImGui::Style::color_text_muted, "%s", detail);
    ImGui::PopTextWrapPos();

    if (filtered)
    {
        ImGui::Dummy(ImVec2(0, 4));
        const char* label        = "Clear filters";
        const float button_width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(origin_x + max(0.0f, (width - button_width) * 0.5f));
        if (ImGui::Button(label))
        {
            SetSearch("");
            m_kind_filter = -1;
        }
    }
}

void FileDialog::ShowMiddle()
{
    const float content_width  = ImGui::GetContentRegionAvail().x;
    const float content_height = ImGui::GetContentRegionAvail().y - m_offset_bottom;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(col_content_bg));

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

    // drop target for entities dragged from the world hierarchy - saves as a .prefab file
    if (m_type == FileDialog_Type_Browser)
    {
        const ImVec2 content_min = ImGui::GetItemRectMin();
        const ImVec2 content_max = ImGui::GetItemRectMax();

        // the drop was invisible before, now the panel says what will happen before you let go
        const ImGuiPayload* dragged = ImGui::GetDragDropPayload();
        if (dragged && dragged->IsDataType("ENTITY") && ImGui::IsMouseHoveringRect(content_min, content_max))
        {
            ImDrawList* overlay = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
            const ImVec4 accent = ImGui::Style::color_accent_1;
            overlay->AddRectFilled(content_min, content_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, 0.06f)), 4.0f);
            overlay->AddRect(ImVec2(content_min.x + 1, content_min.y + 1), ImVec2(content_max.x - 1, content_max.y - 1), ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, 0.8f)), 4.0f, 2.0f);

            string folder = m_current_path;
            while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\'))
            {
                folder.pop_back();
            }
            folder = FileSystem::GetFileNameFromFilePath(folder);
            const string message   = "Drop to save it as a prefab in " + folder;
            const ImVec2 text_size = ImGui::CalcTextSize(message.c_str());
            const ImVec2 pad       = ImVec2(ImGui::EditorUi::scaled(14.0f), ImGui::EditorUi::scaled(8.0f));
            const ImVec2 center    = ImVec2((content_min.x + content_max.x) * 0.5f, (content_min.y + content_max.y) * 0.5f);
            const ImVec2 pill_min  = ImVec2(center.x - text_size.x * 0.5f - pad.x, center.y - text_size.y * 0.5f - pad.y);
            const ImVec2 pill_max  = ImVec2(center.x + text_size.x * 0.5f + pad.x, center.y + text_size.y * 0.5f + pad.y);
            overlay->AddRectFilled(pill_min, pill_max, ImGui::EditorUi::color(ImGui::Style::color_panel), (pill_max.y - pill_min.y) * 0.5f);
            overlay->AddRect(pill_min, pill_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, 0.8f)), (pill_max.y - pill_min.y) * 0.5f);
            overlay->AddText(ImVec2(pill_min.x + pad.x, pill_min.y + pad.y), ImGui::EditorUi::color(ImGui::Style::color_text), message.c_str());
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                if (payload->DataSize == sizeof(uint64_t))
                {
                    const uint64_t entity_id = *(const uint64_t*)payload->Data;
                    if (Entity* entity = World::GetEntityById(entity_id))
                    {
                        // save the entity as a .prefab file in the current browser directory
                        string prefab_path = m_current_path + "/" + entity->GetObjectName() + ".prefab";
                        if (Prefab::SaveToFile(entity, prefab_path))
                        {
                            entity->SetPrefabFilePath(prefab_path);
                            m_is_dirty = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void FileDialog::ItemReleased(FileDialogItem* item)
{
    item->Clicked();
    const bool is_single_click = item->GetTimeSinceLastClickMs() > 400;

    m_selected_item_id = item->GetId();
    if (!item->IsDirectory())
    {
        m_input_box = item->GetLabel();
    }

    if (is_single_click)
    {
        if (m_callback_on_item_clicked)
        {
            m_callback_on_item_clicked(item->GetPath());
        }
        return;
    }

    const string path = item->GetPath();
    if (item->IsDirectory())
    {
        NavigateTo(path);
    }
    else
    {
        m_selection_made = true;
        if (m_type == FileDialog_Type_Browser)
        {
            FileSystem::OpenUrl(path);
        }
    }

    if (m_callback_on_item_double_clicked)
    {
        m_callback_on_item_double_clicked(path);
    }
}

void FileDialog::RenderGridView()
{
    // reset drag tracking at the start of a new press
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_was_dragging = false;
    }

    if (m_displayed_item_count == 0)
    {
        ShowEmptyState();
        return;
    }

    const float content_width = ImGui::GetContentRegionAvail().x;
    const float icon_size     = m_item_size.x;
    const float line_height   = ImGui::GetTextLineHeight();

    // from medium size up a card has room for a second line of name and a tag saying what it is
    const bool detailed       = icon_size >= 64.0f;
    const float tag_height    = detailed ? ImGui::GetFontSize() * 0.95f : 0.0f;
    const float label_height  = (detailed ? line_height * 2.0f + 2.0f : line_height) + tag_height + 6.0f;
    const float item_width    = icon_size + grid_item_padding * 2;
    const float item_height   = icon_size + label_height + grid_item_padding * 2;

    int columns = static_cast<int>((content_width - 16) / item_width);
    if (columns < 1)
    {
        columns = 1;
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(8.0f);

    lock_guard lock(m_mutex_items);
    int col = 0;
    bool first_in_row = true;

    for (size_t i = 0; i < m_items.size(); i++)
    {
        auto& item = m_items[i];
        if (!IsItemVisible(item))
        {
            continue;
        }

        if (!first_in_row)
        {
            ImGui::SameLine(0, 4);
        }
        first_in_row = false;

        ImGui::PushID(static_cast<int>(i));

        // wrap the whole cell in a group so the rename input text (if any) cannot
        // become the trailing item that SameLine() snaps to, which would break tiling
        ImGui::BeginGroup();

        const ImVec2 card_min  = ImGui::GetCursorScreenPos();
        const ImVec2 card_max  = ImVec2(card_min.x + item_width - 4, card_min.y + item_height - 4);
        const float card_width = card_max.x - card_min.x;

        ImGui::InvisibleButton("##card", ImVec2(card_width, card_max.y - card_min.y));
        const ImGuiID card_id  = ImGui::GetItemID();
        const bool is_hovered  = ImGui::IsItemHovered();
        const bool is_selected = m_selected_item_id == item.GetId();

        ItemDrag(&item);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImGui::EditorUi::draw_card(card_min, card_max, is_hovered, is_selected, card_rounding, card_id);

        const kind_info& info = kind_of(item.GetKind());

        // icon, a rendered thumbnail replaces the glyph once it exists, only cards on screen ask for one
        const float icon_area = icon_size - grid_item_padding;
        spartan::RHI_Texture* thumbnail =
            !item.IsDirectory() && ImGui::IsRectVisible(card_min, card_max)
                ? AssetThumbnails::Get(item.GetPath())
                : nullptr;
        const spartan::Icon& icon = item.GetIcon();
        if (thumbnail)
        {
            const float side  = icon_area;
            const float img_x = card_min.x + (card_width - side) * 0.5f;
            const float img_y = card_min.y + grid_item_padding;
            draw_list->AddImageRounded(
                reinterpret_cast<ImTextureID>(thumbnail),
                ImVec2(img_x, img_y),
                ImVec2(img_x + side, img_y + side),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                IM_COL32_WHITE,
                card_rounding - 2.0f
            );
        }
        else if (
            icon.texture &&
            icon.texture->GetResourceState() == ResourceState::PreparedForGpu &&
            icon.texture->GetRhiResource()
        )
        {
            // source size derived from the uv sub rect, works for both atlas icons and full thumbnails
            ImVec2 img_size(
                (icon.uv_max.x - icon.uv_min.x) * static_cast<float>(icon.texture->GetWidth()),
                (icon.uv_max.y - icon.uv_min.y) * static_cast<float>(icon.texture->GetHeight())
            );
            // atlas glyphs are white line art, they sit quietly and a little smaller than thumbnails until pointed at
            const bool is_glyph = icon.texture == spartan::ResourceCache::GetIcon(spartan::IconType::Folder).texture;
            float scale = min(icon_area / img_size.x, icon_area / img_size.y) * (is_glyph ? 0.66f : 1.0f);
            img_size.x *= scale;
            img_size.y *= scale;

            const float img_x = card_min.x + (card_width - img_size.x) * 0.5f;
            const float img_y = card_min.y + grid_item_padding + (icon_area - img_size.y) * 0.5f;

            ImU32 icon_tint = IM_COL32_WHITE;
            if (is_glyph)
            {
                // each kind carries its colour at rest, so a folder of mixed assets can be scanned by colour before reading a word
                const float lit   = ImGui::EditorUi::animate(card_id ^ 0x91c0f00du, is_hovered || is_selected ? 1.0f : 0.0f, 14.0f);
                const ImVec4 rest = ImGui::Style::lerp(ImGui::Style::color_text_muted, info.tint, item.IsDirectory() ? 0.35f : 0.6f);
                const ImVec4 lit_tint = is_selected ? ImGui::Style::color_accent_hi : ImGui::Style::lerp(info.tint, ImGui::Style::color_text, 0.35f);
                icon_tint = ImGui::EditorUi::color(ImGui::Style::lerp(rest, lit_tint, lit));
            }

            draw_list->AddImage(
                reinterpret_cast<ImTextureID>(icon.texture),
                ImVec2(img_x, img_y),
                ImVec2(img_x + img_size.x, img_y + img_size.y),
                ImVec2(icon.uv_min.x, icon.uv_min.y),
                ImVec2(icon.uv_max.x, icon.uv_max.y),
                icon_tint
            );
        }

        // name, two lines broken at word boundaries, so "car_playground_resources" is read, not guessed from "car_playg..."
        const string& label     = item.GetLabel();
        const float inner_width = card_width - grid_item_padding * 2;
        const float label_y     = card_min.y + grid_item_padding + icon_area + 4;
        float tag_y             = label_y + line_height + 2.0f;

        const bool is_renaming_this = m_is_renaming && m_rename_item_id == item.GetId();
        if (is_renaming_this)
        {
            ImGui::SetCursorScreenPos(ImVec2(card_min.x + grid_item_padding, label_y - 2));
            RenameItemInline(&item, inner_width);
        }
        else
        {
            const char* begin   = label.c_str();
            const char* end     = begin + label.size();
            const char* split   = detailed ? wrap_break(begin, end, inner_width) : end;
            const ImU32 text    = ImGui::EditorUi::color(is_selected || is_hovered ? ImGui::Style::color_text : ImGui::Style::lerp(ImGui::Style::color_text_muted, ImGui::Style::color_text, 0.8f));
            const float right   = card_max.x - grid_item_padding;
            bool truncated      = false;

            if (split >= end)
            {
                const float width = ImGui::CalcTextSize(begin, end).x;
                truncated = width > inner_width;
                const float x = card_min.x + (card_width - min(width, inner_width)) * 0.5f;
                ImGui::RenderTextEllipsis(draw_list, ImVec2(x, label_y), ImVec2(right, label_y + line_height), right, begin, end, nullptr);
            }
            else
            {
                const float first_width = ImGui::CalcTextSize(begin, split).x;
                draw_list->AddText(ImVec2(card_min.x + (card_width - first_width) * 0.5f, label_y), text, begin, split);

                const float second_y     = label_y + line_height;
                const float second_width = ImGui::CalcTextSize(split, end).x;
                truncated = second_width > inner_width;
                const float x = card_min.x + (card_width - min(second_width, inner_width)) * 0.5f;
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(text));
                ImGui::RenderTextEllipsis(draw_list, ImVec2(x, second_y), ImVec2(right, second_y + line_height), right, split, end, nullptr);
                ImGui::PopStyleColor();
                tag_y = second_y + line_height + 2.0f;
            }

            if (is_hovered && truncated)
            {
                ImGui::SetTooltip("%s", label.c_str());
            }
        }

        // a quiet tag under the name, a folder says how much is inside, a file says what it is and, if it is foreign, its format
        if (detailed && !is_renaming_this)
        {
            char tag[64];
            if (item.IsDirectory())
            {
                const uint32_t count = item.GetChildCount();
                if (count == 0)
                {
                    snprintf(tag, sizeof(tag), "empty");
                }
                else
                {
                    snprintf(tag, sizeof(tag), "%u item%s", count, count == 1 ? "" : "s");
                }
            }
            else
            {
                const FileDialog_Kind kind = item.GetKind();
                const bool native = kind == Kind_Material || kind == Kind_Prefab || kind == Kind_World || kind == Kind_Script || _stricmp(item.GetExtension().c_str(), info.name) == 0;
                if (native || item.GetExtension().empty())
                {
                    snprintf(tag, sizeof(tag), "%s", info.name);
                }
                else
                {
                    snprintf(tag, sizeof(tag), "%s \xC2\xB7 %s", info.name, item.GetExtension().c_str());
                }
            }

            const float tag_width = ImGui::EditorUi::micro_label_width(tag);
            if (tag_width <= inner_width)
            {
                const ImVec4 tint = item.IsDirectory() ? ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.85f) : ImGui::EditorUi::alpha(info.tint, 0.85f);
                ImGui::EditorUi::draw_micro_label(draw_list, ImVec2(card_min.x + (card_width - tag_width) * 0.5f, tag_y), tag_height, tag, tint);
            }
        }

        // handle click on release, but only if the user didn't drag
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && is_hovered && !m_was_dragging)
        {
            ItemReleased(&item);
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
        {
            m_is_hovering_item  = true;
            m_hovered_item_path = item.GetPath();
            m_hovered_item_id   = item.GetId();
        }

        ItemClick(&item);
        ItemContextMenu(&item);

        ImGui::EndGroup();
        ImGui::PopID();

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
    if (m_displayed_item_count == 0)
    {
        ShowEmptyState();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 4));
    ImGui::EditorUi::push_table_style();

    const float dpi = spartan::Window::GetDpiScale();
    if (ImGui::BeginTable("##files", 4, ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Kind",     ImGuiTableColumnFlags_WidthFixed, 120.0f * dpi);
        ImGui::TableSetupColumn("Size",     ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80.0f * dpi);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 100.0f * dpi);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs())
        {
            if (sorts_specs->SpecsDirty && sorts_specs->SpecsCount > 0)
            {
                static const FileDialog_SortColumn columns[] = { Sort_Name, Sort_Type, Sort_Size, Sort_Modified };
                m_sort_column           = columns[clamp<int>(sorts_specs->Specs[0].ColumnIndex, 0, 3)];
                m_sort_ascending        = sorts_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                m_is_dirty              = true;
                sorts_specs->SpecsDirty = false;
            }
        }

        const float line_height = ImGui::GetTextLineHeight();
        const float row_height  = max(list_row_height, line_height + 10.0f);
        const float icon_size   = ImGui::GetFontSize() * 1.25f;
        const ImVec4 muted      = ImGui::Style::color_text_muted;

        lock_guard lock(m_mutex_items);
        for (size_t i = 0; i < m_items.size(); i++)
        {
            auto& item = m_items[i];
            if (!IsItemVisible(item))
            {
                continue;
            }

            ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));

            const float row_y      = ImGui::GetCursorPosY();
            const bool is_selected = m_selected_item_id == item.GetId();
            const kind_info& info  = kind_of(item.GetKind());

            if (ImGui::Selectable("##row", is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, row_height)) && !m_was_dragging)
            {
                ItemReleased(&item);
            }
            ItemDrag(&item);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
            {
                m_is_hovering_item  = true;
                m_hovered_item_path = item.GetPath();
                m_hovered_item_id   = item.GetId();
            }

            ItemClick(&item);
            ItemContextMenu(&item);

            // icon, glyphs tinted by kind like the grid so both views teach the same colours
            ImGui::SameLine(0, 0);
            const spartan::Icon& icon = item.GetIcon();
            spartan::RHI_Texture* thumbnail =
                !item.IsDirectory() && ImGui::IsRectVisible(ImVec2(icon_size, row_height))
                    ? AssetThumbnails::Get(item.GetPath())
                    : nullptr;
            if (thumbnail)
            {
                ImGui::SetCursorPosY(row_y + (row_height - icon_size) * 0.5f);
                const ImVec2 position = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddImageRounded(
                    reinterpret_cast<ImTextureID>(thumbnail),
                    position,
                    ImVec2(position.x + icon_size, position.y + icon_size),
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    IM_COL32_WHITE,
                    2.0f
                );
                ImGui::Dummy(ImVec2(icon_size, icon_size));
                ImGui::SameLine(0, 8);
            }
            else if (
                icon.texture &&
                icon.texture->GetResourceState() == ResourceState::PreparedForGpu &&
                icon.texture->GetRhiResource()
            )
            {
                const bool is_glyph = icon.texture == spartan::ResourceCache::GetIcon(spartan::IconType::Folder).texture;
                ImGui::SetCursorPosY(row_y + (row_height - icon_size) * 0.5f);
                const ImVec2 position = ImGui::GetCursorScreenPos();
                const ImU32 tint      = is_glyph ? ImGui::EditorUi::color(ImGui::Style::lerp(muted, info.tint, 0.6f)) : IM_COL32_WHITE;
                ImGui::GetWindowDrawList()->AddImage(
                    reinterpret_cast<ImTextureID>(icon.texture),
                    position,
                    ImVec2(position.x + icon_size, position.y + icon_size),
                    ImVec2(icon.uv_min.x, icon.uv_min.y),
                    ImVec2(icon.uv_max.x, icon.uv_max.y),
                    tint
                );
                ImGui::Dummy(ImVec2(icon_size, icon_size));
                ImGui::SameLine(0, 8);
            }

            ImGui::SetCursorPosY(row_y + (row_height - line_height) * 0.5f);
            if (m_is_renaming && m_rename_item_id == item.GetId())
            {
                ImGui::SetCursorPosY(row_y + (row_height - ImGui::GetFrameHeight()) * 0.5f);
                RenameItemInline(&item, -1.0f);
            }
            else
            {
                ImGui::TextUnformatted(item.GetLabel().c_str());
            }

            // kind, a coloured dot and a word instead of a raw extension, the format only when it tells you something
            ImGui::TableSetColumnIndex(1);
            {
                ImGui::SetCursorPosY(row_y + (row_height - line_height) * 0.5f);
                const ImVec2 position = ImGui::GetCursorScreenPos();
                const float dot       = ImGui::EditorUi::scaled(3.0f);
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(position.x + dot, position.y + line_height * 0.5f), dot, ImGui::EditorUi::color(info.tint), 12);
                ImGui::SetCursorScreenPos(ImVec2(position.x + dot * 2.0f + 6.0f, position.y));
                const FileDialog_Kind kind = item.GetKind();
                const bool native = kind == Kind_Folder || kind == Kind_Material || kind == Kind_Prefab || kind == Kind_World || kind == Kind_Script || _stricmp(item.GetExtension().c_str(), info.name) == 0;
                if (native || item.GetExtension().empty())
                {
                    ImGui::TextColored(muted, "%s", info.name);
                }
                else
                {
                    ImGui::TextColored(muted, "%s \xC2\xB7 %s", info.name, item.GetExtension().c_str());
                }
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetCursorPosY(row_y + (row_height - line_height) * 0.5f);
            if (item.IsDirectory())
            {
                const uint32_t count = item.GetChildCount();
                ImGui::TextColored(ImGui::EditorUi::alpha(muted, 0.82f), count == 0 ? "empty" : (count == 1 ? "1 item" : "%u items"), count);
            }
            else
            {
                ImGui::TextColored(ImGui::EditorUi::alpha(muted, 0.82f), "%s", format_size(item.GetSizeBytes()).c_str());
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::SetCursorPosY(row_y + (row_height - line_height) * 0.5f);
            ImGui::TextColored(ImGui::EditorUi::alpha(muted, 0.82f), "%s", format_age(item.GetModified()).c_str());

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::EditorUi::pop_table_style();
    ImGui::PopStyleVar();
}

void FileDialog::ShowBottom(bool* is_visible)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 window_pos     = ImGui::GetWindowPos();
    ImVec2 window_size    = ImGui::GetWindowSize();
    float bar_y           = window_size.y - m_offset_bottom;

    ImVec2 bar_min = ImVec2(window_pos.x, window_pos.y + bar_y);
    ImVec2 bar_max = ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y);
    draw_list->AddRectFilled(bar_min, bar_max, col_toolbar_bg);
    draw_list->AddLine(bar_min, ImVec2(bar_max.x, bar_min.y), col_separator);

    if (m_type == FileDialog_Type_Browser)
    {
        // the status bar answers "what is this" for the selection and "what can I do with it" for whatever is under the mouse
        string name;
        string details;
        const char* hint = nullptr;
        int hint_kind    = -1;
        {
            lock_guard<mutex> lock(m_mutex_items);
            const FileDialogItem* selected = nullptr;
            const FileDialogItem* hovered  = nullptr;
            for (const FileDialogItem& item : m_items)
            {
                if (item.GetId() == m_selected_item_id && IsItemVisible(item))
                {
                    selected = &item;
                }
                if (item.GetId() == m_hovered_item_id && m_is_hovering_item)
                {
                    hovered = &item;
                }
            }

            if (selected)
            {
                const kind_info& info = kind_of(selected->GetKind());
                name    = selected->GetLabel();
                details = info.name;
                if (selected->IsDirectory())
                {
                    const uint32_t count = selected->GetChildCount();
                    details += count == 0 ? string("  \xC2\xB7  empty") : "  \xC2\xB7  " + to_string(count) + (count == 1 ? " item" : " items");
                }
                else
                {
                    details += "  \xC2\xB7  " + format_size(selected->GetSizeBytes());
                }
                const string age = format_age(selected->GetModified());
                if (!age.empty())
                {
                    details += "  \xC2\xB7  modified " + age;
                }
            }

            const FileDialogItem* hint_item = hovered ? hovered : selected;
            if (hint_item)
            {
                hint      = kind_of(hint_item->GetKind()).hint;
                hint_kind = hint_item->GetKind();
            }
        }

        if (name.empty())
        {
            uint32_t total = 0;
            for (uint32_t count : m_kind_counts)
            {
                total += count;
            }
            const bool filtered = m_search_filter.IsActive() || m_kind_filter >= 0;
            char buffer[96];
            if (filtered)
            {
                snprintf(buffer, sizeof(buffer), "%u of %u items", m_displayed_item_count, total);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), total == 1 ? "%u item" : "%u items", total);
            }
            details = buffer;
            if (!hint)
            {
                hint = "Right-click for a new folder, script or material";
            }
        }

        const float text_y = bar_y + (m_offset_bottom - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::SetCursorPos(ImVec2(12, text_y));
        if (!name.empty())
        {
            ImGui::TextColored(ImGui::Style::color_text, "%s", name.c_str());
            ImGui::SameLine(0, 0);
            ImGui::TextColored(ImGui::Style::color_text_muted, "  \xC2\xB7  %s", details.c_str());
        }
        else
        {
            ImGui::TextColored(ImGui::Style::color_text_muted, "%s", details.c_str());
        }
        const float left_end = ImGui::GetItemRectMax().x;

        // the hint gives way to the facts when the panel is narrow
        // prefixed with the kind in its colour, so it is clear the hint is about the item under the mouse, not the selection on the left
        if (hint)
        {
            const string prefix    = hint_kind >= 0 ? string(kind_of(hint_kind).name) + "  \xC2\xB7  " : string();
            const float prefix_w   = ImGui::CalcTextSize(prefix.c_str()).x;
            const float hint_width = prefix_w + ImGui::CalcTextSize(hint).x;
            const float hint_x     = window_pos.x + window_size.x - hint_width - 12.0f;
            if (hint_x > left_end + 24.0f)
            {
                const float y = window_pos.y + text_y;
                if (hint_kind >= 0)
                {
                    draw_list->AddText(ImVec2(hint_x, y), ImGui::EditorUi::color(kind_of(hint_kind).tint), prefix.c_str());
                }
                draw_list->AddText(ImVec2(hint_x + prefix_w, y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.75f)), hint);
            }
        }
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

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::SetNextItemWidth(input_width);
        ImGui::InputTextWithHint("##filename", "File name", &m_input_box);
        ImGui::PopStyleVar(2);

        ImGui::SameLine(0, 8);

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::Style::color_text_muted, FILTER_NAME);

        ImGui::SameLine(0, 8);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 6));

        if (ImGui::Button("Cancel"))
        {
            m_selection_made = false;
            (*is_visible)    = false;
        }

        ImGui::SameLine(0, button_spacing);

        // the confirm button uses the shared primary style, not its own blue
        ImGui::BeginDisabled(m_input_box.empty());
        ImGui::EditorUi::push_primary_button();
        if (ImGui::Button(OPERATION_NAME))
        {
            m_selection_made = true;
        }
        ImGui::EditorUi::pop_primary_button();
        ImGui::EndDisabled();

        ImGui::PopStyleVar(2);
    }
}

void FileDialog::ItemDrag(FileDialogItem* item)
{
    if (!item || m_type != FileDialog_Type_Browser)
    {
        return;
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        m_was_dragging = true;
        const auto set_payload = [this](const ImGuiSp::DragPayloadType type, const string& path_full, const string& path_relative)
        {
            m_drag_drop_payload.type = type;
            m_drag_drop_payload.set_paths(path_full.c_str(), path_relative.c_str());
            ImGuiSp::create_drag_drop_payload(m_drag_drop_payload);
        };

        const string& path_full     = item->GetPath();
        const string& path_relative = item->GetPathRelative();

        if (FileSystem::IsSupportedModelFile(path_full))  { set_payload(ImGuiSp::DragPayloadType::Model,    path_full, path_relative); }
        if (FileSystem::IsSupportedImageFile(path_full))  { set_payload(ImGuiSp::DragPayloadType::Texture,  path_full, path_relative); }
        if (FileSystem::IsEngineTextureFile(path_full))   { set_payload(ImGuiSp::DragPayloadType::Texture,  path_full, path_relative); }
        if (FileSystem::IsSupportedAudioFile(path_full))  { set_payload(ImGuiSp::DragPayloadType::Audio,    path_full, path_relative); }
        if (FileSystem::IsEngineMaterialFile(path_full))  { set_payload(ImGuiSp::DragPayloadType::Material, path_full, path_relative); }
        if (FileSystem::IsEngineLuaFile(path_full))       { set_payload(ImGuiSp::DragPayloadType::Lua,      path_full, path_relative); }
        if (FileSystem::IsEnginePrefabFile(path_full))    { set_payload(ImGuiSp::DragPayloadType::Prefab,   path_full, path_relative); }

        // drag preview
        ImGui::BeginTooltip();
        const spartan::Icon& drag_icon = item->GetIcon();
        if (spartan::RHI_Texture* thumbnail = AssetThumbnails::Get(item->GetPath()))
        {
            ImGuiSp::image(thumbnail, ImVec2(48, 48));
            ImGui::SameLine();
        }
        else if (
            drag_icon.texture &&
            drag_icon.texture->GetResourceState() == ResourceState::PreparedForGpu &&
            drag_icon.texture->GetRhiResource()
        )
        {
            ImGuiSp::image(drag_icon.texture, ImVec2(48, 48), drag_icon.uv_min, drag_icon.uv_max);
            ImGui::SameLine();
        }
        ImGui::Text("%s", item->GetLabel().c_str());
        ImGui::EndTooltip();

        ImGui::EndDragDropSource();
    }
}

void FileDialog::ItemClick(FileDialogItem* item) const
{
    if (!item || !m_is_hovering_window)
    {
        return;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        m_context_menu_id = item->GetId();
        ImGui::OpenPopup("##context_menu");
    }
}

void FileDialog::ItemContextMenu(FileDialogItem* item)
{
    if (m_context_menu_id != item->GetId())
    {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);

    if (ImGui::BeginPopup("##context_menu"))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));

        if (ImGui::MenuItem("Rename"))
        {
            m_is_renaming           = true;
            m_rename_request_focus  = true;
            m_rename_select_pending = true;
            m_rename_buffer         = item->GetLabel();
            m_rename_item_id        = item->GetId();
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

        if (AssetThumbnails::IsSupported(item->GetPath()))
        {
            if (ImGui::MenuItem("Regenerate thumbnail"))
            {
                AssetThumbnails::Regenerate(item->GetPath());
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
}

void FileDialog::DialogUpdateFromDirectory(const string& file_path)
{
    if (!FileSystem::IsDirectory(file_path))
    {
        SP_LOG_ERROR("provided path doesn't point to a directory.");
        return;
    }

    // capture watch baseline so the auto refresh watcher only triggers on subsequent external changes
    try
    {
        m_watch_path     = file_path;
        m_watch_dir_time = filesystem::last_write_time(file_path);
    }
    catch (...) {}

    lock_guard<mutex> lock(m_mutex_items);
    m_items.clear();
    m_selected_item_id      = UINT32_MAX;
    m_is_renaming           = false;
    m_rename_request_focus  = false;
    m_rename_select_pending = false;
    m_rename_item_id        = UINT32_MAX;

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
        for (const string& path : paths_anything)
        {
            if (FileSystem::IsSupportedImageFile(path))
            {
                // load the thumbnail off the main thread so the folder opens instantly
                ThreadPool::AddTask([this, path]()
                {
                    auto texture = spartan::ResourceCache::Load<RHI_Texture>(path);
                    if (texture)
                    {
                        texture->PrepareForGpu();
                    }
                    lock_guard<mutex> lock(m_mutex_items);
                    m_items.emplace_back(path, texture.get());
                });
            }
            else if (FileSystem::IsSupportedAudioFile(path))
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Audio));
            }
            else if (FileSystem::IsSupportedModelFile(path))
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Model));
            }
            else if (FileSystem::IsSupportedFontFile(path))
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Font));
            }
            else if (FileSystem::IsEngineMaterialFile(path))
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Material));
            }
            else if (FileSystem::IsEnginePrefabFile(path))
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Entity));
            }
            else if (FileSystem::IsEngineWorldFile(path))
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::World));
            }
            else if (FileSystem::IsEngineLuaFile(path))
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Script));
            }
            else if (FileSystem::IsEngineTextureFile(path))
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Texture));
            }
            else if (FileSystem::GetExtensionFromFilePath(path) == ".7z" || FileSystem::GetExtensionFromFilePath(path) == ".zip")
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Compressed));
            }
            else
            {
                m_items.emplace_back(path, spartan::ResourceCache::GetIcon(spartan::IconType::Undefined));
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
    else if (m_filter == FileDialog_Filter_Image)
    {
        for (const string& path : paths_anything)
        {
            if (!is_prompt_reference_image(path))
            {
                continue;
            }

            ThreadPool::AddTask([this, path]()
            {
                auto texture = spartan::ResourceCache::Load<RHI_Texture>(path);
                if (texture)
                {
                    texture->PrepareForGpu();
                }
                lock_guard<mutex> lock(m_mutex_items);
                m_items.emplace_back(path, texture.get());
            });
        }
    }

    // sort items
    sort(m_items.begin(), m_items.end(), [this](const FileDialogItem& a, const FileDialogItem& b)
    {
        bool a_is_dir = a.IsDirectory();
        bool b_is_dir = b.IsDirectory();

        // directories always first
        if (a_is_dir != b_is_dir)
        {
            return a_is_dir;
        }

        if (m_sort_column == Sort_Name)
        {
            return m_sort_ascending ? a.GetLabel() < b.GetLabel() : a.GetLabel() > b.GetLabel();
        }

        // ties fall back to the name so equal rows keep a stable, readable order
        const bool by_name = a.GetLabel() < b.GetLabel();

        if (m_sort_column == Sort_Type)
        {
            if (a.GetKind() != b.GetKind())
            {
                return m_sort_ascending ? a.GetKind() < b.GetKind() : a.GetKind() > b.GetKind();
            }
            if (a.GetExtension() != b.GetExtension())
            {
                return a.GetExtension() < b.GetExtension();
            }
            return by_name;
        }

        if (m_sort_column == Sort_Size)
        {
            const uint64_t size_a = a_is_dir ? a.GetChildCount() : a.GetSizeBytes();
            const uint64_t size_b = b_is_dir ? b.GetChildCount() : b.GetSizeBytes();
            if (size_a != size_b)
            {
                return m_sort_ascending ? size_a < size_b : size_a > size_b;
            }
            return by_name;
        }

        if (m_sort_column == Sort_Modified)
        {
            if (a.GetModified() != b.GetModified())
            {
                return m_sort_ascending ? a.GetModified() < b.GetModified() : a.GetModified() > b.GetModified();
            }
            return by_name;
        }

        return false;
    });
}

void FileDialog::RenameItemInline(FileDialogItem* item, float width)
{
    if (m_rename_request_focus)
    {
        ImGui::SetKeyboardFocusHere();
        m_rename_request_focus = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::SetNextItemWidth(width);

    // on first activation, select only the stem (filename without the extension)
    // so the user does not accidentally type over the dot extension
    auto select_stem_callback = [](ImGuiInputTextCallbackData* data) -> int
    {
        bool* pending = static_cast<bool*>(data->UserData);
        if (pending && *pending)
        {
            int stem_len = data->BufTextLen;
            for (int i = data->BufTextLen - 1; i > 0; --i)
            {
                if (data->Buf[i] == '.')
                {
                    stem_len = i;
                    break;
                }
            }
            data->SelectionStart = 0;
            data->SelectionEnd   = stem_len;
            data->CursorPos      = stem_len;
            *pending             = false;
        }
        return 0;
    };

    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways;
    const bool committed            = ImGui::InputText("##rename_inline", &m_rename_buffer, flags, select_stem_callback, &m_rename_select_pending);
    const bool deactivated          = ImGui::IsItemDeactivated();
    const bool escape_pressed       = ImGui::IsKeyPressed(ImGuiKey_Escape);

    ImGui::PopStyleVar(2);

    auto try_commit = [&]()
    {
        if (!m_rename_buffer.empty() && m_rename_buffer != item->GetLabel())
        {
            const string new_path = FileSystem::GetDirectoryFromFilePath(item->GetPath()) + m_rename_buffer;
            FileSystem::Rename(item->GetPath(), new_path);
            m_is_dirty = true;
        }
    };

    if (committed)
    {
        try_commit();
        m_is_renaming = false;
    }
    else if (escape_pressed)
    {
        m_is_renaming = false;
    }
    else if (deactivated)
    {
        try_commit();
        m_is_renaming = false;
    }
}

void FileDialog::WatchDirectory()
{
    // throttle polling so we don't hit the filesystem every frame
    auto now = chrono::steady_clock::now();
    if (now - m_watch_last_check < chrono::milliseconds(500))
    {
        return;
    }

    m_watch_last_check = now;

    // resolve the directory we should be watching
    string dir = m_current_path;
    if (FileSystem::IsFile(dir))
    {
        dir = FileSystem::GetDirectoryFromFilePath(dir);
    }

    if (!FileSystem::IsDirectory(dir))
    {
        return;
    }

    // if the watched path changed, just sync the baseline without triggering a refresh
    if (dir != m_watch_path)
    {
        try
        {
            m_watch_path     = dir;
            m_watch_dir_time = filesystem::last_write_time(dir);
        }
        catch (...) {}
        return;
    }

    // creating, deleting or renaming entries bumps the parent directory mtime
    try
    {
        auto current_time = filesystem::last_write_time(dir);
        if (current_time != m_watch_dir_time)
        {
            m_watch_dir_time = current_time;
            m_is_dirty       = true;
        }
    }
    catch (...) {}
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
    {
        return;
    }

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

    // alt+up or backspace for the parent directory
    const bool go_up = (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && ImGui::GetIO().KeyAlt) || (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !ImGui::GetIO().WantTextInput);
    if (go_up)
    {
        string parent = FileSystem::GetParentDirectory(m_current_path);
        if (parent != m_current_path)
        {
            NavigateTo(parent);
        }
    }
}
