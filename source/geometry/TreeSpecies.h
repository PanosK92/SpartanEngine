/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===============
#include <cstdint>
#include <string>
#include <vector>
#include "../math/Vector3.h"
//==========================

// a species is the full recipe of a real tree, every number a tree creator usually exposes lives here so the
// window only has to offer a handful of growth controls, the files are json under data/trees/species/<region>/
namespace spartan::trees
{
    // the silhouette the first branch level grows into, the ratio is 1 at the crown base and 0 at the top
    enum class crown_shape
    {
        conical,
        spherical,
        hemispherical,
        cylindrical,
        tapered_cylindrical,
        flame,
        inverse_conical,
        tend_flame,
        umbrella,
        vase
    };

    enum class leaf_shape
    {
        elliptic,   // beech, cherry
        lanceolate, // olive, willow, eucalyptus
        ovate,      // holm oak, carob
        round,      // aspen, birch
        heart,      // banyan, kapok
        palmate,    // maple, plane
        lobed,      // oak
        needle,     // pine, spruce, fir
        scale,      // cypress, sequoia
        pinnate,    // date palm frond, acacia
        blade       // bamboo
    };

    enum class bark_style
    {
        furrowed, // oak, poplar
        plated,   // pine, stone pine
        smooth,   // beech, cherry
        birch,    // white with dark lenticels
        peeling,  // plane, eucalyptus
        fibrous,  // cypress, sequoia, palm
        twisted,  // olive
        ringed    // bamboo, palm trunk scars
    };

    enum class tree_class
    {
        shrub,
        broadleaf,
        conifer,
        palm,
        hero
    };

    enum class season
    {
        spring,
        summer,
        autumn,
        bare
    };

    enum class quality
    {
        mobile,
        standard,
        hero
    };

    struct branch_level
    {
        float count          = 8.0f;   // children per parent at full branchiness
        float start          = 0.25f;  // where along the parent the first child sits, 0 to 1
        float end            = 1.0f;   // where along the parent the last child sits
        float length         = 0.5f;   // child length over the parent length
        float length_var     = 0.15f;
        float down_angle     = 45.0f;  // degrees between child and parent at the start of the run
        float down_angle_tip = 30.0f;  // degrees at the end of the run, tips of crowns grow more upright
        float down_angle_var = 10.0f;
        float rotate         = 137.5f; // spiral step around the parent, the golden angle spreads children evenly
        uint32_t whorl       = 1;      // children per node, conifers grow in whorls of four to six
        bool planar          = false;  // children stay in the horizontal plane of the parent, spruce and fir sprays
        float curve          = 0.0f;   // total bend over the length in degrees, positive bends up
        float curve_var      = 10.0f;
        float gravity        = 0.0f;   // droop that grows with length, willow hangs at 1
        float radius_ratio   = 0.65f;  // child base radius over the parent radius at the attachment
        uint32_t segments    = 5;
    };

    struct species
    {
        // identity
        std::string id;
        std::string name;
        std::string scientific;
        std::string region;
        std::string description;
        std::string file_path;
        tree_class type = tree_class::broadleaf;
        bool evergreen  = false;

        // size
        float height     = 14.0f;
        float height_var = 0.15f;

        // trunk
        float trunk_radius      = 0.025f; // base radius over height
        float trunk_taper       = 0.15f;  // tip radius over base radius
        float trunk_flare       = 0.45f;  // root flare added at the base
        float trunk_lean        = 3.0f;   // degrees
        float trunk_gnarl       = 0.15f;  // random bending of the trunk, 0 to 1
        float trunk_twist       = 0.0f;   // spiral of the bark mapping, olive twists hard
        uint32_t trunk_segments = 12;
        uint32_t stems          = 1;      // stems after the split, 1 means no split
        float split_height      = 0.0f;   // where the stems part, 0 is a clump from the ground
        float split_angle       = 25.0f;  // degrees each stem leans out from the trunk
        float stem_spread       = 0.0f;   // ground radius of a clump in metres

        // crown
        crown_shape shape = crown_shape::spherical;
        float crown_base  = 0.3f;
        float crown_width = 0.7f; // crown radius over height, keeps branch lengths honest
        std::vector<branch_level> levels;

        // foliage
        leaf_shape leaf               = leaf_shape::elliptic;
        float leaf_size               = 0.08f; // single leaf length in metres
        float card_size               = 0.55f; // twig card length in metres
        float card_density            = 1.0f;  // cards per card length along a terminal branch
        float leaf_start              = 0.35f; // where along terminal branches cards begin
        float leaf_tilt               = 0.5f;  // how much cards face the sky, 0 follows the branch
        uint32_t leaves_per_card      = 9;
        math::Vector3 leaf_color      = math::Vector3(0.18f, 0.32f, 0.09f);
        math::Vector3 leaf_color_back = math::Vector3(0.30f, 0.40f, 0.16f);
        math::Vector3 spring_color    = math::Vector3(0.36f, 0.52f, 0.14f);
        math::Vector3 autumn_color    = math::Vector3(0.62f, 0.30f, 0.06f);
        math::Vector3 autumn_color_b  = math::Vector3(0.75f, 0.55f, 0.10f);
        float leaf_color_var          = 0.12f;
        bool flowers                  = false;
        math::Vector3 flower_color    = math::Vector3(0.95f, 0.72f, 0.80f);
        float translucency            = 0.45f;

        // palm crowns, a ring of fronds at the top of each stem instead of branches
        uint32_t fronds     = 0;
        float frond_length  = 3.0f;
        float frond_droop   = 0.5f;

        // bark
        bark_style bark              = bark_style::furrowed;
        math::Vector3 bark_color     = math::Vector3(0.30f, 0.24f, 0.18f);
        math::Vector3 bark_color_dark = math::Vector3(0.12f, 0.09f, 0.07f);
        float bark_scale             = 1.0f; // metres one bark tile covers around a 1 m girth
        float bark_roughness         = 0.9f;

        // triangle budget at standard quality, mobile takes about a third and hero twice
        uint32_t budget = 12000;
    };

    // the only knobs the window shows, each is 0 to 1 and 0.5 is the species as described
    struct controls
    {
        float age          = 0.5f;
        float leafiness    = 0.5f;
        float branchiness  = 0.5f;
        float wildness     = 0.5f;
        season time        = season::summer;
        uint32_t seed      = 1;
        quality detail     = quality::standard;
    };

    const char* to_string(season value);
    const char* to_string(quality value);
    bool season_from_string(const std::string& text, season& value);
    bool quality_from_string(const std::string& text, quality& value);

    // the directory the species json lives in, the repository copy wins so edits show without a build
    std::string species_directory();

    // every species found under the directory, sorted by region then name
    std::vector<species> load_all_species(std::string* errors = nullptr);
    bool load_species(const std::string& file_path, species& output, std::string& error);
    bool parse_species(const std::string& json, species& output, std::string& error);
    std::string serialize_species(const species& input);

    // region labels in the order the window lists them
    std::vector<std::string> region_order();

    // hashes everything the bark and leaf textures depend on, textures are cached per species state and season
    uint64_t texture_hash(const species& input, season time);
}
