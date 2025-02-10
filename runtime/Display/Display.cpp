/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ==============
#include "pch.h"
#include "Display.h"
#include "Window.h"
SP_WARNINGS_OFF
#include <SDL3/SDL_video.h>
SP_WARNINGS_ON
//=========================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        vector<DisplayMode> display_modes;
    }

    void Display::RegisterDisplayMode(const uint32_t width, const uint32_t height, float hz, uint32_t display_index)
    {
        SP_ASSERT_MSG(width  != 0,    "width can't be zero");
        SP_ASSERT_MSG(height != 0,    "height can't be zero");
        SP_ASSERT_MSG(hz     != 0.0f, "hz can't be zero");

        // early exit if the display mode is already registered
        for (const DisplayMode& display_mode : display_modes)
        {
            if (display_mode.width         == width  &&
                display_mode.height        == height &&
                display_mode.hz            == hz     &&
                display_mode.display_id == display_index)

            return;
        }

        // add the new display mode
        display_modes.emplace_back(width, height, hz, display_index);

        // sort display modes based on width, descending order
        sort(display_modes.begin(), display_modes.end(), [](const DisplayMode& display_mode_a, const DisplayMode& display_mode_b)
        {
            return display_mode_a.width > display_mode_b.width;
        });
    }

    void Display::Initialize()
    {
        display_modes.clear();

        uint32_t display_id = GetId();

        // get display mode count
        int display_mode_count;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display_id, &display_mode_count);
        if (!modes || display_mode_count <= 0)
        {
            SP_LOG_ERROR("Failed to get display modes: %s", SDL_GetError());
            return;
        }

        // register display modes
        for (int i = 0; i < display_mode_count; i++)
        {
            const SDL_DisplayMode* mode = modes[i];
            RegisterDisplayMode(
                mode->w,
                mode->h,
                mode->refresh_rate,
                display_id
            );
        }

        // free modes array when done
        SDL_free(modes);
   
        // log display info
        SP_LOG_INFO("Name: %s, Hz: %d, Gamma: %.1f, HDR: %s, max luminance: %.0f nits", GetName(), GetRefreshRate(), GetGamma(), GetHdr() ? "true" : "false", GetLuminanceMax());
    }

    const vector<DisplayMode>& Display::GetDisplayModes()
    {
        return display_modes;
    }
    
    uint32_t Display::GetWidth()
    {
        const SDL_DisplayMode* display_mode = SDL_GetCurrentDisplayMode(GetId());
        SP_ASSERT_MSG(display_mode, "Failed to get display mode");
        return display_mode->w;
    }

    uint32_t Display::GetHeight()
    {
        const SDL_DisplayMode* display_mode = SDL_GetCurrentDisplayMode(GetId());
        SP_ASSERT_MSG(display_mode, "Failed to get display mode");
        return display_mode->h;
    }

    uint32_t Display::GetRefreshRate()
    {
        const SDL_DisplayMode* display_mode = SDL_GetCurrentDisplayMode(GetId());
        SP_ASSERT_MSG(display_mode, "Failed to get display mode");
        return display_mode->refresh_rate;
    }

    uint32_t Display::GetId()
    {
        uint32_t index = SDL_GetDisplayForWindow(static_cast<SDL_Window*>(Window::GetHandleSDL()));

        // during engine startup, the window doesn't exist yet, therefore it's not displayed by any monitor.
        // in this case the index can be -1, so we'll instead set the index to 0 (whatever the primary display is)
        return index != -1 ? index : 0;
    }

    bool Display::GetHdr()
    {
        SDL_PropertiesID props = SDL_GetDisplayProperties(GetId());
        return SDL_GetBooleanProperty(props, SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false);
    }
    
    float Display::GetLuminanceMax()
    {
        float default_value = 350.0f; // a common value

        SDL_PropertiesID props = SDL_GetDisplayProperties(GetId());
        const char* property   = GetHdr() ? "SDL.display.HDR_white_level" : "SDL.display.SDR_white_level";
        return SDL_GetFloatProperty(props, property, default_value);
    }
    
    float Display::GetGamma()
    {
        // no good way to do that, so just return a default value
        // this needs to be calibrate per display by the user
        return 2.2f;
    }

    const char* Display::GetName()
    {
        return SDL_GetDisplayName(GetId());
    }
}
