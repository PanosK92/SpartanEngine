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

bool AssetViewer::Refresh(string& error)
{
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before refreshing";
        return false;
    }
    RefreshCatalog(true);
    return true;
}

bool AssetViewer::ListAssets(
    const ListRequest& request,
    ListResult& result,
    string& error
)
{
    if (
        !m_working_modified &&
        !m_working_lods_built
    )
    {
        RefreshCatalog(false);
    }
    string type = lower_copy(request.type);
    if (type == "all")
    {
        type.clear();
    }
    if (
        !type.empty() &&
        type != "mesh" &&
        type != "material" &&
        type != "prefab" &&
        type != "texture"
    )
    {
        error = "type must be mesh, material, prefab or texture";
        return false;
    }

    const string sort_mode = lower_copy(request.sort);
    if (
        sort_mode != "name" &&
        sort_mode != "quality" &&
        sort_mode != "type"
    )
    {
        error = "sort must be name, quality or type";
        return false;
    }
    if (
        request.limit == 0 ||
        request.limit > 500
    )
    {
        error = "limit must be between 1 and 500";
        return false;
    }

    const string query = lower_copy(request.query);
    vector<const AssetEntry*> matches;
    for (const AssetEntry& asset : m_assets)
    {
        if (
            (!type.empty() && asset.type != type) ||
            (!request.include_disk_only && asset.disk_only)
        )
        {
            continue;
        }

        string searchable =
            asset.id + " " +
            asset.name + " " +
            asset.type + " " +
            join_strings(asset.aliases, " ") + " " +
            join_strings(asset.tags, " ");
        searchable = lower_copy(move(searchable));
        istringstream terms(query);
        string term;
        bool found = true;
        while (terms >> term)
        {
            if (searchable.find(term) == string::npos)
            {
                found = false;
                break;
            }
        }
        if (found)
        {
            matches.push_back(&asset);
        }
    }

    sort(
        matches.begin(),
        matches.end(),
        [this, &sort_mode](
            const AssetEntry* first,
            const AssetEntry* second
        )
        {
            if (sort_mode == "quality")
            {
                const float first_quality =
                    first->quality_score;
                const float second_quality =
                    second->quality_score;
                if (first_quality != second_quality)
                {
                    return first_quality > second_quality;
                }
            }
            else if (
                sort_mode == "type" &&
                first->type != second->type
            )
            {
                return first->type < second->type;
            }
            return lower_copy(first->name) <
                lower_copy(second->name);
        }
    );

    result = ListResult();
    result.total = matches.size();
    result.offset = min<uint64_t>(
        request.offset,
        result.total
    );
    result.limit = request.limit;
    const uint64_t end = min<uint64_t>(
        result.total,
        result.offset + result.limit
    );
    for (uint64_t index = result.offset; index < end; index++)
    {
        const AssetEntry& asset = *matches[index];
        AssetSummary summary;
        summary.id = asset.id;
        summary.name = asset.name;
        summary.type = asset.type;
        summary.path = asset.path;
        summary.source_path = asset.source_path;
        summary.thumbnail_path = asset.thumbnail_path;
        summary.quality_score = asset.quality_score;
        summary.quality_verified = asset.quality_verified;
        summary.disk_only = asset.disk_only;
        result.assets.emplace_back(move(summary));
    }
    return true;
}
bool AssetViewer::InspectAsset(
    const string& asset_id,
    AssetInspection& result,
    string& error
) const
{
    if (asset_id.empty())
    {
        error = "asset_id is required";
        return false;
    }

    const AssetEntry* asset = nullptr;
    for (const AssetEntry& candidate : m_assets)
    {
        if (candidate.id == asset_id)
        {
            asset = &candidate;
            break;
        }
    }
    if (!asset)
    {
        error = "asset was not found in the active catalog";
        return false;
    }

    result = AssetInspection();
    result.asset.id = asset->id;
    result.asset.name = asset->name;
    result.asset.type = asset->type;
    result.asset.path = asset->path;
    result.asset.source_path = asset->source_path;
    result.asset.thumbnail_path = asset->thumbnail_path;
    result.asset.quality_score = asset->quality_score;
    result.asset.quality_verified = asset->quality_verified;
    result.asset.disk_only = asset->disk_only;
    result.aliases = asset->aliases;
    result.tags = asset->tags;
    result.dependencies = asset->dependencies;
    result.source_exists = FileSystem::Exists(asset->path);
    if (result.source_exists)
    {
        error_code size_error;
        result.source_bytes = static_cast<uint64_t>(
            filesystem::file_size(
                filesystem::path(asset->path),
                size_error
            )
        );
        if (size_error)
        {
            result.source_bytes = 0;
        }
    }

    const bool inspecting_loaded =
        normalized_path(asset->path) ==
            normalized_path(m_loaded_path);
    if (inspecting_loaded)
    {
        const auto [vertex_count, index_count] =
            GetPreviewGeometryCounts();
        result.vertex_count = vertex_count;
        result.index_count = index_count;
        result.prefab_entity_count = m_prefab_entity_count;
        result.missing_dependencies = m_missing_dependencies;
        if (m_texture)
        {
            result.texture_width = m_texture->GetWidth();
            result.texture_height = m_texture->GetHeight();
            result.texture_channels = m_texture->GetChannelCount();
        }
    }
    return true;
}

