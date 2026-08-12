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

#pragma once

//= INCLUDES =========================
#include "Component.h"
#include <atomic>
#include <array>
#include <unordered_map>
#include <vector>
#include "../../RHI/RHI_Definitions.h"
#include "../../Math/Quaternion.h"
#include "../../Math/Ray.h"
#include "../../Math/Vector4.h"
#include "../TerrainSystem.h"
#include "../TerrainLayer.h"
//====================================

namespace spartan
{
    class Mesh;
    class Material;
    namespace math
    {
        class Vector3;
    }

    enum class TerrainProp
    {
        Tree,
        Grass,
        Flower,
        Rock,
        Max
    };

    struct TerrainPropDescription
    {
        bool  align_to_surface_normal  = true;
        float max_slope_angle_rad      = math::deg_to_rad * 35.0f;
        float surface_offset           = 0.05f;
        float min_spawn_height         = 0.0f;
        float max_spawn_height         = 1000.0f;
        float min_scale                = 0.8f;
        float max_scale                = 1.2f;
        bool  scale_adjust_by_slope    = false;
        uint32_t instances_per_cluster = 0;
        float cluster_radius           = 0.0f;
        // biome prop mask, -1 ignores the mask, 0=r grass, 1=g trees, 2=b rocks
        int   prop_mask_channel        = -1;
        float prop_mask_min            = 0.2f;
    };

    // precomputed per-triangle data for prop placement
    struct TriangleData
    {
        math::Vector3 normal;
        math::Vector3 v0;
        math::Vector3 v1_minus_v0;
        math::Vector3 v2_minus_v0;
        float slope_radians;
        float height_min;
        float height_max;
        math::Quaternion rotation_to_normal;
        math::Vector3 centroid;
    };

    class Terrain : public Component
    {
    public:
        Terrain(Entity* entity);
        ~Terrain();


        // height map
        RHI_Texture* GetHeightMapSeed() const          { return m_height_map_seed; }
        void SetHeightMapSeed(RHI_Texture* height_map) { m_height_map_seed = height_map;}
        RHI_Texture* GetHeightMapFinal() const         { return m_height_map_final.get(); }
        // r32 local-space heights for gpu grass populate, not the r8 imgui preview
        RHI_Texture* GetHeightMapGpu() const           { return m_height_map_gpu.get(); }
        // xy = world min xz, zw = 1 / world size, matches analysis and height sampling
        const math::Vector4& GetWorldMapping() const   { return m_world_mapping; }
        float GetHeightBakeMin() const                 { return m_height_bake_min; }
        float GetHeightBakeMax() const                 { return m_height_bake_max; }

        // dimensions
        uint32_t GetWidth() const  { return m_width; }
        uint32_t GetHeight() const { return m_height; }

        // height range
        float GetMinY() const     { return m_min_y; }
        void SetMinY(float min_z) { m_min_y = min_z; }
        float GetMaxY() const     { return m_max_y; }
        void SetMaxY(float max_z) { m_max_y = max_z; }

        // parameters - world levels
        float GetSeaLevel() const          { return m_level_sea; }
        void SetSeaLevel(float level)      { m_level_sea = level; }
        float GetSnowLevel() const         { return m_level_snow; }
        void SetSnowLevel(float level)     { m_level_snow = level; }
        float GetShoreWidth() const        { return m_shore_width; }
        void SetShoreWidth(float width)    { m_shore_width = width; }

        // parameters - mesh generation
        uint32_t GetSmoothingPasses() const       { return m_smoothing; }
        void SetSmoothingPasses(uint32_t passes)  { m_smoothing = passes; }
        uint32_t GetDensity() const               { return m_density; }
        void SetDensity(uint32_t density)         { m_density = density; }
        uint32_t GetScale() const                 { return m_scale; }
        void SetScale(uint32_t scale)             { m_scale = scale; }
        bool GetCreateBorder() const              { return m_create_border; }
        void SetCreateBorder(bool create)         { m_create_border = create; }

        // stats
        float GetArea() const                   { return m_area_km2; }
        uint32_t GetVertexCount() const         { return m_vertex_count; }
        uint32_t GetIndexCount() const          { return m_index_count; }
        uint64_t GetHeightSampleCount() const   { return m_height_samples; }
        float* GetHeightData()                  { return !m_height_data.empty() ? &m_height_data[0] : nullptr; }
        std::shared_ptr<Material> GetMaterial() { return m_material; }
        // build the surface material and the procedural layer set, used by island and procedural forest
        void ApplyDefaultMaterial();

