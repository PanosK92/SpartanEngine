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

//= INCLUDES ========
#include <array>
#include <cstdint>
#include <string>
//===================

namespace spartan
{
    // hard cap on terrain layers, the shader walks this many rules per pixel before picking the
    // dominant few, every extra slot is a handful of alu so the cap is generous
    const uint32_t terrain_layer_max = 8;

    // must match the terrain_flags bits in shared_buffers.h MaterialParameters
    enum TerrainLayerFlags : uint32_t
    {
        TerrainLayerFlags_None      = 0,
        TerrainLayerFlags_Biplanar  = 1u << 0, // project on the two dominant axes, kills cliff stretching
        TerrainLayerFlags_Pom       = 1u << 1, // parallax occlusion march when this layer dominates up close
        TerrainLayerFlags_Snow      = 1u << 2, // driven by the snow accumulation model instead of the generic rule
        TerrainLayerFlags_BelowSea  = 1u << 3, // height range is measured against sea level, not absolute world y
        TerrainLayerFlags_HasMaps   = 1u << 4  // set on the surface material once the analysis maps are baked
    };

    // viewport debug views, packed into bits 12 to 15 of the surface material's terrain_flags
    // must match the terrain_debug_* constants in common_terrain.hlsl
    enum class TerrainDebugView : uint32_t
    {
        Off,
        Weights,
        DominantLayer,
        Curvature,
        Flow,
        Occlusion,
        Insolation,
        Deposition,
        Wear,
        Talus,
        Wetness,
        Max
    };

    // one procedural rule, the shader turns this into a weight per pixel
    // ranges are soft, weight ramps in over the first half of the band and out over the second half
    struct TerrainLayerRule
    {
        // folder under project/materials, textures are albedo/normal/roughness/occlusion/height .png
        std::string name;

        // slope band in degrees from horizontal
        float slope_min = 0.0f;
        float slope_max = 90.0f;

        // altitude band in meters, relative to sea level when TerrainLayerFlags_BelowSea is set
        float height_min = -100000.0f;
        float height_max = 100000.0f;

        // analysis map influences, signed, zero means the layer ignores that channel
        float curvature_influence  = 0.0f; // positive favours concave (gullies), negative favours convex (ridges)
        float flow_influence       = 0.0f; // positive favours water channels
        float occlusion_influence  = 0.0f; // positive favours crevices and valley floors
        float insolation_influence = 0.0f; // positive favours sun facing slopes
        float wear_influence       = 0.0f; // positive favours eroded bedrock
        float deposition_influence = 0.0f; // positive favours accumulated sediment
        float talus_influence      = 0.0f; // positive favours scree below cliffs

        // appearance
        float tiling_scale   = 1.0f; // multiplies the terrain uv
        float blend_contrast = 0.2f; // height blend band width, smaller is sharper
        float porosity       = 0.5f; // wet darkening amount
        float macro_strength = 1.0f; // large scale colour breakup amount
        float weight_bias    = 1.0f; // overall priority against the other layers

        uint32_t flags = TerrainLayerFlags_None;
    };

    // the default rule set, tuned so a terrain with only grass, rock and sand present still reads
    // correctly, and gains dirt, gravel, snow, forest floor and moss the moment those folders exist
    class TerrainLayerDefaults
    {
    public:
        static const std::array<TerrainLayerRule, terrain_layer_max>& Get();
    };
}
