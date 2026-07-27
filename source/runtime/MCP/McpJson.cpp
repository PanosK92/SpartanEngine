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

//= INCLUDES =====
#include "pch.h"
#include "McpJson.h"
//================

namespace spartan::mcp_json
{
    namespace
    {
        constexpr uint32_t max_depth = 24;

        void skip_whitespace(const std::string& text, size_t& cursor)
        {
            while (cursor < text.size())
            {
                const char character = text[cursor];
                if (character != ' ' && character != '\t' && character != '\n' && character != '\r')
                {
                    break;
                }

                cursor++;
            }
        }

        void append_utf8(std::string& output, const uint32_t code_point)
        {
            if (code_point < 0x80)
            {
                output.push_back(static_cast<char>(code_point));
            }
            else if (code_point < 0x800)
            {
                output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
                output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            }
            else
            {
                output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
                output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            }
        }

        bool parse_string(const std::string& text, size_t& cursor, std::string& output, std::string& error)
        {
            if (cursor >= text.size() || text[cursor] != '"')
            {
                error = "expected a string";
                return false;
            }

            cursor++;

            while (cursor < text.size())
            {
                const char character = text[cursor++];
                if (character == '"')
                {
                    return true;
                }

                if (character != '\\')
                {
                    output.push_back(character);
                    continue;
                }

                if (cursor >= text.size())
                {
                    break;
                }

                const char escaped = text[cursor++];
                switch (escaped)
                {
                case '"':  output.push_back('"');  break;
                case '\\': output.push_back('\\'); break;
                case '/':  output.push_back('/');  break;
                case 'b':  output.push_back('\b'); break;
                case 'f':  output.push_back('\f'); break;
                case 'n':  output.push_back('\n'); break;
                case 'r':  output.push_back('\r'); break;
                case 't':  output.push_back('\t'); break;
                case 'u':
                {
                    if (cursor + 4 > text.size())
                    {
                        error = "truncated unicode escape";
                        return false;
                    }

                    uint32_t code_point = 0;
                    for (uint32_t i = 0; i < 4; i++)
                    {
                        const char digit = text[cursor + i];
                        code_point <<= 4;
                        if (digit >= '0' && digit <= '9')
                        {
                            code_point |= static_cast<uint32_t>(digit - '0');
                        }
                        else if (digit >= 'a' && digit <= 'f')
                        {
                            code_point |= static_cast<uint32_t>(digit - 'a' + 10);
                        }
                        else if (digit >= 'A' && digit <= 'F')
                        {
                            code_point |= static_cast<uint32_t>(digit - 'A' + 10);
                        }
                        else
                        {
                            error = "invalid unicode escape";
                            return false;
                        }
                    }

                    cursor += 4;
                    append_utf8(output, code_point);
                    break;
                }
                default:
                    error = "invalid escape sequence";
                    return false;
                }
            }

            error = "unterminated string";
            return false;
        }

        bool parse_value(const std::string& text, size_t& cursor, uint32_t depth, value& output, std::string& error);

        bool parse_array(const std::string& text, size_t& cursor, const uint32_t depth, value& output, std::string& error)
        {
            output.type = kind::array;
            cursor++;
            skip_whitespace(text, cursor);

            if (cursor < text.size() && text[cursor] == ']')
            {
                cursor++;
                return true;
            }

            while (cursor < text.size())
            {
                value item;
                if (!parse_value(text, cursor, depth + 1, item, error))
                {
                    return false;
                }

                output.array_items.push_back(std::move(item));

                skip_whitespace(text, cursor);
                if (cursor >= text.size())
                {
                    break;
                }

                if (text[cursor] == ',')
                {
                    cursor++;
                    skip_whitespace(text, cursor);
                    continue;
                }

                if (text[cursor] == ']')
                {
                    cursor++;
                    return true;
                }

                error = "expected a comma or a closing bracket";
                return false;
            }

            error = "unterminated array";
            return false;
        }

        bool parse_object(const std::string& text, size_t& cursor, const uint32_t depth, value& output, std::string& error)
        {
            output.type = kind::object;
            cursor++;
            skip_whitespace(text, cursor);

            if (cursor < text.size() && text[cursor] == '}')
            {
                cursor++;
                return true;
            }

            while (cursor < text.size())
            {
                std::string key;
                if (!parse_string(text, cursor, key, error))
                {
                    return false;
                }

                skip_whitespace(text, cursor);
                if (cursor >= text.size() || text[cursor] != ':')
                {
                    error = "expected a colon";
                    return false;
                }

                cursor++;

                value item;
                if (!parse_value(text, cursor, depth + 1, item, error))
                {
                    return false;
                }

                output.object_items.emplace_back(std::move(key), std::move(item));

                skip_whitespace(text, cursor);
                if (cursor >= text.size())
                {
                    break;
                }

                if (text[cursor] == ',')
                {
                    cursor++;
                    skip_whitespace(text, cursor);
                    continue;
                }

                if (text[cursor] == '}')
                {
                    cursor++;
                    return true;
                }

                error = "expected a comma or a closing brace";
                return false;
            }

            error = "unterminated object";
            return false;
        }

