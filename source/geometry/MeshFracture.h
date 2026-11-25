/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =======================
#include <cstdint>
#include <vector>
#include "../math/BoundingBox.h"
#include "../math/Vector3.h"
#include "../rhi/RHI_Vertex.h"
//==================================

namespace spartan
{
    enum class FracturePattern : uint32_t
    {
        Uniform      = 0,
        RadialGlass  = 1,
        SplinterWood = 2,
        LayeredStone = 3,
        Max
    };

    struct FractureParams
    {
        uint32_t seed               = 1337;
        uint32_t chunk_count        = 16;
        FracturePattern pattern     = FracturePattern::Uniform;
        math::Vector3 impact_local  = math::Vector3::Zero; // metric space relative to mesh origin (scaled)
        bool has_impact_point       = false;
        float impact_bias           = 0.45f;               // fraction of Voronoi sites clustered around impact
        float interior_uv_scale     = 0.5f;                // meters^-1 for freshly fractured faces
        float min_volume            = 1.0e-5f;             // discard tiny sliver shards below this m^3
    };

    struct FractureCell
    {
        std::vector<RHI_Vertex_PosTexNorTan> vertices;
        std::vector<uint32_t> indices;
        math::Vector3 centroid      = math::Vector3::Zero; // metric space offset from source mesh origin
        math::BoundingBox bounds    = math::BoundingBox::Zero;
        float volume                = 0.0f;
        std::vector<uint32_t> neighbors;
        bool grounded               = false;               // touches bottom face of the intact mesh
    };

    namespace mesh_fracture
    {
        // Fractures a closed triangle mesh (in mesh-local space scaled by entity_scale into metric space)
        // into watertight 3D Voronoi shards centered at each shard's center of mass.
        bool Fracture(
            const std::vector<RHI_Vertex_PosTexNorTan>& source_vertices,
            const std::vector<uint32_t>& source_indices,
            const math::Vector3& entity_scale,
            const FractureParams& params,
            std::vector<FractureCell>& out_cells
        );

        // Runs a connectivity flood-fill from all remaining grounded cells across shared fracture faces.
        // Any intact cell not reachable from the ground is appended to out_unsupported.
        void FindUnsupportedCells(
            const std::vector<FractureCell>& cells,
            const std::vector<uint8_t>& detached,
            std::vector<uint32_t>& out_unsupported
        );
    }
}
