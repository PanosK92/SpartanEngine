/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==================================
#include "Definitions.h"
#include "../commands/console/ConsoleCommands.h"
//=============================================

namespace spartan
{
    // persisted in spartan.xml as debug_*, all but log_to_file and shader_optimization are read at startup only
    extern TConsoleVar<bool> cvar_debug_validation_layer;
    extern TConsoleVar<bool> cvar_debug_gpu_assisted_validation;
    extern TConsoleVar<bool> cvar_debug_log_to_file;
    extern TConsoleVar<bool> cvar_debug_breadcrumbs;
    extern TConsoleVar<bool> cvar_debug_renderdoc;
    extern TConsoleVar<bool> cvar_debug_gpu_marking;
    extern TConsoleVar<bool> cvar_debug_gpu_timing;
    extern TConsoleVar<bool> cvar_debug_shader_optimization;
    extern TConsoleVar<bool> cvar_debug_steam;
    extern TConsoleVar<bool> cvar_debug_d3d12_enhanced_barriers;

    class Settings
    {
    public:
        static void LoadPreInitSettings();
        static void Initialize();
        static void Shutdown();
        static bool HasLoadedUserSettingsFromFile();
    };
}
