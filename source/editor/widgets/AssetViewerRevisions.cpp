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
    const JsonValue* json_object_value(
        const JsonValue& root,
        const char* key
    )
    {
        const JsonValue* value = root.find(key);
        if (value)
        {
            return value;
        }

        const JsonValue* revision = root.find("revision");
        return
            revision &&
            revision->type == mcp_json::kind::object ?
            revision->find(key) :
            nullptr;
    }

    string json_string_any(
        const JsonValue& root,
        const initializer_list<const char*>& keys
    )
    {
        for (const char* key : keys)
        {
            const JsonValue* value = json_object_value(root, key);
            if (value && value->type == mcp_json::kind::string)
            {
                return value->string_value;
            }
        }
        return "";
    }

    uint64_t json_uint_any(
        const JsonValue& root,
        const initializer_list<const char*>& keys
    )
    {
        for (const char* key : keys)
        {
            const JsonValue* value = json_object_value(root, key);
            if (
                value &&
                value->type == mcp_json::kind::number &&
                value->number_value > 0.0
            )
            {
                return static_cast<uint64_t>(value->number_value);
            }
        }
        return 0;
    }

    pair<uint64_t, uint64_t> prefab_counts(const string& path)
    {
        pugi::xml_document document;
        if (!document.load_file(path.c_str()))
        {
            return {};
        }

        const pugi::xml_node prefab = document.child("Prefab");
        if (!prefab)
        {
            return {};
        }

        unordered_set<string> dependencies;
        vector<pugi::xml_node> pending = { prefab };
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
                if (!reference.empty())
                {
                    dependencies.insert(normalized_path(reference));
                }
            }
        }

        return
        {
            static_cast<uint64_t>(
                1 + count_prefab_entities(prefab)
            ),
            static_cast<uint64_t>(dependencies.size())
        };
    }

    string absolute_manifest_path(
        const string& value,
        const string& manifest_path
    )
    {
        filesystem::path path(value);
        if (path.is_relative())
        {
            path =
                filesystem::path(manifest_path).parent_path() /
                path;
        }
        return path.lexically_normal().generic_string();
    }
}

