/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Input.h"
#include "SDL.h"
#include "Window.h"
//==================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Input::PollMouse()
    {
        // Get state
        int x, y;
        Uint32 keys_states = SDL_GetGlobalMouseState(&x, &y);
        Vector2 position = Vector2(static_cast<float>(x), static_cast<float>(y));

        // Get delta
        m_mouse_delta = position - m_mouse_position;

        // Get position
        m_mouse_position = position;

        // Get keys
        m_keys[start_index_mouse]       = (keys_states & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;    // Left button pressed
        m_keys[start_index_mouse + 1]   = (keys_states & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;  // Middle button pressed
        m_keys[start_index_mouse + 2]   = (keys_states & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;   // Right button pressed
    }

    void Input::OnEventMouse(void* event_mouse)
    {
        // Validate event
        SP_ASSERT(event_mouse != nullptr);
        SDL_Event* sdl_event = static_cast<SDL_Event*>(event_mouse);
        Uint32 event_type = sdl_event->type;

        // Wheel
        if (event_type == SDL_MOUSEWHEEL)
        {
            if (sdl_event->wheel.x > 0) m_mouse_wheel_delta.x += 1;
            if (sdl_event->wheel.x < 0) m_mouse_wheel_delta.x -= 1;
            if (sdl_event->wheel.y > 0) m_mouse_wheel_delta.y += 1;
            if (sdl_event->wheel.y < 0) m_mouse_wheel_delta.y -= 1;
        }
    }

    void Input::SetMouseCursorVisible(const bool visible)
    {
        if (visible == m_mouse_cursor_visible)
            return;

        if (visible)
        {
            if (SDL_ShowCursor(SDL_ENABLE) != 0)
            {
                LOG_ERROR("Failed to show cursor");
                return;
            }
        }
        else
        {
            if (SDL_ShowCursor(SDL_DISABLE) != 1)
            {
                LOG_ERROR("Failed to hide cursor");
                return;
            }
        }

        m_mouse_cursor_visible = visible;
    }

    void Input::SetMousePosition(const Math::Vector2& position)
    {
        if (SDL_WarpMouseGlobal(static_cast<int>(position.x), static_cast<int>(position.y)) != 0)
        {
            LOG_ERROR("Failed to set mouse position.");
            return;
        }

        m_mouse_position = position;
    }

    const Spartan::Math::Vector2 Input::GetMousePositionRelativeToWindow() const
    {
        SDL_Window* window = static_cast<SDL_Window*>(m_context->GetSubsystem<Window>()->GetHandleSDL());
        int window_x, window_y;
        SDL_GetWindowPosition(window, &window_x, &window_y);
        return Vector2(static_cast<float>(m_mouse_position.x - window_x), static_cast<float>(m_mouse_position.y - window_y));
    }

    const Spartan::Math::Vector2 Input::GetMousePositionRelativeToEditorViewport() const
    {
        return GetMousePositionRelativeToWindow() - m_editor_viewport_offset;
    }
}
