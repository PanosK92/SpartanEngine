/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Widget.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "RHI/RHI_Vertex.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace spartan
{
    class Material;
    class Mesh;
    class Entity;
    class RHI_Texture;
    enum class Renderer_SecondaryViewBackdrop;
}

class AssetViewer : public Widget
{
public:
    AssetViewer(Editor* editor);
    ~AssetViewer() override;

    void OnVisible() override;
    void OnInvisible() override;
    void OnTickVisible() override;

    enum class PreviewView
    {
        Front,
        Back,
        Left,
        Right,
        Top,
        Bottom,
        Perspective
    };

    // these mirror the order the panel stores its shading and backdrop selection in
    enum class PreviewShading
    {
        Solid,
        Wireframe,
        Vertices
    };

    enum class PreviewBackdrop
    {
        Auto,
        Sky,
        Charcoal,
        Slate,
        Paper
    };

    // an unset field keeps whatever the panel already had
    struct ViewRequest
    {
        std::optional<PreviewView> view;
        std::optional<float> yaw;
        std::optional<float> pitch;
        std::optional<float> zoom;
    };

    struct CaptureRequest
    {
        std::string path;
        uint32_t width = 768;
        uint32_t height = 768;
        PreviewShading shading = PreviewShading::Solid;
        PreviewBackdrop backdrop = PreviewBackdrop::Slate;
    };

    struct CaptureResult
    {
        // the size that was actually used, a request outside what the panel can render is clamped
        uint32_t width = 0;
        uint32_t height = 0;
        // false when the render lands on a later frame, the file appears once it does
        bool ready = false;
    };

    struct PreviewStatus
    {
        std::string selected_asset_id;
        std::string selected_asset_name;
        std::string selected_version_id;
        std::string loaded_path;
        // the entity a caller asked to preview, zero while the panel owns the root it built itself
        uint64_t previewed_entity_id = 0;
        uint64_t vertex_count = 0;
        uint64_t index_count = 0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float zoom = 1.0f;
        bool visible = false;
    };

    // control surface for drivers outside the panel, a failure is reported through error rather than
    // formatted into a reply so the panel carries no knowledge of who is calling it
    void SetPanelVisible(bool visible);
    bool SelectAsset(
        const std::string& query,
        const std::string& version,
        std::string& error
    );
    bool PreviewEntityById(
        uint64_t entity_id,
        std::string& error
    );
    void SetPreviewView(const ViewRequest& request);
    bool HasPreviewContent() const;
    bool CapturePreview(
        const CaptureRequest& request,
        CaptureResult& result,
        std::string& error
    );
    PreviewStatus GetPreviewStatus() const;

private:
    struct AssetVersion
    {
        std::string id;
        std::string path;
        std::string notes;
        std::vector<std::string> dependencies;
        int number = 0;
        float quality_score = 0.0f;
        bool quality_verified = false;
    };

    // one editable copy per sub mesh, sub meshes cannot be merged into a single buffer and
    // simplified as one blob because each carries its own material
    struct WorkingSubMesh
    {
        std::vector<spartan::RHI_Vertex_PosTexNorTan> vertices;
        std::vector<uint32_t> indices;
        // the slot this came from, edits are written back into the same slot because prefabs and
        // material slots address sub meshes by index
        uint32_t source_sub_mesh = 0;
        uint32_t source_vertex_count = 0;
        uint32_t source_index_count = 0;
    };

    // what a library cleanup would remove, built before anything is touched so the confirmation can
    // list it, superseded versions and orphans are kept apart because one is safe and one is a guess
    struct CleanupPlan
    {
        std::vector<std::string> superseded_labels;
        std::vector<std::string> superseded_files;
        std::vector<std::string> orphan_files;
        std::vector<std::string> directories;
        uint64_t bytes = 0;
        bool scanned = false;
        std::string error;
    };

    struct AssetEntry
    {
        std::string id;
        std::string name;
        std::string type;
        std::string active_version;
        std::vector<std::string> aliases;
        std::vector<std::string> tags;
        std::vector<AssetVersion> versions;
    };

