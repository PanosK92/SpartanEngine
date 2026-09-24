/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ================
#include "McpQueue.h"
#include <functional>
//===========================

namespace spartan
{
    // handlers run on the engine main thread via McpQueue::Tick
    using McpCommandHandler = std::function<std::string(const McpRequest&)>;

    std::string ExecuteMcpCommand(const McpRequest& request);
    // Thread-safe read-only snapshot; bypasses the main-thread queue during loads.
    std::string GetMcpProgressSnapshot();
    void RegisterMcpCommand(const std::string& name, McpCommandHandler handler);
    void UnregisterMcpCommand(const std::string& name);
}
