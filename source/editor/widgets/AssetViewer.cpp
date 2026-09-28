/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//======================================
#include "pch.h"
#include "../EditorHistory.h"
#include "AssetViewer.h"
#include "AssetViewerCommon.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/source/imgui_stdlib.h"
#include "file_system/FileSystem.h"
#include "geometry/GeometryProcessing.h"
#include "geometry/Mesh.h"
#include "io/pugixml.hpp"
#include "rhi/RHI_Texture.h"
#include "rendering/Material.h"
#include "rendering/Renderer.h"
#include "resource/ResourceCache.h"
#include "world/Entity.h"
#include "world/GameReady.h"
#include "world/Prefab.h"
#include "world/World.h"
#include "core/Event.h"
#include "world/components/Camera.h"
#include "world/components/Light.h"
#include "world/components/Render.h"
#include "mcp/McpJson.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <sstream>
//======================================

using namespace std;
using namespace spartan;
using namespace asset_viewer_common;

namespace
{
    bool json_object_erase(JsonValue& object, const string& key)
    {
        if (object.type != mcp_json::kind::object)
        {
            return false;
        }

        const auto iterator = find_if(
            object.object_items.begin(),
            object.object_items.end(),
            [&](const pair<string, JsonValue>& item)
            {
                return item.first == key;
            }
        );
        if (iterator == object.object_items.end())
        {
            return false;
        }

        object.object_items.erase(iterator);
        return true;
    }

    string asset_display_name(const string& value)
    {
        string result;
        result.reserve(value.size() + 8);
        for (size_t index = 0; index < value.size(); index++)
        {
            const unsigned char character = value[index];
            const bool separator =
                character == '_' ||
                character == '-' ||
                isspace(character);
            if (separator)
            {
                if (!result.empty() && result.back() != ' ')
                {
                    result += ' ';
                }
                continue;
            }

            if (
                isupper(character) &&
                index > 0 &&
                !result.empty() &&
                result.back() != ' ' &&
                (
                    islower(
                        static_cast<unsigned char>(
                            value[index - 1]
                        )
                    ) ||
                    isdigit(
                        static_cast<unsigned char>(
                            value[index - 1]
                        )
                    )
                )
            )
            {
                result += ' ';
            }
            result += static_cast<char>(tolower(character));
        }

        const size_t first_space = result.find(' ');
        const string first_word = result.substr(0, first_space);
        if (
            first_space != string::npos &&
            (
                first_word == "build" ||
                first_word == "create" ||
                first_word == "generate" ||
                first_word == "make"
            )
        )
        {
            result.erase(0, first_space + 1);
        }

        bool capitalize = true;
        for (char& character : result)
        {
            if (character == ' ')
            {
                capitalize = true;
                continue;
            }
            if (capitalize)
            {
                character = static_cast<char>(
                    toupper(
                        static_cast<unsigned char>(character)
                    )
                );
                capitalize = false;
            }
        }
        return result.empty() ? value : result;
    }

    // a packed map is a gpu side artifact, occlusion in red, roughness in green, metalness in blue
    // and height in alpha, the material already binds it and nothing authors it by hand, so it is
    // noise in a browser meant for source assets
    bool is_packed_texture(const string& path)
    {
        const string stem = lower_copy(
            FileSystem::GetFileNameWithoutExtensionFromFilePath(path)
        );
        const auto ends_with =
            [&stem](const string& suffix)
        {
            return
                stem.size() > suffix.size() &&
                stem.compare(
                    stem.size() - suffix.size(),
                    suffix.size(),
                    suffix
                ) == 0;
        };

        // the generator writes name_packed.png, the runtime repacker writes
        // name_packed_slot0.tex_packed.texture
        return
            ends_with("_packed") ||
            ends_with(".tex_packed") ||
            stem.find("_packed_slot") != string::npos;
    }

    string serialize_json(
        const JsonValue& value,
        const uint32_t depth = 0
    )
    {
        const string indentation(depth * 2, ' ');
        const string child_indentation((depth + 1) * 2, ' ');
        switch (value.type)
        {
        case mcp_json::kind::null:
            return "null";
        case mcp_json::kind::boolean:
            return value.boolean_value ? "true" : "false";
        case mcp_json::kind::number:
        {
            ostringstream stream;
            stream.precision(17);
            stream << value.number_value;
            return stream.str();
        }
        case mcp_json::kind::string:
            return serialize_json_string(value.string_value);
        case mcp_json::kind::array:
        {
            if (value.array_items.empty())
            {
                return "[]";
            }
            string result = "[\n";
            for (size_t index = 0; index < value.array_items.size(); index++)
            {
                result +=
                    child_indentation +
                    serialize_json(value.array_items[index], depth + 1);
                result +=
                    index + 1 < value.array_items.size() ?
                    ",\n" :
                    "\n";
            }
            return result + indentation + "]";
        }
        case mcp_json::kind::object:
        {
            if (value.object_items.empty())
            {
                return "{}";
            }
            string result = "{\n";
            size_t index = 0;
            for (const auto& [key, child] : value.object_items)
            {
                result +=
                    child_indentation +
                    serialize_json_string(key) +
                    ": " +
                    serialize_json(child, depth + 1);
                result +=
                    ++index < value.object_items.size() ?
                    ",\n" :
                    "\n";
            }
            return result + indentation + "}";
        }
        }
        return "null";
    }

    // rewrites references to a renamed file inside the library's xml assets, prefabs point at
    // meshes and materials, materials point at textures, a rename without this orphans them
    uint32_t retarget_library_references(
        const string& library_root,
        const string& old_leaf,
        const string& new_leaf
    )
    {
        if (
            old_leaf.empty() ||
            new_leaf.empty() ||
            old_leaf == new_leaf
        )
        {
            return 0;
        }

        uint32_t changed_files = 0;
        error_code error;
        filesystem::recursive_directory_iterator iterator(
            filesystem::path(library_root),
            error
        );
        if (error)
        {
            return 0;
        }

        for (
            ;
            iterator != filesystem::recursive_directory_iterator();
            iterator.increment(error)
        )
        {
            if (error)
            {
                break;
            }

            const filesystem::directory_entry& item = *iterator;
            if (!item.is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }

            const string path = item.path().generic_string();
            if (
                lower_copy(
                    FileSystem::GetExtensionFromFilePath(path)
                ) != ".xml"
            )
            {
                continue;
            }

            string contents;
            if (!FileSystem::ReadFile(path, contents))
            {
                continue;
            }

            string result;
            result.reserve(contents.size() + 64);
            size_t cursor = 0;
            bool touched = false;
            while (true)
            {
                const size_t hit = contents.find(old_leaf, cursor);
                if (hit == string::npos)
                {
                    break;
                }

                // a file name has to begin right after a separator or a quote, without this
                // renaming bottle.mesh would also rewrite glass_bottle.mesh
                const char before =
                    hit == 0 ? '"' : contents[hit - 1];
                const bool boundary =
                    before == '/' ||
                    before == '\\' ||
                    before == '"' ||
                    before == '\'';
                result.append(contents, cursor, hit - cursor);
                result.append(boundary ? new_leaf : old_leaf);
                cursor = hit + old_leaf.size();
                touched = touched || boundary;
            }

            if (!touched)
            {
                continue;
            }
            result.append(contents, cursor, string::npos);
            if (FileSystem::WriteFile(path, result))
            {
                changed_files++;
            }
        }

        return changed_files;
    }

    // imgui 1.92 asserts if setcursorscreenpos is the last layout call in a window
    void finish_overlay_cursor(const ImVec2& cursor)
    {
        if (ImGuiWindow* window = ImGui::GetCurrentWindow())
        {
            window->DC.CurrLineSize = ImVec2(0.0f, 0.0f);
            window->DC.CurrLineTextBaseOffset = 0.0f;
        }
        ImGui::SetCursorScreenPos(cursor);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::SetCursorScreenPos(cursor);
    }

    ImU32 asset_type_color(const string& type, const int alpha = 255)
    {
        if (type == "mesh")
        {
            return IM_COL32(74, 170, 255, alpha);
        }
        if (type == "material")
        {
            return IM_COL32(190, 112, 255, alpha);
        }
        if (type == "texture")
        {
            return IM_COL32(255, 176, 74, alpha);
        }
        return IM_COL32(75, 205, 150, alpha);
    }

    IconType asset_type_icon(const string& type)
    {
        if (type == "mesh")
        {
            return IconType::Model;
        }
        if (type == "material")
        {
            return IconType::Material;
        }
        if (type == "texture")
        {
            return IconType::Texture;
        }
        return IconType::Entity;
    }

    bool toolbar_toggle(
        const char* label,
        const bool active,
        const ImVec2& size = ImVec2(0.0f, 0.0f)
    )
    {
        if (active)
        {
            const ImVec4 accent = ImGui::Style::color_accent_1;
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ImVec4(accent.x, accent.y, accent.z, 0.28f)
            );
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImVec4(accent.x, accent.y, accent.z, 0.42f)
            );
        }
        const bool clicked = ImGui::Button(label, size);
        if (active)
        {
            ImGui::PopStyleColor(2);
        }
        return clicked;
    }

    void horizontal_splitter(
        const char* id,
        float& width,
        const float minimum,
        const float maximum,
        const float height,
        const float direction
    )
    {
        const float thickness = 5.0f * ui_scale();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(thickness, height));
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (active || hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (active)
        {
            width = clamp(
                width + ImGui::GetIO().MouseDelta.x * direction,
                minimum,
                maximum
            );
        }
        const ImU32 color = ImGui::GetColorU32(
            active
                ? ImGuiCol_SeparatorActive
                : hovered
                    ? ImGuiCol_SeparatorHovered
                    : ImGuiCol_Separator
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            position,
            ImVec2(position.x + thickness, position.y + height),
            color
        );
    }

    void vertical_splitter(
        const char* id,
        float& height,
        const float minimum,
        const float maximum,
        const float width
    )
    {
        const float thickness = 5.0f * ui_scale();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(width, thickness));
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (active || hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
        if (active)
        {
            height = clamp(
                height + ImGui::GetIO().MouseDelta.y,
                minimum,
                maximum
            );
        }
        const ImU32 color = ImGui::GetColorU32(
            active
                ? ImGuiCol_SeparatorActive
                : hovered
                    ? ImGuiCol_SeparatorHovered
                    : ImGuiCol_Separator
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            position,
            ImVec2(position.x + width, position.y + thickness),
            color
        );
    }

    void draw_asset_identity(
        const string& name,
        const string& type,
        const char* context
    )
    {
        const float scale = ui_scale();
        const ImVec4 background =
            ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(
                background.x,
                background.y,
                background.z,
                0.42f
            )
        );
        ImGui::BeginChild(
            "##asset_identity",
            ImVec2(0.0f, 72.0f * scale),
            ImGuiChildFlags_None
        );
        ImGui::SetCursorPos(
            ImVec2(12.0f * scale, 16.0f * scale)
        );
        ImGuiSp::image(
            asset_type_icon(type),
            30.0f * scale,
            ImVec4(0.82f, 0.9f, 1.0f, 1.0f)
        );
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(name.c_str());
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(
                asset_type_color(type)
            ),
            "%s  %s",
            type.c_str(),
            context
        );
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // a labelled number in a box, the inspector shows the handful that decide whether an asset is
    // game ready as a row of these rather than as a column of text
    void draw_metric_card(
        const char* label,
        const string& value,
        const float width,
        const ImU32 accent = 0
    )
    {
        const float scale = ui_scale();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(width, 46.0f * scale);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        draw_list->AddRectFilled(
            origin,
            ImVec2(origin.x + size.x, origin.y + size.y),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(frame.x, frame.y, frame.z, 0.45f)
            ),
            5.0f * scale
        );
        if (accent != 0)
        {
            draw_list->AddRectFilled(
                origin,
                ImVec2(origin.x + 3.0f * scale, origin.y + size.y),
                accent,
                2.0f * scale
            );
        }
        draw_list->AddText(
            ImVec2(origin.x + 9.0f * scale, origin.y + 6.0f * scale),
            ImGui::GetColorU32(ImGuiCol_Text),
            value.c_str()
        );
        draw_list->AddText(
            ImVec2(origin.x + 9.0f * scale, origin.y + 25.0f * scale),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            label
        );
        ImGui::Dummy(size);
    }

    void section_title(const char* title)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted(title);
        ImGui::Separator();
    }
}

AssetViewer::AssetViewer(Editor* editor) : Widget(editor)
{
    m_title         = "Asset Viewer";
    m_visible       = false;
    m_toolbar_order = 6;
    m_toolbar_icon  = static_cast<int>(spartan::IconType::Model);
    m_size_initial = math::Vector2(1280.0f, 760.0f);
    m_size_min     = math::Vector2(720.0f, 480.0f);
    m_next_refresh_check = chrono::steady_clock::now();
    m_world_unloading_handle = SP_SUBSCRIBE_TO_EVENT(
        EventType::WorldUnloading,
        SP_EVENT_HANDLER(ClearLoadedAsset)
    );
}

AssetViewer::~AssetViewer()
{
    if (m_world_unloading_handle != 0)
    {
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldUnloading, m_world_unloading_handle);
        m_world_unloading_handle = 0;
    }
    DestroyPreviewScene();
}

void AssetViewer::OnVisible()
{
    RefreshCatalog(true);
    ScanRevisionCandidates(true);
    // an authoring tool parks the asset it is working on in the world under this tag, adopting it is
    // what makes the panel show that work when it is opened by hand rather than driven
    Entity* focused_workspace = nullptr;
    for (Entity* entity : World::GetEntities())
    {
        if (
            entity &&
            entity->HasTag("authoring_workspace")
        )
        {
            entity->SetActive(false);
            entity->SetTransient(true);
            focused_workspace = entity;
        }
    }
    if (
        !PreviewRoot() &&
        m_selected_asset >= 0 &&
        m_selected_asset <
            static_cast<int>(m_assets.size())
    )
    {
        LoadSelectedAsset(false, false);
    }
    else if (!PreviewRoot() && focused_workspace)
    {
        PreviewEntity(focused_workspace);
    }
    else if (!PreviewRoot() && m_selected_asset < 0)
    {
        // an empty viewport on open is a dead end, the first prefab is what the user most likely
        // came to look at so it is up and expanded before they click anything
        for (int index = 0; index < static_cast<int>(m_assets.size()); index++)
        {
            if (m_assets[index].type != "prefab")
            {
                continue;
            }
            m_selected_asset = index;
            m_selected_assets.clear();
            m_selected_assets.insert(m_assets[index].id);
            m_selection_anchor = index;
            m_expanded_assets.insert(m_assets[index].id);
            m_inspector_tab = 0;
            LoadSelectedAsset();
            break;
        }
    }
}

void AssetViewer::OnInvisible()
{
    // the preview is kept and only the pending render is dropped. tearing it down here meant closing the
    // panel threw away the asset being reviewed, so the next capture failed with nothing loaded and the panel
    // had to be reopened and re-previewed before it could be looked at again, which is where the open and
    // close churn in a driven run comes from
    //
    // this is safe to leave standing because the rig, its camera and its lights are all transient and
    // parented under the preview root, and that root is inactive, so none of it lights or draws into the main
    // scene and none of it is written into the world
    Renderer::InvalidateSecondaryView();
    m_preview_dirty = false;
}

