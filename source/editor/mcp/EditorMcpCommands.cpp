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

//= INCLUDES ====================
#include "pch.h"
#include "EditorMcpCommands.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>
//===============================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

namespace editor_mcp
{
    namespace
    {
        // what was handed to the registry, kept so shutdown can take back exactly what it added
        vector<string>& registered_names()
        {
            static vector<string> names;
            return names;
        }
    }

    string to_lower(const string& value)
    {
        string result = value;
        transform(
            result.begin(),
            result.end(),
            result.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(tolower(character));
            }
        );
        return result;
    }

    void add(const string& name, McpCommandHandler handler)
    {
        RegisterMcpCommand(name, move(handler));
        registered_names().push_back(name);
    }

    void Register(Editor* editor)
    {
        register_asset_viewer(editor);
        register_sequencer(editor);
    }

    void Unregister()
    {
        for (const string& name : registered_names())
        {
            UnregisterMcpCommand(name);
        }
        registered_names().clear();
    }

    string escape(const string& value)
    {
        // a control character has to be escaped as well as a quote, an entity or asset name that holds a
        // newline would otherwise produce a reply the client cannot parse
        string result;
        for (const char character : value)
        {
            switch (character)
            {
                case '\"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b";  break;
                case '\f': result += "\\f";  break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:
                {
                    if (static_cast<unsigned char>(character) < 0x20)
                    {
                        char buffer[7] = {};
                        snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(character)));
                        result += buffer;
                    }
                    else
                    {
                        result += character;
                    }
                    break;
                }
            }
        }
        return result;
    }

    string quote(const string& value)
    {
        return "\"" + escape(value) + "\"";
    }

    string failure(const string& message)
    {
        return "{\"ok\":false,\"error\":" + quote(message) + "}";
    }

    const char* boolean(bool value)
    {
        return value ? "true" : "false";
    }

    const string* find(const McpRequest& request, const string& name)
    {
        const auto iterator = request.arguments.find(name);
        return iterator != request.arguments.end()
            ? &iterator->second
            : nullptr;
    }

    const string* find_any(
        const McpRequest& request,
        initializer_list<const char*> names
    )
    {
        for (const char* name : names)
        {
            if (const string* value = find(request, name))
            {
                return value;
            }
        }
        return nullptr;
    }

    optional<bool> as_bool(const string* value)
    {
        if (!value)
        {
            return nullopt;
        }

        // anything that is not an explicit no reads as a yes, a caller that named the argument at all
        // was asking for the thing to happen
        const string normalized = to_lower(*value);
        return
            normalized != "false" &&
            normalized != "0" &&
            normalized != "no" &&
            normalized != "off";
    }

    optional<float> as_float(const string* value)
    {
        if (!value)
        {
            return nullopt;
        }

        try
        {
            size_t consumed = 0;
            const float parsed = stof(*value, &consumed);
            // the whole argument has to be the number, a partial parse would read "12abc" as 12 and aim
            // the camera at a value the caller never wrote
            return
                consumed == value->size() &&
                isfinite(parsed)
                    ? optional<float>(parsed)
                    : nullopt;
        }
        catch (...)
        {
            return nullopt;
        }
    }

    optional<uint64_t> as_uint(const string* value)
    {
        if (!value)
        {
            return nullopt;
        }

        try
        {
            size_t consumed = 0;
            const uint64_t parsed = stoull(*value, &consumed);
            return consumed == value->size()
                ? optional<uint64_t>(parsed)
                : nullopt;
        }
        catch (...)
        {
            return nullopt;
        }
    }
}
