/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Widget.h"
#include "geometry/TreeGenerator.h"
#include "geometry/TreeTextures.h"
//==============================

namespace spartan
{
    class Mesh;
    class Material;
    class RHI_Texture;
    class Entity;
}

// grows trees from regional species presets and exports them as prefabs into the project
//
// the species files hold every number a tree generator usually shows, the window only offers the growth
// controls a person actually reaches for, and the mcp drives the very same state so an agent sees what
// the panel shows
class TreeCreator : public Widget
{
public:
    struct stats
    {
        uint32_t triangles         = 0; // lod 0, bark and foliage
        uint32_t vertices          = 0;
        uint32_t bark_triangles    = 0;
        uint32_t foliage_triangles = 0;
        uint32_t budget            = 0;
        uint32_t stems             = 0;
        uint32_t cards             = 0;
        std::vector<uint32_t> lod_triangles; // per level, bark plus foliage, clamped to each sub mesh
        float height               = 0.0f;
        float crown_width          = 0.0f;
        float generate_ms          = 0.0f;
    };

    struct export_result
    {
        std::string directory;
        std::string prefab;
        std::string mesh;
        uint32_t seed = 0;
        stats numbers;
        bool impostor = false;
    };

    TreeCreator(Editor* editor);
    ~TreeCreator() override;

    void OnTick() override;
    void OnTickVisible() override;
    void OnVisible() override;
    void OnInvisible() override;

    // the mcp drives the same state the panel shows
    void SetPanelVisible(bool visible);
    const std::vector<spartan::trees::species>& GetSpecies() const { return m_species; }
    std::string ReloadSpecies();
    bool SelectSpecies(const std::string& id);
    const spartan::trees::species* GetSelectedSpecies() const;
    const spartan::trees::controls& GetControls() const { return m_controls; }
    void SetControls(const spartan::trees::controls& controls);
    // waits for the tree with the current controls and puts it in the preview
    bool GenerateNow(std::string& error);
    const stats& GetStats() const { return m_stats; }
    void SetPreviewView(float yaw, float pitch, float zoom);
    void SetPreviewLod(int lod);
    int GetPreviewLod() const { return m_preview_lod; }
    float GetPreviewYaw() const { return m_yaw; }
    float GetPreviewPitch() const { return m_pitch; }
    float GetPreviewZoom() const { return m_zoom; }
    bool CapturePreview(const std::string& path, uint32_t width, uint32_t height, std::string& error);
    // one tree per seed starting at the current one, each into its own folder
    bool Export(const std::string& name, uint32_t variations, std::vector<export_result>& results, std::string& error);
    // validates and writes a species file, then reloads the list and selects it
    bool SaveSpecies(const std::string& json, std::string& path, std::string& error);

private:
    struct job
    {
        spartan::trees::species recipe;
        spartan::trees::controls controls;
        spartan::trees::result tree;
        spartan::trees::texture_paths textures;
        bool textures_ok = false;
        float milliseconds = 0.0f;
    };

    void OnWorldUnloading();

    // panel
    void DrawLibrary(float width, float height);
    void DrawPreview(float width, float height);
    void DrawControls(float width, float height);
    void DrawTile(const spartan::trees::species& recipe, float size);
    void DrawSilhouette(const spartan::trees::species& recipe, const ImVec2& min, const ImVec2& max);

    // generation
    void RequestGenerate();
    void StartJob();
    bool CollectJob(bool wait);
    void ApplyJob(job& finished);

    // preview scene
    void BuildPreviewScene();
    void DestroyPreviewScene();
    void UpdatePreviewCamera();
    bool RequestPreviewRender(uint32_t width, uint32_t height);
    spartan::Mesh* PreviewMeshForLod();
    void RefreshThumbnail(const std::string& id);
    spartan::RHI_Texture* GetThumbnail(const std::string& id);

    // export
    bool ExportOne(const spartan::trees::species& recipe, const spartan::trees::controls& controls, const std::string& name, export_result& output, std::string& error);

    std::vector<spartan::trees::species> m_species;
    std::vector<std::string> m_regions;
    std::string m_region;
    int m_selected = -1;
    spartan::trees::controls m_controls;
    std::string m_export_name;
    std::string m_status;
    std::string m_load_errors;

    // the tree being shown, its mesh and the per level copies the lod switcher draws
    spartan::trees::result m_tree;
    std::shared_ptr<spartan::Mesh> m_mesh;
    std::vector<std::shared_ptr<spartan::Mesh>> m_lod_meshes;
    std::shared_ptr<spartan::Material> m_bark_material;
    std::shared_ptr<spartan::Material> m_foliage_material;
    std::shared_ptr<spartan::Material> m_ground_material;
    stats m_stats;
    std::string m_tree_key; // species and season the textures were made for

    std::future<void> m_job_future;
    std::shared_ptr<job> m_job;
    bool m_job_running      = false;
    bool m_generate_pending = false;
    std::chrono::steady_clock::time_point m_generate_after;

    uint64_t m_preview_root_id   = 0;
    uint64_t m_preview_rig_id    = 0;
    uint64_t m_preview_camera_id = 0;
    spartan::math::Vector3 m_preview_center;
    float m_preview_radius = 1.0f;
    float m_preview_aspect = 1.0f;
    float m_yaw            = 0.65f;
    float m_pitch          = 0.12f;
    float m_zoom           = 1.0f;
    int m_preview_lod      = 0;
    bool m_wind            = true;
    bool m_orbiting        = false;
    bool m_preview_dirty   = false;
    uint32_t m_settle      = 0;
    std::chrono::steady_clock::time_point m_next_request;

    // thumbnails are captured from the preview the first time a species is grown with its default controls
    std::unordered_map<std::string, std::shared_ptr<spartan::RHI_Texture>> m_thumbnails;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_thumbnail_pending;
    std::string m_thumbnail_capture;
    uint64_t m_world_unloading_handle = 0;
};
