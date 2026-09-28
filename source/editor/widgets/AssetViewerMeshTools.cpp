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
    // twelve hex characters, the same shape the generator gives its files so a baked mesh sits
    // next to its siblings without looking foreign
    string short_hash(const string& seed)
    {
        const uint64_t now = static_cast<uint64_t>(
            chrono::steady_clock::now().time_since_epoch().count()
        );
        uint64_t value = std::hash<string>{}(seed) ^ (now * 1099511628211ull);
        value ^= value >> 29;
        value *= 0xbf58476d1ce4e5b9ull;
        value ^= value >> 32;
        char buffer[16] = {};
        snprintf(
            buffer,
            sizeof(buffer),
            "%012llx",
            static_cast<unsigned long long>(value & 0xffffffffffffull)
        );
        return buffer;
    }
}

void AssetViewer::LoadWorkingGeometry()
{
    m_working_sub_meshes.clear();
    m_working_lods.clear();
    m_working_vertices.clear();
    m_working_indices.clear();
    m_working_modified = false;
    m_working_editable = false;
    m_working_lods_attempted = false;
    m_preview_lod = 0;
    if (m_working_meshes.empty())
    {
        return;
    }

    // only lod 0, the lods sit back to back with lod local indices so drawing them raw misplaces every triangle
    for (
        uint32_t mesh_index = 0;
        mesh_index < static_cast<uint32_t>(m_working_meshes.size());
        mesh_index++
    )
    {
        Mesh* mesh = m_working_meshes[mesh_index].mesh.get();
        for (
            uint32_t sub_mesh = 0;
            sub_mesh < mesh->GetSubMeshCount();
            sub_mesh++
        )
        {
            if (mesh->GetSubMesh(sub_mesh).lods.empty())
            {
                continue;
            }

            WorkingSubMesh working;
            mesh->GetGeometry(
                sub_mesh,
                &working.indices,
                &working.vertices
            );
            if (working.vertices.empty() || working.indices.size() < 3)
            {
                continue;
            }

            working.source_mesh = mesh_index;
            working.source_sub_mesh = sub_mesh;
            working.source_vertex_count =
                static_cast<uint32_t>(working.vertices.size());
            working.source_index_count =
                static_cast<uint32_t>(working.indices.size());
            m_working_sub_meshes.emplace_back(move(working));
        }
    }

    m_working_editable = !m_working_sub_meshes.empty();
    m_working_lods_scanned = false;
    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
    m_preview_dirty = true;
}

// reads the levels the mesh already carries so the picker exists the moment a mesh is selected, having
// it appear only after pressing build meant a mesh that shipped with a full chain looked like it had none
void AssetViewer::LoadExistingLods()
{
    m_working_lods.clear();
    m_working_lods_built = false;
    if (m_working_meshes.empty() || m_working_sub_meshes.empty())
    {
        return;
    }

    const auto source_mesh =
        [this](const WorkingSubMesh& working)
        {
            return m_working_meshes[working.source_mesh].mesh.get();
        };

    // the chain is only as deep as the shallowest sub mesh, a level that exists for one part and not
    // another would silently drop the parts that ran out of levels
    uint32_t depth =
        source_mesh(m_working_sub_meshes.front())->GetLodCount(
            m_working_sub_meshes.front().source_sub_mesh
        );
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        depth = min(
            depth,
            source_mesh(working)->GetLodCount(working.source_sub_mesh)
        );
    }
    if (depth < 2)
    {
        return;
    }

    vector<vector<WorkingSubMesh>> levels;
    for (uint32_t lod = 0; lod < depth; lod++)
    {
        vector<WorkingSubMesh> level;
        for (const WorkingSubMesh& source : m_working_sub_meshes)
        {
            WorkingSubMesh working;
            working.source_mesh = source.source_mesh;
            working.source_sub_mesh = source.source_sub_mesh;
            working.source_vertex_count = source.source_vertex_count;
            working.source_index_count = source.source_index_count;
            if (
                !source_mesh(source)->GetGeometryLod(
                    source.source_sub_mesh,
                    lod,
                    &working.indices,
                    &working.vertices
                ) ||
                working.vertices.empty() ||
                working.indices.size() < 3
            )
            {
                return;
            }

            level.emplace_back(move(working));
        }

        levels.emplace_back(move(level));
    }

    m_working_lods = move(levels);
}

