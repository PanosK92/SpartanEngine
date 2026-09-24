/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====
#include <cstdint>
#include <string>
#include <vector>
//================

namespace spartan
{
    class Entity;

    // turning an authored hierarchy into something a game can afford to draw
    //
    // authoring wants one entity per surface, it is how a shape gets its own parameters and its own
    // material. drawing wants the opposite, every entity is a draw call and a chair built out of forty
    // parts costs forty of them for no visual gain. this bakes the parts that share a material down into
    // a single mesh so the authoring side stays free to be as detailed as it likes
    namespace game_ready
    {
        // what one material's worth of parts collapsed into
        struct MergeGroup
        {
            std::string material_name;
            std::string entity_name;
            uint32_t source_count  = 0;
            uint32_t sub_mesh_index = 0;
            uint32_t vertex_count  = 0;
            uint32_t index_count   = 0;
        };

        // a part that was left alone, with the reason, so a caller can report why a hierarchy did not
        // collapse as far as it expected
        struct MergeSkip
        {
            std::string entity_name;
            std::string reason;
        };

        struct MergeReport
        {
            bool ok = false;
            std::string error;

            std::string mesh_path;
            uint32_t renderers_before = 0;
            uint32_t renderers_after  = 0;
            uint32_t entities_removed = 0;
            uint32_t vertices_before  = 0;
            uint32_t vertices_after   = 0;
            uint32_t indices_before   = 0;
            uint32_t indices_after    = 0;

            std::vector<MergeGroup> groups;
            std::vector<MergeSkip> skipped;
        };

        // collapses every renderer under root that shares a material into one mesh, writing the result
        // to mesh_file_path. the hierarchy ends up with one renderer per material, positioned at the
        // root, and the parts that fed it are gone. a part is left alone when merging it would lose
        // something, which is why the report names what it skipped
        MergeReport MergeRenderersByMaterial(
            Entity* root,
            const std::string& mesh_file_path,
            const bool generate_lods
        );
    }
}
