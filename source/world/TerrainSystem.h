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

//= INCLUDES =================
#include <vector>
#include <cstdint>
#include "../math/Vector3.h"
#include "../math/Ray.h"
#include "../rhi/RHI_Vertex.h"
//============================

namespace spartan
{
    class RHI_Texture;

    enum class TerrainBrushMode
    {
        Raise,
        Lower,
        Smooth,
        Flatten
    };

    struct TerrainBrush
    {
        TerrainBrushMode mode = TerrainBrushMode::Raise;
        float radius          = 50.0f;
        float strength        = 5.0f;
        float falloff         = 0.5f;
        float target_height   = 0.0f;
    };

    // height edits are weight * op, a brush later just builds a different weight field
    enum class TerrainEditOp
    {
        Add,     // y += amount * weight
        Set,     // lerp toward target
        LowerTo, // pull down toward target, never raise
        RaiseTo  // pull up toward target, never lower
    };

    struct TerrainHeightEdit
    {
        TerrainEditOp op = TerrainEditOp::Set;
        float amount     = 0.0f;
        float target     = 0.0f;
    };

    // remap a 0..1 source map onto a dense-grid weight, optional height band
    struct TerrainWeightFromMap
    {
        float value_low   = 0.0f;
        float value_high  = 1.0f;
        float height_min  = -1.0e8f;
        float height_max  =  1.0e8f;
        float height_soft = 0.0f;
    };

    enum class TerrainFlowSignal
    {
        Wetness, // topographic wetness, broad, what texturing uses
        Channel  // log drainage area, pinched to the actual streams
    };

    // generation recipe, same edit path a river brush would call
    struct TerrainChannelCarve
    {
        float sea_level = 0.0f;
        float bed_depth = 0.65f;
        float reach     = 14.0f;
        float flow_low  = 0.50f;
        float flow_high = 0.85f;
    };

    // one traced drainage path in terrain local space, ready for a spline ribbon
    struct TerrainFlowPath
    {
        std::vector<math::Vector3> points;
        float width_start = 8.0f;
        float width_end   = 16.0f;
    };

    // grid extents derived from density and scale, shared by generation and sculpting
    struct TerrainGridMapping
    {
        float scale_x  = 1.0f;
        float scale_z  = 1.0f;
        float offset_x = 0.0f;
        float offset_z = 0.0f;
        float extent_x = 0.0f;
        float extent_z = 0.0f;
    };

    // what the erosion simulation moved, in world units, on the dense grid
    // this is the single most useful signal for texturing and it used to be thrown away
    struct TerrainErosionMaps
    {
        std::vector<float> wear;       // material removed, exposes bedrock
        std::vector<float> deposition; // sediment at rest, buries rock detail

        bool IsValid(size_t cell_count) const { return wear.size() == cell_count && deposition.size() == cell_count; }
    };

    // heightfield analysis baked once per generate, every channel is autoleveled to 0..1
    // resolution is independent of the dense grid, these are macro signals and do not need texel parity
    struct TerrainAnalysisMaps
    {
        uint32_t width  = 0;
        uint32_t height = 0;

        std::vector<float> curvature;   // 0.5 flat, below convex (ridges), above concave (gullies)
        std::vector<float> flow;        // topographic wetness, high in channels
        std::vector<float> occlusion;   // sky visibility, 1 open, 0 buried
        std::vector<float> deposition;  // sediment accumulation
        std::vector<float> wear;        // bedrock exposure
        std::vector<float> insolation;  // fraction of the sun path that reaches the surface
        std::vector<float> height_norm; // normalized altitude
        std::vector<float> talus;       // scree, slope just under the angle of repose

        bool IsValid() const
        {
            const size_t n = static_cast<size_t>(width) * height;
            return n > 0 && curvature.size() == n && flow.size() == n && occlusion.size() == n &&
                   deposition.size() == n && wear.size() == n && insolation.size() == n &&
                   height_norm.size() == n && talus.size() == n;
        }
    };

    // core terrain algorithms, heightfield ops, and brush sculpting
    class TerrainSystem
    {
    public:
        static TerrainGridMapping ComputeGridMapping(
            uint32_t dense_width,
            uint32_t dense_height,
            uint32_t density,
            uint32_t scale
        );

        static void GetValuesFromHeightMap(
            std::vector<float>& height_data_out,
            RHI_Texture* height_texture,
            float min_y,
            float max_y,
            uint32_t smoothing,
            bool create_border
        );

        static void DensifyHeightMap(
            std::vector<float>& height_data,
            uint32_t width,
            uint32_t height,
            uint32_t density
        );

        static void GeneratePositions(
            std::vector<math::Vector3>& positions,
            const std::vector<float>& height_map,
            uint32_t width,
            uint32_t height,
            uint32_t density,
            uint32_t scale
        );