void AssetViewer::FlattenWorkingGeometry()
{
    m_working_vertices.clear();
    m_working_indices.clear();

    // the preview draws one mesh, so whatever level is selected gets concatenated with rebased
    // indices, the per sub mesh copies stay untouched and remain what gets baked
    const vector<WorkingSubMesh>& source =
        (
            m_preview_lod > 0 &&
            m_preview_lod < static_cast<int>(m_working_lods.size())
        )
            ? m_working_lods[m_preview_lod]
            : m_working_sub_meshes;

    for (const WorkingSubMesh& working : source)
    {
        const uint32_t base =
            static_cast<uint32_t>(m_working_vertices.size());
        m_working_indices.reserve(
            m_working_indices.size() + working.indices.size()
        );
        for (const uint32_t index : working.indices)
        {
            m_working_indices.push_back(base + index);
        }
        m_working_vertices.insert(
            m_working_vertices.end(),
            working.vertices.begin(),
            working.vertices.end()
        );
    }
}

bool AssetViewer::EnsurePreviewMeshes()
{
    uint64_t required_capacity = 0;
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        required_capacity += working.source_vertex_count;
    }

    vector<const Mesh*> sources;
    sources.reserve(m_working_meshes.size());
    for (const WorkingMesh& working : m_working_meshes)
    {
        sources.push_back(working.mesh.get());
    }

    // the pool survives simplify, revert and lod switches, rebuilding it per edit would grow the
    // global geometry buffer without bound because it only ever appends
    if (
        m_preview_meshes.size() == m_working_sub_meshes.size() &&
        m_preview_meshes_sources == sources &&
        m_preview_meshes_capacity >= required_capacity
    )
    {
        return !m_preview_meshes.empty();
    }

    m_preview_meshes.clear();
    m_preview_meshes_sources = sources;
    m_preview_meshes_capacity = required_capacity;
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        // dynamic rounds the reservation up to a power of two, every later edit only shrinks the
        // geometry so it always fits back into this one allocation
        shared_ptr<Mesh> scratch = make_shared<Mesh>();
        scratch->SetObjectName(
            "asset_viewer_working_" +
            to_string(working.source_mesh) +
            "_" +
            to_string(working.source_sub_mesh)
        );

        // a faithful mirror of the working data, any post process here would silently disagree with
        // the triangle counts the panel reports
        scratch->SetFlags(0);
        scratch->SetDynamic(true);

        // sized from the source rather than from the current working copy, an already simplified
        // copy would allocate too little to ever revert back into
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;
        m_working_meshes[working.source_mesh].mesh->GetGeometry(
            working.source_sub_mesh,
            &indices,
            &vertices
        );
        scratch->AddGeometry(vertices, indices, false);
        scratch->CreateGpuBuffers();
        m_preview_meshes.push_back(move(scratch));
    }

    return !m_preview_meshes.empty();
}

void AssetViewer::RefreshPreviewMeshGeometry()
{
    if (m_working_meshes.empty() || m_working_sub_meshes.empty())
    {
        return;
    }

    // an untouched asset draws straight from the cached mesh, the scratch pool costs a geometry
    // buffer allocation per asset and that buffer only ever appends, so it stays unallocated until
    // there is an actual edit or a lod level to show
    const bool wants_scratch =
        m_working_modified ||
        m_preview_lod > 0;
    if (!wants_scratch)
    {
        for (const PreviewRenderSlot& slot : m_preview_render_slots)
        {
            Entity* entity = World::GetEntityById(slot.entity_id);
            Render* render =
                entity ?
                entity->GetComponent<Render>() :
                nullptr;
            if (
                !render ||
                slot.mesh_index >= m_working_meshes.size()
            )
            {
                continue;
            }

            Mesh* source = m_working_meshes[slot.mesh_index].mesh.get();
            if (render->GetMesh() == source)
            {
                continue;
            }

            render->SetMesh(source, slot.sub_mesh);
        }
        m_preview_dirty = true;
        return;
    }

    if (!EnsurePreviewMeshes())
    {
        return;
    }

    const vector<WorkingSubMesh>& source =
        (
            m_preview_lod > 0 &&
            m_preview_lod < static_cast<int>(m_working_lods.size())
        )
            ? m_working_lods[m_preview_lod]
            : m_working_sub_meshes;

    for (size_t index = 0; index < m_preview_meshes.size(); index++)
    {
        if (index >= source.size())
        {
            break;
        }

        vector<RHI_Vertex_PosTexNorTan> vertices =
            source[index].vertices;
        vector<uint32_t> indices = source[index].indices;
        if (vertices.empty() || indices.size() < 3)
        {
            continue;
        }

        m_preview_meshes[index]->UpdateGeometry(
            vertices,
            indices
        );
    }

    // repoint every render at its scratch mesh, the mapping is kept separately because the render
    // sub mesh index becomes zero the first time this runs
    for (const PreviewRenderSlot& slot : m_preview_render_slots)
    {
        Entity* entity = World::GetEntityById(slot.entity_id);
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        if (!render)
        {
            continue;
        }

        for (size_t index = 0; index < m_working_sub_meshes.size(); index++)
        {
            if (
                m_working_sub_meshes[index].source_mesh !=
                    slot.mesh_index ||
                m_working_sub_meshes[index].source_sub_mesh !=
                    slot.sub_mesh
            )
            {
                continue;
            }

            render->SetMesh(m_preview_meshes[index].get(), 0);
            break;
        }
    }
    m_preview_dirty = true;
}