bool AssetViewer::SetSelection(
    const SelectionRequest& request,
    string& error
)
{
    vector<int> indices;
    indices.reserve(request.asset_ids.size());
    for (const string& id : request.asset_ids)
    {
        int found = -1;
        for (size_t index = 0; index < m_assets.size(); index++)
        {
            if (m_assets[index].id == id)
            {
                found = static_cast<int>(index);
                break;
            }
        }
        if (found < 0)
        {
            error = "asset was not found: " + id;
            return false;
        }
        indices.push_back(found);
    }

    int focus = -1;
    if (request.focus_id)
    {
        for (size_t index = 0; index < m_assets.size(); index++)
        {
            if (m_assets[index].id == *request.focus_id)
            {
                focus = static_cast<int>(index);
                break;
            }
        }
        if (focus < 0)
        {
            error = "focus asset was not found";
            return false;
        }
    }
    else if (
        request.mode == SelectionMode::Replace &&
        !indices.empty()
    )
    {
        focus = indices.front();
    }

    unordered_set<string> next_selection =
        request.mode == SelectionMode::Replace
            ? unordered_set<string>()
            : m_selected_assets;
    for (const int index : indices)
    {
        const string& id = m_assets[index].id;
        if (request.mode == SelectionMode::Remove)
        {
            next_selection.erase(id);
        }
        else if (request.mode == SelectionMode::Toggle)
        {
            if (!next_selection.erase(id))
            {
                next_selection.insert(id);
            }
        }
        else
        {
            next_selection.insert(id);
        }
    }

    if (focus < 0)
    {
        const bool keeps_current =
            m_selected_asset >= 0 &&
            m_selected_asset < static_cast<int>(m_assets.size()) &&
            next_selection.count(
                m_assets[m_selected_asset].id
            );
        if (keeps_current)
        {
            focus = m_selected_asset;
        }
        else if (!next_selection.empty())
        {
            for (size_t index = 0; index < m_assets.size(); index++)
            {
                if (next_selection.count(m_assets[index].id))
                {
                    focus = static_cast<int>(index);
                    break;
                }
            }
        }
    }
    if (
        focus >= 0 &&
        (
            request.mode == SelectionMode::Remove ||
            request.mode == SelectionMode::Toggle
        ) &&
        !next_selection.count(m_assets[focus].id)
    )
    {
        error = "focus_id must remain selected";
        return false;
    }
    const bool changes_focus = focus != m_selected_asset;
    if (
        changes_focus &&
        (m_working_modified || m_working_lods_built)
    )
    {
        error =
            "save or revert unsaved mesh changes before changing focus";
        return false;
    }
    if (focus >= 0)
    {
        next_selection.insert(m_assets[focus].id);
    }

    m_selected_assets = move(next_selection);

    if (focus >= 0)
    {
        m_selected_asset = focus;
        m_selection_anchor = focus;
        m_selected_dependency_path.clear();
        if (changes_focus)
        {
            LoadSelectedAsset();
            if (m_loaded_path.empty())
            {
                error = m_status;
                return false;
            }
        }
    }
    else
    {
        m_selected_asset = -1;
        m_selection_anchor = -1;
        m_selected_dependency_path.clear();
        ClearLoadedAsset();
    }

    m_status =
        to_string(m_selected_assets.size()) +
        (
            m_selected_assets.size() == 1
                ? " asset selected"
                : " assets selected"
        );
    return true;
}

