/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ===========
#include "DisplayMode.h"
//======================

namespace Spartan
{
    class SPARTAN_CLASS Display
    {
    public:
        Display() = default;

        // Display modes
        static void RegisterDisplayMode(const DisplayMode& display_mode, Context* context);      
        static void SetActiveDisplayMode(const DisplayMode& display_mode)   { m_display_mode_active = display_mode; }
        static const DisplayMode& GetActiveDisplayMode()                    { return m_display_mode_active; }
        static const std::vector<DisplayMode>& GetDisplayModes()            { return m_display_modes; }

        // Display dimensions
        static uint32_t GetWidth();
        static uint32_t GetHeight();

        // Virtual display dimensions (multiple displays)
        static uint32_t GetWidthVirtual();
        static uint32_t GetHeightVirtual();

    private:
        static std::vector<DisplayMode> m_display_modes;
        static DisplayMode m_display_mode_active;
    };
}
