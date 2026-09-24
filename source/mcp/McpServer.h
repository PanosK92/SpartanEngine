/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ====
#include <cstdint>
#include <string>
#include <vector>
//===============

namespace spartan
{
    class McpServer
    {
    public:
        static void Initialize(const std::vector<std::string>& args);
        static bool Start(uint16_t port = 47777);
        static void Shutdown();
        static void Tick();
        static bool IsRunning();
        static uint16_t GetPort();
    };
}