AssetViewer::GeometryState AssetViewer::CaptureGeometry() const
{
    return {m_loaded_path, m_working_sub_meshes, m_working_lods, m_working_modified,
        m_working_lods_built, m_working_lods_attempted, m_working_lods_scanned, m_preview_lod};
}

void AssetViewer::RestoreGeometry(const GeometryState& state)
{
    if (m_loaded_path != state.path)
    {
        std::string error;
        if (!PreviewPath(state.path, error)) { SP_LOG_WARNING("Cannot restore mesh edit: %s", error.c_str()); return; }
    }
    m_working_sub_meshes = state.meshes;
    m_working_lods = state.lods;
    m_working_modified = state.modified;
    m_working_lods_built = state.built;
    m_working_lods_attempted = state.attempted;
    m_working_lods_scanned = state.scanned;
    m_preview_lod = state.preview_lod;
    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
    m_visible = true;
    m_preview_dirty = true;
}

void AssetViewer::RecordGeometry(const GeometryState& before)
{
    if (before.meshes.empty()) return;
    auto after = CaptureGeometry();
    editor_history::Record("geometry:" + before.path, [this, before] { RestoreGeometry(before); },
        [this, after] { RestoreGeometry(after); });
}

void AssetViewer::SimplifyWorkingGeometry(const float ratio)
{
    const auto before = CaptureGeometry();
    // always start from the source so the slider stays absolute, welding first
    // because simplification cannot collapse edges across duplicated vertices
    LoadWorkingGeometry();
    if (m_working_sub_meshes.empty())
    {
        return;
    }

    const float clamped = clamp(ratio, 0.01f, 1.0f);
    for (WorkingSubMesh& working : m_working_sub_meshes)
    {
        geometry_processing::optimize(
            working.vertices,
            working.indices
        );

        // each sub mesh is simplified against its own budget, a shared triangle target would
        // erase the small parts of a model long before the large ones lose any detail
        const size_t target = max<size_t>(
            24,
            static_cast<size_t>(
                static_cast<float>(working.indices.size()) *
                clamped
            )
        );
        if (
            working.indices.size() < 36 ||
            target >= working.indices.size()
        )
        {
            continue;
        }

        geometry_processing::simplify(
            working.indices,
            working.vertices,
            target,
            true,
            false
        );
    }

    m_working_modified = true;
    m_preview_lod = 0;
    // the mesh's saved chain was derived from geometry that no longer exists, leaving it in the picker
    // would offer levels that do not match the lod 0 on screen, and it must not be read back either
    m_working_lods.clear();
    m_working_lods_built = false;
    m_working_lods_attempted = false;
    m_working_lods_scanned = true;
    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
    RecordGeometry(before);
}

void AssetViewer::OptimizeWorkingGeometry()
{
    const auto before = CaptureGeometry();
    if (m_working_sub_meshes.empty())
    {
        return;
    }

    for (WorkingSubMesh& working : m_working_sub_meshes)
    {
        geometry_processing::optimize(
            working.vertices,
            working.indices
        );
    }

    m_working_modified = true;
    m_working_lods.clear();
    m_working_lods_built = false;
    m_working_lods_attempted = false;
    m_working_lods_scanned = true;
    m_preview_lod = 0;
    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
    RecordGeometry(before);
}

