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
#include <algorithm>
#include <array>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../rhi/RHI_Definitions.h"
#include "../../math/Matrix.h"
#include "../../math/Quaternion.h"
#include "../../math/Ray.h"
#include "../../math/Vector4.h"
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

    // everything the baked analysis knows about one point on the surface, every channel is 0 to 1
    // this is what a scatter rule reads, the same signals the surface shader reads
    struct TerrainSurfaceSample
    {
        float curvature       = 0.5f;
        float flow            = 0.0f;
        float occlusion       = 1.0f;
        float deposition      = 0.0f;
        float wear            = 0.0f;
        float insolation      = 0.5f;
        float talus           = 0.0f;
        float mask_grass      = 0.0f;
        float mask_trees      = 0.0f;
        float mask_rocks      = 0.0f;
        // which surface layer won this point, matches the layer rule index
        uint32_t dominant_layer = 0;
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

    // a level pad cut or filled under a snapped floor, stored with the world so generate rebuilds it
    struct TerrainPlatform
    {
        uint64_t entity_id = 0;
        float min_x        = 0.0f;
        float min_z        = 0.0f;
        float max_x        = 0.0f;
        float max_z        = 0.0f;
        float center_x     = 0.0f;
        float center_z     = 0.0f;
        float half_x       = 0.0f;
        float half_z       = 0.0f;
        float yaw          = 0.0f;
        float height       = 0.0f;
        float margin       = 0.0f;
    };

    // inclusive cell rect on a grid, empty until something merges into it
    struct TerrainDirtyRect
    {
        int32_t x0 = 0;
        int32_t z0 = 0;
        int32_t x1 = -1;
        int32_t z1 = -1;

        bool IsEmpty() const { return x1 < x0 || z1 < z0; }
        void Clear()         { x0 = 0; z0 = 0; x1 = -1; z1 = -1; }
        void Merge(int32_t ax0, int32_t az0, int32_t ax1, int32_t az1)
        {
            if (ax1 < ax0 || az1 < az0)
            {
                return;
            }

            if (IsEmpty())
            {
                x0 = ax0;
                z0 = az0;
                x1 = ax1;
                z1 = az1;
                return;
            }

            x0 = std::min(x0, ax0);
            z0 = std::min(z0, az0);
            x1 = std::max(x1, ax1);
            z1 = std::max(z1, az1);
        }
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
        // metres of ground that creep over anything sinking into the surface, 0 disables the blend
        float GetBlendHeight() const            { return m_blend_height; }
        void SetBlendHeight(float height)       { m_blend_height = height; }
        TerrainDebugView GetDebugView() const   { return m_debug_view; }
        void SetDebugView(TerrainDebugView view);
        RHI_Texture* GetAnalysisMapA() const    { return m_map_a.get(); }
        RHI_Texture* GetAnalysisMapB() const    { return m_map_b.get(); }
        RHI_Texture* GetPropMask() const        { return m_prop_mask.get(); }
        void RebuildPropMask();
        // after biome scatter, keep instance seeds then hide props on occupied pads
        void OnBiomePropsPopulated();
        // same bookkeeping after a partial scatter, only the tiles that were re-placed are visited
        void OnBiomePropsRepopulated(const std::vector<uint32_t>& tile_indices);
        // drop the seed entries of a prop root that is about to be removed, and of everything under it
        void ForgetPropSeeds(Entity* prop_root);
        // recompute the placement triangles of these tiles from the live mesh, sculpted ground
        // would otherwise hand the scatter rules the heights from before the edit
        void RefreshPlacementData(const std::vector<uint32_t>& tile_indices);
        // builds only the tiles that have no placement triangles yet, call before parallel scatter
        void EnsurePlacementData(const std::vector<uint32_t>& tile_indices);
        void MarkSplinePropCarvesDirty(uint64_t spline_id = 0);
        // roads grade the ground they sit on, queue the affected corridor for a re-carve
        void MarkSplineHeightCarvesDirty(uint64_t spline_id = 0);
        // every road whose carve footprint overlaps the dense grid rect
        void MarkRoadCarvesDirtyInGridRect(int32_t x0, int32_t z0, int32_t x1, int32_t z1);

        // r=grass, g=trees, b=rocks, bilinear sample in world xz
        math::Vector3 SamplePropMask(float world_x, float world_z) const;
        float SamplePropMaskChannel(float world_x, float world_z, int channel) const;

        bool GetSpawnBiomeProps() const         { return m_spawn_biome_props; }
        void SetSpawnBiomeProps(bool enabled)   { m_spawn_biome_props = enabled; }

        // scatter layers, the prop rule set, authored per world and saved with it
        const std::array<TerrainScatterLayer, terrain_scatter_max>& GetScatterLayers() const { return m_scatter_layers; }
        std::array<TerrainScatterLayer, terrain_scatter_max>& GetScatterLayers()             { return m_scatter_layers; }
        // a layer only scatters when it is switched on, has an asset, and nothing else is soloed
        bool IsScatterSoloed() const;
        bool IsScatterActive(const TerrainScatterLayer& layer) const;
        // ground area one instance of density 1 covers, used to turn instances per hectare into a count
        float GetTriangleArea() const;
        // sea and snow in entity local y, triangle heights are local and the levels are world, the
        // sea comes from the water component when there is one, every cpu and gpu path reads these
        float GetSeaLevelLocal() const;
        float GetSnowLevelLocal() const;
        float ResolveSeaLevelWorld() const;
        // baked analysis and biome mask at a world xz, false when nothing is baked yet
        bool SampleSurface(float world_x, float world_z, TerrainSurfaceSample& sample_out) const;
        // reload the layer materials from project/materials and hand the whole set to the renderer
        void RefreshLayers();
        // hand the current layer set, analysis maps and world mapping to the renderer
        void PushToRenderer() const;
        bool IsGenerating() const { return m_is_generating.load(); }
        void Tick() override;

        // generation
        void Generate();
        void CreateFlat(uint32_t base_width = 128, uint32_t base_height = 128);
        void RebuildSurface(bool update_placement = false, bool preview = false);
        // rebuild from heightmap or flat params, the sculpt layer and the pads are applied on top again
        void Regenerate();
        // remove the sculpt layer from one tile so it shows the procedural ground again
        bool RegenerateTile(uint32_t tile_index);
        // hand sculpting lives here, not in the heightfield, so it survives a regenerate and is
        // saved next to the world, brush strokes record into it, pads and roads stay derived
        const TerrainSculptLayer& GetSculptLayer() const { return m_sculpt; }
        void ClearSculptLayer();
        void SaveSculptLayer(const std::string& directory) const;
        // tile_N child -> 0-based index, or -1
        static int ParseTileIndex(Entity* entity);
        uint32_t GetTileCountAxis() const { return m_tile_count; }
        void SetTileCountAxis(uint32_t count);
        uint32_t GetTileEntityCount() const { return static_cast<uint32_t>(m_tile_offsets.size()); }
        // place one scatter layer over one tile, the transforms come back tile local so every prop
        // can be parented to its tile and inherit the tile offset
        void FindTransforms(
            const uint32_t tile_index,
            const TerrainScatterLayer& layer,
            std::vector<math::Matrix>& transforms_out,
            float* coverage_out = nullptr
        );

        // sculpting
        // false while a worker is rebuilding the arrays, so every sampler bails instead of reading a
        // vector that is being resized on another thread
        bool HasHeightfield() const { return !IsHeightfieldUnsafe() && !m_positions.empty() && m_dense_width > 1 && m_dense_height > 1; }
        // dense grid in entity local space, indexed as row_z * dense_width + column_x
        const std::vector<math::Vector3>& GetPositions() const { return m_positions; }
        uint32_t GetDenseWidth() const  { return m_dense_width; }
        uint32_t GetDenseHeight() const { return m_dense_height; }
        bool Raycast(const math::Ray& ray, math::Vector3& hit_out) const;
        bool SampleHeight(float world_x, float world_z, float& height_out) const;
        // height with every road cut and fill taken back out, roads conform to this so they cannot
        // chase the ground they just moved
        bool SampleHeightBase(float world_x, float world_z, float& height_out) const;
        bool HasRoadCarve() const { return !m_road_carve_delta.empty(); }
        // world-space unit normal at xz, returns false if no heightfield
        bool SampleNormal(float world_x, float world_z, math::Vector3& normal_out) const;
        void ApplyBrush(const math::Vector3& world_center, const TerrainBrush& brush);
        // incremental edit path, every height edit marks the dense cells it touched and one flush
        // repairs only that region, mesh, normals and the gpu height texture every call, collision
        // and the prop mask only on a commit flush, a full RebuildSurface is only needed when the
        // whole map changes shape, shoreline, flow carving, a new heightmap
        void MarkHeightsDirty(int32_t x0, int32_t z0, int32_t x1, int32_t z1);
        void MarkHeightsDirtyLocal(float local_min_x, float local_min_z, float local_max_x, float local_max_z);
        bool HasPendingHeightEdits() const { return !m_height_dirty.IsEmpty(); }
        // true when a gpu texture had to be recreated and the renderer was already told
        bool FlushHeightEdits(bool commit);
        void FlushPendingPhysics();
        bool FlushPendingPropMask();
        // re-place the mesh props on the tiles a height edit touched, nothing else moves, the
        // brush marks this on every flush and the editor fires it once the stroke ends
        void FlushPendingProps();
        bool HasPendingPhysics() const { return !m_physics_dirty.IsEmpty(); }
        bool HasPendingProps() const   { return !m_props_dirty.IsEmpty(); }
        // level the heightfield inside a world space xz obb, ramped back to the original
        // ground over blend_margin so the pad does not end in a cliff
        bool FlattenRegion(
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw,
            float world_height,
            float blend_margin
        );
        TerrainGridMapping GetGridMapping() const;
        // bend map borders down to sea level so the ocean meets land
        void MakeIslandShore();
        // raise the real coastline above the waves and cut a beach, no full regenerate
        void LockShoreline();
        // drop near-sea flow channels just below the ocean so water shows through
        void CarveFlowChannels();
        // trace the flow map into spline ribbons that use the ocean water material
        void SpawnFlowRivers();

        // find the first terrain with a heightfield in the loaded world
        static Terrain* FindActive();
        // snap this entity and any mesh descendants onto the surface below each one, a large floor
        // under the group levels the group, drops it onto the ground, then cuts a pad to meet it
        static bool SnapEntityToTerrain(Entity* entity, float offset = 0.0f);
        static uint32_t SnapEntitiesToTerrain(
            const std::vector<Entity*>& entities,
            float offset = 0.0f,
            bool build_platform = true
        );
        // level the selection, drop it onto the ground, flatten a pad under it, keep it upright
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
        void BakeHeightMapPixels();
        void UploadHeightMapTextures();
        void BakeHeightMapTexture();
        // curvature, flow, occlusion, insolation, wear, deposition and talus into two rgba8 textures
        // allow_cache false when the heights carry sculpt or pads, the cache only describes procedural ground
        void BakeTerrainMaps(bool allow_cache);
        void UploadTerrainMaps();
        void BuildCpuMesh();
        void BuildTileMesh(Mesh& mesh, const bool generate_lods);
        void CommitGpu();
        void CommitProps();
        void FinishGenerate();
        void BakePropMask();
        // map space rect, inclusive, the full bake and the region rebake share this
        bool BakePropMaskCells(int32_t mx0, int32_t mz0, int32_t mx1, int32_t mz1);
        // after a full bake, seed from the fresh pixels and punch the road and pad holes again
        void ReapplyPropMaskHoles();
        bool LoadTerrainMapsFromCache();
        void SaveTerrainMapsToCache() const;
        // sculpt layer plumbing, the lattice follows the dense grid so cell writes are exact
        void EnsureSculptGrid();
        // add the layer onto freshly generated ground, true when any cell moved
        bool ApplySculptLayer();
        // undo the layer inside a local rect, repaint the pads that overlap it, flush the region
        void ClearSculptInRect(float min_x, float min_z, float max_x, float max_z);
        uint64_t ComputeCacheHash() const;
        float ResolveSeaLevelLocal() const;
        float GetEntityY() const;
        // true for any thread other than the generating worker while it is rebuilding the arrays
        bool IsHeightfieldUnsafe() const;
        // the local mapping shifted into world xz, for every sampler that is handed world coordinates
        math::Vector4 GetMappingWorld() const;
        bool ApplyShorelineLock();
        bool ApplyFlowChannelCarve();
        void RememberPlatform(const TerrainPlatform& platform);
        void PruneOrphanPlatforms();
        void ApplyPlatformsToProps();
        void RefreshSplinePropCarves();
        void ApplySplineCarveToProps(float min_x, float min_z, float max_x, float max_z);
        // rebuild the road cut and fill inside whatever corridor changed, then repair that region only
        void RefreshSplineHeightCarves();
        void ClearRoadCarve();
        void PatchTilesInRegion(float local_min_x, float local_min_z, float local_max_x, float local_max_z);
        void RebuildPhysicsInRegion(float local_min_x, float local_min_z, float local_max_x, float local_max_z);
        void PunchPropMaskFootprint(
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw
        );
        void ClearFootprintProps(
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw
        );
        void RestoreFootprintProps(
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw
        );
        void SnapshotPropInstances();
        // record the instance seeds of every prop under root, the full snapshot walks the terrain,
        // a partial scatter walks its tiles
        void SnapshotPropSeedsUnder(Entity* root, bool inside_prop);
        // pixel rect writers call this so the upload can stay a sub-region copy
        void MarkPropMaskDirty(int32_t x0, int32_t z0, int32_t x1, int32_t z1);
        // true when the texture object was replaced and the gpu scatter needs the new pointer
        bool UploadPropMask();
        // true when the r32 texture had to be recreated instead of patched
        bool UploadHeightRegion(const TerrainDirtyRect& rect);
        // tile indices whose footprint overlaps a local space aabb
        void CollectTilesInRegion(
            float local_min_x,
            float local_min_z,
            float local_max_x,
            float local_max_z,
            std::unordered_set<uint32_t>& tiles_out
        ) const;
        // same, for a world space aabb, the rect is brought into entity space first
        void CollectTilesInWorldRect(
            float world_min_x,
            float world_min_z,
            float world_max_x,
            float world_max_z,
            std::unordered_set<uint32_t>& tiles_out
        ) const;
        // prop renderers under tiles that overlap a world aabb, everything else is skipped untouched
        void CollectPropRendersInWorldRect(
            float world_min_x,
            float world_min_z,
            float world_max_x,
            float world_max_z,
            bool cull_by_bounds,
            std::vector<Entity*>& props_out
        );
        void ApplyFlattenToPositions(
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw,
            float world_height,
            float blend_margin
        );
        void ForgetPlatform(uint64_t entity_id);
        void SnapshotSeed();
        void UpdateLivePads();
        void PaintPadFromSeed(
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw,
            float world_height,
            float blend_margin,
            bool restore_only
        );
        bool SampleSeedMax(
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw,
            float& out_max
        );
        void CommitLivePad(bool punch);
        void RestoreLivePad();
        void RestorePlatform(const TerrainPlatform& pad);
        void PruneVanishedPlatforms();
        void RestorePropMaskFootprint(
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw
        );
        void DestroyPadOverlays();
        void DestroyPadRefine(uint64_t entity_id);
        void DestroyAllPadRefines();
        void SyncPadRefine(const TerrainPlatform& pad, bool cook_physics);
        void RebuildCommittedRefines();
        void RestampPropsForPad(const TerrainPlatform& pad, bool restore);
        void SnapPropsToSurface(
            float center_x,
            float center_z,
            float deform_hx,
            float deform_hz,
            float object_hx,
            float object_hz,
            float yaw
        );
        void SyncLivePadVisuals(const TerrainPlatform* restore, const TerrainPlatform* paint);
        bool ApplyPlatformsToHeightfield();
        bool BuildSnapPlatform(const std::vector<Entity*>& entities, bool require_floor);
        void RebuildMeshData(bool update_placement);

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
        std::vector<uint8_t> m_prop_mask_seed;
        std::vector<uint8_t> m_spline_carve_bits;
        bool m_spline_carve_dirty = false;
        bool m_spline_carve_dirty_all = false;
        std::unordered_set<uint64_t> m_spline_carve_dirty_ids;
        std::unordered_map<uint64_t, std::array<int32_t, 4>> m_spline_carve_spline_bounds;
        int32_t m_spline_carve_x0 = 0;
        int32_t m_spline_carve_x1 = -1;
        int32_t m_spline_carve_z0 = 0;
        int32_t m_spline_carve_z1 = -1;
        // height the roads added to the dense grid, subtracting it gives the untouched ground back
        std::vector<float> m_road_carve_delta;
        std::unordered_map<uint64_t, std::array<int32_t, 4>> m_road_carve_bounds;
        std::unordered_set<uint64_t> m_road_carve_dirty_ids;
        bool m_road_carve_dirty     = false;
        bool m_road_carve_dirty_all = false;
        // full scatter instances, live pads hide from this and restore when the pad leaves
        std::unordered_map<uint64_t, std::vector<math::Matrix>> m_prop_instance_seed;
        std::unordered_map<uint64_t, bool> m_prop_entity_seed;
        std::vector<uint8_t> m_layer_dominant;   // one surface layer index per analysis cell
        std::vector<uint8_t> m_height_gpu_bytes;
        std::vector<uint8_t> m_height_preview_bytes;
        uint32_t m_map_width  = 0;
        uint32_t m_map_height = 0;

        bool m_spawn_biome_props = true;
        std::array<TerrainScatterLayer, terrain_scatter_max> m_scatter_layers;
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
        // a worker is inside generate, the heightfield arrays are being resized under it, samplers
        // on other threads bail instead of reading them, the destructor waits for it
        std::atomic<uint32_t> m_worker_busy = 0;
        std::thread::id m_worker_thread;
        std::atomic<bool> m_gpu_commit_pending = false;
        std::atomic<bool> m_props_commit_pending = false;
        std::shared_ptr<Mesh> m_mesh_pending;
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
        float m_blend_height          = 0.35f;
        TerrainDebugView m_debug_view = TerrainDebugView::Off;
        // xy = world min xz, zw = 1 / world size xz, maps world position onto the analysis maps
        math::Vector4 m_world_mapping = math::Vector4::Zero;
        // what the erosion simulation moved, only populated on a from-scratch generate
        TerrainErosionMaps m_erosion_maps;
        std::vector<math::Vector3> m_tile_offsets;
        std::vector<math::Vector3> m_positions;
        // unpadded heightfield, procedural ground plus the sculpt layer, live pads restore from this
        std::vector<math::Vector3> m_positions_seed;
        // hand sculpting, survives generate, saved with the world, never cleared by Clear()
        TerrainSculptLayer m_sculpt;
        // pads under buildings, replayed after generate from the seed heightfield
        std::vector<TerrainPlatform> m_platforms;
        bool m_live_pad_active           = false;
        TerrainPlatform m_live_pad;
        bool m_live_pad_dirty            = false;
        double m_live_pad_changed_ms     = 0.0;
        uint64_t m_live_track_entity     = 0;
        math::Vector3 m_live_track_position = math::Vector3::Zero;
        math::Quaternion m_live_track_rotation;
        math::Vector3 m_live_track_scale = math::Vector3::One;
        // dense pad meshes, one per occupied floor, collision included
        std::unordered_map<uint64_t, std::shared_ptr<Mesh>> m_pad_refine_meshes;
        // dense cells edited since the last flush, and cells whose collision still needs a rebuild
        TerrainDirtyRect m_height_dirty;
        TerrainDirtyRect m_physics_dirty;
        // prop mask pixels rewritten since the last upload
        TerrainDirtyRect m_prop_mask_dirty;
        // dense cells whose slope changed, the biome mask under them rebakes on the next commit
        TerrainDirtyRect m_prop_mask_bake_dirty;
        // dense cells whose props still sit on the old ground, re-placed by FlushPendingProps
        TerrainDirtyRect m_props_dirty;

        // placement data (per-terrain, not static)
        std::unordered_map<uint64_t, std::vector<TriangleData>> m_triangle_data;
    };
}
