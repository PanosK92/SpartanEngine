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

//= INCLUDES ================
#include "MCP/McpCommands.h"
#include <initializer_list>
#include <optional>
#include <string>
//===========================

class Editor;

// mcp command handling for the editor
//
// the runtime cannot reach up into the editor, so editor side commands are pushed down into
// McpCommands' registry from here instead. every registration, every argument parse and every reply
// lives in this directory, which is what keeps the widgets themselves free of any transport
namespace editor_mcp
{
    void Register(Editor* editor);
    void Unregister();

    // registers a handler and remembers its name so Unregister can take it back out
    void add(const std::string& name, spartan::McpCommandHandler handler);

    // reply helpers, a handler either answers with its own object or fails with a message
    std::string escape(const std::string& value);
    std::string quote(const std::string& value);
    std::string failure(const std::string& message);
    const char* boolean(bool value);

    std::string to_lower(const std::string& value);

    // argument helpers, an absent or unparseable argument reads as nothing rather than as a zero
    const std::string* find(const spartan::McpRequest& request, const std::string& name);
    // the first name that is present, a caller may spell the same argument several ways
    const std::string* find_any(const spartan::McpRequest& request, std::initializer_list<const char*> names);
    std::optional<bool> as_bool(const std::string* value);
    std::optional<float> as_float(const std::string* value);
    std::optional<uint64_t> as_uint(const std::string* value);

    // one translation unit per widget, each registers the commands that drive it
    void register_asset_viewer(Editor* editor);
    void register_sequencer(Editor* editor);
}