bool AssetViewer::SetDisplay(
    const DisplayRequest& request,
    string& error
)
{
    if (request.preview_lod)
    {
        if (!m_working_editable)
        {
            error = "a previewed editable mesh is required";
            return false;
        }
        if (!m_working_lods_scanned)
        {
            m_working_lods_scanned = true;
            LoadExistingLods();
        }
        const int lod_count = max(
            1,
            static_cast<int>(m_working_lods.size())
        );
        if (
            *request.preview_lod < 0 ||
            *request.preview_lod >= lod_count
        )
        {
            error = "preview_lod is out of range";
            return false;
        }
    }
    if (request.frame && !HasPreviewContent())
    {
        error = "load a preview before framing it";
        return false;
    }

    if (request.reset)
    {
        m_preview_mode = 0;
        m_preview_backdrop = 3;
        m_preview_show_stats = true;
        m_preview_auto_rotate = false;
        m_preview_yaw = 0.65f;
        m_preview_pitch = 0.35f;
        m_preview_zoom = 1.0f;
        m_preview_lod = 0;
        m_texture_pan = math::Vector2::Zero;
    }
    if (request.shading)
    {
        m_preview_mode = static_cast<int>(*request.shading);
    }
    if (request.backdrop)
    {
        m_preview_backdrop = static_cast<int>(*request.backdrop);
    }
    if (request.show_stats)
    {
        m_preview_show_stats = *request.show_stats;
    }
    if (request.auto_rotate)
    {
        m_preview_auto_rotate = *request.auto_rotate;
    }
    if (request.preview_lod)
    {
        m_preview_lod = *request.preview_lod;
        FlattenWorkingGeometry();
        RefreshPreviewMeshGeometry();
    }
    if (request.frame)
    {
        RefreshPreviewBounds();
        m_preview_zoom = 1.0f;
    }

    m_visible = true;
    m_preview_dirty = true;
    m_status = "Updated asset preview display";
    return true;
}

bool AssetViewer::PreviewPath(
    const string& path,
    string& error
)
{
    if (path.empty())
    {
        error = "path is required";
        return false;
    }
    const string extension = lower_copy(FileSystem::GetExtensionFromFilePath(path));
    if (extension == ".glb" || extension == ".gltf" || extension == ".fbx" || extension == ".obj")
    {
        error = "model files import as entity hierarchies, not previewable paths; preview an entity that uses the model (e.g. a car wheel) with asset_viewer_preview_entity {id}";
        return false;
    }
    if (!path_is_in_viewer_roots(path))
    {
        error =
            "path must be inside the asset library or mcp blockout, for anything else in the world use asset_viewer_preview_entity {id}";
        return false;
    }
    if (!FileSystem::Exists(path))
    {
        error = "linked resource was not found";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        if (
            normalized_path(path) ==
            normalized_path(m_loaded_path)
        )
        {
            m_visible = true;
            m_status =
                "Preview already loaded with unsaved mesh changes";
            return true;
        }
        error =
            "save or revert unsaved mesh changes before changing preview";
        return false;
    }
    for (size_t index = 0; index < m_assets.size(); index++)
    {
        AssetEntry& asset = m_assets[index];
        if (
            normalized_path(asset.path) !=
            normalized_path(path)
        )
        {
            continue;
        }
        m_selected_asset = static_cast<int>(index);
        m_selected_assets.clear();
        m_selected_assets.insert(asset.id);
        m_selection_anchor = m_selected_asset;
        LoadSelectedAsset();
        if (m_loaded_path.empty())
        {
            error = m_status;
            return false;
        }
        m_visible = true;
        return true;
    }
    const string type = asset_type_from_path(path);
    if (
        type != "mesh" &&
        type != "material" &&
        type != "texture"
    )
    {
        error = "linked resource type is not previewable";
        return false;
    }
    LoadDependencyPreview(path);
    if (m_loaded_path.empty())
    {
        error = m_status;
        return false;
    }
    m_visible = true;
    return true;
}

