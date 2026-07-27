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