void AssetViewer::OnTickVisible()
{
    const string world_file_path = World::GetFilePath();
    if (world_file_path != m_world_file_path)
    {
        DestroyPreviewScene();
        RefreshCatalog(m_visible);
    }
    else if (chrono::steady_clock::now() >= m_next_refresh_check)
    {
        m_next_refresh_check =
            chrono::steady_clock::now() +
            chrono::seconds(1);
        RefreshCatalog(false);
        ScanRevisionCandidates(false);
        // unsaved mesh edits win over the auto reload, otherwise a background
        // write would silently throw the edits away
        if (
            !m_loaded_path.empty() &&
            !m_working_modified &&
            !m_working_lods_built &&
            FileSystem::Exists(m_loaded_path)
        )
        {
            const string write_time =
                FileSystem::GetLastWriteTime(m_loaded_path);
            if (write_time != m_loaded_write_time)
            {
                if (m_selected_asset >= 0)
                {
                    LoadSelectedAsset(false, true);
                }
                else
                {
                    // a linked file preview has no catalog entry, reloading through the
                    // catalog path would just clear it
                    const float yaw = m_preview_yaw;
                    const float pitch = m_preview_pitch;
                    const float zoom = m_preview_zoom;
                    const math::Vector2 pan = m_texture_pan;
                    const string path = m_loaded_path;
                    LoadDependencyPreview(path);
                    m_preview_yaw = yaw;
                    m_preview_pitch = pitch;
                    m_preview_zoom = zoom;
                    m_texture_pan = pan;
                }
            }
        }
    }

    // the world can remove the previewed entity at any time, drop the stale id
    // before anything walks the preview hierarchy this frame
    const bool preview_entity_alive = PreviewRoot() != nullptr;
    if (m_preview_root_id != 0 && !preview_entity_alive)
    {
        m_preview_root_id = 0;
        m_preview_camera_id = 0;
        m_preview_root_owned = false;
        m_preview_dirty = false;
        m_preview_orbiting = false;
    }

    if (
        m_preview_auto_rotate &&
        (m_mesh || m_material || preview_entity_alive) &&
        !m_texture &&
        !m_preview_orbiting
    )
    {
        m_preview_yaw -= ImGui::GetIO().DeltaTime * 0.42f;
        m_preview_dirty = true;
    }

    // an inline rename whose row is no longer drawn can never be committed or cancelled, drop it
    // rather than leaving the library stuck in a rename that is invisible
    if (!m_rename_asset_id.empty())
    {
        bool found = false;
        for (const AssetEntry& entry : m_assets)
        {
            if (entry.id == m_rename_asset_id)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            m_rename_asset_id.clear();
        }
    }
    if (
        !m_rename_dependency_path.empty() &&
        !FileSystem::Exists(m_rename_dependency_path)
    )
    {
        m_rename_dependency_path.clear();
    }

    const float available_width =
        ImGui::GetContentRegionAvail().x;
    const bool compact_layout =
        available_width < 1040.0f * ui_scale();

    const float status_height =
        ImGui::GetTextLineHeightWithSpacing() +
        8.0f * ui_scale();
    const float available_height =
        max(
            1.0f,
            ImGui::GetContentRegionAvail().y -
            status_height
        );
    const float splitter_width = 5.0f * ui_scale();

    if (compact_layout)
    {
        const float minimum_preview = min(
            240.0f * ui_scale(),
            available_height * 0.55f
        );
        const float minimum_lower = min(
            220.0f * ui_scale(),
            available_height -
                minimum_preview -
                splitter_width
        );
        const float maximum_preview = max(
            minimum_preview,
            available_height -
                minimum_lower -
                splitter_width
        );
        m_compact_preview_height = clamp(
            m_compact_preview_height,
            minimum_preview,
            maximum_preview
        );
        const float lower_height = max(
            1.0f,
            available_height -
                m_compact_preview_height -
                splitter_width
        );

        DrawPreview(
            available_width,
            m_compact_preview_height
        );
        vertical_splitter(
            "##asset_preview_splitter",
            m_compact_preview_height,
            minimum_preview,
            maximum_preview,
            available_width
        );

        const float minimum_library = min(
            230.0f * ui_scale(),
            available_width * 0.4f
        );
        const float minimum_inspector = min(
            320.0f * ui_scale(),
            available_width * 0.45f
        );
        const float maximum_library = max(
            minimum_library,
            available_width -
                minimum_inspector -
                splitter_width
        );
        m_library_width = clamp(
            m_library_width,
            minimum_library,
            maximum_library
        );

        DrawAssetList(
            m_library_width,
            lower_height
        );
        ImGui::SameLine(0.0f, 0.0f);
        horizontal_splitter(
            "##asset_compact_library_splitter",
            m_library_width,
            minimum_library,
            maximum_library,
            lower_height,
            1.0f
        );
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginGroup();
        DrawDetails(lower_height);
        ImGui::EndGroup();
    }
    else
    {
        const float minimum_library = 230.0f * ui_scale();
        const float minimum_preview = 360.0f * ui_scale();
        const float minimum_inspector = 320.0f * ui_scale();
        const float maximum_library = min(
            340.0f * ui_scale(),
            available_width -
                minimum_preview -
                minimum_inspector -
                splitter_width * 2.0f
        );
        m_library_width = clamp(
            m_library_width,
            minimum_library,
            max(minimum_library, maximum_library)
        );

        const float maximum_inspector = min(
            400.0f * ui_scale(),
            available_width -
                minimum_preview -
                m_library_width -
                splitter_width * 2.0f
        );
        m_inspector_width = clamp(
            m_inspector_width,
            minimum_inspector,
            max(minimum_inspector, maximum_inspector)
        );
        const float preview_width =
            available_width -
            m_library_width -
            m_inspector_width -
            splitter_width * 2.0f;

        DrawAssetList(m_library_width, available_height);
        ImGui::SameLine(0.0f, 0.0f);
        horizontal_splitter(
            "##asset_library_splitter",
            m_library_width,
            minimum_library,
            max(minimum_library, maximum_library),
            available_height,
            1.0f
        );
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginGroup();
        DrawPreview(preview_width, available_height);
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 0.0f);
        horizontal_splitter(
            "##asset_inspector_splitter",
            m_inspector_width,
            minimum_inspector,
            max(minimum_inspector, maximum_inspector),
            available_height,
            -1.0f
        );
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginGroup();
        DrawDetails(available_height);
        ImGui::EndGroup();
    }

    if (!m_rename_commit_id.empty())
    {
        const string id = m_rename_commit_id;
        const string name = m_rename_commit_name;
        m_rename_commit_id.clear();
        m_rename_commit_name.clear();
        for (int index = 0; index < static_cast<int>(m_assets.size()); index++)
        {
            if (m_assets[index].id == id)
            {
                RenameAsset(index, name);
                break;
            }
        }
    }

    if (!m_rename_commit_path.empty())
    {
        const string path = m_rename_commit_path;
        const string name = m_rename_commit_name;
        m_rename_commit_path.clear();
        m_rename_commit_name.clear();
        const string new_path =
            FileSystem::GetDirectoryFromFilePath(path) +
            sanitize_asset_name(name) +
            FileSystem::GetExtensionFromFilePath(path);
        const bool previewing =
            normalized_path(m_loaded_path) == normalized_path(path);
        if (RenameAssetFile(path, name))
        {
            // reloading the preview overwrites the status line, the rename result is what the
            // user needs to read
            const string status = m_status;
            RefreshCatalog(true);
            if (previewing && FileSystem::Exists(new_path))
            {
                LoadDependencyPreview(new_path);
            }
            m_status = status;
        }
    }

    // f2 renames the selection, same as the hierarchy, the rename input owns the keyboard while
    // it is open so the shortcut cannot fire on top of itself
    if (
        m_rename_asset_id.empty() &&
        m_rename_dependency_path.empty() &&
        m_selected_assets.size() <= 1 &&
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size()) &&
        ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows
        ) &&
        ImGui::IsKeyPressed(ImGuiKey_F2)
    )
    {
        m_rename_dependency_path.clear();
        m_rename_asset_id = m_assets[m_selected_asset].id;
        m_rename_buffer = m_assets[m_selected_asset].name;
        m_rename_request_focus = true;
    }

    DrawDeleteConfirmation();
    DrawCleanupConfirmation();
    DrawStatusBar();
}

void AssetViewer::RefreshCatalog(bool force)
{
    m_next_refresh_check =
        chrono::steady_clock::now() +
        chrono::seconds(1);

    const string world_file_path = World::GetFilePath();
    const string catalog_path =
        World::GetLibraryResourceDirectory() +
        "catalog.json";
    // the directories are part of the signature, files can land on disk without the
    // catalog ever being rewritten. mcp writes into mcp/blockout, the viewer catalog
    // lives in mcp/library, so both roots have to be watched
    string write_time;
    auto append_root_times = [&](const string& root)
    {
        if (root.empty())
        {
            return;
        }
        const string catalog = root + "catalog.json";
        if (FileSystem::Exists(catalog))
        {
            write_time +=
                "|" +
                FileSystem::GetLastWriteTime(catalog);
        }
        for (
            const char* folder :
            { "meshes", "materials", "textures", "prefabs" }
        )
        {
            const string directory = root + folder;
            if (FileSystem::Exists(directory))
            {
                write_time +=
                    "|" +
                    FileSystem::GetLastWriteTime(directory);
            }
        }
    };
    append_root_times(World::GetLibraryResourceDirectory());
    append_root_times(World::GetGeneratedResourceDirectory());

    if (
        !force &&
        world_file_path == m_world_file_path &&
        catalog_path == m_catalog_path &&
        write_time == m_catalog_write_time
    )
    {
        return;
    }

    const bool library_changed =
        world_file_path != m_world_file_path ||
        catalog_path != m_catalog_path ||
        write_time != m_catalog_write_time;
    if (library_changed)
    {
        m_cleanup_generation++;
        m_cleanup.scanned = false;
    }

    const string previous_id =
        (
            m_selected_asset >= 0 &&
            m_selected_asset < static_cast<int>(m_assets.size())
        ) ?
        m_assets[m_selected_asset].id :
        "";
    const bool reload_preview =
        !m_loaded_path.empty();

    m_world_file_path = world_file_path;
    m_catalog_path = catalog_path;
    m_catalog_write_time = write_time;
    m_assets.clear();
    m_selected_asset = -1;
    ClearLoadedAsset();

    // a missing or broken catalog must not hide the files on disk, so the parse only
    // bails out of itself and the directory scan below still runs
    string catalog_error;
    JsonValue root;
    string source;
    const JsonValue* assets = nullptr;
    if (!FileSystem::Exists(m_catalog_path))
    {
        catalog_error = "no catalog registered";
    }
    else if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        catalog_error =
            "catalog could not be read from " +
            m_catalog_path;
    }
    else
    {
        string parse_error;
        if (!mcp_json::parse(source, root, parse_error))
        {
            catalog_error = "invalid catalog, " + parse_error;
        }
        else
        {
            const JsonValue* schema_version =
                root.find("schema_version");
            assets = root.find("assets");
            if (
                root.type != mcp_json::kind::object ||
                !schema_version ||
                static_cast<int>(schema_version->number_or(0.0)) != 2 ||
                !assets ||
                assets->type != mcp_json::kind::object
            )
            {
                catalog_error =
                    "unsupported or invalid asset catalog schema";
                assets = nullptr;
            }
        }
    }

    static const vector<pair<string, JsonValue>> no_assets;
    for (
        const auto& [catalog_id, value] :
        assets ? assets->object_items : no_assets
    )
    {
        if (value.type != mcp_json::kind::object)
        {
            continue;
        }

        AssetEntry asset;
        asset.id =
            value.find("id") ?
            value.find("id")->string_or(catalog_id) :
            catalog_id;
        asset.name =
            value.find("name") ?
            value.find("name")->string_or(asset.id) :
            asset.id;
        asset.type =
            value.find("type") ?
            lower_copy(value.find("type")->string_or("")) :
            "";
        asset.path =
            value.find("path") ?
            value.find("path")->string_or("") :
            "";
        asset.source_path =
            value.find("source_path") ?
            value.find("source_path")->string_or("") :
            "";
        asset.thumbnail_path =
            value.find("thumbnail_path") ?
            value.find("thumbnail_path")->string_or("") :
            "";
        asset.aliases = json_strings(value.find("aliases"));
        asset.tags = json_strings(value.find("tags"));
        asset.dependencies = json_strings(
            value.find("dependencies")
        );
        if (asset.type == "prefab")
        {
            for (
                const char* attribute :
                {
                    "mesh_path",
                    "material_path"
                }
            )
            {
                for (
                    const string& dependency :
                    collect_xml_references(
                        asset.path,
                        attribute
                    )
                )
                {
                    if (
                        find(
                            asset.dependencies.begin(),
                            asset.dependencies.end(),
                            dependency
                        ) == asset.dependencies.end()
                    )
                    {
                        asset.dependencies.push_back(dependency);
                    }
                }
            }
        }
        const vector<string> direct_dependencies =
            asset.dependencies;
        for (const string& dependency : direct_dependencies)
        {
            if (
                lower_copy(
                    FileSystem::GetExtensionFromFilePath(
                        dependency
                    )
                ) != ".xml"
            )
            {
                continue;
            }
            for (
                const string& texture_path :
                collect_xml_references(
                    dependency,
                    "texture_path"
                )
            )
            {
                if (
                    find(
                        asset.dependencies.begin(),
                        asset.dependencies.end(),
                        texture_path
                    ) == asset.dependencies.end()
                )
                {
                    asset.dependencies.push_back(texture_path);
                }
            }
        }
        const JsonValue* quality = value.find("quality");
        if (
            quality &&
            quality->type == mcp_json::kind::object
        )
        {
            asset.quality_score =
                quality->find("score") ?
                static_cast<float>(
                    quality->find("score")->number_or(0.0)
                ) :
                0.0f;
            asset.quality_verified =
                quality->find("verified") ?
                quality->find("verified")->boolean_or(false) :
                false;
        }

        if (
            asset.type == "mesh" ||
            asset.type == "material" ||
            asset.type == "prefab" ||
            asset.type == "texture"
        )
        {
            m_assets.emplace_back(move(asset));
        }
    }

    // the catalog only holds what an agent chose to register, browse the resource
    // directories too so no file on disk is unreachable from the tool
    {
        unordered_set<string> known;
        for (const AssetEntry& entry : m_assets)
        {
            known.insert(entry.type + ":" + lower_copy(entry.id));
            known.insert(entry.type + ":" + lower_copy(entry.name));
            known.insert(
                "path:" + normalized_path(entry.path)
            );
        }

        const auto scan =
            [this, &known](
                const string& root,
                const char* folder,
                const char* type
            )
        {
            if (root.empty())
            {
                return;
            }
            const string directory = root + folder;
            error_code error;
            filesystem::directory_iterator iterator(
                filesystem::path(directory),
                error
            );
            if (error)
            {
                return;
            }

            for (
                ;
                iterator != filesystem::directory_iterator();
                iterator.increment(error)
            )
            {
                if (error)
                {
                    return;
                }

                const filesystem::directory_entry& item = *iterator;
                if (!item.is_regular_file(error) || error)
                {
                    error.clear();
                    continue;
                }

                const string path =
                    item.path().generic_string();
                if (asset_type_from_path(path) != type)
                {
                    continue;
                }
                const string stem =
                    FileSystem::
                        GetFileNameWithoutExtensionFromFilePath(
                            path
                        );
                const string id =
                    FileSystem::GetFileNameFromFilePath(path);
                if (
                    known.count(
                        "path:" + normalized_path(path)
                    ) ||
                    known.count(type + string(":") + lower_copy(stem)) ||
                    known.count(type + string(":") + lower_copy(id))
                )
                {
                    continue;
                }

                AssetEntry entry;
                entry.id = id;
                entry.name = stem;
                entry.type = type;
                entry.path = path;
                entry.disk_only = true;

                known.insert(type + string(":") + lower_copy(stem));
                known.insert("path:" + normalized_path(path));
                m_assets.emplace_back(move(entry));
            }
        };

        const auto scan_root = [&](const string& root)
        {
            scan(root, "meshes", "mesh");
            scan(root, "materials", "material");
            scan(root, "textures", "texture");
            scan(root, "prefabs", "prefab");
        };
        const string library_root =
            FileSystem::GetDirectoryFromFilePath(m_catalog_path);
        scan_root(library_root);
        const string generated_root =
            World::GetGeneratedResourceDirectory();
        if (
            !generated_root.empty() &&
            normalized_path(generated_root) !=
            normalized_path(library_root)
        )
        {
            scan_root(generated_root);
        }
    }

    sort(
        m_assets.begin(),
        m_assets.end(),
        [this](
            const AssetEntry& first,
            const AssetEntry& second
        )
        {
            if (m_sort_mode == 1)
            {
                const float first_quality = first.quality_score;
                const float second_quality = second.quality_score;
                if (first_quality != second_quality)
                {
                    return first_quality > second_quality;
                }
            }
            else if (
                m_sort_mode == 2 &&
                first.type != second.type
            )
            {
                return first.type < second.type;
            }
            return lower_copy(first.name) < lower_copy(second.name);
        }
    );

    for (size_t index = 0; index < m_assets.size(); index++)
    {
        if (m_assets[index].id == previous_id)
        {
            m_selected_asset = static_cast<int>(index);
            break;
        }
    }

    m_status =
        to_string(m_assets.size()) +
        (
            m_assets.size() == 1 ?
            " asset" :
            " assets"
        );
    if (!catalog_error.empty())
    {
        m_status += ", " + catalog_error;
    }
    if (
        reload_preview &&
        m_selected_asset >= 0
    )
    {
        LoadSelectedAsset(false, true);
    }
}

void AssetViewer::ClearLoadedAsset()
{
    m_revision_previewing = false;
    m_revision.candidate_previewed = false;
    DestroyPreviewScene();
    m_mesh.reset();
    m_working_meshes.clear();
    m_preview_meshes.clear();
    m_preview_meshes_sources.clear();
    m_material.reset();
    m_texture.reset();
    m_loaded_path.clear();
    m_loaded_write_time.clear();
    m_prefab_entity_count = 0;
    m_bake_confirmation_open = false;
    m_working_sub_meshes.clear();
    m_working_lods.clear();
    m_working_vertices.clear();
    m_working_indices.clear();
    m_missing_dependencies.clear();
    m_working_modified = false;
    m_working_editable = false;
    m_working_lods_built = false;
    m_working_lods_attempted = false;
    m_working_lods_scanned = false;
    m_preview_lod = 0;
    m_target_ratio = 0.5f;
}