        // layers
        const std::array<TerrainLayerRule, terrain_layer_max>& GetLayerRules() const { return m_layer_rules; }
        std::array<TerrainLayerRule, terrain_layer_max>& GetLayerRules()             { return m_layer_rules; }
        Material* GetLayerMaterial(uint32_t index) const;
        bool IsLayerEnabled(uint32_t index) const;
        // how many of the highest weighted layers get sampled per pixel, 1 to 4
        uint32_t GetLayerQuality() const        { return m_layer_quality; }
        void SetLayerQuality(uint32_t quality);
        float GetSnowAmount() const             { return m_snow_amount; }
        void SetSnowAmount(float amount)        { m_snow_amount = amount; }
        float GetWetness() const                { return m_wetness; }
        void SetWetness(float wetness)          { m_wetness = wetness; }
        TerrainDebugView GetDebugView() const   { return m_debug_view; }
        void SetDebugView(TerrainDebugView view);
        RHI_Texture* GetAnalysisMapA() const    { return m_map_a.get(); }
        RHI_Texture* GetAnalysisMapB() const    { return m_map_b.get(); }
        RHI_Texture* GetPropMask() const        { return m_prop_mask.get(); }

        // r=grass, g=trees, b=rocks, bilinear sample in world xz
        math::Vector3 SamplePropMask(float world_x, float world_z) const;
        float SamplePropMaskChannel(float world_x, float world_z, int channel) const;

        bool GetSpawnBiomeProps() const         { return m_spawn_biome_props; }
        void SetSpawnBiomeProps(bool enabled)   { m_spawn_biome_props = enabled; }

        // multipliers on the tuned base density of each prop type, 1 is the authored look
        static constexpr float prop_density_max = 20.0f;
        float GetPropDensityTree() const          { return m_prop_density_tree; }
        void SetPropDensityTree(float density)    { m_prop_density_tree = ClampPropDensity(density); }
        float GetPropDensityRock() const          { return m_prop_density_rock; }
        void SetPropDensityRock(float density)    { m_prop_density_rock = ClampPropDensity(density); }
        float GetPropDensityFlower() const        { return m_prop_density_flower; }
        void SetPropDensityFlower(float density)  { m_prop_density_flower = ClampPropDensity(density); }
        // reload the layer materials from project/materials and hand the whole set to the renderer
        void RefreshLayers();
        // hand the current layer set, analysis maps and world mapping to the renderer
        void PushToRenderer() const;

        // generation
        void Generate();
        void CreateFlat(uint32_t base_width = 128, uint32_t base_height = 128);
        void RebuildSurface(bool update_placement = false);
        // wipe sculpt and rebuild from heightmap or flat params
        void Regenerate();
        // restore one tile region from the pre-sculpt baseline
        bool RegenerateTile(uint32_t tile_index);
        // tile_N child -> 0-based index, or -1
        static int ParseTileIndex(Entity* entity);
        uint32_t GetTileCountAxis() const { return m_tile_count; }
        void SetTileCountAxis(uint32_t count);
        uint32_t GetTileEntityCount() const { return static_cast<uint32_t>(m_tile_offsets.size()); }
        void FindTransforms(
            const uint32_t tile_index,
            const TerrainProp terrain_prop,
            Entity* entity,
            const float density_fraction,
            const float scale,
            std::vector<math::Matrix>& transforms_out
        );

        // sculpting
        bool HasHeightfield() const { return !m_positions.empty() && m_dense_width > 1 && m_dense_height > 1; }
        // dense grid in entity local space, indexed as row_z * dense_width + column_x
        const std::vector<math::Vector3>& GetPositions() const { return m_positions; }
        uint32_t GetDenseWidth() const  { return m_dense_width; }
        uint32_t GetDenseHeight() const { return m_dense_height; }
        bool Raycast(const math::Ray& ray, math::Vector3& hit_out) const;
        bool SampleHeight(float world_x, float world_z, float& height_out) const;
        // world-space unit normal at xz, returns false if no heightfield
        bool SampleNormal(float world_x, float world_z, math::Vector3& normal_out) const;
        void ApplyBrush(const math::Vector3& world_center, const TerrainBrush& brush);
        // level the heightfield inside a world space xz rectangle, ramped back to the original
        // ground over blend_margin so the pad does not end in a cliff
        bool FlattenRegion(
            float world_min_x,
            float world_min_z,
            float world_max_x,
            float world_max_z,
            float world_height,
            float blend_margin
        );
        TerrainGridMapping GetGridMapping() const;
        // bend map borders down to sea level so the ocean meets land
        void MakeIslandShore();

        // find the first terrain with a heightfield in the loaded world
        static Terrain* FindActive();
        // snap this entity and any mesh descendants onto the surface below each one
        static bool SnapEntityToTerrain(Entity* entity, float offset = 0.0f);
        static uint32_t SnapEntitiesToTerrain(const std::vector<Entity*>& entities, float offset = 0.0f);
        // flatten the ground under the horizontal footprint of the selection, then snap onto it
        static bool SnapEntityToFlatTerrain(Entity* entity, float offset = 0.0f);
        static uint32_t SnapEntitiesToFlatTerrain(const std::vector<Entity*>& entities, float offset = 0.0f);

        // component io
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // cache io
        void SaveToFile(const char* file_path);
        void LoadFromFile(const char* file_path);

        // triangle data access for placement system
        std::unordered_map<uint64_t, std::vector<TriangleData>>& GetTriangleData() { return m_triangle_data; }

