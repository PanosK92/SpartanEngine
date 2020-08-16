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

//= INCLUDES =======
#include "Spartan.h"
#include "Display.h"
#include <windows.h>
//==================

namespace Spartan
{
    std::vector<DisplayMode> Display::m_display_modes;
    DisplayMode Display::m_display_mode_active;

    void Display::RegisterDisplayMode(const DisplayMode& display_mode, Context* context)
    {
        // Early exit if display is already registered
        for (const DisplayMode& display_mode_existing : m_display_modes)
        {
            if (display_mode == display_mode_existing)
                return;
        }

        DisplayMode& mode = m_display_modes.emplace_back(display_mode);

        // Keep display modes sorted, based on refresh rate (from highest to lowest)
        std::sort(m_display_modes.begin(), m_display_modes.end(), [](const DisplayMode& display_mode_a, const DisplayMode& display_mode_b)
        {
            return display_mode_a.hz > display_mode_b.hz;
        });

        // Find preferred display mode
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_display_modes.size()); i++)
        {
            const DisplayMode& display_mode = m_display_modes[i];

            // Try to use higher resolution
            if (display_mode.width > m_display_mode_active.width || display_mode.height > m_display_mode_active.height)
            {
                // But not lower hz
                if (display_mode.hz >= m_display_mode_active.hz)
                { 
                    m_display_mode_active.width         = display_mode.width;
                    m_display_mode_active.height        = display_mode.height;
                    m_display_mode_active.hz            = display_mode.hz;
                    m_display_mode_active.numerator     = display_mode.numerator;
                    m_display_mode_active.denominator   = display_mode.denominator;
                }
            }
        }

        // Let the timer know about the refresh rates this monitor is capable of (will result in low latency/smooth ticking)
        context->GetSubsystem<Timer>()->SetTargetFps(m_display_modes.front().hz);
    }

    uint32_t Display::GetWidth()
    {
        return static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
    }

    uint32_t Display::GetHeight()
    {
        return static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
    }

    uint32_t Display::GetWidthVirtual()
    {
        return static_cast<uint32_t>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    }

    uint32_t Display::GetHeightVirtual()
    {
        return static_cast<uint32_t>(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }
}
