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
#include "RHI/RHI_Vertex.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>
#include <vector>

namespace spartan
{
    class Material;
    class Mesh;
    class Entity;
    class RHI_Texture;
    struct McpRequest;
}

class AssetViewer : public Widget
{
public:
    AssetViewer(Editor* editor);
    ~AssetViewer() override;

    void OnVisible() override;
    void OnTickVisible() override;

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
    void DrawToolbar();
    void DrawStatusBar();
    void DrawAssetList(float width, float height);
    void DrawDetails(float height);
    void DrawPreview(float width, float height);
    void DrawMeshTools();
    bool DeleteSelectedAsset();
    void LoadWorkingGeometry();
    void SimplifyWorkingGeometry(float ratio);
    void OptimizeWorkingGeometry();
    bool SaveWorkingGeometry();
    void CollectPrefabDependencies(const std::string& path);
    void RebuildPreviewScene();
    void PreviewEntity(spartan::Entity* entity);
    spartan::Entity* PreviewRoot() const;
    void CollectPreviewEntities(
        std::vector<spartan::Entity*>& entities
    ) const;
    void RequestPreviewRender();
    void DestroyPreviewScene();
    std::pair<uint64_t, uint64_t>
        GetPreviewGeometryCounts() const;
    void DrawMeshPreview(
        const ImVec2& minimum,
        const ImVec2& maximum
    );
    void DrawMaterialPreview(
        const ImVec2& minimum,
        const ImVec2& maximum
    );
    void DrawPrefabPreview(
        const ImVec2& minimum,
        const ImVec2& maximum
    );
    void DrawTexturePreview(
        const ImVec2& minimum,
        const ImVec2& maximum
    );
    bool AssetMatchesFilter(const AssetEntry& asset) const;
    const AssetVersion* GetActiveVersion(
        const AssetEntry& asset
    ) const;
    std::string HandleMcpCommand(
        const spartan::McpRequest& request
    );
    bool SavePreviewScreenshot(
        const std::string& path,
        uint32_t width,
        uint32_t height
    ) const;

    std::vector<AssetEntry> m_assets;
    std::array<char, 192> m_search = {};
    std::string m_world_file_path;
    std::string m_catalog_path;
    std::string m_catalog_write_time;
    std::string m_status;
    std::string m_loaded_path;
    std::string m_loaded_write_time;
    std::shared_ptr<spartan::Mesh> m_mesh;
    std::shared_ptr<spartan::Material> m_material;
    std::shared_ptr<spartan::RHI_Texture> m_texture;
    std::vector<spartan::RHI_Vertex_PosTexNorTan> m_working_vertices;
    std::vector<uint32_t> m_working_indices;
    std::vector<std::string> m_missing_dependencies;
    bool m_working_modified = false;
    bool m_working_editable = false;
    bool m_working_generate_lods = true;
    float m_target_ratio = 0.5f;
    // the preview root is tracked by id, a raw pointer dangles the moment the
    // world removes the entity behind our back
    uint64_t m_preview_root_id = 0;
    bool m_preview_root_owned = false;
    int m_selected_asset = -1;
    int m_type_filter = 0;
    int m_sort_mode = 0;
    int m_inspector_tab = 0;
    int m_preview_mode = 0;
    int m_prefab_entity_count = 0;
    float m_library_width = 280.0f;
    float m_inspector_width = 340.0f;
    float m_preview_yaw = 0.65f;
    float m_preview_pitch = 0.35f;
    float m_preview_zoom = 1.0f;
    bool m_preview_show_grid = true;
    bool m_preview_show_stats = true;
    bool m_preview_auto_rotate = false;
    bool m_preview_dirty = false;
    bool m_preview_orbiting = false;
    std::chrono::steady_clock::time_point m_next_refresh_check;
};