void AssetViewer::LoadSelectedAsset(
    const bool reset_view,
    const bool force_reload
)
{
    ClearLoadedAsset();
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size())
    )
    {
        return;
    }

    m_selected_dependency_path.clear();
    const AssetEntry& asset = m_assets[m_selected_asset];
    if (asset.path.empty())
    {
        m_status = "The selected asset has no path.";
        return;
    }
    if (!path_is_in_viewer_roots(asset.path))
    {
        m_status =
            "The selected asset is outside the library and mcp blockout folders.";
        return;
    }
    if (!FileSystem::Exists(asset.path))
    {
        m_status =
            "Asset file not found: " +
            asset.path;
        return;
    }

    if (asset.type == "mesh")
    {
        if (!AddWorkingMesh(asset.path, force_reload))
        {
            m_status =
                "Mesh could not be loaded: " +
                asset.path;
            return;
        }
        m_mesh = m_working_meshes.front().mesh;
        LoadWorkingGeometry();
    }
    else if (asset.type == "material")
    {
        m_material =
            ResourceCache::Load<Material>(asset.path);
        if (m_material && force_reload)
        {
            m_material->LoadFromFile(asset.path);
        }
        if (!m_material)
        {
            m_status =
                "Material could not be loaded: " +
                asset.path;
            return;
        }
    }
    else if (asset.type == "texture")
    {
        m_texture =
            ResourceCache::Load<RHI_Texture>(asset.path);
        if (!m_texture)
        {
            m_status =
                "Texture could not be loaded: " +
                asset.path;
            return;
        }
        // load only produces a cpu texture, the preview needs it on the gpu
        m_texture->PrepareForGpu();
        if (!m_texture->GetRhiResource())
        {
            m_texture.reset();
            m_status =
                "Texture could not be uploaded: " +
                asset.path;
            return;
        }
    }
    else
    {
        pugi::xml_document document;
        const pugi::xml_parse_result result =
            document.load_file(asset.path.c_str());
        const pugi::xml_node prefab =
            document.child("Prefab");
        if (!result || !prefab)
        {
            m_status =
                "Prefab could not be parsed: " +
                asset.path;
            return;
        }
        m_prefab_entity_count =
            1 +
            count_prefab_entities(prefab);
        CollectPrefabDependencies(asset.path);
        // the optimize tools see every mesh the prefab uses as one job, a chair is simplified as a
        // chair rather than one plank at a time
        CollectPrefabMeshes(asset.path, force_reload);
        LoadWorkingGeometry();
    }

    m_loaded_path = asset.path;
    m_loaded_write_time =
        FileSystem::GetLastWriteTime(m_loaded_path);
    if (reset_view)
    {
        m_preview_yaw = 0.65f;
        m_preview_pitch = 0.35f;
        m_preview_zoom = 1.0f;
        m_texture_pan = math::Vector2::Zero;
    }
    m_status =
        "Loaded renderer preview for " +
        asset_display_name(asset.name);
    if (!m_missing_dependencies.empty())
    {
        m_status =
            "Loaded " +
            asset_display_name(asset.name) +
            " but " +
            to_string(m_missing_dependencies.size()) +
            " referenced file(s) are missing on disk";
    }
    RebuildPreviewScene();
}

void AssetViewer::LoadDependencyPreview(
    const string& path
)
{
    ClearLoadedAsset();
    m_selected_asset = -1;
    m_selected_dependency_path = path;
    m_preview_zoom = 1.0f;
    m_texture_pan = math::Vector2::Zero;
    if (!FileSystem::Exists(path))
    {
        m_status =
            "Linked resource not found: " +
            path;
        return;
    }

    const string type = asset_type_from_path(path);
    if (type == "mesh")
    {
        if (!AddWorkingMesh(path, false))
        {
            m_status =
                "Linked mesh could not be loaded: " +
                path;
            return;
        }
        m_mesh = m_working_meshes.front().mesh;
        LoadWorkingGeometry();
    }
    else if (type == "material")
    {
        m_material =
            ResourceCache::Load<Material>(path);
        if (!m_material)
        {
            m_status =
                "Linked material could not be loaded: " +
                path;
            return;
        }
    }
    else if (type == "texture")
    {
        m_texture =
            ResourceCache::Load<RHI_Texture>(path);
        if (!m_texture)
        {
            m_status =
                "Linked texture could not be loaded: " +
                path;
            return;
        }
        m_texture->PrepareForGpu();
        if (!m_texture->GetRhiResource())
        {
            m_texture.reset();
            m_status =
                "Linked texture could not be uploaded: " +
                path;
            return;
        }
    }
    else
    {
        m_status =
            "Linked resource type is not previewable: " +
            path;
        return;
    }

    m_loaded_path = path;
    m_loaded_write_time =
        FileSystem::GetLastWriteTime(path);
    m_status =
        "Previewing linked " +
        type +
        " " +
        FileSystem::GetFileNameFromFilePath(path);
    RebuildPreviewScene();
}

void AssetViewer::CollectPrefabDependencies(const string& path)
{
    m_missing_dependencies.clear();

    pugi::xml_document document;
    if (!document.load_file(path.c_str()))
    {
        return;
    }

    vector<pugi::xml_node> pending =
    {
        document.document_element()
    };
    while (!pending.empty())
    {
        const pugi::xml_node node = pending.back();
        pending.pop_back();
        for (
            pugi::xml_node child = node.first_child();
            child;
            child = child.next_sibling()
        )
        {
            pending.push_back(child);
        }

        for (
            const char* attribute_name :
            {
                "mesh_path",
                "material_path",
                "texture_path"
            }
        )
        {
            const string reference =
                node.attribute(attribute_name).as_string();
            if (reference.empty() || FileSystem::Exists(reference))
            {
                continue;
            }

            const string missing =
                FileSystem::GetFileNameFromFilePath(reference);
            if (
                find(
                    m_missing_dependencies.begin(),
                    m_missing_dependencies.end(),
                    missing
                ) == m_missing_dependencies.end()
            )
            {
                m_missing_dependencies.push_back(missing);
            }
        }
    }
}

bool AssetViewer::AddWorkingMesh(
    const string& path,
    const bool force_reload
)
{
    for (const WorkingMesh& working : m_working_meshes)
    {
        if (normalized_path(working.path) == normalized_path(path))
        {
            return true;
        }
    }
    if (!FileSystem::Exists(path))
    {
        return false;
    }

    shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(path);
    if (mesh && force_reload)
    {
        mesh->LoadFromFile(path);
    }
    if (!mesh || mesh->GetVertexCount() == 0)
    {
        return false;
    }

    WorkingMesh working;
    working.mesh = move(mesh);
    working.path = path;
    m_working_meshes.emplace_back(move(working));
    return true;
}

void AssetViewer::CollectPrefabMeshes(
    const string& path,
    const bool force_reload
)
{
    // in the order the prefab references them so the sub mesh list reads top to bottom like the
    // hierarchy does, a set would shuffle a chair's legs above its seat
    const vector<string> mesh_paths =
        collect_xml_references(path, "mesh_path");
    for (const string& mesh_path : mesh_paths)
    {
        if (!path_is_in_viewer_roots(mesh_path))
        {
            continue;
        }
        AddWorkingMesh(mesh_path, force_reload);
    }
}

void AssetViewer::CollectPreviewRenderSlots(Entity* root)
{
    m_preview_render_slots.clear();
    if (!root)
    {
        return;
    }

    vector<Entity*> entities;
    entities.push_back(root);
    root->GetDescendants(&entities);
    for (Entity* entity : entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        if (!render || !render->GetMesh())
        {
            continue;
        }

        for (
            uint32_t mesh_index = 0;
            mesh_index < static_cast<uint32_t>(m_working_meshes.size());
            mesh_index++
        )
        {
            if (
                m_working_meshes[mesh_index].mesh.get() !=
                render->GetMesh()
            )
            {
                continue;
            }

            PreviewRenderSlot slot;
            slot.entity_id = entity->GetObjectId();
            slot.mesh_index = mesh_index;
            slot.sub_mesh = render->GetSubMeshIndex();
            m_preview_render_slots.push_back(slot);
            break;
        }
    }
}

bool AssetViewer::UpdateCatalogAsset(
    const string& asset_id,
    const function<void(JsonValue&)>& edit
)
{
    if (m_catalog_path.empty())
    {
        m_status = "No asset catalog to write to.";
        return false;
    }

    const string catalog_write_time =
        FileSystem::GetLastWriteTime(m_catalog_path);
    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_status = "The asset catalog could not be read.";
        return false;
    }

    JsonValue root;
    string parse_error;
    if (!mcp_json::parse(source, root, parse_error))
    {
        m_status = "The asset catalog is invalid: " + parse_error;
        return false;
    }

    JsonValue* assets = json_object_find(root, "assets");
    if (
        root.type != mcp_json::kind::object ||
        !assets ||
        assets->type != mcp_json::kind::object
    )
    {
        m_status = "The asset catalog has no assets object.";
        return false;
    }

    JsonValue* asset_value = json_object_find(*assets, asset_id);
    if (!asset_value)
    {
        m_status = "The asset is no longer in the catalog.";
        return false;
    }
    edit(*asset_value);

    // written beside the catalog and swapped in, a crash mid write must not leave half a catalog
    const string temporary_path = m_catalog_path + ".update.tmp";
    const string backup_path = m_catalog_path + ".update.backup";
    FileSystem::Delete(temporary_path);
    FileSystem::Delete(backup_path);
    if (
        !FileSystem::WriteFile(
            temporary_path,
            serialize_json(root) + "\n"
        )
    )
    {
        m_status = "The updated asset catalog could not be written.";
        return false;
    }
    if (
        FileSystem::GetLastWriteTime(m_catalog_path) !=
        catalog_write_time
    )
    {
        FileSystem::Delete(temporary_path);
        m_status = "The catalog changed during the update, try again.";
        return false;
    }

    error_code error;
    filesystem::rename(m_catalog_path, backup_path, error);
    if (error)
    {
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be backed up.";
        return false;
    }
    filesystem::rename(temporary_path, m_catalog_path, error);
    if (error)
    {
        filesystem::rename(backup_path, m_catalog_path, error);
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be replaced.";
        return false;
    }
    FileSystem::Delete(backup_path);
    return true;
}

Entity* AssetViewer::PreviewRoot() const
{
    if (m_preview_root_id == 0)
    {
        return nullptr;
    }

    return World::GetEntityById(m_preview_root_id);
}

void AssetViewer::CollectPreviewEntities(
    vector<Entity*>& entities
) const
{
    Entity* root = PreviewRoot();
    if (!root)
    {
        return;
    }

    entities.push_back(root);
    root->GetDescendants(&entities);
}

void AssetViewer::DestroyPreviewScene()
{
    Renderer::InvalidateSecondaryView();
    m_preview_render_slots.clear();
    if (
        !m_preview_root_owned &&
        m_preview_rig_id != 0
    )
    {
        if (
            Entity* rig =
                World::GetEntityById(m_preview_rig_id)
        )
        {
            World::RemoveEntityImmediate(rig);
        }
    }
    if (Entity* root = PreviewRoot())
    {
        if (m_preview_root_owned)
        {
            World::RemoveEntityImmediate(root);
        }
    }
    m_preview_root_id = 0;
    m_preview_rig_id = 0;
    m_preview_camera_id = 0;
    m_preview_root_owned = false;
    m_preview_dirty = false;
    m_preview_settle_frames = 0;
    m_preview_signature = 0;
    m_preview_orbiting = false;
}

void AssetViewer::RebuildPreviewScene()
{
    DestroyPreviewScene();
    if (m_loaded_path.empty() || m_texture)
    {
        return;
    }

    Entity* root = World::CreateEntity();
    if (!root)
    {
        return;
    }

    m_preview_root_id = root->GetObjectId();
    m_preview_root_owned = true;
    root->SetObjectName(
        "asset_viewer_preview"
    );
    root->SetTransient(true);

    if (m_mesh)
    {
        // always built against the source so the sub mesh count and the material slots are the real
        // ones, the scratch meshes are swapped in afterwards
        Mesh* mesh = m_mesh.get();

        // a mesh file holds one sub mesh per material slot, a single render
        // component only ever draws the first one
        const uint32_t sub_mesh_count =
            max(1u, mesh->GetSubMeshCount());
        if (sub_mesh_count == 1)
        {
            Render* render = root->AddComponent<Render>();
            render->SetMesh(mesh, 0);
            render->SetDefaultMaterial();
        }
        else
        {
            for (uint32_t i = 0; i < sub_mesh_count; i++)
            {
                Entity* part = World::CreateEntity();
                part->SetObjectName(
                    "asset_viewer_sub_mesh_" + to_string(i)
                );
                part->SetTransient(true);
                part->SetParent(root);
                Render* render = part->AddComponent<Render>();
                render->SetMesh(mesh, i);
                render->SetDefaultMaterial();
            }
        }

        // the scratch meshes hold the edit in progress, the source is only the layout donor
        CollectPreviewRenderSlots(root);
        RefreshPreviewMeshGeometry();
    }
    else if (m_material)
    {
        Render* render = root->AddComponent<Render>();
        render->SetMesh(MeshType::Sphere);
        render->SetMaterial(m_material);
    }
    else if (
        !Prefab::LoadFromFile(
            m_loaded_path,
            root
        )
    )
    {
        m_status =
            "Prefab could not be loaded into the preview.";
        DestroyPreviewScene();
        return;
    }
    else
    {
        // the prefab's own parts are what the optimize tools preview through, so an edit to a leg
        // shows on the leg rather than on a detached copy of the mesh
        CollectPreviewRenderSlots(root);
        RefreshPreviewMeshGeometry();
    }

    root->SetPosition(math::Vector3::Zero);
    CreatePreviewRig(root);
    root->SetActive(false);
    m_preview_dirty = true;
}

void AssetViewer::CreatePreviewRig(Entity* root)
{
    if (!root)
    {
        return;
    }

    root->SetActive(true);
    RefreshPreviewBounds(root);

    Entity* rig_entity = World::CreateEntity();
    rig_entity->SetObjectName("asset_viewer_rig");
    rig_entity->SetTransient(true);
    rig_entity->SetParent(root);
    m_preview_rig_id = rig_entity->GetObjectId();

    Entity* camera_entity = World::CreateEntity();
    camera_entity->SetObjectName("asset_viewer_camera");
    camera_entity->SetTransient(true);
    camera_entity->SetParent(rig_entity);
    Camera* camera = camera_entity->AddComponent<Camera>();
    camera->SetFovHorizontalDeg(50.0f);
    camera->SetExposureMode(
        CameraExposureMode::automatic
    );
    m_preview_camera_id = camera_entity->GetObjectId();

    Entity* key_entity = World::CreateEntity();
    key_entity->SetObjectName("asset_viewer_key_light");
    key_entity->SetTransient(true);
    key_entity->SetParent(rig_entity);
    key_entity->SetRotation(
        math::Quaternion::FromLookRotation(
            math::Vector3(-0.45f, -0.8f, 0.65f),
            math::Vector3::Up
        )
    );
    Light* key_light = key_entity->AddComponent<Light>();
    key_light->SetLightType(LightType::Directional);
    key_light->SetIntensity(85000.0f);
    key_light->SetColor(Color::standard_white);

    Entity* fill_entity = World::CreateEntity();
    fill_entity->SetObjectName("asset_viewer_fill_light");
    fill_entity->SetTransient(true);
    fill_entity->SetParent(rig_entity);
    fill_entity->SetPosition(
        m_preview_center +
        math::Vector3(
            m_preview_radius * 2.0f,
            m_preview_radius * 1.2f,
            -m_preview_radius * 1.5f
        )
    );
    Light* fill_light = fill_entity->AddComponent<Light>();
    fill_light->SetLightType(LightType::Point);
    fill_light->SetIntensity(5000.0f);
    fill_light->SetRange(m_preview_radius * 8.0f);
    fill_light->SetColor(Color::standard_white);

    UpdatePreviewCamera();

    // the renderer walks the live entity lists, a freshly created entity only lands there on the next
    // world tick, so without this drain the first preview frame draws whichever parts happened to be
    // committed already and the rig lights are not in it at all
    World::ProcessPendingAdditions();
    m_preview_settle_frames = 3;
}

// an asset being authored gains parts while it is being previewed, so the framing cannot be decided once
// when the preview opens, a chair that grows a backrest after the camera was fitted to its base would put
// the backrest off screen with nothing to bring it back
void AssetViewer::RefreshPreviewBounds(Entity* root)
{
    // the rig is built before the world commits the entities it just created, so a lookup by id would come
    // back empty there, the caller that already holds the root passes it
    vector<Entity*> render_entities;
    if (root)
    {
        render_entities.push_back(root);
        root->GetDescendants(&render_entities);
    }
    else
    {
        CollectPreviewEntities(render_entities);
    }
    bool bounds_found = false;
    math::BoundingBox bounds;
    for (Entity* entity : render_entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        if (!render || !render->GetMesh())
        {
            continue;
        }
        const math::BoundingBox render_bounds =
            render->GetBoundingBoxMesh() *
            entity->GetMatrix();
        if (!bounds_found)
        {
            bounds = render_bounds;
            bounds_found = true;
        }
        else
        {
            bounds.Merge(render_bounds);
        }
    }

    m_preview_center = bounds_found
        ? bounds.GetCenter()
        : math::Vector3::Zero;
    if (bounds_found)
    {
        // half the diagonal is the enclosing sphere, the longest axis alone
        // under estimates it and crops elongated assets
        const math::Vector3 size = bounds.GetSize();
        m_preview_radius = max(
            0.001f,
            size.Length() * 0.5f
        );
    }
    else
    {
        m_preview_radius = 1.0f;
    }
}