void AssetViewer::ScanRevisionCandidates(const bool force)
{
    const string candidates_root =
        World::GetLibraryResourceDirectory() +
        "candidates";
    vector<string> manifests;
    vector<string> signature_files;
    string signature;

    error_code iterator_error;
    filesystem::recursive_directory_iterator iterator(
        filesystem::path(candidates_root),
        iterator_error
    );
    for (
        ;
        !iterator_error &&
        iterator != filesystem::recursive_directory_iterator();
        iterator.increment(iterator_error)
    )
    {
        const filesystem::directory_entry& item = *iterator;
        if (!item.is_regular_file(iterator_error) || iterator_error)
        {
            iterator_error.clear();
            continue;
        }

        const string path = item.path().generic_string();
        const string file_name = lower_copy(
            FileSystem::GetFileNameFromFilePath(path)
        );
        if (
            lower_copy(
                FileSystem::GetExtensionFromFilePath(path)
            ) != ".json" ||
            file_name.find(".tmp") != string::npos
        )
        {
            continue;
        }

        signature_files.push_back(path);
        if (file_name != "manifest.json")
        {
            continue;
        }
        manifests.push_back(path);
    }
    sort(manifests.begin(), manifests.end());
    sort(signature_files.begin(), signature_files.end());
    for (const string& path : signature_files)
    {
        signature +=
            path +
            "|" +
            FileSystem::GetLastWriteTime(path) +
            ";";
    }

    if (!force && signature == m_revision_scan_signature)
    {
        return;
    }

    const RevisionStatus previous = m_revision;
    m_revision_scan_signature = signature;
    m_revision_candidates.clear();
    for (const string& manifest_path : manifests)
    {
        string source;
        JsonValue root;
        string parse_error;
        if (
            !FileSystem::ReadFile(manifest_path, source) ||
            !mcp_json::parse(source, root, parse_error) ||
            root.type != mcp_json::kind::object
        )
        {
            continue;
        }

        RevisionCandidate candidate;
        RevisionStatus& status = candidate.status;
        status.manifest_path = manifest_path;
        status.base_asset_id = json_string_any(
            root,
            { "base_asset_id", "asset_id" }
        );
        if (status.base_asset_id.empty())
        {
            const filesystem::path relative =
                filesystem::path(manifest_path).lexically_relative(
                    filesystem::path(candidates_root)
                );
            if (!relative.empty())
            {
                status.base_asset_id =
                    relative.begin()->generic_string();
            }
        }

        const string candidate_value = json_string_any(
            root,
            {
                "candidate_path",
                "prefab_path",
                "output_path",
                "path"
            }
        );
        status.generation = json_uint_any(
            root,
            { "generation", "revision_generation" }
        );
        const string state = lower_copy(
            json_string_any(root, { "status", "state" })
        );
        const JsonValue* active_value =
            json_object_value(root, "active");
        const bool manifest_active =
            !active_value ||
            active_value->type != mcp_json::kind::boolean ||
            active_value->boolean_value;
        if (
            status.base_asset_id.empty() ||
            candidate_value.empty() ||
            status.generation == 0 ||
            !manifest_active ||
            state == "applied" ||
            state == "discarded" ||
            state == "cancelled"
        )
        {
            continue;
        }

        status.candidate_path = absolute_manifest_path(
            candidate_value,
            manifest_path
        );
        const string candidate_directory =
            FileSystem::GetDirectoryFromFilePath(manifest_path);
        const string candidate_extension =
            FileSystem::GetExtensionFromFilePath(
                status.candidate_path
            );
        if (
            (
                candidate_extension != ".prefab" &&
                candidate_extension != ".mesh" &&
                candidate_extension != ".xml" &&
                candidate_extension != ".png" &&
                candidate_extension != ".jpg" &&
                candidate_extension != ".jpeg" &&
                candidate_extension != ".tga" &&
                candidate_extension != ".dds"
            ) ||
            status.candidate_path.find("..") != string::npos ||
            !path_is_within(
                status.candidate_path,
                candidate_directory
            ) ||
            !resolved_path_is_within(
                status.candidate_path,
                candidates_root
            ) ||
            !FileSystem::Exists(status.candidate_path)
        )
        {
            continue;
        }

        const auto [candidate_entities, candidate_dependencies] =
            prefab_counts(status.candidate_path);
        const uint64_t manifest_candidate_entities =
            json_uint_any(
                root,
                { "candidate_entity_count", "entity_count" }
            );
        const uint64_t manifest_candidate_dependencies =
            json_uint_any(
                root,
                {
                    "candidate_dependency_count",
                    "dependency_count"
                }
            );
        status.candidate_entity_count =
            manifest_candidate_entities != 0 ?
            manifest_candidate_entities :
            candidate_entities;
        status.candidate_dependency_count =
            manifest_candidate_dependencies != 0 ?
            manifest_candidate_dependencies :
            candidate_dependencies;

        string base_path = json_string_any(
            root,
            { "base_path", "base_prefab_path" }
        );
        if (!base_path.empty())
        {
            base_path = absolute_manifest_path(
                base_path,
                manifest_path
            );
        }
        for (const AssetEntry& asset : m_assets)
        {
            if (
                asset.id == status.base_asset_id &&
                asset.type == "prefab"
            )
            {
                base_path = asset.path;
                break;
            }
        }
        const auto [base_entities, base_dependencies] =
            prefab_counts(base_path);
        const uint64_t manifest_base_entities =
            json_uint_any(root, { "base_entity_count" });
        const uint64_t manifest_base_dependencies =
            json_uint_any(root, { "base_dependency_count" });
        status.base_entity_count =
            manifest_base_entities != 0 ?
            manifest_base_entities :
            base_entities;
        status.base_dependency_count =
            manifest_base_dependencies != 0 ?
            manifest_base_dependencies :
            base_dependencies;

        status.request_path =
            candidate_directory +
            "revision_request.json";
        if (FileSystem::Exists(status.request_path))
        {
            string request_source;
            JsonValue request_root;
            string request_error;
            if (
                FileSystem::ReadFile(
                    status.request_path,
                    request_source
                ) &&
                mcp_json::parse(request_source, request_root, request_error)
            )
            {
                const uint64_t request_generation =
                    json_uint_any(request_root, { "generation" });
                if (request_generation == status.generation)
                {
                    status.request_pending = true;
                    status.request_action = json_string_any(
                        request_root,
                        { "action" }
                    );
                }
            }
        }
        else
        {
            const string response_path =
                candidate_directory +
                "revision_response.json";
            string response_source;
            JsonValue response_root;
            string response_error;
            if (
                FileSystem::ReadFile(
                    response_path,
                    response_source
                ) &&
                mcp_json::parse(response_source, response_root, response_error) &&
                json_uint_any(
                    response_root,
                    { "generation" }
                ) == status.generation
            )
            {
                status.request_error = json_string_any(
                    response_root,
                    { "error" }
                );
            }
        }

        status.candidate_active = true;
        m_revision_candidates.push_back(move(candidate));
    }

    const string selected_id =
        m_selected_asset >= 0 &&
        m_selected_asset < static_cast<int>(m_assets.size()) ?
        m_assets[m_selected_asset].id :
        "";
    const string preferred_id =
        m_revision_previewing &&
        !previous.base_asset_id.empty() ?
        previous.base_asset_id :
        selected_id;
    RevisionStatus selected;
    for (const RevisionCandidate& candidate : m_revision_candidates)
    {
        const RevisionStatus& status = candidate.status;
        if (
            !preferred_id.empty() &&
            status.base_asset_id != preferred_id
        )
        {
            continue;
        }
        if (
            !selected.candidate_active ||
            status.generation > selected.generation
        )
        {
            selected = status;
        }
    }
    if (!selected.candidate_active)
    {
        for (const RevisionCandidate& candidate : m_revision_candidates)
        {
            if (
                !selected.candidate_active ||
                candidate.status.generation > selected.generation
            )
            {
                selected = candidate.status;
            }
        }
    }

    const bool preview_survived =
        m_revision_previewing &&
        selected.candidate_active &&
        selected.base_asset_id == previous.base_asset_id &&
        selected.generation == previous.generation;
    m_revision = selected;
    m_revision_previewing = preview_survived;
    m_revision.candidate_previewed = preview_survived;
    if (
        previous.candidate_active &&
        previous.candidate_previewed &&
        !preview_survived
    )
    {
        if (
            m_selected_asset >= 0 &&
            m_selected_asset < static_cast<int>(m_assets.size())
        )
        {
            LoadSelectedAsset(false, true);
        }
        else
        {
            ClearLoadedAsset();
        }
        m_status = "Asset revision was completed outside the editor";
    }
}

