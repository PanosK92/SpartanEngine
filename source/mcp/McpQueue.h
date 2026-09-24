/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ====================
#include <string>
#include <unordered_map>
//===============================

namespace spartan
{
    struct McpRequest
    {
        std::string command;
        std::unordered_map<std::string, std::string> arguments;
    };

    // marshals mcp requests from the server thread onto the main thread, Submit blocks until Tick runs it
    class McpQueue
    {
    public:
        static void Initialize();
        static std::string Submit(const McpRequest& request);
        static void Tick();
        static void Shutdown();
    };
}
