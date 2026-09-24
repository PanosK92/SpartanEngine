/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    enum class EngineMode : uint32_t
    {
        EditorVisible = 1 << 0, // off with -game, editor code stays in the binary
        Playing       = 1 << 1,
        Paused        = 1 << 2
    };

    class Engine
    {
    public:
        static void Initialize(const std::vector<std::string>& args);
        static void Shutdown();
        static void Tick();
        static bool IsFlagSet(const EngineMode flag);
        static void SetFlag(const EngineMode flag, const bool enabled);
        static void ToggleFlag(const EngineMode flag);
        static bool HasArgument(const std::string& argument);
    };
}
