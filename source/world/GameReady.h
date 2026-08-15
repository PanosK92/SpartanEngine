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
