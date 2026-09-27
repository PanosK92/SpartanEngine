// Island biome scatter defaults. Vegetation comes from binaries/project/models/forest, built by its sources/build_forest.py.
// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
namespace spartan {
inline void ApplyIslandScatterDefaults(std::array<TerrainScatterLayer, terrain_scatter_max>& layers)
{
    {
        auto& s = layers[0];
        s = TerrainScatterLayer{};
        s.name = "pine_woodland";
        s.habitat = 1;
        s.enabled = true;
        s.density = 1000.0f;
        s.max_per_tile = 14000;
        s.seed = 1147;
        s.height_min = 4.0f;
        s.height_max = 700.0f;
        s.height_fade = 12.0f;
        s.slope_max = 42.0f;
        s.mask_channel = 1;
        s.mask_min = 0.01f;
        s.clump_radius = 30.0f;
        s.clump_count = 32;
        s.clump_raggedness = 0.7f;
        s.mesh_scale = 1.0f;
        s.size_min = 0.72f;
        s.size_max = 1.18f;
        s.align_to_normal = 0.0f;
        s.surface_offset = -0.08f;
        s.render_distance = 2400.0f;
        s.shadow_distance = 220.0f;
        s.foliage_scattering = 0.75f;
        s.flags = 109;
        s.mesh_path = "project/models/forest/pine/pine_a.gltf";
        s.mesh_variants = "project/models/forest/pine/pine_b.gltf;project/models/forest/pine/pine_c.gltf;project/models/forest/fir/fir_a.gltf;project/models/forest/fir/fir_b.gltf;project/models/forest/fir/fir_c.gltf;project/models/forest/broadleaf/broadleaf_a.gltf;project/models/forest/broadleaf/broadleaf_b.gltf;project/models/forest/broadleaf/broadleaf_c.gltf";
    }
    {
        auto& s = layers[1];
        s = TerrainScatterLayer{};
        s.name = "limestone_outcrops";
        s.habitat = 0;
        s.enabled = true;
        s.mountain_rocks = true;
        s.formation_spacing = 130.0f;
        s.formation_length = 60.0f;
        s.formation_width = 30.0f;
        s.formation_height = 16.0f;
        s.formation_jitter = 12.0f;
        s.embed_fraction = 0.48f;
        s.coating = 0.4f;
        s.density = 12.0f;
        s.max_per_tile = 700;
        s.seed = 631;
        s.slope_min = 14.0f;
        s.slope_max = 75.0f;
        s.height_min = 12.0f;
        s.height_fade = 10.0f;
        s.mask_channel = 2;
        s.mask_min = 0.01f;
        s.clump_count = 10;
        s.mesh_scale = 8.0f;
        s.size_min = 0.3f;
        s.size_max = 0.9f;
        s.align_to_normal = 0.5f;
        s.surface_offset = 0.0f;
        s.shadow_distance = 260.0f;
        s.flags = 12;
        s.mesh_path = "project/models/island_biomes/limestone_slab/limestone_slab.gltf";
        s.mesh_variants = "project/models/island_biomes/limestone_boulder/limestone_boulder.gltf";
    }
    {
        auto& s = layers[2];
        s = TerrainScatterLayer{};
        s.name = "loose_stones";
        s.habitat = 0;
        s.enabled = true;
        s.density = 110.0f;
        s.max_per_tile = 2400;
        s.seed = 2591;
        s.slope_min = 0.0f;
        s.slope_max = 70.0f;
        s.height_min = 2.0f;
        s.height_max = 800.0f;
        s.height_fade = 3.0f;
        s.mask_channel = 2;
        s.mask_min = 0.01f;
        s.talus_influence = 0.35f;
        s.deposition_influence = 0.2f;
        s.clump_radius = 14.0f;
        s.clump_count = 4;
        s.mesh_scale = 1.0f;
        s.size_min = 0.3f;
        s.size_max = 2.1f;
        s.align_to_normal = 0.75f;
        s.surface_offset = -0.04f;
        s.sink = 0.06f;
        s.render_distance = 1500.0f;
        s.shadow_distance = 110.0f;
        s.flags = 40;
        s.mesh_path = "project/models/island_biomes/weathered_stone/weathered_stone.gltf";
        s.mesh_variants = "project/models/island_biomes/angular_stone/angular_stone.gltf;project/models/island_biomes/limestone_boulder/limestone_boulder.gltf";
    }
    {
        auto& s = layers[6];
        s = TerrainScatterLayer{};
        s.name = "olive_groves";
        s.habitat = 2;
        s.enabled = true;
        s.density = 160.0f;
        s.max_per_tile = 3500;
        s.seed = 3511;
        s.height_min = 3.0f;
        s.height_max = 280.0f;
        s.height_fade = 5.0f;
        s.slope_max = 25.0f;
        s.mask_channel = 1;
        s.mask_min = 0.01f;
        s.clump_radius = 30.0f;
        s.clump_count = 10;
        s.clump_raggedness = 0.65f;
        s.mesh_scale = 1.0f;
        s.size_min = 0.8f;
        s.size_max = 1.35f;
        s.align_to_normal = 0.0f;
        s.surface_offset = -0.12f;
        s.render_distance = 2600.0f;
        s.shadow_distance = 200.0f;
        s.foliage_scattering = 0.7f;
        s.flags = 109;
        s.mesh_path = "project/models/forest/olive/olive_a.gltf";
        s.mesh_variants = "project/models/forest/olive/olive_b.gltf;project/models/forest/olive/olive_c.gltf";
    }
    {
        auto& s = layers[7];
        s = TerrainScatterLayer{};
        s.name = "maquis_scrub";
        s.habitat = 3;
        s.enabled = true;
        s.density = 720.0f;
        s.max_per_tile = 12000;
        s.seed = 6173;
        s.height_min = 2.0f;
        s.height_max = 800.0f;
        s.height_fade = 3.0f;
        s.slope_max = 45.0f;
        s.ground_mask = 89;
        s.insolation_influence = 0.2f;
        s.deposition_influence = 0.15f;
        s.mask_channel = -1;
        s.mask_min = 0.01f;
        s.clump_radius = 12.0f;
        s.clump_count = 14;
        s.clump_raggedness = 0.95f;
        s.mesh_scale = 1.0f;
        s.size_min = 0.55f;
        s.size_max = 1.2f;
        s.align_to_normal = 0.1f;
        s.surface_offset = -0.05f;
        s.render_distance = 1100.0f;
        s.shadow_distance = 90.0f;
        s.foliage_scattering = 0.7f;
        s.flags = 137;
        s.mesh_path = "project/models/forest/lentisk/lentisk_a.gltf";
        s.mesh_variants = "project/models/forest/lentisk/lentisk_b.gltf;project/models/forest/lentisk/lentisk_c.gltf;project/models/forest/lentisk/lentisk_d.gltf;project/models/forest/myrtle/myrtle_a.gltf;project/models/forest/myrtle/myrtle_b.gltf;project/models/forest/myrtle/myrtle_c.gltf;project/models/forest/myrtle/myrtle_d.gltf;project/models/forest/mastic/mastic_b.gltf;project/models/forest/mastic/mastic_c.gltf";
    }
}
}