void AssetViewer::UpdatePreviewCamera()
{
    Entity* camera_entity =
        World::GetEntityById(m_preview_camera_id);
    if (!camera_entity)
    {
        return;
    }

    const math::Vector3 direction(
        cos(m_preview_pitch) * sin(m_preview_yaw),
        sin(m_preview_pitch),
        -cos(m_preview_pitch) * cos(m_preview_yaw)
    );

    Camera* camera = camera_entity->GetComponent<Camera>();
    const float fov_horizontal =
        camera
            ? camera->GetFovHorizontalRad()
            : 50.0f * math::deg_to_rad;
    const float aspect = max(0.05f, m_preview_aspect);
    const float fov_vertical =
        2.0f *
        atan(
            tan(fov_horizontal * 0.5f) /
            aspect
        );

    // fit the bounding sphere against the tighter frustum axis, otherwise a
    // wide or tall panel crops the asset
    const float fov_fit = min(fov_horizontal, fov_vertical);
    float distance =
        m_preview_radius *
        1.15f /
        max(0.01f, sin(fov_fit * 0.5f));
    distance /= max(0.05f, m_preview_zoom);

    // the near plane is fixed engine side, keep small assets in front of it
    const float near_plane =
        camera ? camera->GetNearPlane() : 0.1f;
    distance = max(
        distance,
        near_plane + m_preview_radius * 1.05f
    );

    const math::Vector3 position =
        m_preview_center + direction * distance;
    camera_entity->SetPosition(position);
    camera_entity->SetRotation(
        math::Quaternion::FromLookRotation(
            (m_preview_center - position).Normalized(),
            math::Vector3::Up
        )
    );
}

bool AssetViewer::RequestPreviewRender(
    const uint32_t width,
    const uint32_t height
)
{
    Entity* root = PreviewRoot();
    Entity* camera =
        World::GetEntityById(m_preview_camera_id);
    if (!root || !camera)
    {
        return false;
    }

    m_preview_aspect =
        static_cast<float>(width) /
        static_cast<float>(max(1u, height));
    UpdatePreviewCamera();
    const Renderer_SecondaryViewMode mode =
        m_preview_mode == 1
            ? Renderer_SecondaryViewMode::Wireframe
            : m_preview_mode == 2
                ? Renderer_SecondaryViewMode::Vertices
                : Renderer_SecondaryViewMode::Solid;
    if (
        Renderer::RequestSecondaryView(
            camera,
            root,
            width,
            height,
            mode,
            ResolvePreviewBackdrop()
        )
    )
    {
        m_preview_dirty = false;
        m_next_preview_request =
            chrono::steady_clock::now() +
            chrono::milliseconds(16);
        return true;
    }
    return false;
}

Renderer_SecondaryViewBackdrop
AssetViewer::ResolvePreviewBackdrop() const
{
    // auto keeps the sky for solid shading since it is the nicest looking backdrop, then swaps to
    // charcoal for wire and vertex modes where a bright sky swallows the wires whole
    if (m_preview_backdrop == 0)
    {
        return m_preview_mode == 0
            ? Renderer_SecondaryViewBackdrop::Sky
            : Renderer_SecondaryViewBackdrop::Charcoal;
    }

    switch (m_preview_backdrop)
    {
        case 1:
            return Renderer_SecondaryViewBackdrop::Sky;
        case 2:
            return Renderer_SecondaryViewBackdrop::Charcoal;
        case 3:
            return Renderer_SecondaryViewBackdrop::Slate;
        default:
            return Renderer_SecondaryViewBackdrop::Paper;
    }
}

void AssetViewer::PreviewEntity(Entity* entity)
{
    if (!entity)
    {
        return;
    }

    DestroyPreviewScene();
    m_mesh.reset();
    m_working_meshes.clear();
    m_working_sub_meshes.clear();
    m_working_lods.clear();
    m_working_vertices.clear();
    m_working_indices.clear();
    m_working_editable = false;
    m_working_modified = false;
    m_preview_meshes.clear();
    m_preview_meshes_sources.clear();
    m_material.reset();
    m_texture.reset();
    m_loaded_path.clear();
    m_loaded_write_time.clear();
    m_selected_asset = -1;
    m_preview_root_id = entity->GetObjectId();
    m_preview_root_owned = false;
    entity->SetTransient(true);
    entity->SetActive(false);
    if (!entity->GetParent())
    {
        entity->SetPosition(
            math::Vector3::Zero
        );
    }
    CreatePreviewRig(entity);
    entity->SetActive(false);
    m_visible = true;
    m_preview_yaw = 0.65f;
    m_preview_pitch = 0.35f;
    m_preview_zoom = 1.0f;
    m_texture_pan = math::Vector2::Zero;
    m_status =
        "Previewing focused asset workspace";
    m_preview_dirty = true;
}

pair<uint64_t, uint64_t>
AssetViewer::GetPreviewGeometryCounts() const
{
    // the working copy is lod 0 of exactly what is being edited, the mesh totals below include every
    // lod level and count a mesh once per part that draws it
    if (
        !m_working_vertices.empty() &&
        m_working_indices.size() >= 3
    )
    {
        return
        {
            static_cast<uint64_t>(
                m_working_vertices.size()
            ),
            static_cast<uint64_t>(
                m_working_indices.size()
            )
        };
    }
    if (m_mesh)
    {
        return
        {
            static_cast<uint64_t>(
                m_mesh->GetVertices().size()
            ),
            static_cast<uint64_t>(
                m_mesh->GetIndices().size()
            )
        };
    }
    if (m_material)
    {
        return { 0, 0 };
    }

    vector<Entity*> entities;
    CollectPreviewEntities(entities);
    uint64_t vertex_count = 0;
    uint64_t index_count = 0;
    for (Entity* entity : entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        Mesh* mesh =
            render ?
            render->GetMesh() :
            nullptr;
        if (!mesh)
        {
            continue;
        }
        vertex_count += static_cast<uint64_t>(
            render->GetVertexCount()
        );
        index_count += static_cast<uint64_t>(
            render->GetIndexCount()
        );
    }
    return
    {
        vertex_count,
        index_count
    };
}

uint64_t AssetViewer::PreviewSceneSignature() const
{
    if (m_texture)
    {
        return 0;
    }

    vector<Entity*> entities;
    CollectPreviewEntities(entities);
    if (entities.empty())
    {
        return 0;
    }

    uint64_t signature = 1469598103934665603ull;
    const auto mix =
        [&signature](const uint64_t value)
        {
            signature =
                (signature ^ value) * 1099511628211ull;
        };

    const auto mix_float =
        [&mix](const float value)
        {
            // a raw float bit pattern, so a zero and a negative zero do not read as different values
            mix(
                static_cast<uint64_t>(
                    std::hash<float>{}(value == 0.0f ? 0.0f : value)
                )
            );
        };

    mix(entities.size());
    for (Entity* entity : entities)
    {
        if (!entity)
        {
            continue;
        }

        Render* render = entity->GetComponent<Render>();
        if (!render)
        {
            continue;
        }

        // a draw is dropped when either the mesh or the material is still missing, so both have to be
        // part of the fingerprint, the glass of a bottle goes missing exactly this way
        Mesh* mesh         = render->GetMesh();
        Material* material = render->GetMaterial();
        mix(entity->GetObjectId());
        mix(reinterpret_cast<uint64_t>(mesh));
        mix(reinterpret_cast<uint64_t>(material));
        if (mesh)
        {
            mix(mesh->GetIndices().size());
            mix(mesh->GetVertices().size());
        }

        // a part being moved, turned or resized changes nothing about which resources are bound, so the
        // pointers alone cannot see it, the placement has to be in the fingerprint too. the local
        // matrix, not the world one, so orbiting the camera rig does not read as the asset changing
        const math::Matrix& transform = entity->GetLocalMatrix();
        for (uint32_t element = 0; element < 16; element++)
        {
            mix_float(transform.Data()[element]);
        }

        // a material is usually retuned in place rather than replaced, so its colour, roughness, metal
        // and texture bindings move without the pointer ever changing, this is what makes a recolour
        // or a newly finished texture upload show up on its own
        if (material)
        {
            mix(static_cast<uint64_t>(material->GetResourceState()));
            for (const float property : material->GetProperties())
            {
                mix_float(property);
            }
            for (const RHI_Texture* texture : material->GetTextures())
            {
                mix(reinterpret_cast<uint64_t>(texture));
                if (texture)
                {
                    mix(static_cast<uint64_t>(texture->GetResourceState()));
                }
            }
        }
    }
    return signature;
}

void AssetViewer::DrawLibraryToolbar(const float width)
{
    const float scale = ui_scale();
    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameRounding,
        5.0f * scale
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(9.0f * scale, 5.0f * scale)
    );

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SetNextItemShortcut(
        ImGuiMod_Ctrl | ImGuiKey_F,
        ImGuiInputFlags_Tooltip
    );
    ImGui::InputTextWithHint(
        "##asset_viewer_search",
        "Search assets, tags and aliases",
        m_search.data(),
        m_search.size(),
        ImGuiInputTextFlags_EscapeClearsAll
    );

    const char* filters =
        "All types\0"
        "Meshes\0"
        "Materials\0"
        "Prefabs\0"
        "Textures\0";
    const float action_width = 28.0f * scale;
    const float gap = 4.0f * scale;
    const float combo_width = max(
        76.0f * scale,
        (
            width -
            action_width -
            gap * 4.0f -
            16.0f * scale
        ) *
        0.52f
    );
    ImGui::SetNextItemWidth(combo_width);
    ImGui::Combo(
        "##asset_type_filter",
        &m_type_filter,
        filters
    );
    ImGuiSp::tooltip("Filter by asset type");
    ImGui::SameLine(0.0f, gap);

    const char* sort_labels[] =
    {
        "Name",
        "Quality",
        "Type"
    };
    ImGui::SetNextItemWidth(
        max(
            70.0f * scale,
            width -
                combo_width -
                action_width -
                gap * 4.0f -
                16.0f * scale
        )
    );
    if (
        ImGui::Combo(
            "##asset_sort",
            &m_sort_mode,
            sort_labels,
            IM_ARRAYSIZE(sort_labels)
        )
    )
    {
        const string selected_id =
            (
                m_selected_asset >= 0 &&
                m_selected_asset < static_cast<int>(m_assets.size())
            )
                ? m_assets[m_selected_asset].id
                : "";
        sort(
            m_assets.begin(),
            m_assets.end(),
            [this](
                const AssetEntry& first,
                const AssetEntry& second
            )
            {
                if (m_sort_mode == 1)
                {
                    const float first_quality =
                        first.quality_score;
                    const float second_quality =
                        second.quality_score;
                    if (first_quality != second_quality)
                    {
                        return first_quality > second_quality;
                    }
                }
                else if (
                    m_sort_mode == 2 &&
                    first.type != second.type
                )
                {
                    return first.type < second.type;
                }
                return lower_copy(first.name) <
                    lower_copy(second.name);
            }
        );
        m_selected_asset = -1;
        for (
            int index = 0;
            index < static_cast<int>(m_assets.size());
            index++
        )
        {
            if (m_assets[index].id == selected_id)
            {
                m_selected_asset = index;
                break;
            }
        }
    }
    ImGuiSp::tooltip("Sort asset library");

    ImGui::SameLine(0.0f, gap);
    if (
        ImGuiSp::image_button(
            IconType::Refresh,
            math::Vector2(17.0f * scale, 17.0f * scale),
            false
        )
    )
    {
        RefreshCatalog(true);
    }
    ImGuiSp::tooltip("Refresh catalog");

    if (ImGui::Button("Library actions", ImVec2(-1.0f, 0.0f)))
    {
        ImGui::OpenPopup("##asset_library_actions");
    }
    if (ImGui::BeginPopup("##asset_library_actions"))
    {
        if (ImGui::MenuItem("Clean up library"))
        {
            ScanLibraryCleanup();
        }
        ImGui::Separator();
        ImGui::TextDisabled(
            "Removes files not referenced by the catalog or worlds"
        );
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

void AssetViewer::DrawSelectionBar()
{
    if (m_selected_assets.size() <= 1)
    {
        return;
    }

    const float scale = ui_scale();
    const ImVec4 accent = ImGui::Style::color_accent_1;
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImVec4(accent.x, accent.y, accent.z, 0.1f)
    );
    ImGui::BeginChild(
        "##asset_selection_actions",
        ImVec2(0.0f, 42.0f * scale),
        ImGuiChildFlags_Borders
    );
    ImGui::SetCursorPos(
        ImVec2(8.0f * scale, 8.0f * scale)
    );
    ImGui::Text(
        "%zu selected",
        m_selected_assets.size()
    );

    const bool has_focus =
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size()) &&
        m_selected_assets.find(
            m_assets[m_selected_asset].id
        ) != m_selected_assets.end();
    const float actions_width = 112.0f * scale;
    const float right =
        ImGui::GetCursorPosX() +
        ImGui::GetContentRegionAvail().x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(
        max(
            ImGui::GetCursorPosX(),
            right - actions_width
        )
    );
    if (!has_focus)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::SmallButton("Focus only"))
    {
        m_selected_assets.clear();
        m_selected_assets.insert(
            m_assets[m_selected_asset].id
        );
        m_status = "Focused selection only";
    }
    if (!has_focus)
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete"))
    {
        m_pending_delete_selection = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void AssetViewer::DrawStatusBar()
{
    ImGui::Separator();
    const float scale = ui_scale();
    int visible_count = 0;
    for (const AssetEntry& asset : m_assets)
    {
        if (AssetMatchesFilter(asset))
        {
            visible_count++;
        }
    }
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() +
        2.0f * scale
    );
    ImGui::TextDisabled(
        "%d / %zu assets",
        visible_count,
        m_assets.size()
    );
    if (!m_catalog_path.empty())
    {
        ImGuiSp::tooltip(m_catalog_path.c_str());
    }
    if (m_selected_assets.size() > 1)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(
            "|  %zu selected",
            m_selected_assets.size()
        );
    }
    if (!m_status.empty())
    {
        const string status_lower = lower_copy(m_status);
        ImVec4 color = ImGui::Style::color_info;
        if (
            status_lower.find("fail") != string::npos ||
            status_lower.find("error") != string::npos ||
            status_lower.find("unsafe") != string::npos ||
            status_lower.find("not found") != string::npos ||
            status_lower.find("could not") != string::npos
        )
        {
            color = ImGui::Style::color_error;
        }
        else if (
            status_lower.find("missing") != string::npos ||
            status_lower.find("warning") != string::npos
        )
        {
            color = ImGui::Style::color_warning;
        }

        const float status_width =
            ImGui::CalcTextSize(m_status.c_str()).x;
        const float right =
            ImGui::GetCursorPosX() +
            ImGui::GetContentRegionAvail().x;
        ImGui::SameLine();
        if (
            ImGui::GetCursorPosX() +
                status_width <
            right
        )
        {
            ImGui::SetCursorPosX(
                right - status_width
            );
        }
        ImGui::TextColored(
            color,
            "%s",
            m_status.c_str()
        );
    }
}