bool AssetViewer::Reload(string& error)
{
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before reloading";
        return false;
    }
    if (!m_selected_dependency_path.empty())
    {
        const string path = m_selected_dependency_path;
        LoadDependencyPreview(path);
    }
    else if (
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size())
    )
    {
        LoadSelectedAsset(false, true);
    }
    else
    {
        error = "select an asset or linked path before reloading";
        return false;
    }
    if (m_loaded_path.empty())
    {
        error = m_status;
        return false;
    }
    return true;
}

bool AssetViewer::EditMesh(
    const MeshRequest& request,
    string& error
)
{
    if (
        !m_working_editable ||
        m_working_sub_meshes.empty()
    )
    {
        error = "a previewed editable mesh or prefab is required";
        return false;
    }
    if (
        request.target_ratio &&
        (
            *request.target_ratio < 0.01f ||
            *request.target_ratio > 1.0f
        )
    )
    {
        error = "target_ratio must be between 0.01 and 1";
        return false;
    }
    if (
        request.target_ratio &&
        request.action != MeshAction::Simplify &&
        request.action != MeshAction::SetOptions
    )
    {
        error =
            "target_ratio is only valid for simplify or set_options";
        return false;
    }
    if (
        request.generate_lods &&
        request.action != MeshAction::SetOptions
    )
    {
        error =
            "generate_lods_on_save is only valid for set_options";
        return false;
    }
    if (
        request.preview_lod &&
        request.action != MeshAction::SetOptions
    )
    {
        error =
            "preview_lod is only valid for set_options";
        return false;
    }
    if (
        request.action == MeshAction::Revert &&
        !request.confirm
    )
    {
        error =
            "confirm=true is required to discard mesh changes";
        return false;
    }
    if (request.preview_lod)
    {
        DisplayRequest display;
        display.preview_lod = request.preview_lod;
        if (!SetDisplay(display, error))
        {
            return false;
        }
    }
    if (request.target_ratio)
    {
        m_target_ratio = *request.target_ratio;
    }
    if (request.generate_lods)
    {
        m_working_generate_lods = *request.generate_lods;
    }

    switch (request.action)
    {
        case MeshAction::Simplify:
            SimplifyWorkingGeometry(m_target_ratio);
            m_status = "Simplified working mesh geometry";
            break;
        case MeshAction::Optimize:
            OptimizeWorkingGeometry();
            m_status = "Optimized working mesh geometry";
            break;
        case MeshAction::BuildLods:
            BuildWorkingLods();
            if (m_working_lods.empty())
            {
                error = "the mesh could not produce a reduced lod level";
                return false;
            }
            m_status = "Built working mesh lods";
            break;
        case MeshAction::Revert:
        {
            const auto before = CaptureGeometry();
            LoadWorkingGeometry();
            RecordGeometry(before);
            m_status = "Reverted unsaved mesh changes";
        }
            break;
        case MeshAction::SetOptions:
            m_status = "Updated mesh edit options";
            break;
    }

    return true;
}

bool AssetViewer::SaveMesh(
    const bool confirm,
    string& error
)
{
    if (!confirm)
    {
        error = "confirm=true is required to overwrite the mesh";
        return false;
    }
    if (
        !m_working_modified &&
        !m_working_lods_built
    )
    {
        error = "there are no unsaved mesh changes";
        return false;
    }
    if (!SaveWorkingGeometry())
    {
        error =
            m_status.empty()
                ? "mesh changes could not be saved"
                : m_status;
        return false;
    }
    return true;
}