void AssetViewer::BuildWorkingLods()
{
    const auto before = CaptureGeometry();
    m_working_lods.clear();
    m_working_lods_built = false;
    m_working_lods_scanned = true;
    m_preview_lod = 0;
    m_working_lods_attempted = true;
    if (m_working_sub_meshes.empty())
    {
        return;
    }

    // mirrors the reduction curve mesh uses when it bakes lods, so what the picker shows is what
    // the saved file gets rather than an approximation of it
    static const float screen_thresholds[mesh_lod_count] =
    {
        0.05f,
        0.025f,
        0.012f,
        0.006f,
        0.003f
    };

    m_working_lods.push_back(m_working_sub_meshes);
    for (
        uint32_t lod = 1;
        lod < mesh_lod_count;
        lod++
    )
    {
        vector<WorkingSubMesh> level = m_working_lods.back();
        const float target_ratio =
            screen_thresholds[lod] / screen_thresholds[0];

        uint64_t previous_indices = 0;
        for (const WorkingSubMesh& working : level)
        {
            previous_indices += working.indices.size();
        }

        for (size_t index = 0; index < level.size(); index++)
        {
            WorkingSubMesh& working = level[index];
            if (working.indices.size() <= 64)
            {
                continue;
            }

            // the target is relative to the original rather than to the previous level, so a level
            // that fails to reduce does not drag every level below it up with it
            const size_t target = max<size_t>(
                64,
                static_cast<size_t>(
                    static_cast<float>(
                        m_working_sub_meshes[index].indices.size()
                    ) *
                    target_ratio
                )
            );
            if (target >= working.indices.size())
            {
                continue;
            }

            geometry_processing::simplify(
                working.indices,
                working.vertices,
                target,
                true,
                false
            );
        }

        uint64_t level_indices = 0;
        for (const WorkingSubMesh& working : level)
        {
            level_indices += working.indices.size();
        }

        // a level that barely moved is a duplicate of the one above it, keeping it would put two
        // identical looking entries in the picker and two identical ranges in the saved file
        if (
            level_indices >=
            static_cast<uint64_t>(
                static_cast<float>(previous_indices) *
                mesh_lod_min_reduction
            )
        )
        {
            break;
        }

        m_working_lods.push_back(move(level));
    }

    // a single entry means nothing could be reduced, so there is no chain worth showing
    if (m_working_lods.size() < 2)
    {
        m_working_lods.clear();
    }
    else
    {
        m_working_lods_built = true;
    }

    FlattenWorkingGeometry();
    RefreshPreviewMeshGeometry();
    RecordGeometry(before);
}

bool AssetViewer::SaveWorkingGeometry()
{
    if (
        m_working_meshes.empty() ||
        !m_working_editable ||
        m_loaded_path.empty() ||
        m_working_sub_meshes.empty()
    )
    {
        return false;
    }

    uint32_t files_written = 0;
    string written_path;
    for (
        uint32_t mesh_index = 0;
        mesh_index < static_cast<uint32_t>(m_working_meshes.size());
        mesh_index++
    )
    {
        const WorkingMesh& source = m_working_meshes[mesh_index];

        // bake into a standalone mesh so the cached resource is untouched until the
        // file is reloaded below, lods are rebuilt from the edited geometry
        Mesh baked;
        baked.SetObjectName(
            FileSystem::GetFileNameWithoutExtensionFromFilePath(
                source.path
            )
        );

        // inherit the import flags so the rebuilt file keeps the behaviour the original was authored
        // with, only the lod flag follows the checkbox
        baked.SetFlags(source.mesh->GetFlags());
        baked.SetFlag(
            static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods),
            m_working_generate_lods
        );

        // sub mesh order has to survive the bake, the prefab and the materials reference sub meshes by
        // index, so slots are reserved up front and written in place
        baked.ReserveSubMeshes(source.mesh->GetSubMeshCount());
        uint32_t written = 0;
        for (const WorkingSubMesh& working : m_working_sub_meshes)
        {
            if (
                working.source_mesh != mesh_index ||
                working.source_sub_mesh >= source.mesh->GetSubMeshCount()
            )
            {
                continue;
            }

            vector<RHI_Vertex_PosTexNorTan> vertices = working.vertices;
            vector<uint32_t> indices = working.indices;
            baked.AddGeometry(
                vertices,
                indices,
                m_working_generate_lods,
                working.source_sub_mesh
            );
            written++;
        }
        if (written == 0)
        {
            continue;
        }

        baked.SaveToFile(source.path);
        if (!FileSystem::Exists(source.path))
        {
            m_status =
                "Failed to write " +
                source.path;
            return false;
        }
        files_written++;
        written_path = source.path;
    }

    const uint64_t vertex_count = m_working_vertices.size();
    m_working_modified = false;
    m_loaded_write_time.clear();
    if (m_selected_asset >= 0)
    {
        LoadSelectedAsset(false, true);
    }
    else
    {
        const string path = m_loaded_path;
        LoadDependencyPreview(path);
    }
    m_status =
        "Saved " +
        (
            files_written == 1
                ? FileSystem::GetFileNameFromFilePath(written_path)
                : to_string(files_written) + " mesh files"
        ) +
        " with " +
        compact_count(vertex_count) +
        " vertices";
    return true;
}

vector<Material*> AssetViewer::PreviewMaterials() const
{
    vector<Material*> materials;
    if (m_material)
    {
        materials.push_back(m_material.get());
        return materials;
    }

    vector<Entity*> entities;
    CollectPreviewEntities(entities);
    for (Entity* entity : entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        Material* material =
            render ? render->GetMaterial() : nullptr;
        if (
            !material ||
            find(
                materials.begin(),
                materials.end(),
                material
            ) != materials.end()
        )
        {
            continue;
        }
        materials.push_back(material);
    }
    return materials;
}

