/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ============
#include "McpCommands.h"
#include <string>
//=======================

namespace spartan
{
    // parametric mesh generation, shapes, profiles and sweeps plus the modifiers that bend and array them
    namespace mcp_mesh
    {
        std::string command_mesh_generate(const McpRequest& request);
        std::string command_mesh_generate_batch(const McpRequest& request);
    }
}
