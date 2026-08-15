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
