/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =========
#include <cstdint>
#include <string>
#include <vector>
#include "mcp/McpJson.h"
//====================

namespace pugi
{
    class xml_node;
}

// helpers shared by the asset viewer translation units
namespace asset_viewer_common
{
    using JsonValue = spartan::mcp_json::value;

    // json
    JsonValue* json_object_find(JsonValue& object, const std::string& key);
    JsonValue& json_object_ensure(JsonValue& object, const std::string& key);
    std::vector<std::string> json_strings(const JsonValue* value);
    std::string serialize_json_string(const std::string& value);

    // strings
    std::string lower_copy(std::string value);
    std::string join_strings(const std::vector<std::string>& values, const char* separator);
    std::string sanitize_asset_name(const std::string& value);
    std::string compact_count(const uint64_t value);

    // paths and assets
    std::string asset_type_from_path(const std::string& path);
    std::string normalized_path(std::string value);
    bool path_is_within(const std::string& value, const std::string& directory);
    bool resolved_path_is_within(const std::string& value, const std::string& directory);
    bool path_is_in_viewer_roots(const std::string& value);
    int count_prefab_entities(const pugi::xml_node& node);
    std::vector<std::string> collect_xml_references(const std::string& path, const char* attribute_name);

    // ui
    float ui_scale();
    void detail_row(const char* label, const std::string& value);
}