void AssetViewer::DrawAssetList(float width, float height)
{
    ImGui::BeginChild(
        "##asset_viewer_list",
        ImVec2(width, height),
        ImGuiChildFlags_Borders
    );

    const float scale = ui_scale();
    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(
        ImVec2(
            cursor.x + 5.0f * scale,
            cursor.y + 4.0f * scale
        )
    );
    ImGui::TextUnformatted("LIBRARY");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu assets", m_assets.size());
    ImGui::Spacing();
    DrawLibraryToolbar(width);
    ImGui::Spacing();
    vector<vector<int>> prefab_children(m_assets.size());
    vector<vector<string>> direct_dependencies(m_assets.size());
    vector<bool> nested_assets(m_assets.size(), false);
    for (
        int prefab_index = 0;
        prefab_index < static_cast<int>(m_assets.size());
        prefab_index++
    )
    {
        const AssetEntry& prefab = m_assets[prefab_index];
        if (prefab.type != "prefab")
        {
            continue;
        }

        if (prefab.path.empty())
        {
            continue;
        }
        for (const string& dependency : prefab.dependencies)
        {
            const string dependency_name = lower_copy(
                FileSystem::GetFileNameWithoutExtensionFromFilePath(
                    dependency
                )
            );
            int matched_index = -1;
            // path first, a mesh and its material share a stem so name matching alone
            // would fold two different assets into one child
            const string dependency_path =
                normalized_path(dependency);
            for (
                int index = 0;
                index < static_cast<int>(m_assets.size());
                index++
            )
            {
                if (
                    index == prefab_index ||
                    m_assets[index].type == "prefab"
                )
                {
                    continue;
                }
                if (
                    !m_assets[index].path.empty() &&
                    normalized_path(m_assets[index].path) ==
                        dependency_path
                )
                {
                    matched_index = index;
                    break;
                }
            }
            for (
                int index = 0;
                matched_index < 0 &&
                index < static_cast<int>(m_assets.size());
                index++
            )
            {
                if (
                    index == prefab_index ||
                    m_assets[index].type == "prefab"
                )
                {
                    continue;
                }
                const string child_file =
                    !m_assets[index].path.empty()
                    ? lower_copy(
                        FileSystem::
                            GetFileNameWithoutExtensionFromFilePath(
                                m_assets[index].path
                            )
                    )
                    : "";
                if (
                    dependency_name ==
                        lower_copy(m_assets[index].id) ||
                    dependency_name ==
                        lower_copy(m_assets[index].name) ||
                    dependency_name == child_file
                )
                {
                    matched_index = index;
                    break;
                }
            }

            if (matched_index >= 0)
            {
                if (
                    find(
                        prefab_children[prefab_index].begin(),
                        prefab_children[prefab_index].end(),
                        matched_index
                    ) == prefab_children[prefab_index].end()
                )
                {
                    prefab_children[prefab_index].push_back(
                        matched_index
                    );
                    nested_assets[matched_index] = true;
                }
            }
            else
            {
                direct_dependencies[prefab_index].push_back(
                    dependency
                );
            }
        }
    }

    const auto dependency_matches_filter =
        [this](const string& path)
        {
            const string type = asset_type_from_path(path);
            if (
                (m_type_filter == 1 && type != "mesh") ||
                (m_type_filter == 2 && type != "material") ||
                (m_type_filter == 3 && type != "prefab") ||
                (m_type_filter == 4 && type != "texture")
            )
            {
                return false;
            }

            const string query = lower_copy(m_search.data());
            if (
                query.find("packed") == string::npos &&
                is_packed_texture(path)
            )
            {
                return false;
            }
            if (query.empty())
            {
                return true;
            }
            const string searchable =
                lower_copy(path + " " + type);
            istringstream stream(query);
            string term;
            while (stream >> term)
            {
                if (searchable.find(term) == string::npos)
                {
                    return false;
                }
            }
            return true;
        };

    vector<int> visible_roots;
    visible_roots.reserve(m_assets.size());
    for (
        int index = 0;
        index < static_cast<int>(m_assets.size());
        index++
    )
    {
        const AssetEntry& asset = m_assets[index];
        if (asset.type != "prefab" && nested_assets[index])
        {
            continue;
        }

        bool visible = AssetMatchesFilter(asset);
        if (asset.type == "prefab")
        {
            for (const int child_index : prefab_children[index])
            {
                visible |= AssetMatchesFilter(
                    m_assets[child_index]
                );
            }
            for (
                const string& dependency :
                direct_dependencies[index]
            )
            {
                visible |= dependency_matches_filter(dependency);
            }
        }
        if (visible)
        {
            visible_roots.push_back(index);
        }
    }

    ImGui::TextDisabled(
        "%zu shown",
        visible_roots.size()
    );
    ImGui::Separator();
    DrawSelectionBar();

    // rebuilt every frame in draw order, select all and shift ranges both operate on what is on screen
    // rather than on m_assets, which is sorted differently and includes collapsed and filtered rows
    vector<int> drawn_rows;
    drawn_rows.reserve(visible_roots.size());

    const auto select_asset =
        [this, &drawn_rows](const int index)
        {
            ApplyRowSelection(index, drawn_rows);
        };
    const auto draw_asset_row =
        [this, scale, &select_asset, &drawn_rows](
            const int index,
            const bool nested,
            const int child_count
        )
        {
            drawn_rows.push_back(index);
            const AssetEntry& asset = m_assets[index];
            const float row_height =
                (nested ? 44.0f : 58.0f) * scale;
            const bool has_children = child_count > 0;

            const bool renaming = m_rename_asset_id == asset.id;

            ImGui::PushID(asset.id.c_str());
            const ImVec2 row_start =
                ImGui::GetCursorScreenPos();
            const float row_width =
                max(
                    1.0f,
                    ImGui::GetContentRegionAvail().x
                );
            ImGui::InvisibleButton(
                "##asset_row",
                ImVec2(row_width, row_height),
                ImGuiButtonFlags_MouseButtonLeft |
                ImGuiButtonFlags_MouseButtonRight
            );
            const ImVec2 row_end =
                ImGui::GetItemRectMax();
            const ImVec2 cursor_below_row =
                ImGui::GetCursorScreenPos();
            // the item queries are cached before the context menu, a popup leaves its own last
            // item behind so anything asked afterwards would be about the popup's contents
            const bool row_hovered = ImGui::IsItemHovered();
            const bool row_clicked = ImGui::IsItemClicked();
            const bool row_double_clicked =
                row_hovered &&
                ImGui::IsMouseDoubleClicked(
                    ImGuiMouseButton_Left
                );
            DrawAssetContextMenu(index);
            const float chevron_width =
                has_children
                    ? 24.0f * scale
                    : 0.0f;
            const ImVec2 mouse =
                ImGui::GetIO().MousePos;
            const bool chevron_hovered =
                has_children &&
                mouse.x >= row_start.x &&
                mouse.x <=
                    row_start.x + chevron_width &&
                mouse.y >= row_start.y &&
                mouse.y <= row_end.y;
            if (row_clicked && !renaming)
            {
                if (chevron_hovered)
                {
                    if (
                        m_expanded_assets.find(asset.id) !=
                        m_expanded_assets.end()
                    )
                    {
                        m_expanded_assets.erase(asset.id);
                    }
                    else
                    {
                        m_expanded_assets.insert(asset.id);
                    }
                }
                else
                {
                    select_asset(index);
                }
            }
            if (
                !renaming &&
                has_children &&
                !chevron_hovered &&
                row_double_clicked
            )
            {
                if (
                    m_expanded_assets.find(asset.id) !=
                    m_expanded_assets.end()
                )
                {
                    m_expanded_assets.erase(asset.id);
                }
                else
                {
                    m_expanded_assets.insert(asset.id);
                }
            }
            const bool open =
                has_children &&
                m_expanded_assets.find(asset.id) !=
                    m_expanded_assets.end();

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const bool row_selected = IsAssetSelected(index);
            if (
                row_selected ||
                row_hovered
            )
            {
                draw_list->AddRectFilled(
                    row_start,
                    row_end,
                    ImGui::GetColorU32(
                        row_selected
                            ? ImGuiCol_Header
                            : ImGuiCol_HeaderHovered
                    ),
                    4.0f * scale
                );
            }
            // the row the inspector is showing gets an edge, otherwise a selection of forty rows gives no
            // clue which one the panel on the right is about
            if (row_selected && m_selected_asset == index)
            {
                draw_list->AddRect(
                    row_start,
                    row_end,
                    ImGui::GetColorU32(ImGuiCol_NavCursor),
                    4.0f * scale
                );
            }
            if (has_children)
            {
                const ImVec2 center(
                    row_start.x + 11.0f * scale,
                    row_start.y + row_height * 0.5f
                );
                const float arrow_size = 4.0f * scale;
                if (open)
                {
                    draw_list->AddTriangleFilled(
                        ImVec2(
                            center.x - arrow_size,
                            center.y - arrow_size * 0.5f
                        ),
                        ImVec2(
                            center.x + arrow_size,
                            center.y - arrow_size * 0.5f
                        ),
                        ImVec2(
                            center.x,
                            center.y + arrow_size
                        ),
                        ImGui::GetColorU32(ImGuiCol_Text)
                    );
                }
                else
                {
                    draw_list->AddTriangleFilled(
                        ImVec2(
                            center.x - arrow_size * 0.5f,
                            center.y - arrow_size
                        ),
                        ImVec2(
                            center.x - arrow_size * 0.5f,
                            center.y + arrow_size
                        ),
                        ImVec2(
                            center.x + arrow_size,
                            center.y
                        ),
                        ImGui::GetColorU32(ImGuiCol_Text)
                    );
                }
            }
            if (!nested)
            {
                draw_list->AddRectFilled(
                    row_start,
                    ImVec2(
                        row_start.x + 3.0f * scale,
                        row_end.y
                    ),
                    asset_type_color(asset.type),
                    2.0f * scale
                );
            }

            const float badge_size =
                (nested ? 25.0f : 31.0f) * scale;
            const float badge_offset = has_children
                ? 29.0f * scale
                : 10.0f * scale;
            const ImVec2 badge_min(
                row_start.x + badge_offset,
                row_start.y +
                    (row_height - badge_size) * 0.5f
            );
            const ImVec2 badge_max(
                badge_min.x + badge_size,
                badge_min.y + badge_size
            );
            draw_list->AddRectFilled(
                badge_min,
                badge_max,
                asset_type_color(asset.type, 38),
                6.0f * scale
            );
            const char* badge = asset.type == "mesh"
                ? "M"
                : asset.type == "material"
                    ? "MAT"
                    : asset.type == "texture"
                        ? "TEX"
                        : "P";
            const ImVec2 badge_text = ImGui::CalcTextSize(badge);
            draw_list->AddText(
                ImVec2(
                    badge_min.x +
                        (badge_size - badge_text.x) * 0.5f,
                    badge_min.y +
                        (badge_size - badge_text.y) * 0.5f
                ),
                asset_type_color(asset.type),
                badge
            );

            const float text_x =
                badge_max.x + 9.0f * scale;
            const string display_name =
                asset_display_name(asset.name);
            if (renaming)
            {
                // the input is submitted after the row button so it wins the mouse, then the
                // cursor goes back below the row and the layout carries on untouched
                ImGui::SetCursorScreenPos(
                    ImVec2(
                        text_x,
                        row_start.y +
                            (nested ? 2.0f : 6.0f) * scale
                    )
                );
                DrawAssetRenameInline(
                    index,
                    max(
                        60.0f * scale,
                        row_end.x - text_x - 8.0f * scale
                    )
                );
                finish_overlay_cursor(cursor_below_row);
            }
            else
            {
                draw_list->AddText(
                    ImVec2(
                        text_x,
                        row_start.y +
                            (nested ? 4.0f : 8.0f) * scale
                    ),
                    ImGui::GetColorU32(ImGuiCol_Text),
                    display_name.c_str()
                );
            }

            string metadata = asset.type;
            if (has_children)
            {
                metadata +=
                    "  " +
                    to_string(child_count) +
                    (
                        child_count == 1
                            ? " resource"
                            : " resources"
                    );
            }
            else if (asset.disk_only)
            {
                metadata += "  unregistered";
            }
            else if (!asset.path.empty())
            {
                char quality[32] = {};
                snprintf(
                    quality,
                    sizeof(quality),
                    "  q %.1f%s",
                    asset.quality_score,
                    asset.quality_verified
                        ? "  verified"
                        : ""
                );
                metadata += quality;
            }
            else
            {
                metadata += "  unavailable";
            }
            draw_list->AddText(
                ImVec2(
                    text_x,
                    row_start.y +
                        (nested ? 24.0f : 31.0f) * scale
                ),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                metadata.c_str()
            );
            if (!renaming && row_hovered)
            {
                ImGuiSp::tooltip(
                    (
                        display_name +
                        "\nCatalog id: " +
                        asset.id +
                        "\nRight click to rename or delete"
                    ).c_str()
                );
            }
            ImGui::PopID();
            return open;
        };

    const bool filtering =
        m_type_filter != 0 ||
        m_search[0] != '\0';
    for (const int index : visible_roots)
    {
        const AssetEntry& asset = m_assets[index];
        const bool parent_matches = AssetMatchesFilter(asset);
        int visible_child_count = 0;
        if (asset.type == "prefab")
        {
            for (const int child_index : prefab_children[index])
            {
                if (
                    !filtering ||
                    parent_matches ||
                    AssetMatchesFilter(m_assets[child_index])
                )
                {
                    visible_child_count++;
                }
            }
            for (
                const string& dependency :
                direct_dependencies[index]
            )
            {
                if (
                    !filtering ||
                    parent_matches ||
                    dependency_matches_filter(dependency)
                )
                {
                    visible_child_count++;
                }
            }
        }

        if (filtering && !parent_matches)
        {
            m_expanded_assets.insert(asset.id);
        }
        const bool open = draw_asset_row(
            index,
            false,
            visible_child_count
        );
        if (!open)
        {
            continue;
        }

        ImGui::Indent(18.0f * scale);
        for (const int child_index : prefab_children[index])
        {
            if (
                filtering &&
                !parent_matches &&
                !AssetMatchesFilter(m_assets[child_index])
            )
            {
                continue;
            }
            draw_asset_row(child_index, true, 0);
        }
        for (
            const string& dependency :
            direct_dependencies[index]
        )
        {
            if (
                filtering &&
                !parent_matches &&
                !dependency_matches_filter(dependency)
            )
            {
                continue;
            }

            const float dependency_height = 38.0f * scale;
            const bool renaming_dependency =
                m_rename_dependency_path == dependency;
            ImGui::PushID(dependency.c_str());
            ImGui::InvisibleButton(
                "##dependency_row",
                ImVec2(
                    max(
                        1.0f,
                        ImGui::GetContentRegionAvail().x
                    ),
                    dependency_height
                ),
                ImGuiButtonFlags_MouseButtonLeft |
                ImGuiButtonFlags_MouseButtonRight
            );
            const ImVec2 cursor_below_dependency =
                ImGui::GetCursorScreenPos();
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            const bool dependency_hovered = ImGui::IsItemHovered();
            const bool dependency_clicked = ImGui::IsItemClicked();
            DrawDependencyContextMenu(dependency);
            if (dependency_clicked && !renaming_dependency)
            {
                LoadDependencyPreview(dependency);
            }
            ImDrawList* draw_list =
                ImGui::GetWindowDrawList();
            if (
                m_selected_dependency_path == dependency ||
                dependency_hovered
            )
            {
                draw_list->AddRectFilled(
                    minimum,
                    maximum,
                    ImGui::GetColorU32(
                        m_selected_dependency_path == dependency
                            ? ImGuiCol_Header
                            : ImGuiCol_HeaderHovered
                    ),
                    4.0f * scale
                );
            }
            const string type =
                asset_type_from_path(dependency);
            const string name = asset_display_name(
                FileSystem::
                    GetFileNameWithoutExtensionFromFilePath(
                        dependency
                    )
            );
            draw_list->AddCircleFilled(
                ImVec2(
                    minimum.x + 12.0f * scale,
                    minimum.y + 18.0f * scale
                ),
                3.0f * scale,
                asset_type_color(type)
            );
            if (renaming_dependency)
            {
                ImGui::SetCursorScreenPos(
                    ImVec2(
                        minimum.x + 23.0f * scale,
                        minimum.y + 1.0f * scale
                    )
                );
                DrawDependencyRenameInline(
                    dependency,
                    max(
                        60.0f * scale,
                        maximum.x -
                        minimum.x -
                        31.0f * scale
                    )
                );
                finish_overlay_cursor(cursor_below_dependency);
            }
            else
            {
                draw_list->AddText(
                    ImVec2(
                        minimum.x + 23.0f * scale,
                        minimum.y + 2.0f * scale
                    ),
                    ImGui::GetColorU32(ImGuiCol_Text),
                    name.c_str()
                );
            }
            draw_list->AddText(
                ImVec2(
                    minimum.x + 23.0f * scale,
                    minimum.y + 20.0f * scale
                ),
                ImGui::GetColorU32(
                    ImGuiCol_TextDisabled
                ),
                (type + "  linked file").c_str()
            );
            if (!renaming_dependency && dependency_hovered)
            {
                ImGuiSp::tooltip(dependency.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Unindent(18.0f * scale);
    }

    if (visible_roots.empty())
    {
        const float offset =
            max(
                12.0f * scale,
                (height - 80.0f * scale) * 0.35f
            );
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() +
            offset
        );
        const char* message = m_assets.empty()
            ? "No generated assets yet"
            : "No assets match these filters";
        const float message_width =
            ImGui::CalcTextSize(message).x;
        ImGui::SetCursorPosX(
            max(
                8.0f * scale,
                (width - message_width) * 0.5f
            )
        );
        ImGui::TextDisabled(
            "%s",
            message
        );
        const char* hint = m_assets.empty()
            ? "AI builds land in mcp/blockout. Refresh the library after a run."
            : "Try another name or asset type.";
        const float hint_width =
            ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX(
            max(
                8.0f * scale,
                (width - hint_width) * 0.5f
            )
        );
        ImGui::TextDisabled("%s", hint);
        if (!m_assets.empty())
        {
            ImGui::SetCursorPosX(
                max(
                    8.0f * scale,
                    (width - 108.0f * scale) * 0.5f
                )
            );
            if (ImGui::SmallButton("Clear filters"))
            {
                m_search.fill('\0');
                m_type_filter = 0;
            }
        }
    }

    m_visible_rows = drawn_rows;

    // the shortcuts read the list the user is looking at, so they are handled here rather than with the
    // panel wide ones, and only while the keyboard is not owned by a rename input or the search box
    const bool list_focused =
        ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows
        ) &&
        m_rename_asset_id.empty() &&
        m_rename_dependency_path.empty() &&
        !ImGui::GetIO().WantTextInput;
    if (
        list_focused &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)
    )
    {
        m_selected_assets.clear();
        for (const int index : drawn_rows)
        {
            m_selected_assets.insert(m_assets[index].id);
        }
        if (!drawn_rows.empty())
        {
            m_selection_anchor = drawn_rows.front();
        }
        m_status =
            "Selected " +
            to_string(m_selected_assets.size()) +
            (m_selected_assets.size() == 1 ? " asset" : " assets");
    }
    if (
        list_focused &&
        !m_selected_assets.empty() &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false)
    )
    {
        m_pending_delete_selection = true;
    }

    ImGui::EndChild();
}

