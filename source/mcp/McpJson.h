/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

// a small read only json parser, the mcp wire protocol carries flat key=value
// pairs so nested arguments arrive as json text in a single value

#include <string>
#include <utility>
#include <vector>

namespace spartan::mcp_json
{
    enum class kind
    {
        null,
        boolean,
        number,
        string,
        array,
        object
    };

    struct value
    {
        kind type = kind::null;
        bool boolean_value = false;
        double number_value = 0.0;
        std::string string_value;
        std::vector<value> array_items;
        std::vector<std::pair<std::string, value>> object_items;

        bool is_null() const   { return type == kind::null; }
        bool is_array() const  { return type == kind::array; }
        bool is_object() const { return type == kind::object; }

        // null when the key is absent or this is not an object
        const value* find(const std::string& key) const;

        // these coerce across types, a number reads fine as a string and the other way around
        double number_or(double fallback) const;
        bool boolean_or(bool fallback) const;
        std::string string_or(const std::string& fallback) const;

        double member_number(const std::string& key, double fallback) const;
        bool member_boolean(const std::string& key, bool fallback) const;
        std::string member_string(const std::string& key, const std::string& fallback) const;
    };

    bool parse(const std::string& text, value& output, std::string& error);
}
