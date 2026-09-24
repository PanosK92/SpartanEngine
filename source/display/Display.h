/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===========
#include "DisplayMode.h"
//======================

namespace spartan
{
    // the monitor the window sits on, its available modes and its hdr capability
    class Display
    {
    public:
        // display modes
        static void Initialize();
        static void RegisterDisplayMode(const uint32_t width, const uint32_t height, const float hz, const uint32_t display_index);
        static const std::vector<DisplayMode>& GetDisplayModes();

        // properties
        static uint32_t GetWidth();
        static uint32_t GetHeight();
        static float GetRefreshRate();
        static uint32_t GetId();
        static bool GetHdr();
        static float GetLuminanceMax();
        static float GetSdrWhiteNits();
        static void InvalidateProperties();
        static float GetGamma();
        static const char* GetName();
    };
}
