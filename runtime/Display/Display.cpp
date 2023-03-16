/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "pch.h"
#include "Display.h"
#include "sdl/SDL.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    vector<DisplayMode> Display::m_display_modes;
    DisplayMode Display::m_display_mode_active;

    void Display::RegisterDisplayMode(const DisplayMode& display_mode, const bool update_fps_limit_to_highest_hz)
    {
        // Early exit if display is already registered
        for (const DisplayMode& display_mode_existing : m_display_modes)
        {
            if (display_mode == display_mode_existing)
                return;
        }

        DisplayMode& mode = m_display_modes.emplace_back(display_mode);

        // Keep display modes sorted, based on refresh rate, in a descending order.
        sort(m_display_modes.begin(), m_display_modes.end(), [](const DisplayMode& display_mode_a, const DisplayMode& display_mode_b)
        {
            return display_mode_a.hz > display_mode_b.hz;
        });

        // Find preferred display mode
        for (const DisplayMode& display_mode : m_display_modes)
        {
            // Try to use higher resolution
            if (display_mode.width > m_display_mode_active.width || display_mode.height > m_display_mode_active.height)
            {
                // But not lower hz
                if (display_mode.hz >= m_display_mode_active.hz)
                { 
                    m_display_mode_active.width       = display_mode.width;
                    m_display_mode_active.height      = display_mode.height;
                    m_display_mode_active.hz          = display_mode.hz;
                    m_display_mode_active.numerator   = display_mode.numerator;
                    m_display_mode_active.denominator = display_mode.denominator;
                }
            }
        }

        // Update FPS limit
        if (update_fps_limit_to_highest_hz)
        {
            double hz = m_display_modes.front().hz;
            if (hz > Timer::GetFpsLimit())
            {
                Timer::SetFpsLimit(hz);
            }
        }
    }

    void Display::DetectDisplayModes()
    {
        // Get display modes of all displays
        for (uint32_t display_index = 0; display_index < static_cast<uint32_t>(SDL_GetNumVideoDisplays()); ++display_index) {

            // Get display mode
            SDL_DisplayMode display_mode;
            if (SDL_GetCurrentDisplayMode(display_index, &display_mode) != 0)
            {
                SP_LOG_ERROR("Failed to get display mode for display index %d", display_index);
                continue;
            }

            // Register display mode (duplicates are discarded)
            bool update_fps_limit_to_highest_hz = true;
            RegisterDisplayMode(DisplayMode(display_mode.w, display_mode.h, display_mode.refresh_rate, 1), update_fps_limit_to_highest_hz);
        }
    }

    uint32_t Display::GetWidth()
    {
        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(0, &display_mode) == 0);

        return display_mode.w;
    }

    uint32_t Display::GetHeight()
    {
        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(0, &display_mode) == 0);

        return display_mode.h;
    }

    uint32_t Display::GetRefreshRate()
    {
        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(0, &display_mode) == 0);

        return display_mode.refresh_rate;
    }
}