bool AssetViewer::IsAssetSelected(const int index) const
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        return false;
    }

    return
        m_selected_assets.find(m_assets[index].id) !=
        m_selected_assets.end();
}

vector<string> AssetViewer::SelectedAssetIds() const
{
    // returned in list order so the confirmation reads the same way the list does
    vector<string> ids;
    ids.reserve(m_selected_assets.size());
    for (const AssetEntry& asset : m_assets)
    {
        if (
            m_selected_assets.find(asset.id) !=
            m_selected_assets.end()
        )
        {
            ids.push_back(asset.id);
        }
    }
    return ids;
}

void AssetViewer::ApplyRowSelection(
    const int index,
    const vector<int>& order
)
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const string& id = m_assets[index].id;

    // ctrl and shift only move the highlight, loading a preview per row would make extending a selection
    // across forty meshes rebuild the preview scene forty times
    if (io.KeyCtrl && !io.KeyShift)
    {
        if (m_selected_assets.erase(id) == 0)
        {
            m_selected_assets.insert(id);
            m_selection_anchor = index;
        }
        return;
    }

    if (io.KeyShift)
    {
        const auto anchor_position = find(
            order.begin(),
            order.end(),
            m_selection_anchor
        );
        const auto index_position = find(
            order.begin(),
            order.end(),
            index
        );
        if (
            anchor_position == order.end() ||
            index_position == order.end()
        )
        {
            m_selection_anchor = index;
            m_selected_assets.insert(id);
            return;
        }

        auto first = anchor_position;
        auto last = index_position;
        if (first > last)
        {
            swap(first, last);
        }
        m_selected_assets.clear();
        for (auto row = first; row <= last; ++row)
        {
            m_selected_assets.insert(m_assets[*row].id);
        }
        return;
    }

    m_selected_assets.clear();
    m_selected_assets.insert(id);
    m_selection_anchor = index;
    m_selected_asset = index;
    m_selected_dependency_path.clear();
    m_inspector_tab = 0;
    LoadSelectedAsset();
}

void AssetViewer::DrawAssetRenameInline(
    const int index,
    const float width
)
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        m_rename_asset_id.clear();
        return;
    }

    if (m_rename_request_focus)
    {
        ImGui::SetKeyboardFocusHere();
        m_rename_request_focus = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * ui_scale());
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(4.0f * ui_scale(), 1.0f * ui_scale())
    );
    ImGui::SetNextItemWidth(width);

    const bool committed = ImGui::InputText(
        "##asset_rename_inline",
        &m_rename_buffer,
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_AutoSelectAll
    );
    const bool deactivated = ImGui::IsItemDeactivated();
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

    ImGui::PopStyleVar(2);

    if (cancelled)
    {
        m_rename_asset_id.clear();
        return;
    }
    if (committed || deactivated)
    {
        // queued rather than applied, the rename rebuilds m_assets and the row being drawn holds
        // a reference into it
        m_rename_commit_id = m_assets[index].id;
        m_rename_commit_name = m_rename_buffer;
        m_rename_asset_id.clear();
    }
}

void AssetViewer::DrawAssetContextMenu(const int index)
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        return;
    }

    if (!ImGui::BeginPopupContextItem("##asset_context"))
    {
        return;
    }

    const AssetEntry& asset = m_assets[index];

    // right clicking inside a selection acts on the selection, right clicking outside one replaces it, which
    // is what stops a menu from quietly deleting rows the user had stopped looking at
    if (!IsAssetSelected(index))
    {
        m_selected_assets.clear();
        m_selected_assets.insert(asset.id);
        m_selection_anchor = index;
    }
    const size_t selected_count = m_selected_assets.size();

    ImGui::TextDisabled(
        "%s",
        selected_count > 1
        ? (
            to_string(selected_count) +
            " assets selected"
        ).c_str()
        : asset_display_name(asset.name).c_str()
    );
    ImGui::Separator();

    if (
        ImGui::MenuItem(
            "Rename",
            "F2",
            false,
            selected_count <= 1
        )
    )
    {
        // one rename at a time, the two inline inputs share the buffer
        m_rename_dependency_path.clear();
        m_rename_asset_id = asset.id;
        m_rename_buffer = asset.name;
        m_rename_request_focus = true;
    }
    if (selected_count > 1)
    {
        if (
            ImGui::MenuItem(
                (
                    "Delete " +
                    to_string(selected_count) +
                    " assets"
                ).c_str(),
                "Del"
            )
        )
        {
            m_pending_delete_selection = true;
        }
    }
    else if (ImGui::MenuItem("Delete", "Del"))
    {
        m_pending_delete_id = asset.id;
    }
    if (
        ImGui::MenuItem(
            "Select all",
            "Ctrl+A",
            false,
            !m_visible_rows.empty()
        )
    )
    {
        m_selected_assets.clear();
        for (const int row : m_visible_rows)
        {
            m_selected_assets.insert(m_assets[row].id);
        }
    }

    ImGui::Separator();
    const bool has_path = !asset.path.empty();
    if (ImGui::MenuItem("Copy path", nullptr, false, has_path))
    {
        ImGui::SetClipboardText(asset.path.c_str());
        m_status = "Copied " + asset.path;
    }
    if (ImGui::MenuItem("Show in explorer", nullptr, false, has_path))
    {
        error_code error;
        const filesystem::path absolute = filesystem::absolute(
            filesystem::path(
                FileSystem::GetDirectoryFromFilePath(asset.path)
            ),
            error
        );
        if (!error)
        {
            FileSystem::OpenUrl(
                "file:///" + absolute.generic_string()
            );
        }
    }

    ImGui::EndPopup();
}

void AssetViewer::DrawDependencyRenameInline(
    const string& path,
    const float width
)
{
    if (m_rename_request_focus)
    {
        ImGui::SetKeyboardFocusHere();
        m_rename_request_focus = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * ui_scale());
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(4.0f * ui_scale(), 1.0f * ui_scale())
    );
    ImGui::SetNextItemWidth(width);

    const bool committed = ImGui::InputText(
        "##dependency_rename_inline",
        &m_rename_buffer,
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_AutoSelectAll
    );
    const bool deactivated = ImGui::IsItemDeactivated();
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

    ImGui::PopStyleVar(2);

    if (cancelled)
    {
        m_rename_dependency_path.clear();
        return;
    }
    if (committed || deactivated)
    {
        m_rename_commit_path = path;
        m_rename_commit_name = m_rename_buffer;
        m_rename_dependency_path.clear();
    }
}

