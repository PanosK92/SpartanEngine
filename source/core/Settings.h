/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===========
#include "Definitions.h"
//======================

namespace spartan
{
    class Settings
    {
    public:
        static void LoadPreInitSettings();
        static void Initialize();
        static void Shutdown();
        static bool HasLoadedUserSettingsFromFile();
    };
}