    void RefreshCatalog(bool force);
    void ClearLoadedAsset();
    void LoadSelectedAsset(
        bool reset_view = true,
        bool force_reload = false
    );
    void LoadDependencyPreview(const std::string& path);
    void DrawToolbar();
    void DrawStatusBar();
    void DrawAssetList(float width, float height);
    void DrawDetails(float height);
    void DrawPreview(float width, float height);
    void DrawMeshTools();
    bool DeleteSelectedAsset();
    bool DeleteAssets(const std::vector<std::string>& ids);
    bool IsAssetSelected(int index) const;
    // plain click replaces the selection, ctrl toggles one row, shift takes the run between the anchor
    // and the row in the order the list is drawn in
    void ApplyRowSelection(
        int index,
        const std::vector<int>& order
    );
    std::vector<std::string> SelectedAssetIds() const;
    bool RenameAsset(int index, const std::string& new_name);
    bool RenameAssetFile(
        const std::string& path,
        const std::string& new_name
    );
    void DrawAssetContextMenu(int index);
    void DrawAssetRenameInline(int index, float width);
    void DrawDependencyContextMenu(const std::string& path);
    void DrawDependencyRenameInline(
        const std::string& path,
        float width
    );
    void DrawDeleteConfirmation();
    void ScanLibraryCleanup();
    bool ApplyLibraryCleanup();
    void DrawCleanupConfirmation();
    void LoadWorkingGeometry();
    void LoadExistingLods();
    void FlattenWorkingGeometry();
    bool EnsurePreviewMeshes();
    void RefreshPreviewMeshGeometry();
    void SimplifyWorkingGeometry(float ratio);
    void OptimizeWorkingGeometry();
    void BuildWorkingLods();
    bool SaveWorkingGeometry();
    void CollectPrefabDependencies(const std::string& path);
    void RebuildPreviewScene();
    void PreviewEntity(spartan::Entity* entity);
    spartan::Entity* PreviewRoot() const;
    void CollectPreviewEntities(
        std::vector<spartan::Entity*>& entities
    ) const;
    bool RequestPreviewRender(uint32_t width, uint32_t height);
    spartan::Renderer_SecondaryViewBackdrop
        ResolvePreviewBackdrop() const;
    void CreatePreviewRig(spartan::Entity* root);
    // refits the orbit centre and radius to whatever the preview currently holds, an asset gains parts
    // while it is being watched so the framing cannot be decided once
    void RefreshPreviewBounds(spartan::Entity* root = nullptr);
    void UpdatePreviewCamera();
    void DestroyPreviewScene();
    std::pair<uint64_t, uint64_t>
        GetPreviewGeometryCounts() const;
    // fingerprint of what the preview would draw, a change means the scene is still assembling
    uint64_t PreviewSceneSignature() const;
    void DrawTexturePreview(
        const ImVec2& minimum,
        const ImVec2& maximum
    );
    bool AssetMatchesFilter(const AssetEntry& asset) const;
    const AssetVersion* GetActiveVersion(
        const AssetEntry& asset
    ) const;
    bool SavePreviewScreenshot(
        const std::string& path,
        uint32_t width,
        uint32_t height
    );

