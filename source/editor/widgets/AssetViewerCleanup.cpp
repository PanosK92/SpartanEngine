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

vector<string> AssetViewer::CleanupFileSignatures() const
{
    vector<string> paths = m_cleanup.orphan_files;
    paths.insert(
        paths.end(),
        m_cleanup.reference_files.begin(),
        m_cleanup.reference_files.end()
    );
    sort(paths.begin(), paths.end());
    paths.erase(unique(paths.begin(), paths.end()), paths.end());

    vector<string> signatures;
    signatures.reserve(paths.size());
    for (const string& path : paths)
    {
        error_code error;
        const filesystem::path resolved =
            filesystem::weakly_canonical(path, error);
        error_code size_error;
        const uintmax_t size =
            filesystem::file_size(path, size_error);
        error_code time_error;
        const auto write_time =
            filesystem::last_write_time(path, time_error);
        signatures.push_back(
            (
                error
                    ? normalized_path(path)
                    : normalized_path(resolved.generic_string())
            ) +
            "|" +
            to_string(size_error ? 0 : size) +
            "|" +
            (
                time_error
                    ? "missing"
                    : to_string(
                        write_time.time_since_epoch().count()
                    )
            )
        );
    }
    return signatures;
}

void AssetViewer::ScanLibraryCleanup()
{
    m_cleanup_generation++;
    m_cleanup = CleanupPlan();
    if (m_catalog_path.empty())
    {
        m_cleanup.error = "No asset catalog to clean up.";
        m_cleanup.scanned = true;
        return;
    }

    const string library_root =
        FileSystem::GetDirectoryFromFilePath(m_catalog_path);
    string source;
    if (!FileSystem::ReadFile(m_catalog_path, source))
    {
        m_cleanup.error =
            "The catalog could not be read from " + m_catalog_path;
        m_cleanup.scanned = true;
        return;
    }
    m_cleanup.reference_files.push_back(m_catalog_path);

    string parse_error;
    JsonValue root;
    const bool parsed = mcp_json::parse(source, root, parse_error);
    const JsonValue* schema_version =
        root.find("schema_version");
    const JsonValue* assets = root.find("assets");
    if (
        !parsed ||
        root.type != mcp_json::kind::object ||
        !schema_version ||
        static_cast<int>(schema_version->number_or(0.0)) != 2 ||
        !assets ||
        assets->type != mcp_json::kind::object
    )
    {
        m_cleanup.error =
            "The asset catalog schema is unsupported or invalid, cleanup aborted.";
        m_cleanup.scanned = true;
        return;
    }

    // the reachable set starts from what the project still needs, everything else in the library is
    // a leftover, references are followed because a kept prefab pulls in meshes, materials and
    // textures stored outside the primary asset folders
    unordered_set<string> reachable;
    vector<string> frontier;
    const auto mark =
        [&reachable, &frontier](const string& path)
    {
        if (path.empty())
        {
            return;
        }

        const string key = normalized_path(path);
        if (reachable.insert(key).second)
        {
            frontier.push_back(path);
        }
    };

    // worlds are roots, whichever one is loaded, a world that is not open right now still owns its
    // references and must not lose them
    {
        // the library root carries a trailing slash, taking the parent of it as is returns itself
        string trimmed = library_root;
        while (!trimmed.empty() && trimmed.back() == '/')
        {
            trimmed.pop_back();
        }

        const string project_root =
            FileSystem::GetDirectoryFromFilePath(trimmed);
        m_cleanup.reference_files.push_back(
            project_root.empty() ? "." : project_root
        );
        error_code error;
        filesystem::recursive_directory_iterator iterator(
            filesystem::path(
                project_root.empty() ? "." : project_root
            ),
            error
        );
        for (
            ;
            !error &&
            iterator != filesystem::recursive_directory_iterator();
            iterator.increment(error)
        )
        {
            const filesystem::directory_entry& item = *iterator;
            if (item.is_directory(error) && !error)
            {
                m_cleanup.reference_files.push_back(
                    item.path().generic_string()
                );
                continue;
            }
            if (!item.is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }

            const string path = item.path().generic_string();
            if (path_is_within(path, library_root))
            {
                continue;
            }
            if (
                lower_copy(
                    FileSystem::GetExtensionFromFilePath(path)
                ) == EXTENSION_WORLD
            )
            {
                mark(path);
            }
        }
    }

    // catalog assets are roots, including their source, thumbnail and dependencies
    for (const auto& [asset_id, entry] : assets->object_items)
    {
        for (
            const char* key :
            {
                "path",
                "source_path",
                "thumbnail_path"
            }
        )
        {
            if (const JsonValue* value = entry.find(key))
            {
                mark(value->string_or(""));
            }
        }
        if (
            const JsonValue* dependencies =
                entry.find("dependencies");
            dependencies &&
            dependencies->type == mcp_json::kind::array
        )
        {
            for (const JsonValue& dependency : dependencies->array_items)
            {
                mark(dependency.string_or(""));
            }
        }
    }
    // follow references out of everything reachable so far
    while (!frontier.empty())
    {
        const string path = frontier.back();
        frontier.pop_back();
        const string extension = lower_copy(
            FileSystem::GetExtensionFromFilePath(path)
        );
        if (
            extension != EXTENSION_WORLD &&
            extension != EXTENSION_PREFAB &&
            extension != EXTENSION_MATERIAL
        )
        {
            continue;
        }

        m_cleanup.reference_files.push_back(path);
        pugi::xml_document document;
        if (!document.load_file(path.c_str()))
        {
            continue;
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
                    "texture_path",
                    "prefab_path"
                }
            )
            {
                mark(
                    node.attribute(attribute_name).as_string()
                );
            }
        }
    }

    // a material xml never stores its packed map, the runtime rebuilds that slot, so a plain
    // reference walk marks the colour and normal maps and misses the packed one next to them, the
    // packed map holds baked roughness and metalness that no other file on disk carries
    {
        const vector<string> reachable_snapshot(
            reachable.begin(),
            reachable.end()
        );
        for (const string& path : reachable_snapshot)
        {
            const string extension = lower_copy(
                FileSystem::GetExtensionFromFilePath(path)
            );
            if (asset_type_from_path(path) != "texture")
            {
                continue;
            }

            string base =
                FileSystem::GetDirectoryFromFilePath(path) +
                FileSystem::
                    GetFileNameWithoutExtensionFromFilePath(path);
            const vector<string> suffixes =
            {
                "_normal",
                "_packed"
            };
            for (const string& suffix : suffixes)
            {
                if (
                    base.size() > suffix.size() &&
                    base.compare(
                        base.size() - suffix.size(),
                        suffix.size(),
                        suffix
                    ) == 0
                )
                {
                    base.erase(base.size() - suffix.size());
                    break;
                }
            }

            for (const string& suffix : suffixes)
            {
                const string sibling = base + suffix + extension;
                if (FileSystem::Exists(sibling))
                {
                    mark(sibling);
                }
            }
        }
    }

    // sweep, anything in the library that nothing reachable needs is a leftover
    unordered_set<string> planned;
    const auto file_size =
        [](const string& path)
    {
        error_code error;
        const uintmax_t size =
            filesystem::file_size(filesystem::path(path), error);
        return error ? 0ull : static_cast<uint64_t>(size);
    };

    {
        m_cleanup.reference_files.push_back(library_root);
        error_code error;
        filesystem::recursive_directory_iterator iterator(
            filesystem::path(library_root),
            error
        );
        for (
            ;
            !error &&
            iterator != filesystem::recursive_directory_iterator();
            iterator.increment(error)
        )
        {
            const filesystem::directory_entry& item = *iterator;
            if (item.is_directory(error) && !error)
            {
                m_cleanup.reference_files.push_back(
                    item.path().generic_string()
                );
                continue;
            }
            if (!item.is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }

            const string path = item.path().generic_string();
            if (
                normalized_path(path) ==
                    normalized_path(m_catalog_path) ||
                reachable.count(normalized_path(path)) ||
                !planned.insert(normalized_path(path)).second
            )
            {
                continue;
            }

            m_cleanup.orphan_files.push_back(path);
            m_cleanup.bytes += file_size(path);
        }
    }

    m_cleanup.file_signatures = CleanupFileSignatures();
    m_cleanup.scanned = true;
}