void AssetViewer::DrawDependencyContextMenu(const string& path)
{
    if (!ImGui::BeginPopupContextItem("##dependency_context"))
    {
        return;
    }

    const string leaf = FileSystem::GetFileNameFromFilePath(path);
    ImGui::TextDisabled("%s", leaf.c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Rename"))
    {
        // one rename at a time, the two inline inputs share the buffer
        m_rename_asset_id.clear();
        m_rename_dependency_path = path;
        m_rename_buffer =
            FileSystem::GetFileNameWithoutExtensionFromFilePath(path);
        m_rename_request_focus = true;
    }
    if (ImGui::MenuItem("Delete"))
    {
        m_pending_delete_path = path;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Copy path"))
    {
        ImGui::SetClipboardText(path.c_str());
        m_status = "Copied " + path;
    }
    if (ImGui::MenuItem("Show in explorer"))
    {
        error_code error;
        const filesystem::path absolute = filesystem::absolute(
            filesystem::path(
                FileSystem::GetDirectoryFromFilePath(path)
            ),
            error
        );
        if (!error)
        {
            FileSystem::OpenUrl(
                "file:///" + absolute.generic_string()
            );
        }
    }

    ImGui::EndPopup();
}

void AssetViewer::DrawDeleteConfirmation()
{
    if (m_pending_delete_selection)
    {
        const vector<string> ids = SelectedAssetIds();
        if (ids.empty())
        {
            m_pending_delete_selection = false;
            return;
        }

        const char* selection_title = "Delete selected assets?";
        if (!ImGui::IsPopupOpen(selection_title))
        {
            ImGui::OpenPopup(selection_title);
        }

        if (
            ImGui::BeginPopupModal(
                selection_title,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            )
        )
        {
            size_t prefab_count = 0;
            for (const string& id : ids)
            {
                for (const AssetEntry& asset : m_assets)
                {
                    if (
                        asset.id == id &&
                        asset.type == "prefab"
                    )
                    {
                        prefab_count++;
                        break;
                    }
                }
            }
            ImGui::Text(
                "Permanently delete %zu %s?",
                ids.size(),
                ids.size() == 1 ? "asset" : "assets"
            );
            ImGui::TextDisabled(
                "The asset path, source and thumbnail go with them."
            );
            if (prefab_count > 0)
            {
                ImGui::TextDisabled(
                    "%zu selected prefab%s will also lose owned dependency copies.",
                    prefab_count,
                    prefab_count == 1 ? "" : "s"
                );
            }
            ImGui::Spacing();

            // a scrolling list rather than a wall of text, a full library selection is hundreds of rows and
            // the modal would grow past the screen
            const float scale = ui_scale();
            ImGui::BeginChild(
                "##delete_selection_list",
                ImVec2(
                    360.0f * scale,
                    min(
                        200.0f * scale,
                        static_cast<float>(ids.size()) *
                        ImGui::GetTextLineHeightWithSpacing() +
                        4.0f * scale
                    )
                ),
                ImGuiChildFlags_Borders
            );
            for (const string& id : ids)
            {
                for (const AssetEntry& asset : m_assets)
                {
                    if (asset.id != id)
                    {
                        continue;
                    }

                    ImGui::TextDisabled(
                        "%s",
                        (
                            asset_display_name(asset.name) +
                            "  -  " +
                            (
                                asset.disk_only
                                ? asset.type + ", file only"
                                : asset.type
                            )
                        ).c_str()
                    );
                    break;
                }
            }
            ImGui::EndChild();

            ImGui::Spacing();
            if (ImGuiSp::button("Delete permanently"))
            {
                m_pending_delete_selection = false;
                DeleteAssets(ids);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Cancel"))
            {
                m_pending_delete_selection = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        else
        {
            m_pending_delete_selection = false;
        }
        return;
    }

    if (!m_pending_delete_path.empty())
    {
        const char* file_title = "Delete linked file?";
        if (!ImGui::IsPopupOpen(file_title))
        {
            ImGui::OpenPopup(file_title);
        }

        if (
            ImGui::BeginPopupModal(
                file_title,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            )
        )
        {
            ImGui::Text(
                "Permanently delete %s?",
                FileSystem::GetFileNameFromFilePath(
                    m_pending_delete_path
                ).c_str()
            );
            ImGui::TextDisabled("%s", m_pending_delete_path.c_str());
            ImGui::TextDisabled(
                "Anything referencing it will fail to load."
            );
            ImGui::Spacing();
            if (ImGuiSp::button("Delete permanently"))
            {
                const string path = m_pending_delete_path;
                m_pending_delete_path.clear();
                FileSystem::Delete(path);
                m_status = FileSystem::Exists(path)
                    ? "Failed to delete " + path
                    : "Deleted " + path;
                if (normalized_path(m_loaded_path) == normalized_path(path))
                {
                    ClearLoadedAsset();
                }
                RefreshCatalog(true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Cancel"))
            {
                m_pending_delete_path.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        else
        {
            m_pending_delete_path.clear();
        }
        return;
    }

    if (m_pending_delete_id.empty())
    {
        return;
    }

    int index = -1;
    for (int i = 0; i < static_cast<int>(m_assets.size()); i++)
    {
        if (m_assets[i].id == m_pending_delete_id)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        m_pending_delete_id.clear();
        return;
    }

    const char* title = "Delete library asset?";
    if (!ImGui::IsPopupOpen(title))
    {
        ImGui::OpenPopup(title);
    }

    if (
        ImGui::BeginPopupModal(
            title,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        const AssetEntry& asset = m_assets[index];
        ImGui::Text(
            "Permanently delete %s?",
            asset_display_name(asset.name).c_str()
        );
        if (asset.disk_only)
        {
            ImGui::TextDisabled("%s", asset.path.c_str());
        }
        else
        {
            if (asset.type == "prefab")
            {
                ImGui::TextDisabled(
                    "This removes the prefab, source, thumbnail and owned dependency copies."
                );
            }
            else
            {
                ImGui::TextDisabled(
                    "This removes the asset path, source and thumbnail."
                );
            }
        }
        ImGui::Spacing();
        if (ImGuiSp::button("Delete permanently"))
        {
            m_selected_asset = index;
            m_selected_dependency_path.clear();
            DeleteSelectedAsset();
            m_pending_delete_id.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            m_pending_delete_id.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
    {
        // the modal was dismissed by clicking away or pressing escape
        m_pending_delete_id.clear();
    }
}

bool AssetViewer::RenameAssetFile(
    const string& path,
    const string& new_name
)
{
    const string name = sanitize_asset_name(new_name);
    if (name.empty())
    {
        m_status = "That name cannot be used.";
        return false;
    }
    if (!FileSystem::Exists(path))
    {
        m_status = "The file is gone: " + path;
        return false;
    }

    const string extension =
        FileSystem::GetExtensionFromFilePath(path);
    const string new_path =
        FileSystem::GetDirectoryFromFilePath(path) +
        name +
        extension;
    if (normalized_path(new_path) == normalized_path(path))
    {
        return true;
    }
    if (FileSystem::Exists(new_path))
    {
        m_status =
            "A file named " + name + extension + " already exists.";
        return false;
    }

    FileSystem::Rename(path, new_path);
    if (!FileSystem::Exists(new_path))
    {
        m_status = "Failed to rename " + path;
        return false;
    }

    const uint32_t retargeted = retarget_library_references(
        FileSystem::GetDirectoryFromFilePath(m_catalog_path),
        FileSystem::GetFileNameFromFilePath(path),
        name + extension
    );
    m_status =
        retargeted == 0
            ? "Renamed to " + name
            : "Renamed to " + name +
              ", updated " + to_string(retargeted) +
              (retargeted == 1 ? " reference" : " references");
    return true;
}

bool AssetViewer::RenameAsset(
    const int index,
    const string& new_name
)
{
    if (
        index < 0 ||
        index >= static_cast<int>(m_assets.size())
    )
    {
        return false;
    }

    const AssetEntry& asset = m_assets[index];
    const string name = sanitize_asset_name(new_name);
    if (name.empty())
    {
        m_status = "That name cannot be used.";
        return false;
    }
    if (name == asset.name)
    {
        return true;
    }

    // an unregistered file has no catalog record, its file name is the only name it has, so the
    // rename has to happen on disk and every xml reference to it has to follow
    if (asset.disk_only)
    {
        const string old_path = asset.path;
        const string new_path =
            FileSystem::GetDirectoryFromFilePath(old_path) +
            name +
            FileSystem::GetExtensionFromFilePath(old_path);
        if (!RenameAssetFile(old_path, name))
        {
            return false;
        }

        const string status = m_status;
        ClearLoadedAsset();
        RefreshCatalog(true);

        // the id of an unregistered asset is its file name, so the refresh cannot restore the
        // selection by id, find the entry that now owns the renamed file instead
        const string wanted = normalized_path(new_path);
        for (int i = 0; i < static_cast<int>(m_assets.size()); i++)
        {
            if (
                normalized_path(m_assets[i].path) ==
                wanted
            )
            {
                m_selected_asset = i;
                m_selected_dependency_path.clear();
                LoadSelectedAsset();
                break;
            }
        }

        m_status = status;
        return true;
    }

    // catalog assets keep their files and their id, only the display name changes, so nothing
    // that points at them can break
    if (m_catalog_path.empty())
    {
        m_status = "No asset catalog to write to.";
        return false;
    }

    const string catalog_write_time =
        FileSystem::GetLastWriteTime(m_catalog_path);
    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_status = "The asset catalog could not be read.";
        return false;
    }

    JsonValue root;
    string parse_error;
    if (!mcp_json::parse(source, root, parse_error))
    {
        m_status = "The asset catalog is invalid: " + parse_error;
        return false;
    }

    JsonValue* assets = json_object_find(root, "assets");
    if (
        root.type != mcp_json::kind::object ||
        !assets ||
        assets->type != mcp_json::kind::object
    )
    {
        m_status = "The asset catalog has no assets object.";
        return false;
    }

    JsonValue* asset_value = json_object_find(*assets, asset.id);
    if (!asset_value)
    {
        m_status = "The selected asset is no longer in the catalog.";
        RefreshCatalog(true);
        return false;
    }

    JsonValue& name_value = json_object_ensure(*asset_value, "name");
    name_value.type = mcp_json::kind::string;
    name_value.string_value = name;

    const string temporary_path = m_catalog_path + ".rename.tmp";
    const string backup_path = m_catalog_path + ".rename.backup";
    FileSystem::Delete(temporary_path);
    FileSystem::Delete(backup_path);
    if (
        !FileSystem::WriteFile(
            temporary_path,
            serialize_json(root) + "\n"
        )
    )
    {
        m_status = "The updated asset catalog could not be written.";
        return false;
    }
    if (
        FileSystem::GetLastWriteTime(m_catalog_path) !=
        catalog_write_time
    )
    {
        FileSystem::Delete(temporary_path);
        m_status = "The catalog changed during the rename, try again.";
        return false;
    }

    error_code error;
    filesystem::rename(m_catalog_path, backup_path, error);
    if (error)
    {
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be backed up.";
        return false;
    }
    filesystem::rename(temporary_path, m_catalog_path, error);
    if (error)
    {
        filesystem::rename(backup_path, m_catalog_path, error);
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be replaced.";
        return false;
    }
    FileSystem::Delete(backup_path);

    RefreshCatalog(true);
    m_status = "Renamed to " + name;
    return true;
}

bool AssetViewer::DeleteSelectedAsset()
{
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size())
    )
    {
        m_status = "Select a catalog asset before deleting.";
        return false;
    }

    return DeleteAssets({ m_assets[m_selected_asset].id });
}

// the whole selection goes through one catalog rewrite. doing it a row at a time would read, rewrite and
// reload the catalog once per asset and refresh the list in between, which for a full library is minutes
// of work and leaves a half deleted catalog behind if any single step fails
bool AssetViewer::DeleteAssets(const vector<string>& ids)
{
    if (ids.empty())
    {
        m_status = "Select an asset before deleting.";
        return false;
    }

    // disk only entries have no catalog record at all, the file itself is the whole asset
    vector<string> catalog_ids;
    vector<string> disk_paths;
    for (const string& id : ids)
    {
        const AssetEntry* entry = nullptr;
        for (const AssetEntry& asset : m_assets)
        {
            if (asset.id == id)
            {
                entry = &asset;
                break;
            }
        }
        if (!entry)
        {
            continue;
        }

        if (entry->disk_only)
        {
            disk_paths.push_back(entry->path);
        }
        else
        {
            catalog_ids.push_back(id);
        }
    }

    uint32_t deleted_count = 0;
    uint32_t failed_count = 0;
    for (const string& path : disk_paths)
    {
        FileSystem::Delete(path);
        if (FileSystem::Exists(path))
        {
            failed_count++;
        }
        else
        {
            deleted_count++;
        }
    }

    if (catalog_ids.empty())
    {
        m_selected_assets.clear();
        m_selection_anchor = -1;
        ClearLoadedAsset();
        RefreshCatalog(true);
        m_status =
            failed_count == 0
            ? "Deleted " + to_string(deleted_count) + " files"
            : "Deleted " +
                to_string(deleted_count) +
                " files, " +
                to_string(failed_count) +
                " could not be removed";
        return failed_count == 0;
    }

    if (m_catalog_path.empty())
    {
        m_status = "There is no asset catalog to delete from.";
        return false;
    }

    const string catalog_write_time =
        FileSystem::GetLastWriteTime(m_catalog_path);
    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_status = "The asset catalog could not be read.";
        return false;
    }

    JsonValue root;
    string parse_error;
    if (!mcp_json::parse(source, root, parse_error))
    {
        m_status =
            "The asset catalog is invalid: " +
            parse_error;
        return false;
    }

    JsonValue* assets = json_object_find(root, "assets");
    if (
        root.type != mcp_json::kind::object ||
        !assets ||
        assets->type != mcp_json::kind::object
    )
    {
        m_status = "The asset catalog has no assets object.";
        return false;
    }

    vector<string> owned_paths;
    vector<string> erased_ids;
    for (const string& asset_id : catalog_ids)
    {
        JsonValue* asset_value = json_object_find(*assets, asset_id);
        if (!asset_value)
        {
            continue;
        }

        for (
            const char* key :
            {
                "path",
                "source_path",
                "thumbnail_path"
            }
        )
        {
            if (const JsonValue* path = asset_value->find(key))
            {
                if (!path->string_or("").empty())
                {
                    owned_paths.push_back(path->string_or(""));
                }
            }
        }

        json_object_erase(*assets, asset_id);
        erased_ids.push_back(asset_id);
    }

    if (erased_ids.empty())
    {
        m_status = "The selected assets are no longer in the catalog.";
        m_selected_assets.clear();
        m_selection_anchor = -1;
        RefreshCatalog(true);
        return false;
    }

    const string temporary_path =
        m_catalog_path + ".delete.tmp";
    const string backup_path =
        m_catalog_path + ".delete.backup";
    FileSystem::Delete(temporary_path);
    FileSystem::Delete(backup_path);
    if (
        !FileSystem::WriteFile(
            temporary_path,
            serialize_json(root) + "\n"
        )
    )
    {
        m_status = "The updated asset catalog could not be written.";
        return false;
    }
    if (
        FileSystem::GetLastWriteTime(m_catalog_path) !=
        catalog_write_time
    )
    {
        FileSystem::Delete(temporary_path);
        m_status =
            "The catalog changed during deletion, try again.";
        return false;
    }

    error_code error;
    filesystem::rename(
        m_catalog_path,
        backup_path,
        error
    );
    if (error)
    {
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be backed up.";
        return false;
    }
    filesystem::rename(
        temporary_path,
        m_catalog_path,
        error
    );
    if (error)
    {
        filesystem::rename(
            backup_path,
            m_catalog_path,
            error
        );
        FileSystem::Delete(temporary_path);
        m_status = "The asset catalog could not be replaced.";
        return false;
    }
    FileSystem::Delete(backup_path);

    ClearLoadedAsset();
    const string library_root =
        FileSystem::GetDirectoryFromFilePath(m_catalog_path);
    for (const string& owned_path : owned_paths)
    {
        if (
            path_is_within(owned_path, library_root) &&
            FileSystem::Exists(owned_path) &&
            !FileSystem::Delete(owned_path)
        )
        {
            failed_count++;
        }
    }
    for (const string& asset_id : erased_ids)
    {
        for (
            const char* folder :
            {
                "meshes",
                "materials",
                "prefabs",
                "sources",
                "thumbnails",
                "dependencies"
            }
        )
        {
            const string owned_directory =
                library_root +
                "/" +
                folder +
                "/" +
                asset_id;
            if (
                path_is_within(
                    owned_directory,
                    library_root
                ) &&
                FileSystem::Exists(owned_directory)
            )
            {
                error.clear();
                filesystem::remove_all(
                    owned_directory,
                    error
                );
                if (error)
                {
                    failed_count++;
                }
            }
        }
    }

    deleted_count += static_cast<uint32_t>(erased_ids.size());
    m_selected_assets.clear();
    m_selection_anchor = -1;
    RefreshCatalog(true);
    m_status =
        failed_count == 0 ?
        "Deleted " +
            to_string(deleted_count) +
            (deleted_count == 1 ? " asset" : " assets") :
        "Deleted " +
            to_string(deleted_count) +
            ", but " +
            to_string(failed_count) +
            " owned files could not be removed";
    return failed_count == 0;
}

void AssetViewer::DrawDetails(float height)
{
    ImGui::BeginChild(
        "##asset_viewer_details",
        ImVec2(0.0f, height),
        ImGuiChildFlags_Borders
    );

    const float scale = ui_scale();
    if (
        (
            m_selected_asset < 0 ||
            m_selected_asset >= static_cast<int>(m_assets.size())
        ) &&
        !m_selected_dependency_path.empty()
    )
    {
        const string type =
            asset_type_from_path(m_selected_dependency_path);
        const string dependency_name = asset_display_name(
            FileSystem::
                GetFileNameWithoutExtensionFromFilePath(
                    m_selected_dependency_path
                )
        );
        draw_asset_identity(
            dependency_name,
            type,
            "linked resource"
        );
        ImGui::Spacing();

        // a mesh reached by expanding a prefab is still a mesh, without this the simplify and lod
        // controls are only reachable when the mesh happens to be a top level catalog entry
        const bool mesh_tools_available =
            m_working_editable &&
            !m_working_vertices.empty();
        if (mesh_tools_available)
        {
            if (ImGui::BeginTabBar("##asset_dependency_tabs"))
            {
                if (ImGui::BeginTabItem("Overview"))
                {
                    m_dependency_tab = 0;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Optimize"))
                {
                    m_dependency_tab = 1;
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        else
        {
            m_dependency_tab = 0;
        }

        if (m_dependency_tab == 1)
        {
            DrawMeshTools();
            ImGui::EndChild();
            return;
        }

        if (type == "texture" && m_texture)
        {
            detail_row(
                "Resolution",
                to_string(m_texture->GetWidth()) +
                " x " +
                to_string(m_texture->GetHeight())
            );
            detail_row(
                "Channels",
                to_string(m_texture->GetChannelCount())
            );
        }
        else if (type == "material" && m_material)
        {
            section_title("TEXTURES");
            const vector<string> texture_paths =
                m_material->GetTexturePaths();
            if (texture_paths.empty())
            {
                ImGui::TextDisabled("No textures bound");
            }
            for (const string& texture_path : texture_paths)
            {
                ImGui::BulletText(
                    "%s",
                    FileSystem::GetFileNameFromFilePath(texture_path).c_str()
                );
            }
        }
        else
        {
            const auto [vertex_count, index_count] =
                GetPreviewGeometryCounts();
            detail_row("Vertices", compact_count(vertex_count));
            detail_row(
                "Triangles",
                compact_count(index_count / 3)
            );
        }

        section_title("SOURCE");
        ImGui::TextWrapped(
            "%s",
            m_selected_dependency_path.c_str()
        );
        if (ImGuiSp::button("Copy path"))
        {
            ImGui::SetClipboardText(
                m_selected_dependency_path.c_str()
            );
            m_status = "Copied linked file path";
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Show in folder"))
        {
            FileSystem::OpenUrl(
                FileSystem::GetDirectoryFromFilePath(
                    m_selected_dependency_path
                )
            );
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Reload"))
        {
            const string path = m_selected_dependency_path;
            LoadDependencyPreview(path);
        }
        ImGui::EndChild();
        return;
    }
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size())
    )
    {
        const char* title = "Nothing selected";
        const char* hint =
            "Choose an asset from the library to inspect it.";
        ImGui::SetCursorPosY(
            max(
                20.0f * scale,
                height * 0.34f
            )
        );
        ImGui::SetCursorPosX(
            max(
                12.0f * scale,
                (
                    ImGui::GetContentRegionAvail().x -
                    ImGui::CalcTextSize(title).x
                ) *
                0.5f
            )
        );
        ImGui::TextUnformatted(title);
        ImGui::SetCursorPosX(18.0f * scale);
        ImGui::TextDisabled("%s", hint);
        ImGui::EndChild();
        return;
    }

    // a copy, an action drawn below can refresh the catalog and rebuild m_assets under a reference
    const AssetEntry asset = m_assets[m_selected_asset];

    const string display_name =
        asset_display_name(asset.name);
    draw_asset_identity(
        display_name,
        asset.type,
        "catalog asset"
    );

    ImGui::Spacing();
    const char* tabs[] = { "Overview", "Optimize" };
    // a prefab is editable through every mesh it references, so the tab shows for it as well
    const int tab_count =
        m_working_editable &&
        !m_working_vertices.empty()
            ? 2
            : 1;

    // panel sections, a real tab bar rather than toggle buttons, it carries the exclusivity
    // and keeps the selected section attached to the content below it
    if (ImGui::BeginTabBar("##asset_inspector_tabs"))
    {
        for (int tab = 0; tab < tab_count; tab++)
        {
            if (ImGui::BeginTabItem(tabs[tab]))
            {
                m_inspector_tab = tab;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    m_inspector_tab = min(m_inspector_tab, tab_count - 1);

    if (m_inspector_tab == 0)
    {
        if (asset.type == "prefab")
        {
            DrawPrefabOverview(asset);
        }
        else
        {
            section_title("TECHNICAL");
            if (asset.type == "texture")
            {
                detail_row(
                    "Resolution",
                    m_texture
                        ? to_string(m_texture->GetWidth()) +
                          " x " +
                          to_string(m_texture->GetHeight())
                        : "Unknown"
                );
                detail_row(
                    "Channels",
                    m_texture
                        ? to_string(m_texture->GetChannelCount())
                        : "Unknown"
                );
            }
            else
            {
                const auto [vertex_count, index_count] =
                    GetPreviewGeometryCounts();
                detail_row("Vertices", compact_count(vertex_count));
                detail_row(
                    "Triangles",
                    compact_count(index_count / 3)
                );
            }
            if (asset.type == "material" && m_material)
            {
                const vector<string> texture_paths =
                    m_material->GetTexturePaths();
                detail_row(
                    "Textures",
                    texture_paths.empty()
                        ? "None"
                        : to_string(texture_paths.size())
                );
            }
        }

        section_title("CATALOG");
        detail_row("Asset ID", asset.id);
        detail_row(
            "Tags",
            asset.tags.empty()
                ? "None"
                : join_strings(asset.tags, ", ")
        );
        detail_row(
            "Aliases",
            asset.aliases.empty()
                ? "None"
                : join_strings(asset.aliases, ", ")
        );
        if (!asset.path.empty())
        {
            detail_row(
                "Quality",
                to_string(asset.quality_score) +
                    (
                        asset.quality_verified
                            ? " verified"
                            : " unverified"
                    )
            );
        }

        section_title("SOURCE");
        if (!asset.path.empty())
        {
            ImGui::TextWrapped("%s", asset.path.c_str());
            if (ImGuiSp::button("Copy path"))
            {
                ImGui::SetClipboardText(asset.path.c_str());
                m_status = "Copied asset path";
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Show in folder"))
            {
                FileSystem::OpenUrl(
                    FileSystem::GetDirectoryFromFilePath(
                        asset.path
                    )
                );
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Reload"))
            {
                LoadSelectedAsset(false, true);
            }
            if (ImGuiSp::button("Capture"))
            {
                const string path =
                    World::GetLibraryResourceDirectory() +
                    "thumbnails/asset_" +
                    asset.id +
                    ".png";
                m_status = SavePreviewScreenshot(
                    path,
                    1024,
                    1024
                )
                    ? "Queued renderer capture to " + path
                    : "Preview capture failed";
            }
            ImGuiSp::tooltip("Save a 1024 x 1024 preview");
        }
    }
    else
    {
        DrawMeshTools();
    }

    if (m_inspector_tab != 1)
    {
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGuiSp::button("Delete asset"))
        {
            ImGui::OpenPopup("Delete asset?");
        }
    }

    if (
        ImGui::BeginPopupModal(
            "Delete asset?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        ImGui::Text(
            "Permanently delete %s?",
            display_name.c_str()
        );
        ImGui::TextDisabled(
            asset.type == "prefab"
                ? "This removes the prefab, source, thumbnail and owned dependency copies."
                : "This removes the asset path, source and thumbnail."
        );
        ImGui::Spacing();
        if (ImGuiSp::button("Delete permanently"))
        {
            DeleteSelectedAsset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    DrawBakeConfirmation();

    ImGui::EndChild();
}

void AssetViewer::DrawPrefabOverview(const AssetEntry& asset)
{
    const float scale = ui_scale();
    const auto [vertex_count, index_count] =
        GetPreviewGeometryCounts();
    const BakeSummary bake = PreviewBakeSummary();
    const vector<Material*> materials = PreviewMaterials();

    // the numbers that decide whether the asset is affordable, read left to right like a scoreboard
    const float gap = 6.0f * scale;
    const float card_width =
        (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(
        ImGui::Style::color_accent_1
    );
    const ImU32 warning = ImGui::ColorConvertFloat4ToU32(
        ImGui::Style::color_warning
    );
    ImGui::Spacing();
    draw_metric_card(
        "triangles",
        compact_count(index_count / 3),
        card_width
    );
    ImGui::SameLine(0.0f, gap);
    draw_metric_card(
        "draw calls",
        to_string(bake.renderers_before),
        card_width,
        bake.renderers_before > bake.renderers_after ? warning : 0
    );
    ImGui::SameLine(0.0f, gap);
    draw_metric_card(
        "materials",
        to_string(materials.size()),
        card_width
    );
    ImGui::Spacing();
    draw_metric_card(
        "vertices",
        compact_count(vertex_count),
        card_width
    );
    ImGui::SameLine(0.0f, gap);
    draw_metric_card(
        "parts",
        to_string(max(0, m_prefab_entity_count - 1)),
        card_width
    );
    ImGui::SameLine(0.0f, gap);
    draw_metric_card(
        "mesh files",
        to_string(m_working_meshes.size()),
        card_width
    );

    if (!m_missing_dependencies.empty())
    {
        const ImVec4 warning_color = ImGui::Style::color_warning;
        ImGui::Spacing();
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(
                warning_color.x,
                warning_color.y,
                warning_color.z,
                0.1f
            )
        );
        ImGui::BeginChild(
            "##missing_dependencies",
            ImVec2(0.0f, 52.0f * scale),
            ImGuiChildFlags_Borders
        );
        ImGui::Text(
            "%zu missing dependenc%s",
            m_missing_dependencies.size(),
            m_missing_dependencies.size() == 1
                ? "y"
                : "ies"
        );
        ImGui::TextDisabled(
            "The preview may be incomplete"
        );
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    section_title("GAME READY");
    const bool can_bake =
        bake.renderers_before > bake.renderers_after &&
        !m_working_modified &&
        !m_working_lods_built &&
        !asset.path.empty();
    if (bake.renderers_before > bake.renderers_after)
    {
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(
                ImGui::Style::color_accent_1.x,
                ImGui::Style::color_accent_1.y,
                ImGui::Style::color_accent_1.z,
                0.1f
            )
        );
        ImGui::BeginChild(
            "##bake_hint",
            ImVec2(0.0f, 50.0f * scale),
            ImGuiChildFlags_Borders
        );
        ImGui::Text(
            "%u parts share %u material%s",
            bake.renderers_before - bake.skipped,
            bake.materials,
            bake.materials == 1 ? "" : "s"
        );
        ImGui::TextDisabled(
            "Baking draws them in %u call%s instead of %u",
            bake.renderers_after,
            bake.renderers_after == 1 ? "" : "s",
            bake.renderers_before
        );
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled(
            bake.renderers_before == 0
                ? "No geometry is drawn yet"
                : "One draw call per material, nothing left to bake"
        );
    }
    if (m_working_modified || m_working_lods_built)
    {
        ImGui::TextDisabled(
            "Save or revert the mesh changes in Optimize before baking"
        );
    }
    if (!can_bake)
    {
        ImGui::BeginDisabled();
    }
    if (
        ImGuiSp::button(
            "Bake parts by material",
            ImVec2(-1.0f, 0.0f)
        )
    )
    {
        m_bake_confirmation_open = true;
    }
    if (!can_bake)
    {
        ImGui::EndDisabled();
    }
    ImGuiSp::tooltip(
        "Merges every part that shares a material into one mesh and rewrites the prefab, "
        "the parts that were merged away are removed from it"
    );
    ImGui::TextDisabled(
        "Simplify and LODs for the whole prefab live in the Optimize tab"
    );

    section_title("MATERIALS");
    if (materials.empty())
    {
        ImGui::TextDisabled("No materials are previewed");
    }
    for (Material* material : materials)
    {
        const string normal_path = material->GetTexturePathByType(
            MaterialTextureType::Normal
        );
        const size_t texture_count =
            material->GetTexturePaths().size();
        ImGui::BulletText(
            "%s",
            asset_display_name(material->GetObjectName()).c_str()
        );
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%zu texture%s%s",
            texture_count,
            texture_count == 1 ? "" : "s",
            normal_path.empty() ? "" : ", normal"
        );
    }
}

void AssetViewer::DrawPreview(float width, float height)
{
    ImGui::BeginChild(
        "##asset_viewer_preview",
        ImVec2(width, height),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse
    );

    DrawRevisionBanner();

    const float scale = ui_scale();
    const float content_right =
        ImGui::GetCursorPosX() +
        ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted("VIEWPORT");
    const auto [vertex_count, index_count] =
        GetPreviewGeometryCounts();
    const string summary =
        compact_count(index_count / 3) +
        " tris  |  " +
        compact_count(vertex_count) +
        " verts";
    const float summary_width =
        ImGui::CalcTextSize(summary.c_str()).x;
    if (
        summary_width + ImGui::GetCursorPosX() <
        content_right
    )
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(
            content_right -
            summary_width
        );
        ImGui::TextDisabled("%s", summary.c_str());
    }

    if (!m_texture)
    {
        if (ImGui::SmallButton("Display"))
        {
            ImGui::OpenPopup("##asset_preview_display");
        }
        ImGuiSp::tooltip(
            "Shading and backdrop settings"
        );
        if (ImGui::BeginPopup("##asset_preview_display"))
        {
            const char* modes =
                "Solid\0"
                "Wireframe\0"
                "Vertices\0";
            ImGui::TextDisabled("Shading");
            ImGui::SetNextItemWidth(180.0f * scale);
            if (
                ImGui::Combo(
                    "##asset_preview_mode",
                    &m_preview_mode,
                    modes
                )
            )
            {
                m_preview_dirty = true;
            }

            const char* backdrops =
                "Auto\0"
                "Sky\0"
                "Charcoal\0"
                "Slate\0"
                "Paper\0";
            ImGui::TextDisabled("Backdrop");
            ImGui::SetNextItemWidth(180.0f * scale);
            if (
                ImGui::Combo(
                    "##asset_preview_backdrop",
                    &m_preview_backdrop,
                    backdrops
                )
            )
            {
                m_preview_dirty = true;
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine(0.0f, 4.0f * scale);
        if (
            toolbar_toggle(
                "Turntable",
                m_preview_auto_rotate
            )
        )
        {
            m_preview_auto_rotate = !m_preview_auto_rotate;
        }
    }
    else
    {
        ImGui::TextDisabled("Texture preview");
    }

    ImGui::SameLine(0.0f, 4.0f * scale);
    if (toolbar_toggle("Stats", m_preview_show_stats))
    {
        m_preview_show_stats = !m_preview_show_stats;
    }
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (ImGui::SmallButton("Frame"))
    {
        m_preview_zoom = 1.0f;
        m_texture_pan = math::Vector2::Zero;
        m_preview_dirty = true;
    }
    ImGuiSp::tooltip("Frame asset");
    ImGui::SameLine(0.0f, 3.0f * scale);
    if (ImGui::SmallButton("Reset"))
    {
        m_preview_yaw = 0.65f;
        m_preview_pitch = 0.35f;
        m_preview_zoom = 1.0f;
        m_texture_pan = math::Vector2::Zero;
        m_preview_dirty = true;
    }
    ImGuiSp::tooltip("Reset camera");

    const ImVec2 available =
        ImGui::GetContentRegionAvail();
    const ImVec2 size(
        max(1.0f, available.x),
        max(1.0f, available.y)
    );
    const uint32_t preview_width =
        max(1u, static_cast<uint32_t>(size.x));
    const uint32_t preview_height =
        max(1u, static_cast<uint32_t>(size.y));
    RHI_Texture* renderer_output =
        Renderer::GetSecondaryViewOutput();
    if (
        Renderer::IsSecondaryViewReady() &&
        m_preview_settle_frames > 0
    )
    {
        m_preview_settle_frames--;
        m_preview_dirty = true;
    }

    // a prefab commits its parts across frames and a mesh can finish importing late, so the preview
    // re-renders until what it would draw stops changing, a fixed settle count gives up too early and
    // leaves a part missing with nothing to trigger another attempt
    if (!m_texture)
    {
        const uint64_t signature = PreviewSceneSignature();
        if (signature != m_preview_signature)
        {
            m_preview_signature = signature;
            m_preview_dirty = true;
            // what the preview draws just changed, so the framing it was fitted to is out of date
            RefreshPreviewBounds();
        }
    }
    if (
        !m_texture &&
        PreviewRoot() &&
        !Renderer::IsSecondaryViewReady()
    )
    {
        m_preview_dirty = true;
    }
    if (
        !m_texture &&
        renderer_output &&
        (
            renderer_output->GetWidth() != preview_width ||
            renderer_output->GetHeight() != preview_height
        )
    )
    {
        m_preview_dirty = true;
    }

    if (
        !m_texture &&
        Renderer::IsSecondaryViewReady() &&
        renderer_output
    )
    {
        ImGuiSp::image(renderer_output, size);
    }
    else
    {
        ImGui::Dummy(size);
    }
    const ImVec2 minimum =
        ImGui::GetItemRectMin();
    const ImVec2 maximum =
        ImGui::GetItemRectMax();

    // an invisible button on top of the canvas claims the press, without it imgui
    // treats a drag over blank window space as a window move and the preview orbits
    // at the same time
    ImGui::SetCursorScreenPos(minimum);
    ImGui::InvisibleButton(
        "##asset_viewer_canvas",
        size,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonMiddle
    );
    const ImGuiIO& io = ImGui::GetIO();
    const bool preview_hovered = ImGui::IsItemHovered();

    if (
        preview_hovered &&
        (
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle)
        )
    )
    {
        m_preview_orbiting = true;
    }
    if (
        !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle)
    )
    {
        m_preview_orbiting = false;
    }
    if (
        preview_hovered &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
    )
    {
        m_preview_zoom = 1.0f;
        m_texture_pan = math::Vector2::Zero;
        m_preview_orbiting = false;
        m_preview_dirty = true;
    }
    if (
        m_preview_orbiting &&
        (
            io.MouseDelta.x != 0.0f ||
            io.MouseDelta.y != 0.0f
        )
    )
    {
        if (m_texture)
        {
            m_texture_pan.x += io.MouseDelta.x;
            m_texture_pan.y += io.MouseDelta.y;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        else
        {
            m_preview_yaw -= io.MouseDelta.x * 0.012f;
            m_preview_pitch = clamp(
                m_preview_pitch +
                io.MouseDelta.y * 0.012f,
                -1.45f,
                1.45f
            );
        }
        m_preview_dirty = true;
    }
    if (preview_hovered && io.MouseWheel != 0.0f)
    {
        const float zoom_previous = m_preview_zoom;
        const float step = io.MouseWheel > 0.0f
            ? 1.15f
            : 1.0f / 1.15f;
        m_preview_zoom = clamp(
            m_preview_zoom * step,
            m_texture ? 0.05f : 0.2f,
            m_texture ? 64.0f : 8.0f
        );
        // keep the texel under the cursor pinned while zooming
        if (m_texture && zoom_previous > 0.0f)
        {
            const float ratio =
                m_preview_zoom / zoom_previous - 1.0f;
            const float center_x =
                (minimum.x + maximum.x) * 0.5f +
                m_texture_pan.x;
            const float center_y =
                (minimum.y + maximum.y) * 0.5f +
                m_texture_pan.y;
            m_texture_pan.x -=
                (io.MousePos.x - center_x) * ratio;
            m_texture_pan.y -=
                (io.MousePos.y - center_y) * ratio;
        }
        m_preview_dirty = true;
    }

    if (m_texture)
    {
        DrawTexturePreview(minimum, maximum);
    }
    else if (
        !Renderer::IsSecondaryViewReady() ||
        !renderer_output
    )
    {
        ImGui::GetWindowDrawList()->AddRectFilled(
            minimum,
            maximum,
            IM_COL32(15, 18, 24, 255)
        );
        const char* title = PreviewRoot()
            ? "Preparing preview"
            : "No asset selected";
        const char* hint = PreviewRoot()
            ? "Loading geometry and materials"
            : "Choose an asset from the Library";
        const ImVec2 title_size =
            ImGui::CalcTextSize(title);
        const ImVec2 hint_size =
            ImGui::CalcTextSize(hint);
        const float center_y =
            (minimum.y + maximum.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                (minimum.x + maximum.x - title_size.x) *
                    0.5f,
                center_y - 16.0f * scale
            ),
            ImGui::GetColorU32(ImGuiCol_Text),
            title
        );
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                (minimum.x + maximum.x - hint_size.x) *
                    0.5f,
                center_y + 6.0f * scale
            ),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            hint
        );
    }
    if (
        m_preview_show_stats &&
        !m_texture &&
        Renderer::IsSecondaryViewReady()
    )
    {
        const ImVec2 text_size =
            ImGui::CalcTextSize(summary.c_str());
        const ImVec2 card_min(
            minimum.x + 10.0f * scale,
            maximum.y -
                text_size.y -
                22.0f * scale
        );
        const ImVec2 card_max(
            card_min.x + text_size.x + 16.0f * scale,
            maximum.y - 8.0f * scale
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            card_min,
            card_max,
            IM_COL32(10, 13, 18, 190),
            4.0f * scale
        );
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                card_min.x + 8.0f * scale,
                card_min.y + 7.0f * scale
            ),
            IM_COL32(225, 230, 235, 220),
            summary.c_str()
        );
    }
    // a pending capture owns the next secondary render, it asked for a size and a shading of its own
    // and a panel refresh in between would hand it the panel's frame instead
    if (
        m_preview_dirty &&
        !m_texture &&
        !Renderer::IsSecondaryScreenshotPending() &&
        chrono::steady_clock::now() >=
            m_next_preview_request
    )
    {
        RequestPreviewRender(
            preview_width,
            preview_height
        );
    }
    if (preview_hovered && !m_preview_orbiting)
    {
        ImGuiSp::tooltip(
            m_texture
                ? "Drag to pan\nWheel to zoom\nDouble click to fit"
                : "Drag to orbit\nWheel to zoom\nDouble click to frame"
        );
    }
    ImGui::EndChild();
}

void AssetViewer::DrawTexturePreview(
    const ImVec2& minimum,
    const ImVec2& maximum
)
{
    if (!m_texture)
    {
        return;
    }

    const float canvas_width =
        maximum.x - minimum.x;
    const float canvas_height =
        maximum.y - minimum.y;
    const float texture_width =
        static_cast<float>(m_texture->GetWidth());
    const float texture_height =
        static_cast<float>(m_texture->GetHeight());
    if (
        canvas_width <= 0.0f ||
        canvas_height <= 0.0f ||
        texture_width <= 0.0f ||
        texture_height <= 0.0f
    )
    {
        return;
    }

    ImDrawList* draw_list =
        ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        minimum,
        maximum,
        IM_COL32(15, 18, 24, 255)
    );

    const float fit_scale =
        min(
            canvas_width / texture_width,
            canvas_height / texture_height
        );
    const float draw_scale =
        fit_scale * m_preview_zoom;
    const ImVec2 image_size(
        texture_width * draw_scale,
        texture_height * draw_scale
    );
    const ImVec2 image_min(
        minimum.x +
            (canvas_width - image_size.x) * 0.5f +
            m_texture_pan.x,
        minimum.y +
            (canvas_height - image_size.y) * 0.5f +
            m_texture_pan.y
    );
    const ImVec2 image_max(
        image_min.x + image_size.x,
        image_min.y + image_size.y
    );
    draw_list->PushClipRect(
        minimum,
        maximum,
        true
    );
    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(m_texture.get()),
        image_min,
        image_max
    );

    if (!m_preview_show_stats)
    {
        draw_list->PopClipRect();
        return;
    }

    const string dimensions =
        to_string(m_texture->GetWidth()) +
        " x " +
        to_string(m_texture->GetHeight());
    draw_list->AddText(
        ImVec2(
            minimum.x + 10.0f,
            maximum.y - 26.0f
        ),
        IM_COL32(225, 230, 235, 220),
        dimensions.c_str()
    );
    draw_list->PopClipRect();
}

bool AssetViewer::AssetMatchesFilter(
    const AssetEntry& asset
) const
{
    if (
        m_type_filter == 1 &&
        asset.type != "mesh"
    )
    {
        return false;
    }
    if (
        m_type_filter == 2 &&
        asset.type != "material"
    )
    {
        return false;
    }
    if (
        m_type_filter == 3 &&
        asset.type != "prefab"
    )
    {
        return false;
    }
    if (
        m_type_filter == 4 &&
        asset.type != "texture"
    )
    {
        return false;
    }

    const string query = lower_copy(m_search.data());

    // packed maps are hidden rather than removed, searching for them is the way back in, the file
    // on disk is checked as well because a catalog entry can carry any display name it likes
    if (query.find("packed") == string::npos)
    {
        if (
            is_packed_texture(asset.id) ||
            is_packed_texture(asset.name) ||
            is_packed_texture(asset.path)
        )
        {
            return false;
        }
    }

    if (query.empty())
    {
        return true;
    }

    string searchable =
        asset.id +
        " " +
        asset.name +
        " " +
        asset.type +
        " " +
        join_strings(asset.aliases, " ") +
        " " +
        join_strings(asset.tags, " ");
    searchable = lower_copy(move(searchable));

    istringstream stream(query);
    string term;
    while (stream >> term)
    {
        if (searchable.find(term) == string::npos)
        {
            return false;
        }
    }
    return true;
}