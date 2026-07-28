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

//= INCLUDES =========================
#include "McpCommands.h"
#include "../World/Components/Spline.h"
#include "../World/Components/SplineFollower.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
//====================================

namespace spartan
{
    class Entity;

    namespace math
    {
        class BoundingBox;
        class Vector2;
        class Vector3;
    }

    // the pieces every group of mcp commands needs, reading arguments, writing json and finding the entity a
    // request is talking about. they live here so a command group can sit in its own file instead of everything
    // sharing one translation unit just to reach them
    namespace mcp_common
    {
        //= JSON ==========================================================================
        std::string json_escape(const std::string& value);
        std::string json_string(const std::string& value);
        std::string json_bool(bool value);
        std::string json_number(double value);
        std::string json_error(const std::string& error);
        std::string json_vector3(const math::Vector3& value);
        std::string json_bounding_box(const math::BoundingBox& value);
        //=================================================================================

        //= ARGUMENTS =====================================================================
        std::optional<std::string> get_argument(const McpRequest& request, const std::string& name);
        bool parse_bool(const std::string& value, bool& result);
        bool parse_uint64(const std::string& value, uint64_t& result);
        bool parse_float(const std::string& value, float& result);
        bool parse_float_list(const std::string& value, std::vector<float>& values, uint32_t expected_count);
        bool parse_vector2(const std::string& value, math::Vector2& result);
        bool parse_vector3(const std::string& value, math::Vector3& result);
        std::string to_lower_copy(std::string value);
        //=================================================================================

        //= ENTITIES ======================================================================
        bool is_edit_mode();
        Entity* find_entity_by_name_unique(const std::string& name, bool exact, std::string& error);
        Entity* get_entity_from_request(const McpRequest& request, std::string& error);
        std::string entity_components_json(Entity* entity);
        std::string entity_tags_json(Entity* entity);
        std::string entity_to_json_compact(Entity* entity);
        //=================================================================================

        //= SPLINES =======================================================================
        std::string spline_profile_to_name(SplineProfile profile);
        std::optional<SplineProfile> spline_profile_from_name(const std::string& name);
        std::string spline_follow_mode_to_name(SplineFollowMode mode);
        //=================================================================================
    }
}