    private:
        void Clear();
        // clear mesh refs on tile renders before deferred entity delete frees geometry
        void DetachTileMeshes();
        void ClearTileEntities();
        void CreateTileEntities();
        // static heightfield body covering the whole surface, rebuilt whenever the grid changes
        void RefreshPhysics();
        void BakeHeightMapTexture();
        // curvature, flow, occlusion, insolation, wear, deposition and talus into two rgba8 textures
        void BakeTerrainMaps();
        void BakePropMask();
        bool LoadTerrainMapsFromCache();
        void SaveTerrainMapsToCache() const;
        void SnapshotBaseline();
        uint64_t ComputeCacheHash() const;

        // textures
        RHI_Texture* m_height_map_seed                  = nullptr;
        std::shared_ptr<RHI_Texture> m_height_map_final = nullptr;
        // previous bake kept alive so imgui cannot reference a destroyed texture mid-frame
        std::shared_ptr<RHI_Texture> m_height_map_final_retired;
        // world-space y in r32, consumed by grass_populate
        std::shared_ptr<RHI_Texture> m_height_map_gpu = nullptr;
        std::shared_ptr<RHI_Texture> m_height_map_gpu_retired;

        // baked heightfield analysis, see the tex_terrain_map_a/b comment in common_resources.hlsl
        std::shared_ptr<RHI_Texture> m_map_a;
        std::shared_ptr<RHI_Texture> m_map_b;
        std::shared_ptr<RHI_Texture> m_prop_mask;
        std::shared_ptr<RHI_Texture> m_map_a_retired;
        std::shared_ptr<RHI_Texture> m_map_b_retired;
        std::shared_ptr<RHI_Texture> m_prop_mask_retired;
        std::vector<uint8_t> m_map_a_pixels; // mip 0 rgba8, kept so the cache write does not re-derive it
        std::vector<uint8_t> m_map_b_pixels;
        std::vector<uint8_t> m_prop_mask_pixels; // r=grass g=trees b=rocks
        uint32_t m_map_width  = 0;
        uint32_t m_map_height = 0;
        static constexpr float ClampPropDensity(float density)
        {
            return density < 0.0f ? 0.0f : (density > prop_density_max ? prop_density_max : density);
        }

        bool m_spawn_biome_props = true;
        // the base densities live in WorldHelpers, these scale them so a world can be sparser or
        // denser without recompiling, rocks need the headroom most, their base is very low
        float m_prop_density_tree   = 1.0f;
        float m_prop_density_rock   = 1.0f;
        float m_prop_density_flower = 1.0f;
        float m_height_bake_min = 0.0f;
        float m_height_bake_max = 1.0f;

        // configurable parameters
        float m_min_y          = 0.0f;
        float m_max_y          = 755.0f;
        float m_level_sea      = 0.0f;
        float m_level_snow     = 400.0f;
        float m_shore_width    = 2000.0f;
        uint32_t m_smoothing   = 0;
        uint32_t m_density     = 1;
        uint32_t m_scale       = 25;
        uint32_t m_tile_count  = 16;
        bool m_create_border   = false;

        // runtime state
        uint32_t m_width                  = 0;
        uint32_t m_height                 = 0;
        float m_area_km2                  = 0.0f;
        std::atomic<bool> m_is_generating = false;
        uint32_t m_height_samples         = 0;
        uint32_t m_vertex_count           = 0;
        uint32_t m_index_count            = 0;
        uint32_t m_triangle_count         = 0;
        uint32_t m_dense_width            = 0;
        uint32_t m_dense_height           = 0;

        // geometry data
        std::vector<float> m_height_data;
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>> m_tile_vertices;
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices;
        std::vector<uint32_t> m_indices;
        std::vector<std::vector<uint32_t>> m_tile_indices;
        std::shared_ptr<Mesh> m_mesh;
        // the surface material, tile renders point at this one, it carries no layer textures of its
        // own, only the terrain flag and the pointer to where the layer table starts
        std::shared_ptr<Material> m_material;
        // one ordinary material per layer, a null entry is a layer whose folder is missing
        std::array<std::shared_ptr<Material>, terrain_layer_max> m_layer_materials;
        std::array<TerrainLayerRule, terrain_layer_max> m_layer_rules;
        // seven layers exist by default, two picks per pixel is not enough to read as more than a
        // two tone surface anywhere the rules overlap, three is where the ground starts looking mixed
        uint32_t m_layer_quality      = 3;
        float m_snow_amount           = 1.0f;
        float m_wetness               = 0.0f;
        TerrainDebugView m_debug_view = TerrainDebugView::Off;
        // xy = world min xz, zw = 1 / world size xz, maps world position onto the analysis maps
        math::Vector4 m_world_mapping = math::Vector4::Zero;
        // what the erosion simulation moved, only populated on a from-scratch generate
        TerrainErosionMaps m_erosion_maps;
        std::vector<math::Vector3> m_tile_offsets;
        std::vector<math::Vector3> m_positions;
        // heights right after generate/create flat, used to undo sculpt
        std::vector<math::Vector3> m_positions_baseline;

        // placement data (per-terrain, not static)
        std::unordered_map<uint64_t, std::vector<TriangleData>> m_triangle_data;
    };
}
