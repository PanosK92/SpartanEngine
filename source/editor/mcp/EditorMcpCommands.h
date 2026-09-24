/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ================
#include "mcp/McpCommands.h"
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
