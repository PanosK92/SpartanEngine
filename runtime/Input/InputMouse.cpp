/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "Input.h"
#include "../Core/Window.h"
SP_WARNINGS_OFF
#include <SDL/SDL.h>
SP_WARNINGS_ON
//=========================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    // mouse
    Vector2 m_mouse_position         = Vector2::Zero;
    Vector2 m_mouse_delta            = Vector2::Zero;
    Vector2 m_mouse_wheel_delta      = Vector2::Zero;
    Vector2 m_editor_viewport_offset = Vector2::Zero;
    bool m_mouse_is_in_viewport      = true;

    void Input::PreTick()
    {
        m_mouse_wheel_delta = Vector2::Zero;
    }

    void Input::PollMouse()
    {
        // get state
        int x, y;
        Uint32 keys_states = SDL_GetGlobalMouseState(&x, &y);
        Vector2 position   = Vector2(static_cast<float>(x), static_cast<float>(y));

        // get delta
        m_mouse_delta = position - m_mouse_position;

        // get position
        m_mouse_position = position;

        // get keys
        GetKeys()[m_start_index_mouse]     = (keys_states & SDL_BUTTON(SDL_BUTTON_LEFT))   != 0; // Left button pressed
        GetKeys()[m_start_index_mouse + 1] = (keys_states & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0; // Middle button pressed
        GetKeys()[m_start_index_mouse + 2] = (keys_states & SDL_BUTTON(SDL_BUTTON_RIGHT))  != 0; // Right button pressed
    }

    void Input::OnEventMouse(void* event)
    {
        SDL_Event* sdl_event = static_cast<SDL_Event*>(event);
        uint32_t event_type  = sdl_event->type;

        if (event_type == SDL_MOUSEWHEEL)
        {
            if (sdl_event->wheel.x > 0) m_mouse_wheel_delta.x += 1;
            if (sdl_event->wheel.x < 0) m_mouse_wheel_delta.x -= 1;
            if (sdl_event->wheel.y > 0) m_mouse_wheel_delta.y += 1;
            if (sdl_event->wheel.y < 0) m_mouse_wheel_delta.y -= 1;
        }
    }

    bool Input::GetMouseCursorVisible()
    {
        return SDL_ShowCursor(SDL_QUERY) == 1;
    }

    void Input::SetMouseCursorVisible(const bool visible)
    {
        if (visible == GetMouseCursorVisible())
            return;

        if (SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE) < 0)
        {
            SP_LOG_ERROR("Failed to change cursor visibility");
        }
    }

    const Vector2 Input::GetMousePositionRelativeToWindow()
    {
        SDL_Window* window = static_cast<SDL_Window*>(Window::GetHandleSDL());
        int window_x, window_y;
        SDL_GetWindowPosition(window, &window_x, &window_y);
        return Vector2(static_cast<float>(m_mouse_position.x - window_x), static_cast<float>(m_mouse_position.y - window_y));
    }

    const Vector2 Input::GetMousePositionRelativeToEditorViewport()
    {
        return GetMousePositionRelativeToWindow() - m_editor_viewport_offset;
    }

    void Input::SetMouseIsInViewport(const bool is_in_viewport)
    {
        m_mouse_is_in_viewport = is_in_viewport;
    }

    bool Input::GetMouseIsInViewport()
    {
        return m_mouse_is_in_viewport;
    }

    const Vector2& Input::GetMousePosition()
    {
        return m_mouse_position;
    }

    void Input::SetMousePosition(const Math::Vector2& position)
    {
        if (SDL_WarpMouseGlobal(static_cast<int>(position.x), static_cast<int>(position.y)) != 0)
        {
            SP_LOG_ERROR("Failed to set mouse position.");
            return;
        }

        m_mouse_position = position;
    }

    const Spartan::Math::Vector2& Input::GetMouseDelta()
    {
        return m_mouse_delta;
    }

    const Spartan::Math::Vector2& Input::GetMouseWheelDelta()
    {
        return m_mouse_wheel_delta;
    }

    void Input::SetEditorViewportOffset(const Math::Vector2& offset)
    {
        m_editor_viewport_offset = offset;
    }
}
