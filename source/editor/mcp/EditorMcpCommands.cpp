/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "pch.h"
#include "EditorMcpCommands.h"
#include "../Editor.h"
#include "../GeneralWindows.h"
#include "../widgets/Properties.h"
#include "../widgets/AssetBrowser.h"
#include "../widgets/Console.h"
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

        // lists every editor window, or shows/hides (and optionally undocks, resizes with width + height, or moves with x + y, in pixels relative to the editor window) the one whose title matches, case insensitive
        add(
            "editor_window",
            [editor](const McpRequest& request) -> string
            {
                const string* title          = find_any(request, { "title", "name", "window" });
                const optional<bool> visible = as_bool(find(request, "visible"));
                const string wanted          = title ? to_lower(*title) : "";

                Widget* match = nullptr;
                string windows;
                editor->ForEachWidget([&](Widget* widget)
                {
                    if (!wanted.empty() && to_lower(widget->GetTitle()) == wanted)
                    {
                        match = widget;
                    }
                    windows += windows.empty() ? "" : ",";
                    windows += "{\"title\":" + quote(widget->GetTitle()) + ",\"visible\":" + boolean(widget->GetVisible()) + "}";
                });

                // the world launcher is not a widget, it is placed by itself so only visibility applies
                windows += string(",{\"title\":\"Worlds\",\"visible\":") + boolean(GeneralWindows::GetVisibilityWorlds()) + "}";

                if (wanted.empty())
                {
                    return "{\"ok\":true,\"windows\":[" + windows + "]}";
                }

                if (wanted == "worlds" || wanted == "world launcher")
                {
                    GeneralWindows::SetVisibilityWorlds(visible.value_or(true));
                    return string("{\"ok\":true,\"title\":\"Worlds\",\"visible\":") + boolean(GeneralWindows::GetVisibilityWorlds()) + "}";
                }

                if (!match)
                {
                    return "{\"ok\":false,\"error\":" + quote("no editor window titled '" + *title + "'") + ",\"windows\":[" + windows + "]}";
                }

                match->SetVisible(visible.value_or(true));
                if (as_bool(find(request, "undock")).value_or(false))
                {
                    match->RequestUndock();
                }
                const optional<float> width  = as_float(find(request, "width"));
                const optional<float> height = as_float(find(request, "height"));
                if (width && height && *width > 0.0f && *height > 0.0f)
                {
                    match->RequestSize(math::Vector2(*width, *height));
                }
                const optional<float> x = as_float(find(request, "x"));
                const optional<float> y = as_float(find(request, "y"));
                if (x && y)
                {
                    match->RequestPosition(math::Vector2(*x, *y));
                }
                return "{\"ok\":true,\"title\":" + quote(match->GetTitle()) + ",\"visible\":" + boolean(match->GetVisible()) + "}";
            }
        );

        // opens or closes a component in the properties inspector by its header name ("Light", "Physics"), or every one with "all"
        // a single component also gets its groups opened and is scrolled to the top of the inspector
        add(
            "inspector_expand",
            [](const McpRequest& request) -> string
            {
                const string* component = find_any(request, { "component", "name", "title" });
                const bool open         = as_bool(find(request, "open")).value_or(true);
                const string target     = component ? *component : "all";
                Properties::RequestExpand(target, open);
                return "{\"ok\":true,\"component\":" + quote(target) + ",\"open\":" + boolean(open) + "}";
            }
        );

        // drives the assets panel: path (absolute or relative to the project folder), view (grid or list), size (50 to 200),
        // search, kind (folder, model, texture, material, prefab, world, script, audio, font, archive, file or all) and select (an item label)
        // changes apply on the next frame, the reply carries the state of the last drawn frame, call it again without arguments to read the result
        add(
            "asset_browser",
            [](const McpRequest& request) -> string
            {
                AssetBrowserRequest browser_request;
                if (const string* value = find_any(request, { "path", "folder", "directory" }))
                {
                    browser_request.path = *value;
                }
                if (const string* value = find(request, "view"))
                {
                    browser_request.view = *value;
                }
                if (const string* value = find(request, "search"))
                {
                    browser_request.search = *value;
                }
                if (const string* value = find_any(request, { "kind", "type", "filter" }))
                {
                    browser_request.kind = *value;
                }
                if (const string* value = find(request, "select"))
                {
                    browser_request.select = *value;
                }
                browser_request.size = as_float(find(request, "size"));
                AssetBrowser::Request(browser_request);

                const AssetBrowserState state = AssetBrowser::GetState();
                string visible;
                const size_t shown = min<size_t>(state.visible.size(), 64);
                for (size_t i = 0; i < shown; i++)
                {
                    visible += (i > 0 ? "," : "") + quote(state.visible[i]);
                }
                char size[32];
                snprintf(size, sizeof(size), "%.0f", state.size);
                return "{\"ok\":true,\"path\":" + quote(state.path) + ",\"view\":" + quote(state.view) + ",\"size\":" + size +
                       ",\"kind\":" + quote(state.kind) + ",\"selected\":" + quote(state.selected) +
                       ",\"visible_count\":" + to_string(state.visible.size()) + ",\"visible\":[" + visible + "]}";
            }
        );

        // drives the console panel: search (filter text), info / warnings / errors (show or hide that severity),
        // execute (runs a line exactly as if typed into the console input) and clear (true empties it)
        // changes apply on the next frame, the reply carries the state of the last drawn frame, call it again without arguments to read the result
        add(
            "console_view",
            [](const McpRequest& request) -> string
            {
                ConsoleRequest console_request;
                if (const string* value = find_any(request, { "search", "filter" }))
                {
                    console_request.search = *value;
                }
                if (const string* value = find_any(request, { "execute", "command", "run" }))
                {
                    console_request.execute = *value;
                }
                console_request.show[0] = as_bool(find(request, "info"));
                console_request.show[1] = as_bool(find(request, "warnings"));
                console_request.show[2] = as_bool(find(request, "errors"));
                console_request.clear   = as_bool(find(request, "clear")).value_or(false);
                Console::Request(console_request);

                const ConsoleState state = Console::GetState();
                string tail;
                for (size_t i = 0; i < state.tail.size(); i++)
                {
                    tail += (i > 0 ? "," : "") + quote(state.tail[i]);
                }
                return "{\"ok\":true,\"total\":" + to_string(state.total) + ",\"visible\":" + to_string(state.visible) +
                       ",\"info\":" + to_string(state.counts[0]) + ",\"warnings\":" + to_string(state.counts[1]) + ",\"errors\":" + to_string(state.counts[2]) +
                       ",\"shown\":{\"info\":" + boolean(state.shown[0]) + ",\"warnings\":" + boolean(state.shown[1]) + ",\"errors\":" + boolean(state.shown[2]) + "}" +
                       ",\"search\":" + quote(state.search) + ",\"tail\":[" + tail + "]}";
            }
        );
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