AssetViewer::BakeSummary AssetViewer::PreviewBakeSummary() const
{
    BakeSummary summary;
    vector<Entity*> entities;
    CollectPreviewEntities(entities);

    // mirrors what game_ready groups on closely enough to promise a number, the merge itself is
    // the one that decides and its report is what the status line quotes afterwards
    vector<pair<Material*, uint32_t>> groups;
    for (Entity* entity : entities)
    {
        Render* render =
            entity ?
            entity->GetComponent<Render>() :
            nullptr;
        if (
            !render ||
            !render->GetMesh() ||
            render->GetIndexCount() == 0
        )
        {
            continue;
        }

        summary.renderers_before++;
        Material* material = render->GetMaterial();
        if (
            !material ||
            render->GetMesh()->IsSkinned() ||
            render->HasInstancing() ||
            entity->GetComponentCount() > 1
        )
        {
            summary.skipped++;
            summary.renderers_after++;
            continue;
        }

        const auto group = find_if(
            groups.begin(),
            groups.end(),
            [material](const pair<Material*, uint32_t>& candidate)
            {
                return candidate.first == material;
            }
        );
        if (group == groups.end())
        {
            groups.emplace_back(material, 1u);
        }
        else
        {
            group->second++;
        }
    }

    summary.materials = static_cast<uint32_t>(groups.size());
    summary.renderers_after += summary.materials;
    return summary;
}

bool AssetViewer::BakePrefabByMaterial(const bool generate_lods)
{
    if (
        m_selected_asset < 0 ||
        m_selected_asset >= static_cast<int>(m_assets.size()) ||
        m_assets[m_selected_asset].type != "prefab"
    )
    {
        m_status = "Select a prefab to bake.";
        return false;
    }
    if (m_working_modified || m_working_lods_built)
    {
        m_status =
            "Save or revert the mesh changes before baking.";
        return false;
    }

    // copied, the catalog refresh at the end rebuilds m_assets under the reference
    const AssetEntry asset = m_assets[m_selected_asset];
    const string prefab_path = asset.path;
    if (!FileSystem::Exists(prefab_path))
    {
        m_status = "Prefab file not found: " + prefab_path;
        return false;
    }

    // the merged mesh lives with the prefab's other files, next to the first mesh it already has
    // or in its own dependency folder when it has none yet
    string directory;
    for (const string& dependency : asset.dependencies)
    {
        if (asset_type_from_path(dependency) == "mesh")
        {
            directory =
                FileSystem::GetDirectoryFromFilePath(dependency);
            break;
        }
    }
    if (directory.empty())
    {
        const filesystem::path prefab_directory =
            filesystem::path(
                FileSystem::GetDirectoryFromFilePath(prefab_path)
            ).parent_path();
        directory =
            (
                prefab_directory.parent_path() /
                "dependencies" /
                asset.id
            ).generic_string() + "/";
    }
    const string mesh_path =
        directory +
        sanitize_asset_name(asset.id) +
        "_merged_" +
        short_hash(prefab_path) +
        ".mesh";

    pugi::xml_document document;
    document.load_file(prefab_path.c_str());
    const string prefab_name =
        document.child("Prefab").attribute("name").as_string(
            asset.name.c_str()
        );

    // the bake runs on a private copy of the hierarchy so the rig never ends up inside the saved
    // file, the preview is torn down first because both copies would carry the prefab's entity ids
    DestroyPreviewScene();
    Entity* root = World::CreateEntity();
    if (!root)
    {
        m_status = "The bake could not create a working entity.";
        return false;
    }
    root->SetObjectName(prefab_name);
    root->SetTransient(true);
    if (!Prefab::LoadFromFile(prefab_path, root))
    {
        World::RemoveEntityImmediate(root);
        m_status = "Prefab could not be loaded for baking.";
        return false;
    }
    World::ProcessPendingAdditions();

    const game_ready::MergeReport report =
        game_ready::MergeRenderersByMaterial(
            root,
            mesh_path,
            generate_lods
        );
    if (!report.ok)
    {
        World::RemoveEntityImmediate(root);
        m_status = "Bake failed: " + report.error;
        return false;
    }
    if (report.groups.empty())
    {
        World::RemoveEntityImmediate(root);
        m_status =
            "Nothing to bake, every material already draws once.";
        return true;
    }

    const bool saved = Prefab::SaveToFile(root, prefab_path);
    World::RemoveEntityImmediate(root);
    if (!saved)
    {
        m_status = "The baked prefab could not be written.";
        return false;
    }

    // the catalog lists what the prefab depends on, the parts that were folded away are gone from
    // the file so their mesh entries go too and the merged mesh takes their place
    const vector<string> mesh_references =
        collect_xml_references(prefab_path, "mesh_path");
    const bool catalog_updated =
        asset.disk_only ||
        UpdateCatalogAsset(
            asset.id,
            [&](JsonValue& record)
            {
                JsonValue& dependencies =
                    json_object_ensure(record, "dependencies");
                vector<string> kept;
                for (const string& dependency : json_strings(&dependencies))
                {
                    if (asset_type_from_path(dependency) != "mesh")
                    {
                        kept.push_back(dependency);
                    }
                }
                for (const string& reference : mesh_references)
                {
                    if (
                        find(kept.begin(), kept.end(), reference) ==
                        kept.end()
                    )
                    {
                        kept.push_back(reference);
                    }
                }
                dependencies.type = mcp_json::kind::array;
                dependencies.array_items.clear();
                for (const string& dependency : kept)
                {
                    JsonValue item;
                    item.type = mcp_json::kind::string;
                    item.string_value = dependency;
                    dependencies.array_items.push_back(move(item));
                }
            }
        );

    const string status =
        "Baked " +
        to_string(report.renderers_before) +
        " parts into " +
        to_string(report.renderers_after) +
        " draw calls across " +
        to_string(report.groups.size()) +
        (report.groups.size() == 1 ? " material" : " materials") +
        (
            report.skipped.empty()
                ? ""
                : ", " + to_string(report.skipped.size()) + " left alone"
        ) +
        (
            catalog_updated
                ? ""
                : ", the catalog could not be updated"
        );

    ClearLoadedAsset();
    RefreshCatalog(true);
    for (int index = 0; index < static_cast<int>(m_assets.size()); index++)
    {
        if (m_assets[index].id == asset.id)
        {
            m_selected_asset = index;
            m_selected_dependency_path.clear();
            LoadSelectedAsset(false, true);
            break;
        }
    }
    m_status = status;
    return true;
}