bool AssetViewer::SelectRevisionCandidate(
    const string& asset_id,
    const uint64_t generation,
    string& error
)
{
    const RevisionStatus previous = m_revision;
    const bool was_previewing = m_revision_previewing;
    ScanRevisionCandidates(true);
    RevisionStatus selected;
    for (const RevisionCandidate& candidate : m_revision_candidates)
    {
        const RevisionStatus& status = candidate.status;
        if (
            !asset_id.empty() &&
            status.base_asset_id != asset_id
        )
        {
            continue;
        }
        if (generation != 0 && status.generation != generation)
        {
            continue;
        }
        if (
            !selected.candidate_active ||
            status.generation > selected.generation
        )
        {
            selected = status;
        }
    }
    if (!selected.candidate_active)
    {
        error =
            generation == 0 ?
            "no active asset revision candidate was found" :
            "asset revision generation is stale";
        return false;
    }

    m_revision = selected;
    m_revision_previewing =
        was_previewing &&
        previous.base_asset_id == selected.base_asset_id &&
        previous.generation == selected.generation;
    m_revision.candidate_previewed = m_revision_previewing;
    return true;
}

bool AssetViewer::LoadRevisionCandidate(string& error)
{
    if (!m_revision.candidate_active)
    {
        error = "no active asset revision candidate was found";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        error =
            "save or revert unsaved mesh changes before previewing a revision";
        return false;
    }

    const string extension =
        FileSystem::GetExtensionFromFilePath(
            m_revision.candidate_path
        );
    if (extension != ".prefab")
    {
        LoadDependencyPreview(m_revision.candidate_path);
    }
    else
    {
        pugi::xml_document document;
        const pugi::xml_parse_result result =
            document.load_file(m_revision.candidate_path.c_str());
        const pugi::xml_node prefab = document.child("Prefab");
        if (!result || !prefab)
        {
            error =
                "asset revision candidate prefab could not be parsed";
            return false;
        }

        ClearLoadedAsset();
        m_selected_dependency_path.clear();
        m_prefab_entity_count =
            1 +
            count_prefab_entities(prefab);
        CollectPrefabDependencies(m_revision.candidate_path);
        CollectPrefabMeshes(m_revision.candidate_path, false);
        LoadWorkingGeometry();
        m_loaded_path = m_revision.candidate_path;
        m_loaded_write_time =
            FileSystem::GetLastWriteTime(m_loaded_path);
        RebuildPreviewScene();
    }
    if (
        !PreviewRoot() &&
        !m_mesh &&
        !m_material &&
        !m_texture
    )
    {
        error = m_status.empty()
            ? "asset revision candidate could not be previewed"
            : m_status;
        return false;
    }

    m_revision_previewing = true;
    m_revision.candidate_previewed = true;
    m_visible = true;
    m_status =
        "Previewing revision " +
        to_string(m_revision.generation) +
        " for " +
        m_revision.base_asset_id;
    return true;
}

