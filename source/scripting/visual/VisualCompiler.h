/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==========
#include <cstdint>
#include <string>
#include <vector>
#include "VisualGraph.h"
//=====================

namespace spartan::scripting::visual
{
    struct Diagnostic
    {
        uint32_t node = 0; // 0 when it is about the graph as a whole
        bool error    = true;
        std::string message;
    };

    struct CompileResult
    {
        bool success = false;
        // the whole file: the graph block, then the generated script
        std::string source;
        std::vector<Diagnostic> diagnostics;
        // the node each line of source came from, 0 for lines no node owns, so a lua error's line points back into the graph
        std::vector<uint32_t> line_nodes;
    };

    // a graph with errors still compiles to a loadable script, one that reports the errors when play starts
    CompileResult compile(const Graph& graph);

    // the node a lua error message points at, "path:12: attempt to..." maps line 12 through line_nodes
    uint32_t node_from_lua_error(const CompileResult& result, const std::string& error);
}
