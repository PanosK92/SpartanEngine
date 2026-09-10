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
#include "WorldSelector.h"
#include "../WorldPreviews.h"
#include "../GeneralWindows.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/source/imgui_stdlib.h"
#include "../widgets/Viewport.h"
#include "core/ProgressTracker.h"
#include <cctype>
#include <filesystem>
#include <unordered_set>
SP_WARNINGS_OFF
#include "io/pugixml.hpp"
SP_WARNINGS_ON
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    Editor* editor = nullptr;

    vector<spartan::WorldMetadata> world_files;
    vector<int> visible_indices;
    int selected_index         = 0;
    int selected_visible_index = 0;
    int visible_columns        = 1;

    bool visible_download_prompt  = false;
    bool visible_update_prompt    = false;
    bool visible_world_list       = false;
    bool downloaded_and_extracted = false;
    bool update_check_started     = false;

    const float card_rounding      = 8.0f;
    const float panel_rounding     = 8.0f;
    const float card_padding       = 12.0f;
    const float card_spacing       = 12.0f;
    const float card_width_base    = 230.0f;
    const float card_height_base   = 252.0f;
    const float section_spacing    = 14.0f;
    const float details_width_base = 370.0f;

    struct LauncherColors
    {
        ImU32 card_bg            = 0;
        ImU32 card_bg_hover      = 0;
        ImU32 card_bg_selected   = 0;
        ImU32 card_border        = 0;
        ImU32 card_border_accent = 0;
        ImU32 panel_bg           = 0;
        ImU32 panel_border       = 0;
        ImU32 surface_bg         = 0;
        ImU32 chip_bg            = 0;
        ImU32 shadow             = 0;
        ImU32 preview_bg         = 0;
        ImU32 text_primary       = 0;
        ImU32 text_muted         = 0;
        ImU32 accent             = 0;
        ImU32 warning            = 0;
        ImVec4 button            = {};
        ImVec4 button_hover      = {};
        ImVec4 button_active     = {};
        ImVec4 primary           = {};
        ImVec4 primary_hover     = {};
        ImVec4 primary_active    = {};
    };

    LauncherColors colors;
    ImGuiTextFilter search_filter;

    float last_click_time    = -1.0f;
    int last_click_index     = -1;
    bool scroll_to_selection = false;

    string rename_world_path;
    string rename_buffer;
    bool rename_request_focus = false;
    string pending_rename_path;
    string pending_rename_name;
    string pending_delete_path;
    string pending_duplicate_path;

    void load_selected_world();

    unordered_map<string, int64_t> world_last_opened;

    const char* world_recency_file = "spartan_worlds.xml";

    const char* assets_url          = "https://www.dropbox.com/scl/fi/pkh7ix0qm6rd747ym21pn/project.7z?rlkey=wdqf3qzmemjzirrvrk78043vp&dl=1";
    const char* assets_destination  = "project/project.7z";
    const char* assets_extract_dir  = "project/";
    const char* assets_expected_sha = "0e9fc06fc3ee542057623abdc200fd0d6161ac28930be8ffdd51c17394df7813";

    float dpi()
    {
        return spartan::Window::GetDpiScale();
    }

    float scaled(float value)
    {
        return value * dpi();
    }

    ImVec2 scaled_vec(float x, float y)
    {
        return ImVec2(scaled(x), scaled(y));
    }

    ImVec4 with_alpha(const ImVec4& color, float alpha)
    {
        return ImVec4(color.x, color.y, color.z, alpha);
    }

    ImU32 to_u32(const ImVec4& color)
    {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    void update_colors()
    {
        const ImVec4 bg_1    = ImGui::Style::bg_color_1;
        const ImVec4 bg_2    = ImGui::Style::bg_color_2;
        const ImVec4 accent  = ImGui::Style::color_accent_1;
        const ImVec4 warning = ImGui::Style::color_warning;
        const ImVec4 text    = ImGui::GetStyle().Colors[ImGuiCol_Text];
        const ImVec4 muted   = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];

        colors.card_bg            = to_u32(ImGui::Style::lerp(bg_1, bg_2, 0.28f));
        colors.card_bg_hover      = to_u32(ImGui::Style::lerp(bg_1, bg_2, 0.42f));
        colors.card_bg_selected   = to_u32(ImGui::Style::lerp(bg_1, accent, 0.20f));
        colors.card_border        = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.75f), 0.90f));
        colors.card_border_accent = to_u32(accent);
        colors.panel_bg           = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.18f), 0.96f));
        colors.panel_border       = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.70f), 0.78f));
        colors.surface_bg         = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.10f), 0.96f));
        colors.chip_bg            = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.55f), 0.78f));
        colors.shadow             = IM_COL32(0, 0, 0, 75);
        colors.preview_bg         = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.10f), 0.92f));
        colors.text_primary       = to_u32(text);
        colors.text_muted         = to_u32(muted);
        colors.accent             = to_u32(accent);
        colors.warning            = to_u32(warning);
        colors.button             = with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.50f), 0.80f);
        colors.button_hover       = with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.70f), 0.95f);
        colors.button_active      = with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.85f), 1.00f);
        colors.primary            = with_alpha(accent, 0.32f);
        colors.primary_hover      = with_alpha(accent, 0.46f);
        colors.primary_active     = with_alpha(accent, 0.62f);
    }

    void push_button_style(bool primary)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, scaled(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, scaled_vec(12.0f, 6.0f));

        if (primary)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        colors.primary);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.primary_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  colors.primary_active);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        colors.button);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.button_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  colors.button_active);
        }
    }

    void pop_button_style()
    {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }

    bool launcher_button(const char* label, const ImVec2& size, bool primary = false)
    {
        push_button_style(primary);
        bool pressed = ImGui::Button(label, size);
        pop_button_style();
        return pressed;
    }

    void draw_panel_background(const ImVec2& min_pos, const ImVec2& max_pos)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(min_pos, max_pos, colors.panel_bg, scaled(panel_rounding));
        draw_list->AddRect(min_pos, max_pos, colors.panel_border, scaled(panel_rounding));
    }

    void begin_panel(const char* id, const ImVec2& size)
    {
        ImGui::BeginChild(id, size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        draw_panel_background(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
        ImGui::SetCursorPos(scaled_vec(14.0f, 14.0f));
    }

    void end_panel()
    {
        ImGui::EndChild();
    }

    void draw_chip(const char* text, ImU32 bg_col, ImU32 text_col)
    {
        ImVec2 text_size = ImGui::CalcTextSize(text);
        ImVec2 pos       = ImGui::GetCursorScreenPos();
        ImVec2 size      = ImVec2(text_size.x + scaled(16.0f), text_size.y + scaled(6.0f));
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg_col, size.y * 0.5f);
        draw->AddText(ImVec2(pos.x + scaled(8.0f), pos.y + scaled(3.0f)), text_col, text);
        ImGui::Dummy(size);
    }

    string lowercase_copy(string value)
    {
        transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        return value;
    }

    string normalize_world_path(string path)
    {
        replace(path.begin(), path.end(), '\\', '/');
#if defined(_WIN32)
        return lowercase_copy(path);
#else
        return path;
#endif
    }

    int64_t get_current_timestamp()
    {
        return static_cast<int64_t>(chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count());
    }

    int64_t get_last_opened(const string& path)
    {
        const auto it = world_last_opened.find(normalize_world_path(path));
        if (it != world_last_opened.end())
        {
            return it->second;
        }

        return 0;
    }

    void load_world_recency()
    {
        world_last_opened.clear();

        if (!spartan::FileSystem::Exists(world_recency_file))
        {
            return;
        }

        pugi::xml_document doc;
        if (!doc.load_file(world_recency_file))
        {
            return;
        }

        pugi::xml_node root = doc.child("WorldRecency");
        for (pugi::xml_node world_node = root.child("World"); world_node; world_node = world_node.next_sibling("World"))
        {
            const string path             = normalize_world_path(world_node.attribute("path").as_string());
            const string last_opened_text = world_node.attribute("last_opened").as_string();
            if (path.empty() || last_opened_text.empty())
            {
                continue;
            }

            try
            {
                world_last_opened[path] = stoll(last_opened_text);
            }
            catch (...)
            {
                continue;
            }
        }
    }

    void save_world_recency()
    {
        pugi::xml_document doc;
        pugi::xml_node root = doc.append_child("WorldRecency");

        for (const auto& [path, last_opened] : world_last_opened)
        {
            const string last_opened_text = to_string(last_opened);
            pugi::xml_node world_node = root.append_child("World");
            world_node.append_attribute("path").set_value(path.c_str());
            world_node.append_attribute("last_opened").set_value(last_opened_text.c_str());
        }

        if (!doc.save_file(world_recency_file))
        {
            SP_LOG_ERROR("Failed to save world recency file: %s", world_recency_file);
        }
    }

    void record_world_opened(const string& path)
    {
        world_last_opened[normalize_world_path(path)] = get_current_timestamp();
        save_world_recency();
    }

    int world_index_from_path(const string& path)
    {
        const string normalized_path = normalize_world_path(path);
        for (int i = 0; i < static_cast<int>(world_files.size()); i++)
        {
            if (normalize_world_path(world_files[i].file_path) == normalized_path)
            {
                return i;
            }
        }

        return -1;
    }

    void sort_world_files()
    {
        stable_sort(world_files.begin(), world_files.end(), [](const spartan::WorldMetadata& a, const spartan::WorldMetadata& b)
        {
            return get_last_opened(a.file_path) > get_last_opened(b.file_path);
        });
    }

    bool is_work_in_progress(const spartan::WorldMetadata& world)
    {
        return lowercase_copy(world.name).find("plant") != string::npos;
    }

    string source_label(const string& path)
    {
        string lowercase_path = lowercase_copy(path);
        if (lowercase_path.find("project/") != string::npos)
        {
            return "project";
        }

        return "bundled";
    }

    bool world_matches_filter(const spartan::WorldMetadata& world)
    {
        if (!search_filter.IsActive())
        {
            return true;
        }

        return search_filter.PassFilter(world.name.c_str()) ||
               search_filter.PassFilter(world.description.c_str()) ||
               search_filter.PassFilter(world.file_path.c_str());
    }

    int visible_position_from_world_index(int world_index)
    {
        for (int i = 0; i < static_cast<int>(visible_indices.size()); i++)
        {
            if (visible_indices[i] == world_index)
            {
                return i;
            }
        }

        return -1;
    }

    void rebuild_visible_indices()
    {
        visible_indices.clear();

        for (int i = 0; i < static_cast<int>(world_files.size()); i++)
        {
            if (world_matches_filter(world_files[i]))
            {
                visible_indices.push_back(i);
            }
        }

        if (visible_indices.empty())
        {
            selected_index         = -1;
            selected_visible_index = -1;
            return;
        }

        selected_visible_index = visible_position_from_world_index(selected_index);
        if (selected_visible_index == -1)
        {
            selected_visible_index = 0;
            selected_index         = visible_indices[0];
            scroll_to_selection    = true;
        }
    }

    void select_visible_index(int visible_index)
    {
        if (visible_indices.empty())
        {
            selected_index         = -1;
            selected_visible_index = -1;
            return;
        }

        visible_index           = clamp(visible_index, 0, static_cast<int>(visible_indices.size()) - 1);
        selected_visible_index  = visible_index;
        selected_index          = visible_indices[visible_index];
    }

    const spartan::WorldMetadata* selected_world()
    {
        if (selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
        {
            return &world_files[selected_index];
        }

        return nullptr;
    }

    void scan_directory_recursive(const string& directory, unordered_set<string>& visited)
    {
        if (!spartan::FileSystem::Exists(directory) || !spartan::FileSystem::IsDirectory(directory))
        {
            return;
        }

        // Runtime asset junctions can point back into the project. Resolve them
        // before descending so a cycle cannot grow paths until filesystem throws.
        error_code error;
        const auto canonical = filesystem::canonical(directory, error);
        if (error || !visited.insert(canonical.generic_string()).second) return;

        vector<string> files = spartan::FileSystem::GetFilesInDirectory(directory);
        for (const string& file : files)
        {
            if (spartan::FileSystem::IsEngineWorldFile(file))
            {
                string normalized_path = file;
                replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

                spartan::WorldMetadata metadata;
                if (spartan::World::ReadMetadata(normalized_path, metadata))
                {
                    world_files.push_back(metadata);
                }
            }
        }

        vector<string> subdirectories = spartan::FileSystem::GetDirectoriesInDirectory(directory);
        for (const string& subdir : subdirectories)
        {
            scan_directory_recursive(subdir, visited);
        }
    }

    void scan_for_world_files()
    {
        string selected_path;
        if (selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
        {
            selected_path = world_files[selected_index].file_path;
        }

        world_files.clear();

        unordered_set<string> visited;

        string project_dir = spartan::ResourceCache::GetProjectDirectory();
        scan_directory_recursive(project_dir, visited);

        vector<string> worlds_dirs = { "worlds", "../worlds" };
        for (const string& worlds_dir : worlds_dirs)
        {
            if (spartan::FileSystem::Exists(worlds_dir))
            {
                scan_directory_recursive(worlds_dir, visited);
                break;
            }
        }

        sort_world_files();

        if (!selected_path.empty())
        {
            selected_index = world_index_from_path(selected_path);
        }

        if (selected_index < 0 || selected_index >= static_cast<int>(world_files.size()))
        {
            selected_index = world_files.empty() ? -1 : 0;
        }
    }

    string sanitize_world_name(string name)
    {
        size_t start = 0;
        while (
            start < name.size() &&
            isspace(static_cast<unsigned char>(name[start]))
        )
        {
            start++;
        }

        size_t end = name.size();
        while (
            end > start &&
            isspace(static_cast<unsigned char>(name[end - 1]))
        )
        {
            end--;
        }
        name = name.substr(start, end - start);

        string cleaned;
        cleaned.reserve(name.size());
        for (unsigned char character : name)
        {
            if (
                character < 32 ||
                character == '<' ||
                character == '>' ||
                character == ':' ||
                character == '"' ||
                character == '/' ||
                character == '\\' ||
                character == '|' ||
                character == '?' ||
                character == '*'
            )
            {
                cleaned += '_';
            }
            else
            {
                cleaned += static_cast<char>(character);
            }
        }

        while (
            !cleaned.empty() &&
            (cleaned.back() == '.' || cleaned.back() == ' ')
        )
        {
            cleaned.pop_back();
        }

        return cleaned;
    }

    bool is_same_world_path(const string& a, const string& b)
    {
        return normalize_world_path(a) == normalize_world_path(b);
    }

    bool is_loaded_world(const string& path)
    {
        const string loaded = spartan::World::GetFilePath();
        return !loaded.empty() && is_same_world_path(loaded, path);
    }

    string authored_preview_path(const string& world_file_path)
    {
        return string(spartan::ResourceCache::GetProjectDirectory()) +
            "/previews/" +
            spartan::FileSystem::GetFileNameWithoutExtensionFromFilePath(
                world_file_path
            ) +
            ".png";
    }

    string unique_copy_world_path(const string& world_file_path)
    {
        const string directory =
            spartan::FileSystem::GetDirectoryFromFilePath(world_file_path);
        const string stem =
            spartan::FileSystem::GetFileNameWithoutExtensionFromFilePath(
                world_file_path
            );

        auto path_for = [&](const string& suffix)
        {
            return directory +
                stem +
                suffix +
                string(spartan::EXTENSION_WORLD);
        };

        string candidate = path_for("_copy");
        int index = 2;
        while (spartan::FileSystem::Exists(candidate))
        {
            candidate = path_for("_copy" + to_string(index));
            index++;
        }

        return candidate;
    }

    void copy_directory_if_exists(
        const string& source,
        const string& destination
    )
    {
        if (
            source.empty() ||
            destination.empty() ||
            !spartan::FileSystem::Exists(source)
        )
        {
            return;
        }

        error_code error;
        filesystem::copy(
            source,
            destination,
            filesystem::copy_options::recursive |
            filesystem::copy_options::overwrite_existing,
            error
        );
        if (error)
        {
            SP_LOG_ERROR(
                "Failed to copy %s to %s: %s",
                source.c_str(),
                destination.c_str(),
                error.message().c_str()
            );
        }
    }

    bool write_world_display_name(
        const string& world_file_path,
        const string& name
    )
    {
        pugi::xml_document doc;
        if (!doc.load_file(world_file_path.c_str()))
        {
            SP_LOG_ERROR(
                "Failed to load world for rename: %s",
                world_file_path.c_str()
            );
            return false;
        }

        pugi::xml_node world_node = doc.child("World");
        if (!world_node)
        {
            SP_LOG_ERROR(
                "No World node in: %s",
                world_file_path.c_str()
            );
            return false;
        }

        pugi::xml_attribute name_attr = world_node.attribute("name");
        if (name_attr)
        {
            name_attr.set_value(name.c_str());
        }
        else
        {
            world_node.append_attribute("name").set_value(name.c_str());
        }

        if (!doc.save_file(world_file_path.c_str()))
        {
            SP_LOG_ERROR(
                "Failed to save world name: %s",
                world_file_path.c_str()
            );
            return false;
        }

        return true;
    }

    void relocate_world_recency(
        const string& old_path,
        const string& new_path
    )
    {
        const string old_key = normalize_world_path(old_path);
        const string new_key = normalize_world_path(new_path);
        const auto it = world_last_opened.find(old_key);
        if (it == world_last_opened.end())
        {
            return;
        }

        const int64_t last_opened = it->second;
        world_last_opened.erase(it);
        if (!new_path.empty())
        {
            world_last_opened[new_key] = last_opened;
        }
        save_world_recency();
    }

    void refresh_world_list(const string& select_path)
    {
        scan_for_world_files();
        if (!select_path.empty())
        {
            const int index = world_index_from_path(select_path);
            if (index >= 0)
            {
                selected_index = index;
            }
        }
        rebuild_visible_indices();
        scroll_to_selection = true;
    }

    void start_world_rename(int world_index)
    {
        if (
            world_index < 0 ||
            world_index >= static_cast<int>(world_files.size())
        )
        {
            return;
        }

        const spartan::WorldMetadata& world = world_files[world_index];
        rename_world_path = world.file_path;
        rename_buffer = world.name.empty()
            ? spartan::FileSystem::GetFileNameWithoutExtensionFromFilePath(
                world.file_path
            )
            : world.name;
        rename_request_focus = true;
        select_visible_index(
            visible_position_from_world_index(world_index)
        );
    }

    void apply_world_rename(const string& old_path, const string& raw_name)
    {
        const string name = sanitize_world_name(raw_name);
        if (name.empty())
        {
            SP_LOG_ERROR("World name cannot be empty");
            return;
        }

        const string directory =
            spartan::FileSystem::GetDirectoryFromFilePath(old_path);
        const string new_path =
            directory + name + string(spartan::EXTENSION_WORLD);

        if (
            !is_same_world_path(old_path, new_path) &&
            spartan::FileSystem::Exists(new_path)
        )
        {
            SP_LOG_ERROR(
                "A world named %s already exists",
                name.c_str()
            );
            return;
        }

        const string old_resources =
            spartan::World::GetResourceDirectory(old_path);
        const string new_resources =
            spartan::World::GetResourceDirectory(new_path);
        if (
            !is_same_world_path(old_resources, new_resources) &&
            spartan::FileSystem::Exists(old_resources)
        )
        {
            if (spartan::FileSystem::Exists(new_resources))
            {
                SP_LOG_ERROR(
                    "Resource folder already exists: %s",
                    new_resources.c_str()
                );
                return;
            }
            spartan::FileSystem::Rename(old_resources, new_resources);
        }

        const string old_preview = authored_preview_path(old_path);
        const string new_preview = authored_preview_path(new_path);
        if (
            !is_same_world_path(old_preview, new_preview) &&
            spartan::FileSystem::Exists(old_preview)
        )
        {
            spartan::FileSystem::Rename(old_preview, new_preview);
        }

        if (!is_same_world_path(old_path, new_path))
        {
            spartan::FileSystem::Rename(old_path, new_path);
            if (!spartan::FileSystem::Exists(new_path))
            {
                SP_LOG_ERROR(
                    "Failed to rename world to %s",
                    new_path.c_str()
                );
                return;
            }
        }

        write_world_display_name(new_path, name);
        relocate_world_recency(old_path, new_path);

        if (is_loaded_world(old_path))
        {
            spartan::World::SetFilePath(new_path);
        }

        refresh_world_list(new_path);
    }

    void apply_world_delete(const string& path)
    {
        if (path.empty() || !spartan::FileSystem::Exists(path))
        {
            return;
        }

        const string resources =
            spartan::World::GetResourceDirectory(path);
        const string preview = authored_preview_path(path);
        const string generated_preview =
            WorldPreviews::GetPreviewPath(path);

        spartan::FileSystem::Delete(path);
        if (
            !resources.empty() &&
            spartan::FileSystem::Exists(resources)
        )
        {
            spartan::FileSystem::Delete(resources);
        }
        if (spartan::FileSystem::Exists(preview))
        {
            spartan::FileSystem::Delete(preview);
        }
        if (
            !is_same_world_path(preview, generated_preview) &&
            spartan::FileSystem::Exists(generated_preview)
        )
        {
            spartan::FileSystem::Delete(generated_preview);
        }

        relocate_world_recency(path, "");
        refresh_world_list("");
    }

    void apply_world_duplicate(const string& path)
    {
        if (path.empty() || !spartan::FileSystem::Exists(path))
        {
            return;
        }

        const string new_path = unique_copy_world_path(path);
        if (!spartan::FileSystem::CopyFileFromTo(path, new_path))
        {
            SP_LOG_ERROR(
                "Failed to duplicate world: %s",
                path.c_str()
            );
            return;
        }

        const string new_name =
            spartan::FileSystem::GetFileNameWithoutExtensionFromFilePath(
                new_path
            );
        write_world_display_name(new_path, new_name);

        copy_directory_if_exists(
            spartan::World::GetResourceDirectory(path),
            spartan::World::GetResourceDirectory(new_path)
        );

        const string old_preview = authored_preview_path(path);
        const string new_preview = authored_preview_path(new_path);
        if (spartan::FileSystem::Exists(old_preview))
        {
            spartan::FileSystem::CopyFileFromTo(old_preview, new_preview);
        }

        refresh_world_list(new_path);
    }

    void process_pending_world_actions()
    {
        if (!pending_rename_path.empty())
        {
            const string path = pending_rename_path;
            const string name = pending_rename_name;
            pending_rename_path.clear();
            pending_rename_name.clear();
            apply_world_rename(path, name);
        }

        if (!pending_duplicate_path.empty())
        {
            const string path = pending_duplicate_path;
            pending_duplicate_path.clear();
            apply_world_duplicate(path);
        }
    }

    void draw_delete_confirmation()
    {
        if (pending_delete_path.empty())
        {
            return;
        }

        const char* title = "Delete world?";
        if (!ImGui::IsPopupOpen(title))
        {
            ImGui::OpenPopup(title);
        }

        ImGui::SetNextWindowPos(
            ImGui::GetMainViewport()->GetWorkCenter(),
            ImGuiCond_Appearing,
            ImVec2(0.5f, 0.5f)
        );

        if (
            ImGui::BeginPopupModal(
                title,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoDocking
            )
        )
        {
            const string leaf =
                spartan::FileSystem::GetFileNameFromFilePath(
                    pending_delete_path
                );
            ImGui::Text("Delete %s from disk?", leaf.c_str());
            ImGui::TextDisabled(
                "The world file, preview and resources folder go with it."
            );
            if (is_loaded_world(pending_delete_path))
            {
                ImGui::Dummy(ImVec2(0.0f, scaled(4.0f)));
                ImGui::TextDisabled(
                    "This world is currently open. Saving will recreate it."
                );
            }

            ImGui::Dummy(ImVec2(0.0f, scaled(8.0f)));
            if (launcher_button("Delete", ImVec2(scaled(96.0f), 0.0f), true))
            {
                const string path = pending_delete_path;
                pending_delete_path.clear();
                ImGui::CloseCurrentPopup();
                apply_world_delete(path);
            }
            ImGui::SameLine(0.0f, scaled(8.0f));
            if (launcher_button("Cancel", ImVec2(scaled(96.0f), 0.0f)))
            {
                pending_delete_path.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void draw_world_context_menu(int world_index)
    {
        if (
            world_index < 0 ||
            world_index >= static_cast<int>(world_files.size())
        )
        {
            return;
        }

        if (!ImGui::BeginPopupContextItem("##world_context"))
        {
            return;
        }

        const spartan::WorldMetadata& world = world_files[world_index];
        select_visible_index(
            visible_position_from_world_index(world_index)
        );

        ImGui::TextDisabled("%s", world.name.c_str());
        ImGui::Separator();

        if (ImGui::MenuItem("Open", "Enter"))
        {
            load_selected_world();
        }
        if (ImGui::MenuItem("Rename", "F2"))
        {
            start_world_rename(world_index);
        }
        if (ImGui::MenuItem("Duplicate"))
        {
            pending_duplicate_path = world.file_path;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Copy path"))
        {
            ImGui::SetClipboardText(world.file_path.c_str());
        }
        if (ImGui::MenuItem("Show in explorer"))
        {
            error_code error;
            const filesystem::path absolute = filesystem::absolute(
                filesystem::path(
                    spartan::FileSystem::GetDirectoryFromFilePath(
                        world.file_path
                    )
                ),
                error
            );
            if (!error)
            {
                spartan::FileSystem::OpenUrl(
                    "file:///" + absolute.generic_string()
                );
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del"))
        {
            pending_delete_path = world.file_path;
        }

        ImGui::EndPopup();
    }

    void check_assets_outdated_async()
    {
        spartan::ThreadPool::AddTask([]()
        {
            if (!spartan::FileSystem::Exists(assets_destination))
            {
                return;
            }

            string local_hash = spartan::FileSystem::ComputeFileSha256(assets_destination);
            if (!local_hash.empty() && local_hash != assets_expected_sha)
            {
                visible_update_prompt = true;
            }
        });
    }

    void download_and_extract()
    {
        visible_download_prompt = false;

        spartan::ThreadPool::AddTask([]()
        {
            spartan::Progress& progress = spartan::ProgressTracker::GetProgress(spartan::ProgressType::Download);
            progress.Start(0, "Downloading projects...");
            spartan::ProgressTracker::SetGlobalLoadingState(true);

            bool success = spartan::FileSystem::DownloadFile(
                assets_url,
                assets_destination,
                [&progress](float download_progress)
                {
                    progress.SetFraction(download_progress * 0.9f);
                }
            );

            if (success)
            {
                progress.SetText("Extracting projects...");
                progress.SetFraction(0.9f);
                success = spartan::FileSystem::ExtractArchive(assets_destination, assets_extract_dir);
                progress.SetFraction(1.0f);
            }

            spartan::ProgressTracker::SetGlobalLoadingState(false);
            if (success)
            {
                downloaded_and_extracted = true;
                scan_for_world_files();
                visible_world_list = true;
            }
        });
    }

    void prompt_text_centered(const char* text)
    {
        float text_w = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - text_w) * 0.5f);
        ImGui::TextDisabled("%s", text);
    }

    void render_prompt_body(const char* body_0, const char* body_1, const char* primary_label, const char* secondary_label, void (*primary_action)(), void (*secondary_action)())
    {
        ImGui::SetCursorPosY(scaled(38.0f));
        prompt_text_centered(body_0);
        prompt_text_centered(body_1);

        ImGui::Dummy(ImVec2(0.0f, scaled(18.0f)));

        float button_w = scaled(148.0f);
        float total_w  = button_w * 2.0f + scaled(10.0f);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - total_w) * 0.5f);

        if (launcher_button(primary_label, ImVec2(button_w, 0.0f), true))
        {
            primary_action();
        }

        ImGui::SameLine(0.0f, scaled(10.0f));
        if (launcher_button(secondary_label, ImVec2(button_w, 0.0f)))
        {
            secondary_action();
        }
    }

    void skip_download_prompt()
    {
        visible_download_prompt = false;
    }

    void update_assets()
    {
        visible_update_prompt = false;
        if (spartan::FileSystem::Exists(assets_destination))
        {
            spartan::FileSystem::Delete(assets_destination);
        }
        download_and_extract();
    }

    void skip_update_prompt()
    {
        visible_update_prompt = false;
        visible_world_list    = true;
    }

    void window_download_prompt()
    {
        if (!visible_download_prompt)
        {
            return;
        }

        update_colors();
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(scaled_vec(500.0f, 150.0f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, scaled(panel_rounding));
        if (ImGui::Begin("Download starter worlds", &visible_download_prompt,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize))
        {
            draw_panel_background(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
            render_prompt_body("No default worlds are present.", "Download the curated project package to populate the launcher.", "Download", "Not now", download_and_extract, skip_download_prompt);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void window_update_prompt()
    {
        if (!visible_update_prompt)
        {
            return;
        }

        visible_world_list = false;

        update_colors();
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(scaled_vec(500.0f, 150.0f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, scaled(panel_rounding));
        if (ImGui::Begin("Update available", &visible_update_prompt,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize))
        {
            draw_panel_background(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
            render_prompt_body("A newer starter project package is available.", "Update now, or keep browsing the worlds already on disk.", "Update", "Skip", update_assets, skip_update_prompt);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void load_selected_world()
    {
        if (selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
        {
            const string world_path = world_files[selected_index].file_path;
            record_world_opened(world_path);
            WorldPreviews::RequestGeneration(world_path);
            spartan::World::LoadFromFile(world_path);
        }
        visible_world_list = false;
    }

    void draw_world_preview(const spartan::WorldMetadata& world, const ImVec2& min_pos, const ImVec2& max_pos, float rounding)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        spartan::RHI_Texture* preview_tex = WorldPreviews::GetTexture(world.file_path);
        const spartan::Icon& world_icon   = spartan::ResourceCache::GetIcon(spartan::IconType::World);

        draw_list->AddRectFilled(min_pos, max_pos, colors.preview_bg, rounding);

        if (preview_tex && preview_tex->GetRhiResource())
        {
            // Fill cards and the larger detail preview without stretching the scene.
            const float image_aspect = static_cast<float>(preview_tex->GetWidth()) / preview_tex->GetHeight();
            const float frame_aspect = (max_pos.x - min_pos.x) / (max_pos.y - min_pos.y);
            ImVec2 uv_min(0.0f, 0.0f);
            ImVec2 uv_max(1.0f, 1.0f);
            if (image_aspect > frame_aspect)
            {
                uv_min.x = (1.0f - frame_aspect / image_aspect) * 0.5f;
                uv_max.x = 1.0f - uv_min.x;
            }
            else
            {
                uv_min.y = (1.0f - image_aspect / frame_aspect) * 0.5f;
                uv_max.y = 1.0f - uv_min.y;
            }
            draw_list->AddImageRounded(
                reinterpret_cast<ImTextureID>(preview_tex),
                min_pos,
                max_pos,
                uv_min,
                uv_max,
                IM_COL32(255, 255, 255, 255),
                rounding
            );
            return;
        }

        if (!world_icon.texture || !world_icon.texture->GetRhiResource())
        {
            return;
        }

        float icon_size = min(max_pos.x - min_pos.x, max_pos.y - min_pos.y) * 0.38f;
        ImVec2 icon_min = ImVec2(min_pos.x + (max_pos.x - min_pos.x - icon_size) * 0.5f, min_pos.y + (max_pos.y - min_pos.y - icon_size) * 0.5f);
        ImVec2 icon_max = ImVec2(icon_min.x + icon_size, icon_min.y + icon_size);
        draw_list->AddImage(
            reinterpret_cast<ImTextureID>(world_icon.texture),
            icon_min,
            icon_max,
            ImVec2(world_icon.uv_min.x, world_icon.uv_min.y),
            ImVec2(world_icon.uv_max.x, world_icon.uv_max.y),
            IM_COL32(255, 255, 255, 190)
        );
    }

    bool draw_world_card(int world_index, float card_w, float card_h)
    {
        const spartan::WorldMetadata& world = world_files[world_index];
        ImGui::PushID(world_index);

        ImVec2 card_min = ImGui::GetCursorScreenPos();
        ImVec2 card_max = ImVec2(card_min.x + card_w, card_min.y + card_h);
        ImGui::InvisibleButton(
            "##world_card",
            ImVec2(card_w, card_h),
            ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonRight
        );
        const ImGuiID card_id = ImGui::GetItemID();
        const ImVec2 cursor_after_card = ImGui::GetCursorScreenPos();

        bool is_hovered  = ImGui::IsItemHovered();
        bool is_selected = selected_index == world_index;
        bool loaded      = false;
        const bool renaming =
            is_same_world_path(rename_world_path, world.file_path);

        if (is_selected && scroll_to_selection)
        {
            ImGui::SetScrollHereY(0.5f);
            scroll_to_selection = false;
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            select_visible_index(
                visible_position_from_world_index(world_index)
            );
        }

        if (
            !renaming &&
            ImGui::IsItemClicked(ImGuiMouseButton_Left)
        )
        {
            float now = static_cast<float>(ImGui::GetTime());
            if (last_click_index == world_index && (now - last_click_time) < 0.4f)
            {
                selected_index = world_index;
                load_selected_world();
                loaded = true;
            }
            else
            {
                last_click_time = now;
                last_click_index = world_index;
                select_visible_index(visible_position_from_world_index(world_index));
            }
        }

        draw_world_context_menu(world_index);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImGui::EditorUi::draw_card(
            card_min,
            card_max,
            is_hovered,
            is_selected,
            card_rounding,
            card_id
        );

        float pad       = scaled(card_padding);
        ImVec2 image_min = ImVec2(card_min.x + pad, card_min.y + pad);
        ImVec2 image_max = ImVec2(card_max.x - pad, card_min.y + card_h * 0.58f);
        draw_world_preview(world, image_min, image_max, scaled(6.0f));

        ImVec2 text_min = ImVec2(card_min.x + pad, image_max.y + scaled(12.0f));
        ImVec2 text_max = ImVec2(card_max.x - pad, card_max.y - pad);
        draw_list->PushClipRect(text_min, text_max, true);

        ImFont* title_font = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
        float title_size   = ImGui::GetFontSize();
        float body_size    = ImGui::GetFontSize() * 0.82f;
        float path_size    = ImGui::GetFontSize() * 0.72f;
        float title_y      = text_min.y;

        if (renaming)
        {
            ImGui::SetCursorScreenPos(ImVec2(text_min.x, title_y));
            ImGui::SetNextItemWidth(max(1.0f, text_max.x - text_min.x));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, scaled(3.0f));
            ImGui::PushStyleVar(
                ImGuiStyleVar_FramePadding,
                scaled_vec(4.0f, 1.0f)
            );
            if (rename_request_focus)
            {
                ImGui::SetKeyboardFocusHere();
                rename_request_focus = false;
            }

            const bool committed = ImGui::InputText(
                "##world_rename",
                &rename_buffer,
                ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll
            );
            const bool deactivated = ImGui::IsItemDeactivated();
            const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
            ImGui::PopStyleVar(2);

            if (cancelled)
            {
                rename_world_path.clear();
            }
            else if (committed || deactivated)
            {
                pending_rename_path = world.file_path;
                pending_rename_name = rename_buffer;
                rename_world_path.clear();
            }

            ImGui::SetCursorScreenPos(cursor_after_card);
        }
        else
        {
            draw_list->AddText(
                title_font,
                title_size,
                ImVec2(text_min.x, title_y),
                colors.text_primary,
                world.name.c_str()
            );
        }

        float desc_y = title_y + ImGui::GetTextLineHeightWithSpacing();
        const char* description = world.description.empty() ? "No description available." : world.description.c_str();
        ImVec4 description_clip = ImVec4(text_min.x, desc_y, text_max.x, text_max.y - scaled(28.0f));
        draw_list->AddText(ImGui::GetFont(), body_size, ImVec2(text_min.x, desc_y), colors.text_muted, description, nullptr, text_max.x - text_min.x, &description_clip);

        string source = source_label(world.file_path);
        ImVec2 source_size = ImGui::CalcTextSize(source.c_str());
        ImVec2 chip_min    = ImVec2(text_min.x, text_max.y - source_size.y - scaled(6.0f));
        ImVec2 chip_max    = ImVec2(chip_min.x + source_size.x + scaled(14.0f), text_max.y);
        draw_list->AddRectFilled(chip_min, chip_max, colors.chip_bg, (chip_max.y - chip_min.y) * 0.5f);
        draw_list->AddText(ImGui::GetFont(), path_size, ImVec2(chip_min.x + scaled(7.0f), chip_min.y + scaled(3.0f)), colors.text_muted, source.c_str());

        draw_list->PopClipRect();

        if (is_work_in_progress(world))
        {
            const char* status = "wip";
            ImVec2 status_size = ImGui::CalcTextSize(status);
            ImVec2 chip_min    = ImVec2(image_min.x + scaled(8.0f), image_min.y + scaled(8.0f));
            ImVec2 chip_max    = ImVec2(chip_min.x + status_size.x + scaled(14.0f), chip_min.y + status_size.y + scaled(6.0f));

            draw_list->AddRectFilled(chip_min, chip_max, colors.warning, (chip_max.y - chip_min.y) * 0.5f);
            draw_list->AddText(ImVec2(chip_min.x + scaled(7.0f), chip_min.y + scaled(3.0f)), IM_COL32(0, 0, 0, 255), status);
        }

        if (
            !renaming &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)
        )
        {
            ImGui::SetTooltip("%s\n%s", world.name.c_str(), world.file_path.c_str());
        }

        ImGui::PopID();
        return loaded;
    }

    void render_card_grid_world_files(float content_width)
    {
        if (visible_indices.empty())
        {
            ImVec2 region = ImGui::GetContentRegionAvail();
            ImVec2 pos    = ImGui::GetCursorScreenPos();
            ImVec2 center = ImVec2(pos.x + region.x * 0.5f, pos.y + region.y * 0.42f);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            const char* message = search_filter.IsActive() ? "No worlds match your search" : "No worlds found on disk";
            ImVec2 message_size = ImGui::CalcTextSize(message);
            float marker_half   = scaled(38.0f);
            draw->AddRectFilled(ImVec2(center.x - marker_half, center.y - marker_half), ImVec2(center.x + marker_half, center.y + marker_half), colors.preview_bg, scaled(8.0f));
            draw->AddText(ImVec2(center.x - message_size.x * 0.5f, center.y + scaled(52.0f)), colors.text_muted, message);
            return;
        }

        float spacing = scaled(card_spacing);
        float card_w  = scaled(card_width_base);
        float card_h  = scaled(card_height_base);
        visible_columns = max(1, static_cast<int>((content_width + spacing) / (card_w + spacing)));
        card_w = floorf((content_width - spacing * static_cast<float>(visible_columns - 1)) / static_cast<float>(visible_columns));

        int col = 0;
        for (int world_index : visible_indices)
        {
            if (col > 0)
            {
                ImGui::SameLine(0.0f, spacing);
            }

            if (draw_world_card(world_index, card_w, card_h))
            {
                return;
            }

            col++;
            if (col >= visible_columns)
            {
                col = 0;
            }
        }
    }

    void draw_header(float content_w)
    {
        const float start_x = ImGui::GetCursorPosX();
        ImGui::TextColored(ImGui::Style::color_accent_1, "SPARTAN ENGINE");
        if (Editor::font_bold)
        {
            ImGui::PushFont(Editor::font_bold, scaled(26.0f));
        }
        ImGui::TextUnformatted("Your worlds");
        if (Editor::font_bold)
        {
            ImGui::PopFont();
        }
        ImGui::TextDisabled("Open a scene to explore, build and refine.");

        string count = search_filter.IsActive() ? to_string(visible_indices.size()) + " of " + to_string(world_files.size()) + " worlds" : to_string(world_files.size()) + " worlds";
        float count_w  = ImGui::CalcTextSize(count.c_str()).x + scaled(16.0f);
        float button_w = scaled(92.0f);
        float search_w = max(scaled(220.0f), content_w - count_w - button_w - scaled(28.0f));

        ImGui::Dummy(ImVec2(0.0f, scaled(6.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, scaled(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, scaled_vec(12.0f, 8.0f));
        ImGui::SetNextItemWidth(search_w);
        ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
        if (ImGui::InputTextWithHint("##world_search", "Search worlds", search_filter.InputBuf, IM_ARRAYSIZE(search_filter.InputBuf), ImGuiInputTextFlags_EscapeClearsAll))
        {
            search_filter.Build();
            rebuild_visible_indices();
        }
        ImGui::PopStyleVar(2);

        ImGui::SameLine(0.0f, scaled(8.0f));
        if (launcher_button("Refresh", ImVec2(button_w, 0.0f)))
        {
            scan_for_world_files();
            rebuild_visible_indices();
        }

        ImGui::SameLine(0.0f, scaled(8.0f));
        ImGui::SetCursorPosX(start_x + content_w - count_w);
        draw_chip(count.c_str(), colors.chip_bg, colors.text_muted);
    }

    void draw_detail_panel(float width, float height)
    {
        begin_panel("##world_details", ImVec2(width, height));

        const spartan::WorldMetadata* world = selected_world();
        if (!world)
        {
            ImGui::TextDisabled("Select a world to see details.");
            float footer_y = height - scaled(54.0f);
            if (ImGui::GetCursorPosY() < footer_y)
            {
                ImGui::SetCursorPosY(footer_y);
            }

            float width_available = width - scaled(28.0f);
            float button_w = (width_available - scaled(8.0f)) * 0.5f;
            if (launcher_button("Cancel", ImVec2(button_w, 0.0f)))
            {
                visible_world_list = false;
            }
            ImGui::SameLine(0.0f, scaled(8.0f));
            if (launcher_button("Controls", ImVec2(button_w, 0.0f)))
            {
                *GeneralWindows::GetVisibilityWindowControls() = true;
            }
            end_panel();
            return;
        }

        float content_height = max(scaled(120.0f), height - scaled(104.0f));
        ImGui::BeginChild("##world_details_content", ImVec2(width - scaled(28.0f), content_height), false);

        ImVec2 preview_min = ImGui::GetCursorScreenPos();
        ImVec2 preview_max = ImVec2(preview_min.x + width - scaled(28.0f), preview_min.y + scaled(170.0f));
        draw_world_preview(*world, preview_min, preview_max, 0.0f);
        ImGui::Dummy(ImVec2(width - scaled(28.0f), scaled(170.0f)));

        ImGui::Dummy(ImVec2(0.0f, scaled(12.0f)));

        string source = source_label(world->file_path);
        draw_chip(source.c_str(), colors.chip_bg, colors.text_muted);
        if (is_work_in_progress(*world))
        {
            ImGui::SameLine(0.0f, scaled(8.0f));
            draw_chip("work in progress", colors.warning, IM_COL32(0, 0, 0, 255));
        }

        ImGui::Dummy(ImVec2(0.0f, scaled(8.0f)));
        if (Editor::font_bold)
        {
            ImGui::PushFont(Editor::font_bold, 0.0f);
        }
        ImGui::TextUnformatted(world->name.c_str());
        if (Editor::font_bold)
        {
            ImGui::PopFont();
        }

        if (!world->description.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colors.text_muted));
            ImGui::TextWrapped("%s", world->description.c_str());
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextDisabled("No description available");
        }

        ImGui::Dummy(ImVec2(0.0f, scaled(12.0f)));
        ImGui::TextDisabled("Location");
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", world->file_path.c_str());
        ImGui::PopTextWrapPos();

        ImGui::EndChild();

        float footer_w = width - scaled(28.0f);
        ImGui::SetCursorPosX(scaled(14.0f));
        if (launcher_button("Open World", ImVec2(footer_w, 0.0f), true))
        {
            load_selected_world();
        }

        float button_w = (footer_w - scaled(8.0f)) * 0.5f;
        ImGui::SetCursorPosX(scaled(14.0f));
        if (launcher_button("Cancel", ImVec2(button_w, 0.0f)))
        {
            visible_world_list = false;
        }
        ImGui::SameLine(0.0f, scaled(8.0f));
        if (launcher_button("Controls", ImVec2(button_w, 0.0f)))
        {
            *GeneralWindows::GetVisibilityWindowControls() = true;
        }

        end_panel();
    }

    void handle_keyboard()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            return;
        }

        const bool renaming = !rename_world_path.empty();
        const bool popup_open = ImGui::IsPopupOpen(
            "",
            ImGuiPopupFlags_AnyPopupId
        );

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            if (!renaming && !popup_open)
            {
                visible_world_list = false;
            }
            return;
        }

        if (renaming || popup_open || ImGui::GetIO().WantTextInput)
        {
            return;
        }

        if (visible_indices.empty())
        {
            return;
        }

        int target = selected_visible_index;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
        {
            target++;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
        {
            target--;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
        {
            target += visible_columns;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
        {
            target -= visible_columns;
        }

        if (target != selected_visible_index)
        {
            select_visible_index(target);
            scroll_to_selection = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
        {
            start_world_rename(selected_index);
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            if (
                selected_index >= 0 &&
                selected_index < static_cast<int>(world_files.size())
            )
            {
                pending_delete_path = world_files[selected_index].file_path;
            }
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        {
            load_selected_world();
        }
    }

    void window_world_list()
    {
        if (!visible_world_list)
        {
            return;
        }

        update_colors();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 available(viewport->WorkSize.x * 0.92f, viewport->WorkSize.y * 0.92f);
        // Fit the initial placement to the editor, then let ImGui preserve the user's
        // position and size, including a detached platform window on another monitor.
        ImGui::SetNextWindowSize(ImVec2(min(scaled(1440.0f), available.x), min(scaled(880.0f), available.y)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(scaled_vec(480.0f, 320.0f), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, scaled_vec(16.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, scaled(panel_rounding));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin("World Launcher", &visible_world_list, flags))
        {
            if (spartan::FileSystem::IsDirectoryEmpty(spartan::ResourceCache::GetProjectDirectory()))
            {
                visible_world_list      = false;
                visible_download_prompt = true;
                ImGui::End();
                ImGui::PopStyleVar(2);
                return;
            }

            float content_w = ImGui::GetContentRegionAvail().x;
            rebuild_visible_indices();
            draw_header(content_w);
            ImGui::Dummy(ImVec2(0.0f, scaled(10.0f)));
            handle_keyboard();

            ImVec2 remaining = ImGui::GetContentRegionAvail();
            float panels_h = max(scaled(260.0f), remaining.y);
            float details_w  = min(scaled(details_width_base), max(scaled(320.0f), remaining.x * 0.32f));
            float grid_w     = remaining.x - details_w - scaled(section_spacing);
            if (grid_w < scaled(300.0f))
            {
                grid_w    = remaining.x;
                details_w = remaining.x;
            }

            begin_panel("##world_grid_panel", ImVec2(grid_w, panels_h));
            {
                ImGui::BeginChild("##world_grid_scroll", ImVec2(grid_w - scaled(28.0f), panels_h - scaled(28.0f)), false);
                render_card_grid_world_files(ImGui::GetContentRegionAvail().x);
                ImGui::EndChild();
            }
            end_panel();

            if (details_w != remaining.x)
            {
                ImGui::SameLine(0.0f, scaled(section_spacing));
                draw_detail_panel(details_w, panels_h);
            }
            else
            {
                ImGui::Dummy(ImVec2(0.0f, scaled(section_spacing)));
                draw_detail_panel(details_w, scaled(430.0f));
            }

            process_pending_world_actions();
            draw_delete_confirmation();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

void WorldSelector::Initialize(Editor* editor_in)
{
    editor = editor_in;
    load_world_recency();

    size_t file_count  = spartan::FileSystem::GetFilesInDirectory(spartan::ResourceCache::GetProjectDirectory()).size();
    file_count        += spartan::FileSystem::GetDirectoriesInDirectory(spartan::ResourceCache::GetProjectDirectory()).size();
    downloaded_and_extracted = file_count > 1;

    if (downloaded_and_extracted)
    {
        scan_for_world_files();
    }
}

void WorldSelector::Tick()
{
    window_download_prompt();
    window_update_prompt();
    window_world_list();
}

bool WorldSelector::GetVisible()
{
    return visible_world_list || visible_download_prompt || visible_update_prompt;
}

void WorldSelector::SetVisible(bool visibility)
{
    visible_world_list      = false;
    visible_download_prompt = false;
    visible_update_prompt   = false;

    if (!visibility)
    {
        return;
    }

    if (!downloaded_and_extracted)
    {
        visible_download_prompt = true;
        return;
    }

    visible_world_list = true;
    scan_for_world_files();
    selected_index         = world_files.empty() ? -1 : 0;
    selected_visible_index = 0;
    last_click_index       = -1;
    last_click_time        = -1.0f;
    scroll_to_selection    = true;

    if (!update_check_started)
    {
        update_check_started = true;
        check_assets_outdated_async();
    }
}