bool AssetViewer::Rename(
    const string& asset_id,
    const string& linked_path,
    const string& new_name,
    string& error
)
{
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before renaming";
        return false;
    }
    if (new_name.empty())
    {
        error = "new_name is required";
        return false;
    }
    if (asset_id.empty() == linked_path.empty())
    {
        error = "provide exactly one of asset_id or linked_path";
        return false;
    }

    if (!linked_path.empty())
    {
        const string library_root =
            FileSystem::GetDirectoryFromFilePath(m_catalog_path);
        if (
            linked_path.find("..") != string::npos ||
            library_root.empty() ||
            !path_is_within(linked_path, library_root) ||
            !resolved_path_is_within(
                linked_path,
                library_root
            )
        )
        {
            error = "linked_path must be inside the active asset library";
            return false;
        }
        if (
            (m_working_modified || m_working_lods_built) &&
            normalized_path(linked_path) ==
                normalized_path(m_loaded_path)
        )
        {
            error =
                "save or revert unsaved mesh changes before renaming it";
            return false;
        }
        const string renamed_path =
            FileSystem::GetDirectoryFromFilePath(linked_path) +
            sanitize_asset_name(new_name) +
            FileSystem::GetExtensionFromFilePath(linked_path);
        const bool previewing =
            normalized_path(linked_path) ==
            normalized_path(m_loaded_path);
        if (!RenameAssetFile(linked_path, new_name))
        {
            error = m_status;
            return false;
        }
        const string status = m_status;
        RefreshCatalog(true);
        if (previewing && FileSystem::Exists(renamed_path))
        {
            LoadDependencyPreview(renamed_path);
        }
        m_status = status;
        return true;
    }

    int index = -1;
    for (size_t candidate = 0; candidate < m_assets.size(); candidate++)
    {
        if (m_assets[candidate].id == asset_id)
        {
            index = static_cast<int>(candidate);
            break;
        }
    }
    if (index < 0)
    {
        error = "asset was not found in the active catalog";
        return false;
    }
    if (
        (m_working_modified || m_working_lods_built) &&
        m_selected_asset == index
    )
    {
        error =
            "save or revert unsaved mesh changes before renaming it";
        return false;
    }
    if (!RenameAsset(index, new_name))
    {
        error = m_status;
        return false;
    }
    return true;
}

bool AssetViewer::Delete(
    const vector<string>& asset_ids,
    const string& linked_path,
    const bool confirm,
    string& error
)
{
    if (!confirm)
    {
        error = "confirm=true is required to delete assets";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before deleting";
        return false;
    }
    if (asset_ids.empty() == linked_path.empty())
    {
        error = "provide asset_ids or linked_path, but not both";
        return false;
    }

    if (!linked_path.empty())
    {
        const string library_root =
            FileSystem::GetDirectoryFromFilePath(m_catalog_path);
        if (
            linked_path.find("..") != string::npos ||
            library_root.empty() ||
            !path_is_within(linked_path, library_root) ||
            !resolved_path_is_within(
                linked_path,
                library_root
            )
        )
        {
            error = "linked_path must be inside the active asset library";
            return false;
        }
        if (!FileSystem::Exists(linked_path))
        {
            error = "linked file was not found";
            return false;
        }
        if (
            (m_working_modified || m_working_lods_built) &&
            normalized_path(linked_path) ==
                normalized_path(m_loaded_path)
        )
        {
            error =
                "save or revert unsaved mesh changes before deleting it";
            return false;
        }
        FileSystem::Delete(linked_path);
        if (FileSystem::Exists(linked_path))
        {
            error = "failed to delete linked file";
            return false;
        }
        if (
            normalized_path(m_loaded_path) ==
            normalized_path(linked_path)
        )
        {
            ClearLoadedAsset();
        }
        RefreshCatalog(true);
        m_status = "Deleted " + linked_path;
        return true;
    }

    for (const string& id : asset_ids)
    {
        const auto found = find_if(
            m_assets.begin(),
            m_assets.end(),
            [&id](const AssetEntry& asset)
            {
                return asset.id == id;
            }
        );
        if (found == m_assets.end())
        {
            error = "asset was not found: " + id;
            return false;
        }
        if (
            (m_working_modified || m_working_lods_built) &&
            normalized_path(found->path) ==
                normalized_path(m_loaded_path)
        )
        {
            error =
                "save or revert unsaved mesh changes before deleting it";
            return false;
        }
    }
    if (!DeleteAssets(asset_ids))
    {
        error = m_status;
        return false;
    }
    return true;
}