        // noise is faded out towards the water line so it can never move the coastline
        static void ApplyPerlinNoise(
            std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float level_sea,
            float amplitude = 5.0f,
            float frequency = 0.01f,
            uint32_t octaves = 4,
            float persistence = 1.0f
        );

        // hydraulic droplet erosion, thermal weathering and differential rock hardness
        // maps_out, when supplied, receives what the simulation moved so texturing can key off it
        static void ApplyErosion(
            std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float level_sea,
            float intensity = 1.0f,
            TerrainErosionMaps* maps_out = nullptr
        );

        // curvature, flow accumulation, sky occlusion, sun path insolation and talus
        // runs on a capped grid because every channel here is a macro signal, the cost of the
        // horizon marches is what sets the cap
        static void ComputeAnalysisMaps(
            TerrainAnalysisMaps& maps_out,
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float level_sea,
            const TerrainErosionMaps* erosion       = nullptr,
            uint32_t resolution_max                 = 1024
        );

        static void GenerateVerticesAndIndices(
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices,
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height
        );

        static void GenerateNormals(
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            uint32_t width,
            uint32_t height
        );

        static float ComputeSurfaceAreaKm2(
            const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            const std::vector<uint32_t>& indices
        );

        // create a flat heightfield at sea_level
        static void CreateFlatHeightfield(
            std::vector<float>& height_data_out,
            std::vector<math::Vector3>& positions_out,
            uint32_t base_width,
            uint32_t base_height,
            uint32_t density,
            uint32_t scale,
            float sea_level
        );

        static float SampleHeight(
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float world_x,
            float world_z,
            const TerrainGridMapping& mapping
        );

        // unit normal in terrain local space from neighboring height samples
        static math::Vector3 SampleNormal(
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float world_x,
            float world_z,
            const TerrainGridMapping& mapping
        );

        static bool RaycastHeightfield(
            const math::Ray& ray,
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            const TerrainGridMapping& mapping,
            math::Vector3& hit_out,
            float max_distance = 10000.0f
        );

        // mutate positions around world_center, also syncs height_data if provided
        static void ApplyBrush(
            std::vector<math::Vector3>& positions,
            std::vector<float>* height_data,
            uint32_t width,
            uint32_t height,
            const TerrainGridMapping& mapping,
            const math::Vector3& world_center,
            const TerrainBrush& brush
        );

        // weighted height edit, weights is dense width*height or null for 1
        // x0,z0,x1,z1 is the dirty rect, x1/z1 of ~0u means the full grid
        static bool ApplyHeightEdit(
            std::vector<math::Vector3>& positions,
            std::vector<float>* height_data,
            uint32_t width,
            uint32_t height,
            const TerrainHeightEdit& edit,
            const float* weights = nullptr,
            uint32_t x0 = 0,
            uint32_t z0 = 0,
            uint32_t x1 = 0xffffffffu,
            uint32_t z1 = 0xffffffffu
        );

        static void BuildWeightsFromMap(
            std::vector<float>& weights_out,
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            const float* source,
            uint32_t source_width,
            uint32_t source_height,
            const TerrainWeightFromMap& remap
        );

        static void ComputeFlowMap(
            std::vector<float>& flow_out,
            uint32_t& width_out,
            uint32_t& height_out,
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            TerrainFlowSignal signal = TerrainFlowSignal::Wetness,
            uint32_t resolution_max = 1024
        );

        static bool CarveFlowChannels(
            std::vector<math::Vector3>& positions,
            std::vector<float>* height_data,
            uint32_t width,
            uint32_t height,
            const TerrainChannelCarve& params
        );

        // walk the channel mask downhill into polylines the spline water can extrude
        static void TraceFlowPaths(
            std::vector<TerrainFlowPath>& paths_out,
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float sea_level,
            uint32_t path_max = 16
        );

        // raise the existing island above the waves, do not grow or shrink its outline
        // returns true when any cell moved by more than a centimeter
        static bool ApplyCoastalProfile(
            std::vector<math::Vector3>& positions,
            std::vector<float>* height_data,
            uint32_t width,
            uint32_t height,
            const TerrainGridMapping& mapping,
            float sea_level,
            float freeboard   = 1.6f,
            float beach_width = 32.0f
        );

        // bend samples near the map border down toward edge_height_local (smooth shore)
        static void ApplyIslandShore(
            std::vector<math::Vector3>& positions,
            std::vector<float>* height_data,
            uint32_t width,
            uint32_t height,
            const TerrainGridMapping& mapping,
            float shore_width,
            float edge_height_local
        );

        static void SyncHeightDataFromPositions(
            std::vector<float>& height_data,
            const std::vector<math::Vector3>& positions
        );
    };
}
