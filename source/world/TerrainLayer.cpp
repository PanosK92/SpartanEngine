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

//= INCLUDES ============
#include "pch.h"
#include "TerrainLayer.h"
//=======================

using namespace std;

namespace spartan
{
    const array<TerrainLayerRule, terrain_layer_max>& TerrainLayerDefaults::Get()
    {
        static array<TerrainLayerRule, terrain_layer_max> rules;
        static bool built = false;
        if (built)
        {
            return rules;
        }

        // layer 0, grass, the ground cover, rock only takes over once the slope is actually a face
        {
            TerrainLayerRule& r     = rules[0];
            r.name                  = "whispy_grass_meadow";
            r.slope_min             = 0.0f;
            r.slope_max             = 52.0f;
            r.height_min            = 1.0f;
            r.flags                 = TerrainLayerFlags_BelowSea;
            r.insolation_influence  = 0.25f;
            r.occlusion_influence   = -0.15f;
            r.curvature_influence   = -0.1f;
            r.deposition_influence  = 0.2f;
            r.tiling_scale          = 1.0f;
            r.blend_contrast        = 0.45f;
            r.porosity              = 0.7f;
            r.macro_strength        = 1.0f;
            r.weight_bias           = 1.35f;
        }

        // layer 1, rock, the cliff face, biplanar because planar xz smears on anything vertical
        {
            TerrainLayerRule& r    = rules[1];
            r.name                 = "rock";
            r.slope_min            = 48.0f;
            r.slope_max            = 90.0f;
            r.flags                = TerrainLayerFlags_Biplanar;
            r.curvature_influence  = -0.35f;
            r.wear_influence       = 0.35f;
            r.deposition_influence = -0.3f;
            r.tiling_scale         = 0.6f;
            r.blend_contrast       = 0.4f;
            r.porosity             = 0.25f;
            r.macro_strength       = 0.6f;
            r.weight_bias          = 0.8f;
        }

        // layer 2, sand, the shoreline and anything sitting at or below the water line
        {
            TerrainLayerRule& r    = rules[2];
            r.name                 = "sand";
            r.slope_min            = 0.0f;
            r.slope_max            = 30.0f;
            r.height_min           = -100000.0f;
            r.height_max           = 4.0f;
            r.flags                = TerrainLayerFlags_BelowSea;
            r.deposition_influence = 0.35f;
            r.flow_influence       = 0.15f;
            r.tiling_scale         = 1.4f;
            r.blend_contrast       = 0.3f;
            r.porosity             = 0.9f;
            r.macro_strength       = 0.5f;
            r.weight_bias          = 1.1f;
        }

        // layer 3, dirt, exposed soil where grass cannot hold, mid slopes and eroded shoulders
        {
            TerrainLayerRule& r    = rules[3];
            r.name                 = "dirt";
            r.slope_min            = 22.0f;
            r.slope_max            = 55.0f;
            r.height_min           = 1.0f;
            r.flags                = TerrainLayerFlags_BelowSea;
            r.curvature_influence  = 0.2f;
            r.wear_influence       = 0.35f;
            r.occlusion_influence  = 0.2f;
            r.insolation_influence = 0.15f;
            r.tiling_scale         = 1.1f;
            r.blend_contrast       = 0.22f;
            r.porosity             = 0.8f;
            r.macro_strength       = 0.9f;
            r.weight_bias          = 0.9f;
        }

        // layer 4, gravel, scree fans below cliffs, keyed off talus and deposition together
        // the slope floor has to clear the ramp width, scree does not collect on level ground
        //
        // no flow influence, that channel is a topographic wetness index and keying scree off it paints
        // the whole drainage network in gravel, which is the one pattern this layer must not produce
        {
            TerrainLayerRule& r    = rules[4];
            r.name                 = "gravel";
            r.slope_min            = 26.0f;
            r.slope_max            = 42.0f;
            r.talus_influence      = 0.55f;
            r.deposition_influence = 0.3f;
            r.curvature_influence  = 0.1f;
            r.tiling_scale         = 1.2f;
            r.blend_contrast       = 0.15f;
            r.porosity             = 0.55f;
            r.macro_strength       = 0.7f;
            r.weight_bias          = 0.85f;
        }

        // layer 5, snow, weight comes from the accumulation model, not the generic rule
        // the height band still gates it so the snow line stays where the terrain says it is
        {
            TerrainLayerRule& r   = rules[5];
            r.name                = "snow";
            r.slope_min           = 0.0f;
            r.slope_max           = 55.0f;
            r.flags               = TerrainLayerFlags_Snow;
            r.curvature_influence = 0.3f;
            r.occlusion_influence = 0.25f;
            r.tiling_scale        = 0.9f;
            r.blend_contrast      = 0.08f; // sharp, snow sits in crevices instead of fading over them
            r.porosity            = 0.15f;
            r.macro_strength      = 0.35f;
            r.weight_bias         = 1.0f;
        }

        // layer 6, forest floor, shaded north facing slopes where litter accumulates
        {
            TerrainLayerRule& r    = rules[6];
            r.name                 = "forest_floor";
            r.slope_min            = 0.0f;
            r.slope_max            = 35.0f;
            r.height_min           = 4.0f;
            r.flags                = TerrainLayerFlags_BelowSea;
            r.insolation_influence = -0.5f;
            r.occlusion_influence  = 0.3f;
            r.curvature_influence  = 0.2f;
            r.deposition_influence = 0.25f;
            r.tiling_scale         = 1.0f;
            r.blend_contrast       = 0.28f;
            r.porosity             = 0.85f;
            r.macro_strength       = 1.0f;
            r.weight_bias          = 0.8f;
        }

        // layer 7, moss, damp shade and crevices, steeper than forest floor so it climbs rock
        {
            TerrainLayerRule& r    = rules[7];
            r.name                 = "moss";
            r.slope_min            = 0.0f;
            r.slope_max            = 50.0f;
            r.height_min           = 2.0f;
            r.flags                = TerrainLayerFlags_BelowSea;
            r.insolation_influence = -0.6f;
            r.occlusion_influence  = 0.4f;
            r.curvature_influence  = 0.15f;
            r.deposition_influence = 0.2f;
            r.tiling_scale         = 0.85f;
            r.blend_contrast       = 0.2f;
            r.porosity             = 0.9f;
            r.macro_strength       = 0.8f;
            r.weight_bias          = 0.75f;
        }

        built = true;
        return rules;
    }