bool AssetViewer::RequestRevision(
    const char* action,
    const uint64_t generation,
    const bool confirm,
    string& error
)
{
    if (!confirm)
    {
        error =
            string("confirm=true is required to ") +
            action +
            " an asset revision";
        return false;
    }
    if (
        generation == 0 ||
        generation != m_revision.generation
    )
    {
        error = "asset revision generation is stale";
        return false;
    }
    if (!m_revision.candidate_active)
    {
        error = "no active asset revision candidate was found";
        return false;
    }
    if (m_revision.request_pending)
    {
        error =
            "an asset revision request is already pending";
        return false;
    }

    const string request =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"action\": " +
        serialize_json_string(action) +
        ",\n"
        "  \"base_asset_id\": " +
        serialize_json_string(m_revision.base_asset_id) +
        ",\n"
        "  \"candidate_path\": " +
        serialize_json_string(m_revision.candidate_path) +
        ",\n"
        "  \"manifest_path\": " +
        serialize_json_string(m_revision.manifest_path) +
        ",\n"
        "  \"generation\": " +
        to_string(m_revision.generation) +
        ",\n"
        "  \"confirm\": true\n"
        "}\n";
    const string temporary_path =
        m_revision.request_path +
        ".tmp";
    if (!FileSystem::WriteFile(temporary_path, request))
    {
        error = "asset revision request could not be written";
        return false;
    }

    error_code rename_error;
    filesystem::rename(
        filesystem::path(temporary_path),
        filesystem::path(m_revision.request_path),
        rename_error
    );
    if (rename_error)
    {
        FileSystem::Delete(temporary_path);
        error = "asset revision request could not be published";
        return false;
    }

    m_revision.request_pending = true;
    m_revision.request_action = action;
    m_status =
        string("Requested asset revision ") +
        action +
        " for generation " +
        to_string(generation);
    return true;
}

AssetViewer::RevisionStatus AssetViewer::GetRevisionStatus(
    const string& asset_id
)
{
    string error;
    if (!asset_id.empty())
    {
        if (!SelectRevisionCandidate(asset_id, 0, error))
        {
            return {};
        }
    }
    else
    {
        ScanRevisionCandidates(false);
    }
    m_revision.candidate_previewed = m_revision_previewing;
    return m_revision;
}

bool AssetViewer::PreviewRevision(
    const string& asset_id,
    const uint64_t generation,
    string& error
)
{
    if (!SelectRevisionCandidate(asset_id, generation, error))
    {
        return false;
    }
    return LoadRevisionCandidate(error);
}