void AssetViewer::DrawBakeConfirmation()
{
    if (m_bake_confirmation_open)
    {
        ImGui::OpenPopup("Bake prefab?");
        m_bake_confirmation_open = false;
    }
    if (
        !ImGui::BeginPopupModal(
            "Bake prefab?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        return;
    }

    const BakeSummary bake = PreviewBakeSummary();
    ImGui::TextUnformatted(
        "Merge the parts that share a material and overwrite the prefab?"
    );
    ImGui::TextDisabled(
        "%u draw calls become %u, the merged parts are removed from the prefab",
        bake.renderers_before,
        bake.renderers_after
    );
    if (bake.skipped > 0)
    {
        ImGui::TextDisabled(
            "%u part%s carry other components and stay as they are",
            bake.skipped,
            bake.skipped == 1 ? "" : "s"
        );
    }
    ImGui::Checkbox(
        "Generate LODs for the merged mesh",
        &m_bake_generate_lods
    );
    ImGui::Spacing();
    if (ImGuiSp::button("Bake and overwrite"))
    {
        BakePrefabByMaterial(m_bake_generate_lods);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetViewer::DrawMeshTools()
{
    if (
        !m_working_editable ||
        m_working_vertices.empty()
    )
    {
        return;
    }

    // the levels already in the mesh are read on first sight of this panel, so a mesh that ships with a
    // chain can be paged through without building anything
    if (!m_working_lods_scanned)
    {
        m_working_lods_scanned = true;
        LoadExistingLods();
    }

    // the mesh wide counts include every lod, so lod 0 is summed instead or the reduction readout
    // compares the edit against geometry it never touched
    uint64_t source_vertices = 0;
    uint64_t source_indices = 0;
    uint64_t working_vertices = 0;
    uint64_t working_indices = 0;
    for (const WorkingSubMesh& working : m_working_sub_meshes)
    {
        source_vertices += working.source_vertex_count;
        source_indices += working.source_index_count;
        working_vertices += working.vertices.size();
        working_indices += working.indices.size();
    }
    const uint64_t source_triangles = source_indices / 3;
    // the lod picker changes what the viewport shows, not what the edit is, so these track the
    // working geometry rather than the flattened preview
    const uint64_t working_triangles = working_indices / 3;
    const float reduction = source_triangles > 0
        ? 1.0f -
            static_cast<float>(working_triangles) /
            static_cast<float>(source_triangles)
        : 0.0f;

    const float scale = ui_scale();
    const float footer_height = 86.0f * scale;
    const float content_height = max(
        80.0f * scale,
        ImGui::GetContentRegionAvail().y -
            footer_height
    );
    ImGui::BeginChild(
        "##mesh_tools_content",
        ImVec2(0.0f, content_height),
        ImGuiChildFlags_None
    );

    if (m_working_modified || m_working_lods_built)
    {
        const ImVec4 accent = ImGui::Style::color_accent_1;
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(accent.x, accent.y, accent.z, 0.1f)
        );
        ImGui::BeginChild(
            "##mesh_changes",
            ImVec2(0.0f, 48.0f * scale),
            ImGuiChildFlags_Borders
        );
        ImGui::TextUnformatted("Unsaved mesh changes");
        ImGui::TextDisabled(
            "%.0f%% fewer triangles",
            reduction * 100.0f
        );
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::TextUnformatted("GEOMETRY");
    ImGui::Separator();
    detail_row(
        "Source",
        compact_count(source_vertices) +
            " vertices, " +
            compact_count(source_triangles) +
            " triangles"
    );
    detail_row(
        "Working",
        compact_count(working_vertices) +
            " vertices, " +
            compact_count(working_triangles) +
            " triangles"
    );
    detail_row(
        "Sub meshes",
        m_working_meshes.size() > 1
            ? to_string(m_working_sub_meshes.size()) +
                " across " +
                to_string(m_working_meshes.size()) +
                " mesh files"
            : to_string(m_working_sub_meshes.size())
    );
    ImGui::TextDisabled(
        "Current reduction %.1f%%",
        reduction * 100.0f
    );
    if (m_working_meshes.size() > 1 || m_working_sub_meshes.size() > 1)
    {
        // one slider for the whole asset, each part keeps the same share of its own triangles so
        // the small parts do not vanish before the large ones lose any detail
        ImGui::TextDisabled(
            "Every part is reduced together, each keeps the same share of its triangles"
        );
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("1  REDUCE GEOMETRY");
    ImGui::Separator();
    ImGui::TextDisabled(
        "Keep this share of the original triangles"
    );

    // the member is a ratio but a ratio shown through a percent format reads as 0%, the slider works in
    // percent so the number on it is the number the user is choosing
    float target_percent = m_target_ratio * 100.0f;
    ImGui::SetNextItemWidth(-1.0f);
    if (
        ImGui::SliderFloat(
            "##triangle_target",
            &target_percent,
            2.0f,
            100.0f,
            "keep %.0f%%",
            ImGuiSliderFlags_AlwaysClamp
        )
    )
    {
        m_target_ratio = target_percent / 100.0f;
    }
    // simplifying every frame of a drag would rebuild the whole mesh per pixel, so it lands on release
    const bool apply_target =
        ImGui::IsItemDeactivatedAfterEdit();

    ImGui::TextDisabled(
        "About %s triangles, %.0f%% removed",
        compact_count(
            static_cast<uint64_t>(
                static_cast<float>(source_triangles) *
                m_target_ratio
            )
        ).c_str(),
        (1.0f - m_target_ratio) * 100.0f
    );

    ImGui::Spacing();

    // the button is drawn first and tested after, an or would short circuit it away on the very frame
    // the slider is released and the row would blink out of the panel
    const bool apply_pressed =
        ImGuiSp::button(
            "Apply simplification",
            ImVec2(-1.0f, 0.0f)
        );
    if (apply_target || apply_pressed)
    {
        SimplifyWorkingGeometry(m_target_ratio);
    }
    if (
        ImGuiSp::button(
            "Weld and optimize",
            ImVec2(-1.0f, 0.0f)
        )
    )
    {
        OptimizeWorkingGeometry();
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("2  BUILD LEVELS OF DETAIL");
    ImGui::Separator();
    ImGui::Checkbox(
        "Generate LODs when saving",
        &m_working_generate_lods
    );
    ImGuiSp::tooltip(
        "Rebuild lower detail levels from the edited geometry"
    );
    if (
        ImGuiSp::button(
            m_working_lods.empty() ? "Build LODs" : "Rebuild LODs",
            ImVec2(-1.0f, 0.0f)
        )
    )
    {
        BuildWorkingLods();
    }
    ImGuiSp::tooltip(
        "Preview the chain that saving would bake, this does not change the mesh"
    );

    if (!m_working_lods.empty())
    {
        const int lod_count =
            static_cast<int>(m_working_lods.size());
        m_preview_lod = clamp(m_preview_lod, 0, lod_count - 1);

        const auto lod_triangles =
            [this](const int lod)
            {
                uint64_t indices = 0;
                for (
                    const WorkingSubMesh& working :
                    m_working_lods[static_cast<size_t>(lod)]
                )
                {
                    indices += working.indices.size();
                }
                return indices / 3;
            };

        // each entry carries its own cost, so choosing a level is a comparison rather than a guess
        const auto lod_label =
            [&](const int lod)
            {
                const uint64_t triangles = lod_triangles(lod);
                string label =
                    "LOD " +
                    to_string(lod) +
                    "  -  " +
                    compact_count(triangles) +
                    " triangles";
                if (lod == 0)
                {
                    return label + "  (full detail)";
                }

                const int removed = source_triangles > 0
                    ? static_cast<int>(
                        (
                            1.0f -
                            static_cast<float>(triangles) /
                            static_cast<float>(source_triangles)
                        ) *
                        100.0f
                    )
                    : 0;
                return label + "  (-" + to_string(removed) + "%)";
            };

        ImGui::TextDisabled(
            m_working_lods_built
                ? "Showing in the viewport, not saved yet"
                : "Showing in the viewport, saved in this mesh"
        );
        ImGui::SetNextItemWidth(-1.0f);
        if (
            ImGui::BeginCombo(
                "##preview_lod",
                lod_label(m_preview_lod).c_str()
            )
        )
        {
            for (int lod = 0; lod < lod_count; lod++)
            {
                const bool selected = lod == m_preview_lod;
                if (
                    ImGui::Selectable(
                        lod_label(lod).c_str(),
                        selected
                    ) &&
                    !selected
                )
                {
                    m_preview_lod = lod;
                    FlattenWorkingGeometry();
                    RefreshPreviewMeshGeometry();
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGuiSp::tooltip(
            "Switch the preview between the levels that saving would bake"
        );

        // stepping is how the levels get compared, hunting the same entry in a dropdown each time is
        // what makes a chain of five feel like work
        const float step_width =
            (
                ImGui::GetContentRegionAvail().x -
                4.0f * ui_scale()
            ) *
            0.5f;
        const bool at_first = m_preview_lod <= 0;
        const bool at_last = m_preview_lod >= lod_count - 1;
        int stepped = m_preview_lod;
        if (at_first)
        {
            ImGui::BeginDisabled();
        }
        if (
            ImGuiSp::button(
                "More detail",
                ImVec2(step_width, 0.0f)
            )
        )
        {
            stepped = m_preview_lod - 1;
        }
        if (at_first)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine(0.0f, 4.0f * ui_scale());
        if (at_last)
        {
            ImGui::BeginDisabled();
        }
        if (
            ImGuiSp::button(
                "Less detail",
                ImVec2(step_width, 0.0f)
            )
        )
        {
            stepped = m_preview_lod + 1;
        }
        if (at_last)
        {
            ImGui::EndDisabled();
        }
        if (stepped != m_preview_lod)
        {
            m_preview_lod = clamp(stepped, 0, lod_count - 1);
            FlattenWorkingGeometry();
            RefreshPreviewMeshGeometry();
        }
    }
    else if (m_working_lods_attempted)
    {
        // pressing build and getting nothing back looks identical to never pressing it, which is the
        // one state worth spelling out
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImGui::Style::color_accent_1
        );
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(
            "This geometry is already too simple to reduce further, so there is "
            "no chain to preview. Lower the simplification target first if you "
            "want coarser levels."
        );
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled(
            "This mesh has no saved levels, build a chain to preview one"
        );
    }

    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextUnformatted("3  REVIEW AND SAVE");

    const bool can_save =
        m_working_modified ||
        m_working_lods_built;
    ImGui::TextDisabled(
        can_save
            ? "Changes are previewed but not saved"
            : "No pending mesh changes"
    );

    const float action_gap = 4.0f * scale;
    const float action_width =
        (
            ImGui::GetContentRegionAvail().x -
            action_gap
        ) *
        0.5f;
    if (!can_save)
    {
        ImGui::BeginDisabled();
    }
    if (
        ImGuiSp::button(
            "Revert",
            ImVec2(action_width, 0.0f)
        )
    )
    {
        const auto before = CaptureGeometry();
        LoadWorkingGeometry();
        RecordGeometry(before);
    }
    ImGuiSp::tooltip(
        "Discard simplification and rebuilt LOD previews"
    );
    ImGui::SameLine(0.0f, action_gap);
    if (
        ImGuiSp::button(
            "Save mesh",
            ImVec2(action_width, 0.0f)
        )
    )
    {
        ImGui::OpenPopup("Save mesh changes?");
    }
    if (!can_save)
    {
        ImGui::EndDisabled();
    }

    if (
        ImGui::BeginPopupModal(
            "Save mesh changes?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
    )
    {
        // the lod picker only changes what is on screen, the bake always writes the working
        // geometry as lod 0 and derives the rest from it
        ImGui::TextUnformatted(
            m_working_meshes.size() > 1
                ? "Overwrite the prefab's mesh files?"
                : "Overwrite the current mesh?"
        );
        ImGui::TextDisabled(
            "%llu to %llu triangles, %.1f%% reduction",
            static_cast<unsigned long long>(source_triangles),
            static_cast<unsigned long long>(working_triangles),
            reduction * 100.0f
        );
        ImGui::TextDisabled(
            "%zu sub meshes in %zu file%s, LODs %s",
            m_working_sub_meshes.size(),
            m_working_meshes.size(),
            m_working_meshes.size() == 1 ? "" : "s",
            m_working_generate_lods ? "rebuilt" : "dropped"
        );
        ImGui::Spacing();
        if (ImGuiSp::button("Save and overwrite"))
        {
            SaveWorkingGeometry();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