bool AssetViewer::ScanCleanup(
    CleanupSummary& result,
    string& error
)
{
    ScanLibraryCleanup();
    if (!m_cleanup.error.empty())
    {
        error = m_cleanup.error;
        return false;
    }

    result = CleanupSummary();
    result.orphan_files = m_cleanup.orphan_files;
    result.bytes = m_cleanup.bytes;
    result.generation = m_cleanup_generation;
    return true;
}

bool AssetViewer::ApplyCleanup(
    const uint64_t generation,
    const bool confirm,
    string& error
)
{
    if (!confirm)
    {
        error = "confirm=true is required to apply cleanup";
        return false;
    }
    if (
        generation == 0 ||
        generation != m_cleanup_generation
    )
    {
        error =
            "cleanup generation is stale, scan again before applying";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before applying cleanup";
        return false;
    }
    RefreshCatalog(false);
    if (!m_cleanup.scanned)
    {
        error = "run asset_viewer_cleanup_scan before applying cleanup";
        return false;
    }
    if (generation != m_cleanup_generation)
    {
        error =
            "cleanup generation is stale, scan again before applying";
        return false;
    }
    if (
        CleanupFileSignatures() !=
        m_cleanup.file_signatures
    )
    {
        error =
            "cleanup files changed since the scan, scan again";
        return false;
    }
    if (!ApplyLibraryCleanup())
    {
        error =
            m_cleanup.error.empty()
                ? m_status
                : m_cleanup.error;
        return false;
    }
    m_cleanup_generation++;
    return true;
}

void AssetViewer::SetPanelVisible(bool visible)
{
    m_visible = visible;
    if (!m_visible)
    {
        // the preview survives the panel being hidden, see OnInvisible, a driver that closes the panel is
        // not saying it is finished with the asset it was reviewing
        Renderer::InvalidateSecondaryView();
    }
    if (!m_working_modified && !m_working_lods_built)
    {
        RefreshCatalog(m_visible);
    }
}

bool AssetViewer::SelectAsset(
    const string& query,
    string& error
)
{
    if (m_working_modified || m_working_lods_built)
    {
        const bool same_asset =
            m_selected_asset >= 0 &&
            m_selected_asset < static_cast<int>(m_assets.size()) &&
            (
                m_assets[m_selected_asset].id == query ||
                m_assets[m_selected_asset].name == query
            );
        if (same_asset)
        {
            m_visible = true;
            return true;
        }
        error =
            "save or revert unsaved mesh changes before selecting another asset";
        return false;
    }

    m_visible = true;
    RefreshCatalog(true);
    if (query.empty())
    {
        error = "asset_id or name is required";
        return false;
    }

    m_selected_asset = -1;
    for (size_t index = 0; index < m_assets.size(); index++)
    {
        const AssetEntry& asset = m_assets[index];
        if (
            asset.id == query ||
            asset.name == query
        )
        {
            m_selected_asset = static_cast<int>(index);
            break;
        }
    }
    if (m_selected_asset < 0)
    {
        error = "asset was not found in the active catalog";
        return false;
    }

    m_selected_assets.clear();
    m_selected_assets.insert(
        m_assets[m_selected_asset].id
    );
    m_selection_anchor = m_selected_asset;
    LoadSelectedAsset(true, true);
    if (m_loaded_path.empty())
    {
        error = m_status;
        return false;
    }
    return true;
}

