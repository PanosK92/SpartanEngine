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
#include "Window.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    static std::vector<DisplayMode> display_modes;

    void Display::RegisterDisplayMode(const uint32_t width, const uint32_t height, uint32_t hz, uint8_t display_index)
    {
        SP_ASSERT_MSG(width  != 0,    "width can't be zero");
        SP_ASSERT_MSG(height != 0,    "height can't be zero");
        SP_ASSERT_MSG(hz     != 0.0f, "hz can't be zero");

        // Early exit if the display mode is already registered
        for (const DisplayMode& display_mode : display_modes)
        {
            if (display_mode.width         == width  &&
                display_mode.height        == height &&
                display_mode.hz            == hz     &&
                display_mode.display_index == display_index)
                return;
        }

        // Add the new display mode
        display_modes.emplace_back(width, height, hz, display_index);

        // Sort display modes based on width, descending order
        sort(display_modes.begin(), display_modes.end(), [](const DisplayMode& display_mode_a, const DisplayMode& display_mode_b)
        {
            return display_mode_a.width > display_mode_b.width;
        });

        // Set the FPS limit to the HZ corresponding to our optimal display mode
        if (GetRefreshRate() > Timer::GetFpsLimit())
        {
            Timer::SetFpsLimit(static_cast<float>(GetRefreshRate()));
        }
    }

    void Display::DetectDisplayModes()
    {
        display_modes.clear();

        // Get display index of the display that contains this window
        int display_index = SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(Window::GetHandleSDL()));
        if (display_index < 0)
        {
            SP_LOG_ERROR("Failed to window display index");
            return;
        }

        // Get display mode count
        int display_mode_count = SDL_GetNumDisplayModes(display_index);
        if (display_mode_count <= 0)
        {
            SP_LOG_ERROR("Failed to get display mode count");
            return;
        }

        // Register display modes
        for (uint32_t display_mode_index = 0; display_mode_index < display_mode_count; display_mode_index++)
        {
            SDL_DisplayMode display_mode;
            if (SDL_GetDisplayMode(display_index, display_mode_index, &display_mode) == 0)
            {
                RegisterDisplayMode(display_mode.w, display_mode.h, display_mode.refresh_rate, display_index);
            }
            else
            {
                SP_LOG_ERROR("Failed to get display mode %d for display %d", display_mode_index, display_index);
            }
        }
    }

    const vector<DisplayMode>& Display::GetDisplayModes()
    {
        return display_modes;
    }

    uint32_t Display::GetWidth()
    {
        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(GetIndex(), &display_mode) == 0);

        return display_mode.w;
    }

    uint32_t Display::GetHeight()
    {
        int display_index = SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(Window::GetHandleSDL()));

        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(GetIndex(), &display_mode) == 0);

        return display_mode.h;
    }

    uint32_t Display::GetRefreshRate()
    {
        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(GetIndex(), &display_mode) == 0);

        return display_mode.refresh_rate;
    }

    uint32_t Display::GetIndex()
    {
        int index = SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(Window::GetHandleSDL()));

        // during engine startup, the window doesn't exist yet, therefore it's not displayed by any monitor.
        // in this case the index can be -1, so we'll instead set the index to 0 (whatever the primary display is)
        return index != -1 ? index : 0;
    }
}
