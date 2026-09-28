/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =========================
#include "McpCommands.h"
#include "../world/components/Spline.h"
#include "../world/components/SplineFollower.h"
#include "../resource/IResource.h"
#include <cstdint>
#include <filesystem>
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
        bool parse_uint32(const std::string& value, uint32_t& result);
        std::string to_lower_copy(std::string value);
        //=================================================================================

        //= RESOURCES =====================================================================
        std::string resource_type_to_name(ResourceType type);
        std::string resource_to_json(IResource* resource);
        bool path_is_within(const std::filesystem::path& path, const std::filesystem::path& directory);
        // keeps generated files inside project/mcp/blockout/<directory_name>, a path outside it keeps only its file name
        std::optional<std::string> resolve_mcp_output_path(const std::string& requested_path, const char* directory_name, const std::string& extension, std::string& error);
        //=================================================================================

        //= BATCHES =======================================================================
        bool item_succeeded(const std::string& item_result);
        std::string json_batch_failure(const std::string& error, const std::string& items_field, const std::string& applied_items, uint32_t applied_count, uint64_t failed_index, const std::string& failure);
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