bool AssetViewer::PreviewEntityById(
    uint64_t entity_id,
    string& error
)
{
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before previewing an entity";
        return false;
    }
    Entity* entity = World::GetEntityById(entity_id);
    if (!entity)
    {
        error = "preview entity was not found";
        return false;
    }

    PreviewEntity(entity);
    return true;
}

void AssetViewer::SetPreviewView(const ViewRequest& request)
{
    if (request.view)
    {
        switch (*request.view)
        {
            case PreviewView::Front:
                m_preview_yaw = 0.0f;
                m_preview_pitch = 0.0f;
                break;
            case PreviewView::Back:
                m_preview_yaw = 3.14159265f;
                m_preview_pitch = 0.0f;
                break;
            case PreviewView::Left:
                m_preview_yaw = -1.57079633f;
                m_preview_pitch = 0.0f;
                break;
            case PreviewView::Right:
                m_preview_yaw = 1.57079633f;
                m_preview_pitch = 0.0f;
                break;
            case PreviewView::Top:
                m_preview_yaw = 0.0f;
                m_preview_pitch = 1.45f;
                break;
            case PreviewView::Bottom:
                m_preview_yaw = 0.0f;
                m_preview_pitch = -1.45f;
                break;
            case PreviewView::Perspective:
                m_preview_yaw = 0.65f;
                m_preview_pitch = 0.35f;
                break;
        }
    }

    // an explicit angle wins over the preset, a caller can nudge one axis of a named view
    if (request.yaw)
    {
        m_preview_yaw = *request.yaw;
    }
    if (request.pitch)
    {
        m_preview_pitch = clamp(
            *request.pitch,
            -1.45f,
            1.45f
        );
    }
    if (request.zoom)
    {
        m_preview_zoom = clamp(
            *request.zoom,
            0.2f,
            8.0f
        );
    }

    m_visible = true;
    m_preview_dirty = true;
}

bool AssetViewer::HasPreviewContent() const
{
    return
        m_mesh ||
        !m_working_meshes.empty() ||
        m_material ||
        m_texture ||
        PreviewRoot();
}

bool AssetViewer::CapturePreview(
    const CaptureRequest& request,
    CaptureResult& result,
    string& error
)
{
    if (!HasPreviewContent())
    {
        error =
            "load an asset preview before taking a screenshot";
        return false;
    }

    result.width = clamp(request.width, 256u, 2048u);
    result.height = clamp(request.height, 256u, 2048u);

    // the render lands on a later frame, so the caller has nothing to look at except this file. an older
    // capture at the same path has to be gone before the request is made, otherwise whoever is waiting for
    // the image reads the previous one and believes it is looking at the asset as it is now. the previous
    // capture is saved on a worker thread and can still hold the file, which is why the delete is checked
    if (FileSystem::Exists(request.path))
    {
        FileSystem::Delete(request.path);
        if (FileSystem::Exists(request.path))
        {
            error =
                "the previous screenshot at this path is still being written, "
                "capture again in a moment or use a different path";
            return false;
        }
    }

    const int mode = static_cast<int>(request.shading);
    const int backdrop = static_cast<int>(request.backdrop);
    const int mode_restore = m_preview_mode;
    const int backdrop_restore = m_preview_backdrop;
    m_preview_mode = mode;
    m_preview_backdrop = backdrop;
    const bool saved = SavePreviewScreenshot(
        request.path,
        result.width,
        result.height
    );
    m_preview_mode = mode_restore;
    m_preview_backdrop = backdrop_restore;

    // the request the renderer holds already carries the capture settings, marking the panel dirty
    // brings it back to what the user had once that capture has been consumed
    if (
        mode != mode_restore ||
        backdrop != backdrop_restore
    )
    {
        m_preview_dirty = true;
    }
    if (!saved)
    {
        error =
            "asset preview screenshot could not be saved";
        return false;
    }

    // a texture is written from the copy already in memory, geometry needs a render first
    result.ready = m_texture != nullptr;
    return true;
}