bool AssetViewer::RequestRevisionApply(
    const string& asset_id,
    const uint64_t generation,
    const bool confirm,
    string& error
)
{
    if (!SelectRevisionCandidate(asset_id, generation, error))
    {
        return false;
    }
    return RequestRevision("apply", generation, confirm, error);
}

bool AssetViewer::RequestRevisionDiscard(
    const string& asset_id,
    const uint64_t generation,
    const bool confirm,
    string& error
)
{
    if (!SelectRevisionCandidate(asset_id, generation, error))
    {
        return false;
    }
    return RequestRevision("discard", generation, confirm, error);
}

void AssetViewer::DrawRevisionBanner()
{
    if (!m_revision.candidate_active)
    {
        return;
    }

    const float scale = ui_scale();
    ImVec4 revision_background =
        ImGui::Style::color_accent_1;
    revision_background.w = 0.12f;
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        revision_background
    );
    ImGui::BeginChild(
        "##asset_revision_banner",
        ImVec2(
            0.0f,
            (
                m_revision.request_error.empty() ?
                78.0f :
                98.0f
            ) * scale
        ),
        ImGuiChildFlags_Borders
    );
    ImGui::TextUnformatted("ASSET REVISION AVAILABLE");
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s  generation %llu",
        m_revision.base_asset_id.c_str(),
        static_cast<unsigned long long>(
            m_revision.generation
        )
    );
    ImGui::TextDisabled(
        "%llu to %llu entities  |  %llu to %llu dependencies",
        static_cast<unsigned long long>(
            m_revision.base_entity_count
        ),
        static_cast<unsigned long long>(
            m_revision.candidate_entity_count
        ),
        static_cast<unsigned long long>(
            m_revision.base_dependency_count
        ),
        static_cast<unsigned long long>(
            m_revision.candidate_dependency_count
        )
    );

    if (m_revision.request_pending)
    {
        ImGui::BeginDisabled();
    }
    if (ImGuiSp::button("Preview"))
    {
        string error;
        if (!LoadRevisionCandidate(error))
        {
            m_status = error;
        }
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Apply revision"))
    {
        m_revision_confirmation_action = "apply";
        m_revision_confirmation_generation =
            m_revision.generation;
        ImGui::OpenPopup("Apply asset revision?");
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Discard revision"))
    {
        m_revision_confirmation_action = "discard";
        m_revision_confirmation_generation =
            m_revision.generation;
        ImGui::OpenPopup("Discard asset revision?");
    }
    if (m_revision.request_pending)
    {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s request pending",
            m_revision.request_action.c_str()
        );
    }
    if (!m_revision.request_error.empty())
    {
        m_status =
            "Asset revision request failed: " +
            m_revision.request_error;
        ImGui::TextUnformatted(m_status.c_str());
    }

    DrawRevisionConfirmation();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void AssetViewer::DrawRevisionConfirmation()
{
    const bool applying =
        m_revision_confirmation_action == "apply";
    const char* title = applying
        ? "Apply asset revision?"
        : "Discard asset revision?";
    if (
        m_revision_confirmation_action.empty() ||
        !ImGui::BeginPopupModal(
            title,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        return;
    }

    ImGui::TextUnformatted(
        applying
            ? "Request this revision to replace the current asset?"
            : "Request this revision candidate to be discarded?"
    );
    ImGui::TextDisabled(
        "%s  generation %llu",
        m_revision.base_asset_id.c_str(),
        static_cast<unsigned long long>(
            m_revision_confirmation_generation
        )
    );
    ImGui::Spacing();

    if (
        ImGuiSp::button(
            applying
                ? "Request apply"
                : "Request discard"
        )
    )
    {
        string error;
        const bool requested = applying
            ? RequestRevisionApply(
                m_revision.base_asset_id,
                m_revision_confirmation_generation,
                true,
                error
            )
            : RequestRevisionDiscard(
                m_revision.base_asset_id,
                m_revision_confirmation_generation,
                true,
                error
            );
        if (!requested)
        {
            m_status = error;
        }
        m_revision_confirmation_action.clear();
        m_revision_confirmation_generation = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Cancel"))
    {
        m_revision_confirmation_action.clear();
        m_revision_confirmation_generation = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
