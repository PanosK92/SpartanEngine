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

    // scatter layers are the prop half of the same rule system, a slope band, an altitude band and
    // an analysis influence mean exactly what they mean for a surface layer above
    const uint32_t terrain_scatter_max = 8;

    enum class TerrainScatterKind : uint32_t
    {
        Mesh,   // instanced entities cloned onto every terrain tile
        Grass,  // gpu procedural grass rings, no entities exist for it
        Detail, // the same gpu rings with a solid mesh, pebbles and chips right around the camera
        Max
    };

    enum TerrainScatterFlags : uint32_t
    {
        TerrainScatterFlags_None           = 0,
        TerrainScatterFlags_Wind           = 1u << 0, // wind animation on the alpha masked parts, leaves and twigs
        TerrainScatterFlags_ColorVariation = 1u << 1, // tint each instance a little differently
        TerrainScatterFlags_Collision      = 1u << 2, // convex hull body on the opaque parts, trunks not leaves
        TerrainScatterFlags_CastShadows    = 1u << 3,
        TerrainScatterFlags_Tumble         = 1u << 4, // fully random rotation, debris that has come to rest
        TerrainScatterFlags_LogSize        = 1u << 5  // sample size logarithmically, wide ranges read better
    };

    // one prop scatter rule, everything the placer needs to answer where, how many, how big
    struct TerrainScatterLayer
    {
        std::string name;
        // mesh asset, or builtin/grass_blade and builtin/flower for the generated foliage meshes
        std::string mesh_path;
        // optional folder under project/materials, when set it replaces the imported materials
        std::string material_folder;
        bool enabled                = false;
        TerrainScatterKind kind     = TerrainScatterKind::Mesh;

        // how many, density is resolution independent so changing the mesh density does not change
        // the prop count, it is the count on ground the rules fully accept
        float density               = 8.0f;  // instances per hectare, or 0 to 1 fill for grass
        uint32_t max_per_tile       = 0;     // 0 is uncapped
        uint32_t seed               = 0;     // change this to reroll the same rules differently

        // where, slope band in degrees from horizontal
        float slope_min             = 0.0f;
        float slope_max             = 35.0f;
        // signed exponent on the position inside the band, positive favours the steep end
        float slope_bias            = 0.0f;

        // where, altitude band in meters above sea level
        float height_min            = 1.0f;
        float height_max            = 100000.0f;
        float height_fade           = 0.0f; // meters of ramp above height_min, keeps shorelines soft

        // where, analysis influences, signed, zero means the layer ignores that channel
        float curvature_influence   = 0.0f; // positive favours concave gullies, negative convex ridges
        float flow_influence        = 0.0f; // positive favours water channels
        float occlusion_influence   = 0.0f; // positive favours crevices and valley floors
        float insolation_influence  = 0.0f; // positive favours sun facing slopes
        float wear_influence        = 0.0f; // positive favours eroded bedrock
        float deposition_influence  = 0.0f; // positive favours accumulated sediment
        float talus_influence       = 0.0f; // positive favours scree below cliffs

        // where, ground type gate, bit i allows surface layer i, 0 allows every layer
        uint32_t ground_mask        = 0;
        // where, biome mask gate, -1 ignores it, 0 grass, 1 trees, 2 rocks
        int mask_channel            = -1;
        float mask_min              = 0.0f;

        // clumping, a radius of zero scatters evenly
        float clump_radius          = 0.0f; // meters
        uint32_t clump_count        = 1;    // instances per clump
        float clump_raggedness      = 1.0f; // 0 is a clean circle, 1 is an organic blob

        // size, the final scale is mesh_scale times the size pick
        float mesh_scale            = 1.0f; // asset unit fix, the size below is relative to it
        float size_min              = 0.8f;
        float size_max              = 1.2f;
        float size_from_slope       = 0.0f; // 0 to 1, steeper ground picks nearer size_max
        float size_from_altitude    = 0.0f; // 0 to 1, higher ground picks nearer size_max
        float altitude_span         = 180.0f; // meters over which size_from_altitude reaches full
        float giant_chance          = 0.0f; // 0 to 1, odds of a landmark sized instance
        float giant_size            = 0.0f; // size of that landmark, 0 falls back to size_max

        // seating on the surface
        float align_to_normal       = 1.0f;  // 0 stands upright, 1 lies flat on the slope
        float surface_offset        = 0.05f; // meters lifted off the ground, negative pushes it in
        float sink                  = 0.0f;  // fraction of the final size pushed into the ground

        // how the ground creeps over this prop where it meets the terrain, the band itself is derived
        // from the prop's own size at scatter time so a pebble takes a few centimeters where a boulder
        // takes half a meter, this height is a trim on that, 1 leaves it alone
        // sharpness is 0 for a long gradient and 1 for a hard waterline, it is a look rather than a
        // size so it stays authored per layer
        float blend_height          = 1.0f;
        float blend_sharpness       = 0.5f;

        // rendering
        float render_distance       = 0.0f;   // meters, 0 is unlimited
        float shadow_distance       = 150.0f; // meters

        // gpu kinds only, grass and detail, the three concentric rings the gpu populates around the
        // camera, the spacing is what sets the density and the reach is what sets the cost
        float grass_ring_radius[3]  = { 55.0f, 180.0f, 500.0f };
        float grass_cell_size[3]    = { 0.36f, 0.82f, 2.1f };

        uint32_t flags              = TerrainScatterFlags_CastShadows;

        // runtime only, never serialized, these exist so the editor can show what a rule produced
        // and so one layer can be inspected on its own
        bool solo                   = false;
        uint32_t instance_count     = 0;
        float coverage              = 0.0f; // fraction of the surface this layer accepted
    };

    // the default scatter set, this is the authored forest look, every value here used to be a
    // literal buried in the placement code
    class TerrainScatterDefaults
    {
    public:
        static const std::array<TerrainScatterLayer, terrain_scatter_max>& Get();
    };
}
