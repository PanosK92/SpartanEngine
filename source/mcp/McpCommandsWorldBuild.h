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
    // the commands that lay out a world, splines and the roads built on them, districts and whole city
    // blockouts. the dispatch table in McpCommands.cpp is still the one place every command is listed
    namespace mcp_world_build
    {
        std::string command_spline_query(const McpRequest& request);
        std::string command_spline_distribute(const McpRequest& request);
        std::string command_spline_create_road(const McpRequest& request);
        std::string command_spline_set_control_points(const McpRequest& request);
        std::string command_spline_reroute(const McpRequest& request);
        std::string command_spline_connect(const McpRequest& request);
        std::string command_spline_junction(const McpRequest& request);
        std::string command_spline_decorate(const McpRequest& request);
        std::string command_world_landmarks(const McpRequest& request);
        std::string command_district_blockout(const McpRequest& request);
        std::string command_city_blockout(const McpRequest& request);
    }
}