    std::vector<AssetEntry> m_assets;
    std::array<char, 192> m_search = {};
    std::string m_world_file_path;
    std::string m_catalog_path;
    std::string m_catalog_write_time;
    std::string m_status;
    std::string m_loaded_path;
    std::string m_selected_dependency_path;
    // inline rename and delete are tracked by catalog id, the list is rebuilt and resorted on
    // every refresh so an index would point at a different asset a frame later
    std::string m_rename_asset_id;
    std::string m_rename_buffer;
    // a commit refreshes the catalog, which rebuilds m_assets, so it has to run after the list
    // has finished drawing rather than from inside a row
    std::string m_rename_commit_id;
    std::string m_rename_commit_name;
    std::string m_pending_delete_id;
    // linked dependency rows are keyed by path, they have no catalog record at all
    std::string m_rename_dependency_path;
    std::string m_rename_commit_path;
    std::string m_pending_delete_path;
    // the whole selection is confirmed as one, deleting a hundred rows one modal at a time is not a thing
    // anybody would sit through
    bool m_pending_delete_selection = false;
    CleanupPlan m_cleanup;
    bool m_cleanup_include_orphans = true;
    bool m_rename_request_focus = false;
    std::unordered_set<std::string> m_expanded_assets;
    std::string m_loaded_write_time;
    std::shared_ptr<spartan::Mesh> m_mesh;
    // one scratch mesh per editable sub mesh, allocated once at the source size and then updated in
    // place, the global geometry buffer only ever appends so rebuilding them per edit would grow it
    // without bound
    std::vector<std::shared_ptr<spartan::Mesh>> m_preview_meshes;
    // what the pool was sized against, a scratch mesh can only ever be updated with geometry that
    // fits its original allocation
    const spartan::Mesh* m_preview_meshes_source = nullptr;
    uint64_t m_preview_meshes_capacity = 0;
    // entity id to source sub mesh index, the render components lose that mapping the moment their
    // sub mesh index is repointed at a scratch mesh
    std::vector<std::pair<uint64_t, uint32_t>> m_preview_render_slots;
    std::shared_ptr<spartan::Material> m_material;
    std::shared_ptr<spartan::RHI_Texture> m_texture;
    // the authoritative editable geometry, one entry per sub mesh
    std::vector<WorkingSubMesh> m_working_sub_meshes;
    // the lod chain built from the working geometry, [lod][sub mesh], lod 0 is the working
    // geometry itself, built on demand so switching the preview level is instant
    std::vector<std::vector<WorkingSubMesh>> m_working_lods;
    // flattened view of whatever the preview is showing, kept for the counts and the preview mesh
    std::vector<spartan::RHI_Vertex_PosTexNorTan> m_working_vertices;
    std::vector<uint32_t> m_working_indices;
    std::vector<std::string> m_missing_dependencies;
    bool m_working_modified = false;
    bool m_working_editable = false;
    bool m_working_generate_lods = true;
    // whether a chain was ever asked for, an empty chain means nothing could be reduced rather than
    // nothing was tried, and the panel has to say which
    bool m_working_lods_attempted = false;
    // true only when the chain was built here, the mesh's own saved chain is previewable but is not
    // itself a pending change, so it must not make the save button live
    bool m_working_lods_built = false;
    // the mesh's saved chain costs roughly another copy of the geometry to read, so it is read when the
    // optimize panel is first opened rather than on every selection
    bool m_working_lods_scanned = false;
    // which level of the built lod chain the preview shows, 0 is the working geometry
    int m_preview_lod = 0;
    float m_target_ratio = 0.5f;
    // the preview root is tracked by id, a raw pointer dangles the moment the
    // world removes the entity behind our back
    uint64_t m_preview_root_id = 0;
    uint64_t m_preview_rig_id = 0;
    uint64_t m_preview_camera_id = 0;
    bool m_preview_root_owned = false;
    int m_selected_asset = -1;
    // the highlighted set is kept by catalog id because the list is rebuilt and resorted on every refresh,
    // m_selected_asset stays the row the inspector is showing and is always one of these
    std::unordered_set<std::string> m_selected_assets;
    int m_selection_anchor = -1;
    // the rows drawn last frame in the order they appeared, which is what select all and a shift range
    // both have to mean, the tree is filtered and collapsible so it is not the order of m_assets
    std::vector<int> m_visible_rows;
    int m_type_filter = 0;
    int m_sort_mode = 0;
    int m_inspector_tab = 0;
    // section of the linked file panel, 0 is overview and 1 is the mesh optimize tools
    int m_dependency_tab = 0;
    int m_preview_mode = 0;
    // slate by default, 0 is auto which keeps the sky for solid shading, the sky still lights the
    // asset in every mode, only the visible background is replaced
    int m_preview_backdrop = 3;
    int m_prefab_entity_count = 0;
    float m_library_width = 280.0f;
    float m_inspector_width = 340.0f;
    float m_preview_yaw = 0.65f;
    float m_preview_pitch = 0.35f;
    float m_preview_zoom = 1.0f;
    spartan::math::Vector2 m_texture_pan =
        spartan::math::Vector2::Zero;
    spartan::math::Vector3 m_preview_center =
        spartan::math::Vector3::Zero;
    float m_preview_radius = 1.0f;
    float m_preview_aspect = 1.0f;
    uint32_t m_preview_settle_frames = 0;
    uint64_t m_preview_signature = 0;
    bool m_preview_show_stats = true;
    bool m_preview_auto_rotate = false;
    bool m_preview_dirty = false;
    bool m_preview_orbiting = false;
    std::chrono::steady_clock::time_point m_next_refresh_check;
    std::chrono::steady_clock::time_point
        m_next_preview_request;
};
