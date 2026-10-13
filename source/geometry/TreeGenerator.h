/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================
#include <memory>
#include <vector>
#include "TreeSpecies.h"
#include "../rhi/RHI_Vertex.h"
#include "../math/BoundingBox.h"
//==============================

namespace spartan
{
    class Mesh;
}

// grows a tree from a species and the window's controls
//
// the skeleton is a weber and penn style recursion: a trunk (or a split into stems), branch levels placed
// along their parent in spirals or whorls, with lengths shaped by the crown silhouette, bending under
// gravity and their own curvature. the finest twigs are never modelled, they are painted on the leaf cards,
// which is how a tree keeps a dense crown at a few thousand triangles
namespace spartan::trees
{
    // the leaf atlas is a 2x2 grid of twig variants, a palm uses the same grid for frond variants
    constexpr uint32_t atlas_tiles = 2;

    struct stem
    {
        std::vector<math::Vector3> points;
        std::vector<float> radii;
        uint32_t level  = 0;
        int32_t parent  = -1;
        float length    = 0.0f;
        bool terminal   = true;
        bool pruned     = false; // shaded out inside the crown, it neither draws nor carries leaves
        bool flare      = false; // grows from the ground, the base carries root buttresses
    };

    struct leaf_card
    {
        math::Vector3 base;
        math::Vector3 direction; // base to tip, carries the length
        math::Vector3 side;      // half width
        math::Vector3 normal;
        uint32_t tile = 0;
        float fold    = 0.25f;
    };

    struct frond
    {
        std::vector<math::Vector3> points;
        std::vector<math::Vector3> sides; // half width per point
        uint32_t tile = 0;
    };

    struct geometry
    {
        std::vector<RHI_Vertex_PosTexNorTan> vertices;
        std::vector<uint32_t> indices;

        uint32_t triangles() const { return static_cast<uint32_t>(indices.size() / 3); }
    };

    struct result
    {
        std::vector<stem> stems;
        std::vector<leaf_card> cards;
        std::vector<frond> fronds;

        std::vector<geometry> bark_lods; // lod 0 first, each level drops branch orders before it is simplified
        geometry foliage;                // lod 0, the mesh thins the cards for its distance levels

        float height              = 0.0f;
        float trunk_radius        = 0.0f;
        float trunk_clear_height  = 0.0f; // height of the bare trunk, the collider stops there
        math::Vector3 crown_center;
        math::Vector3 crown_radii;
        math::BoundingBox bounds;
        uint32_t budget           = 0;
    };

    uint32_t budget_for(const species& s, quality detail);

    result generate(const species& s, const controls& c);

    // bark is sub mesh 0 with its branch order lods, foliage is sub mesh 1 with thinned card lods and,
    // when asked, the hemi-octahedral impostor as the last level
    std::shared_ptr<Mesh> build_mesh(result& tree, const std::string& name, bool impostor);
}