bool AssetViewer::ApplyLibraryCleanup()
{
    if (!m_cleanup.scanned || !m_cleanup.error.empty())
    {
        return false;
    }

    const string library_root =
        FileSystem::GetDirectoryFromFilePath(m_catalog_path);
    uint32_t deleted = 0;
    uint32_t failed = 0;
    const auto remove_file =
        [&](const string& path)
    {
        if (
            !path_is_within(path, library_root) ||
            !resolved_path_is_within(path, library_root)
        )
        {
            return;
        }
        if (!FileSystem::Exists(path))
        {
            return;
        }

        if (FileSystem::Delete(path))
        {
            deleted++;
        }
        else
        {
            failed++;
        }
    };

    for (const string& path : m_cleanup.orphan_files)
    {
        remove_file(path);
    }

    m_cleanup = CleanupPlan();
    ClearLoadedAsset();
    RefreshCatalog(true);
    m_status =
        "Cleanup removed " +
        to_string(deleted) +
        (deleted == 1 ? " file" : " files") +
        (
            failed == 0
                ? ""
                : ", " + to_string(failed) + " could not be removed"
        );
    return failed == 0;
}

void AssetViewer::DrawCleanupConfirmation()
{
    if (!m_cleanup.scanned)
    {
        return;
    }

    const char* title = "Clean up asset library?";
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
        if (!m_cleanup.error.empty())
        {
            ImGui::TextUnformatted(m_cleanup.error.c_str());
            ImGui::Spacing();
            if (ImGuiSp::button("Close"))
            {
                m_cleanup = CleanupPlan();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        const size_t total = m_cleanup.orphan_files.size();
        if (total == 0)
        {
            ImGui::TextUnformatted(
                "The asset library is already clean."
            );
            ImGui::TextDisabled(
                "Every file is reachable from a world or catalog asset."
            );
            ImGui::Spacing();
            if (ImGuiSp::button("Close"))
            {
                m_cleanup = CleanupPlan();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        ImGui::Text(
            "Delete %zu files, %.1f MB?",
            total,
            static_cast<double>(m_cleanup.bytes) /
                (1024.0 * 1024.0)
        );
        ImGui::TextDisabled(
            "Catalog assets and anything referenced by a world are kept."
        );
        ImGui::Spacing();

        ImGui::BeginChild(
            "##cleanup_list",
            ImVec2(560.0f * ui_scale(), 240.0f * ui_scale()),
            ImGuiChildFlags_Borders
        );
        if (!m_cleanup.orphan_files.empty())
        {
            ImGui::TextUnformatted("UNREFERENCED LEFTOVERS");
            ImGui::Separator();
            for (const string& path : m_cleanup.orphan_files)
            {
                ImGui::TextDisabled("%s", path.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        if (ImGuiSp::button("Delete permanently"))
        {
            ApplyLibraryCleanup();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            m_cleanup = CleanupPlan();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
    {
        m_cleanup = CleanupPlan();
    }
}