AssetViewer::PreviewStatus AssetViewer::GetPreviewStatus() const
{
    PreviewStatus status;
    status.visible = m_visible;
    status.loaded_path = m_loaded_path;
    status.dependency_path = m_selected_dependency_path;
    status.catalog_path = m_catalog_path;
    status.catalog_count = m_assets.size();
    status.status_message = m_status;
    status.yaw = m_preview_yaw;
    status.pitch = m_preview_pitch;
    status.zoom = m_preview_zoom;
    status.shading =
        static_cast<PreviewShading>(m_preview_mode);
    status.backdrop =
        static_cast<PreviewBackdrop>(m_preview_backdrop);
    status.show_stats = m_preview_show_stats;
    status.auto_rotate = m_preview_auto_rotate;
    status.preview_lod = m_preview_lod;
    status.lod_count = max(
        1,
        static_cast<int>(m_working_lods.size())
    );
    status.mesh_target_ratio = m_target_ratio;
    status.mesh_editable = m_working_editable;
    status.mesh_modified = m_working_modified;
    status.mesh_lods_built = m_working_lods_built;
    status.mesh_lods_attempted = m_working_lods_attempted;
    status.mesh_generate_lods = m_working_generate_lods;
    status.has_preview_content = HasPreviewContent();
    status.selected_asset_ids.assign(
        m_selected_assets.begin(),
        m_selected_assets.end()
    );
    sort(
        status.selected_asset_ids.begin(),
        status.selected_asset_ids.end()
    );
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        status.mesh_source_vertices +=
            working.source_vertex_count;
        status.mesh_source_indices +=
            working.source_index_count;
        status.mesh_working_vertices += working.vertices.size();
        status.mesh_working_indices += working.indices.size();
    }

    if (
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size())
    )
    {
        const AssetEntry& asset = m_assets[m_selected_asset];
        status.selected_asset_id = asset.id;
        status.selected_asset_name = asset.name;
        if (
            find(
                status.selected_asset_ids.begin(),
                status.selected_asset_ids.end(),
                asset.id
            ) == status.selected_asset_ids.end()
        )
        {
            status.selected_asset_ids.push_back(asset.id);
            sort(
                status.selected_asset_ids.begin(),
                status.selected_asset_ids.end()
            );
        }
    }

    if (PreviewRoot() && !m_preview_root_owned)
    {
        status.previewed_entity_id = m_preview_root_id;
    }

    const auto [vertex_count, index_count] =
        GetPreviewGeometryCounts();
    status.vertex_count = vertex_count;
    status.index_count = index_count;
    return status;
}

bool AssetViewer::SavePreviewScreenshot(
    const string& path,
    const uint32_t width,
    const uint32_t height
)
{
    // a texture is already an image, resampling it would only lose detail
    if (m_texture && !m_loaded_path.empty())
    {
        const string directory =
            FileSystem::GetDirectoryFromFilePath(path);
        if (!directory.empty())
        {
            FileSystem::CreateDirectory_(directory);
        }

        std::error_code error;
        std::filesystem::copy_file(
            m_loaded_path,
            path,
            std::filesystem::copy_options::overwrite_existing,
            error
        );
        return !error && FileSystem::Exists(path);
    }

    if (!PreviewRoot())
    {
        return false;
    }

    // the capture is satisfied by the first secondary frame at or after the generation it was registered
    // against, and the renderer consumes requests on its own thread. registering the capture after its render
    // had already been consumed leaves it waiting for a frame that is in the past, and nothing re-requests
    // one unless the panel happens to be open and dirty, so a closed panel waits forever
    //
    // so the render is asked for, the capture is registered against it, and one more render is queued behind
    // the capture, which is the frame that is guaranteed to arrive after it and write the file
    if (!RequestPreviewRender(width, height))
    {
        return false;
    }
    if (!Renderer::ScreenshotSecondary(path))
    {
        return false;
    }
    return RequestPreviewRender(width, height);
}