        bool parse_value(const std::string& text, size_t& cursor, const uint32_t depth, value& output, std::string& error)
        {
            if (depth > max_depth)
            {
                error = "json is nested too deeply";
                return false;
            }

            skip_whitespace(text, cursor);
            if (cursor >= text.size())
            {
                error = "unexpected end of json";
                return false;
            }

            const char character = text[cursor];
            if (character == '{')
            {
                return parse_object(text, cursor, depth, output, error);
            }

            if (character == '[')
            {
                return parse_array(text, cursor, depth, output, error);
            }

            if (character == '"')
            {
                output.type = kind::string;
                return parse_string(text, cursor, output.string_value, error);
            }

            if (text.compare(cursor, 4, "true") == 0)
            {
                output.type          = kind::boolean;
                output.boolean_value = true;
                cursor += 4;
                return true;
            }

            if (text.compare(cursor, 5, "false") == 0)
            {
                output.type          = kind::boolean;
                output.boolean_value = false;
                cursor += 5;
                return true;
            }

            if (text.compare(cursor, 4, "null") == 0)
            {
                output.type = kind::null;
                cursor += 4;
                return true;
            }

            const size_t number_begin = cursor;
            if (text[cursor] == '-' || text[cursor] == '+')
            {
                cursor++;
            }

            while (cursor < text.size())
            {
                const char digit = text[cursor];
                const bool is_number_character =
                    (digit >= '0' && digit <= '9') ||
                    digit == '.' ||
                    digit == 'e' ||
                    digit == 'E' ||
                    digit == '+' ||
                    digit == '-';

                if (!is_number_character)
                {
                    break;
                }

                cursor++;
            }

            if (cursor == number_begin)
            {
                error = "unexpected character in json";
                return false;
            }

            try
            {
                output.type         = kind::number;
                output.number_value = std::stod(text.substr(number_begin, cursor - number_begin));
            }
            catch (...)
            {
                error = "invalid number in json";
                return false;
            }

            return true;
        }
    }

    const value* value::find(const std::string& key) const
    {
        if (type != kind::object)
        {
            return nullptr;
        }

        for (const std::pair<std::string, value>& item : object_items)
        {
            if (item.first == key)
            {
                return &item.second;
            }
        }

        return nullptr;
    }

    double value::number_or(const double fallback) const
    {
        if (type == kind::number)
        {
            return number_value;
        }

        if (type == kind::boolean)
        {
            return boolean_value ? 1.0 : 0.0;
        }

        if (type == kind::string)
        {
            try
            {
                return std::stod(string_value);
            }
            catch (...)
            {
                return fallback;
            }
        }

        return fallback;
    }

    bool value::boolean_or(const bool fallback) const
    {
        if (type == kind::boolean)
        {
            return boolean_value;
        }

        if (type == kind::number)
        {
            return number_value != 0.0;
        }

        if (type == kind::string)
        {
            return string_value == "true" || string_value == "1" || string_value == "yes";
        }

        return fallback;
    }

    std::string value::string_or(const std::string& fallback) const
    {
        return type == kind::string ? string_value : fallback;
    }

    double value::member_number(const std::string& key, const double fallback) const
    {
        const value* member = find(key);
        return member ? member->number_or(fallback) : fallback;
    }

    bool value::member_boolean(const std::string& key, const bool fallback) const
    {
        const value* member = find(key);
        return member ? member->boolean_or(fallback) : fallback;
    }

    std::string value::member_string(const std::string& key, const std::string& fallback) const
    {
        const value* member = find(key);
        return member ? member->string_or(fallback) : fallback;
    }

    bool parse(const std::string& text, value& output, std::string& error)
    {
        size_t cursor = 0;
        if (!parse_value(text, cursor, 0, output, error))
        {
            return false;
        }

        skip_whitespace(text, cursor);
        if (cursor != text.size())
        {
            error = "trailing characters after json value";
            return false;
        }

        return true;
    }
}