    const array<TerrainScatterLayer, terrain_scatter_max>& TerrainScatterDefaults::Get()
    {
        static array<TerrainScatterLayer, terrain_scatter_max> layers;
        static bool built = false;
        if (built)
        {
            return layers;
        }

        // scatter 0, trees, groves on living ground, they stand upright because a trunk that leans
        // with the slope reads as broken rather than as natural
        {
            TerrainScatterLayer& s = layers[0];
            s.name                 = "trees";
            s.mesh_path            = "project/models/tree/tree.fbx";
            s.enabled              = true;
            s.density              = 9.0f;
            s.slope_max            = 36.0f;
            s.height_min           = 2.0f;
            s.height_max           = 410.0f;
            s.mask_channel         = 1;
            s.mask_min             = 0.05f;
            s.clump_radius         = 14.0f;
            s.clump_count          = 4;
            s.mesh_scale           = 0.026f;
            s.size_min             = 0.22f;
            s.size_max             = 1.35f;
            s.align_to_normal      = 0.0f;
            s.surface_offset       = 0.05f;
            s.flags                = TerrainScatterFlags_Wind |
                                     TerrainScatterFlags_ColorVariation |
                                     TerrainScatterFlags_Collision |
                                     TerrainScatterFlags_CastShadows |
                                     TerrainScatterFlags_LogSize;
        }

        // scatter 1, boulders, the landmark rocks, they only start well above the water line and
        // grow with the relief so a peak carries the big ones
        {
            TerrainScatterLayer& s = layers[1];
            s.name                 = "boulders";
            s.mesh_path            = "project/models/rock_2/model.obj";
            s.enabled              = true;
            s.density              = 11.2f;
            s.max_per_tile         = 400;
            s.slope_min            = 3.0f;
            s.slope_max            = 80.0f;
            s.slope_bias           = 0.35f;
            s.height_min           = 40.0f;
            s.height_fade          = 70.0f;
            s.clump_radius         = 75.0f;
            s.clump_count          = 4;
            s.mesh_scale           = 200.0f;
            s.size_min             = 0.53f;
            s.size_max             = 1.44f;
            s.size_from_slope      = 0.35f;
            s.size_from_altitude   = 0.65f;
            s.altitude_span        = 415.0f;
            s.align_to_normal      = 1.0f;
            s.surface_offset       = 0.0f;
            s.sink                 = 0.10f;
            s.flags                = TerrainScatterFlags_CastShadows;
        }

        // scatter 2, rock debris, small stones on the low flats, never a landmark, never a beach
        {
            TerrainScatterLayer& s = layers[2];
            s.name                 = "rock_debris";
            s.mesh_path            = "project/models/rock_2/model.obj";
            s.enabled              = true;
            s.density              = 44.8f;
            s.max_per_tile         = 3000;
            s.slope_max            = 18.0f;
            s.height_min           = 8.0f;
            s.height_max           = 38.0f;
            s.clump_radius         = 8.0f;
            s.clump_count          = 6;
            s.size_min             = 0.25f;
            s.size_max             = 2.2f;
            s.align_to_normal      = 0.0f;
            s.flags                = TerrainScatterFlags_CastShadows |
                                     TerrainScatterFlags_Tumble |
                                     TerrainScatterFlags_LogSize;
        }

        // scatter 3, flowers, dense patches on the meadow cores only, they carry no shadow because
        // the instance count is what pays for them
        {
            TerrainScatterLayer& s = layers[3];
            s.name                 = "flowers";
            s.mesh_path            = "builtin/flower";
            s.enabled              = true;
            s.density              = 5.76f;
            s.slope_max            = 18.0f;
            s.height_min           = 3.0f;
            s.height_max           = 400.0f;
            s.mask_channel         = 0;
            s.mask_min             = 0.45f;
            s.clump_radius         = 30.0f;
            s.clump_count          = 1000;
            s.mesh_scale           = 0.64f;
            s.size_min             = 0.2f;
            s.size_max             = 1.2f;
            s.render_distance      = 500.0f;
            s.flags                = TerrainScatterFlags_LogSize;
        }

        // scatter 4, grass, the gpu ring populate, density here is a fill fraction not a count
        {
            TerrainScatterLayer& s   = layers[4];
            s.name                   = "grass";
            s.mesh_path              = "builtin/grass_blade";
            s.enabled                = true;
            s.kind                   = TerrainScatterKind::Grass;
            s.density                = 0.9f;
            s.slope_max              = 24.0f;
            s.height_min             = 1.0f;
            s.height_max             = 400.0f;
            s.mask_channel           = 0;
            s.mask_min               = 0.38f;
            s.render_distance        = 500.0f;
            s.grass_ring_radius[0]   = 55.0f;
            s.grass_ring_radius[1]   = 180.0f;
            s.grass_ring_radius[2]   = 500.0f;
            s.grass_cell_size[0]     = 0.36f;
            s.grass_cell_size[1]     = 0.82f;
            s.grass_cell_size[2]     = 2.1f;
            s.flags                  = TerrainScatterFlags_None;
        }

        // slots 5 to 7 stay empty, they are there so a world can add its own scatter without
        // touching the engine, point one at a mesh, name it and switch it on
        built = true;
        return layers;
    }
}
