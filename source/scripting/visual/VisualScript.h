/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =============
#include <cstdint>
#include <string>
#include "VisualCompiler.h"
#include "VisualGraph.h"
//========================

namespace spartan::scripting::visual
{
    bool load_visual_script(const std::string& path, Graph& graph, std::string* error = nullptr);

    // compiles, writes the file and reloads every script component that runs it
    CompileResult save_visual_script(const std::string& path, const Graph& graph, uint32_t* reloaded_components = nullptr);

    // what a new visual script starts as, On Start and On Tick waiting for nodes
    Graph make_starter_graph();
    bool create_visual_script(const std::string& path);

    // script components running the file load it again, returns how many did
    uint32_t reload_script_components(const std::string& path);
}
