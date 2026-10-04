/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==============
#include "pch.h"
#include "Input.h"
#include "../core/Window.h"
#include "../core/Engine.h"
SP_WARNINGS_OFF
#include <SDL3/SDL.h>
SP_WARNINGS_ON
//=========================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        Vector2 mouse_position         = Vector2::Zero;
        Vector2 mouse_delta            = Vector2::Zero;
        Vector2 mouse_wheel_delta      = Vector2::Zero;
        Vector2 editor_viewport_offset = Vector2::Zero;
        bool mouse_is_in_viewport      = true;

        // injected motion still to deliver, spread linearly until injected_motion_end
        Vector2 injected_motion_remaining = Vector2::Zero;
        double injected_motion_end        = 0.0;
        double injected_motion_last       = 0.0;
        Vector2 injected_wheel            = Vector2::Zero;
    }

    void Input::PreTick()
    {
        mouse_wheel_delta = Vector2::Zero;
    }

    void Input::PollMouse()
    {
        // get state
        float x = 0.0f, y = 0.0f;
        SDL_MouseButtonFlags keys_states = 0;
        Vector2 position                 = mouse_position;
        if (!Engine::IsHeadless())
        {
            keys_states = SDL_GetGlobalMouseState(&x, &y);
            position    = Vector2(static_cast<float>(x), static_cast<float>(y));
        }

        // get delta
        mouse_delta = position - mouse_position;

        // get position
        mouse_position = position;

        if (injected_motion_remaining != Vector2::Zero)
        {
            const double now       = GetInjectionTime();
            const double time_left = injected_motion_end - injected_motion_last;
            const double step      = now - injected_motion_last;
            const float fraction   = (time_left <= 0.0 || step >= time_left) ? 1.0f : static_cast<float>(step / time_left);
            const Vector2 portion  = injected_motion_remaining * fraction;
            mouse_delta               += portion;
            injected_motion_remaining  = fraction >= 1.0f ? Vector2::Zero : injected_motion_remaining - portion;
            injected_motion_last       = now;
        }

        mouse_wheel_delta += injected_wheel;
        injected_wheel     = Vector2::Zero;

        // get keys
        m_keys[key_index_mouse]     = (keys_states & SDL_BUTTON_MASK(SDL_BUTTON_LEFT))   != 0; // left button pressed
        m_keys[key_index_mouse + 1] = (keys_states & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) != 0; // middle button pressed
        m_keys[key_index_mouse + 2] = (keys_states & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT))  != 0; // right button pressed
    }

    void Input::OnEventMouse(void* event)
    {
        SDL_Event* sdl_event = static_cast<SDL_Event*>(event);
        uint32_t event_type  = sdl_event->type;

        if (event_type == SDL_EVENT_MOUSE_WHEEL)
        {
            if (sdl_event->wheel.x > 0)
            {
                mouse_wheel_delta.x += 1;
            }
            if (sdl_event->wheel.x < 0)
            {
                mouse_wheel_delta.x -= 1;
            }
            if (sdl_event->wheel.y > 0)
            {
                mouse_wheel_delta.y += 1;
            }
            if (sdl_event->wheel.y < 0)
            {
                mouse_wheel_delta.y -= 1;
            }
        }
    }

    bool Input::GetMouseCursorVisible()
    {
        return SDL_CursorVisible();
    }

    void Input::SetMouseCursorVisible(const bool visible)
    {
        if (visible)
        {
            SDL_ShowCursor();
        }
        else
        {
            SDL_HideCursor();
        }
    }

    const Vector2 Input::GetMousePositionRelativeToWindow()
    {
        SDL_Window* window = static_cast<SDL_Window*>(Window::GetHandleSDL());
        int window_x, window_y;
        SDL_GetWindowPosition(window, &window_x, &window_y);
        return Vector2(static_cast<float>(mouse_position.x - window_x), static_cast<float>(mouse_position.y - window_y));
    }

    const Vector2 Input::GetMousePositionRelativeToEditorViewport()
    {
        return GetMousePositionRelativeToWindow() - editor_viewport_offset;
    }

    void Input::SetMouseIsInViewport(const bool is_in_viewport)
    {
        mouse_is_in_viewport = is_in_viewport;
    }

    bool Input::GetMouseIsInViewport()
    {
        if (Input::IsBlockedByUi())
        {
            return false;
        }

        return mouse_is_in_viewport || IsInjectingMouseButton();
    }

    void Input::InjectMouseMotion(const Vector2& delta, const float seconds)
    {
        const double now = GetInjectionTime();
        if (injected_motion_remaining == Vector2::Zero)
        {
            injected_motion_last = now;
            injected_motion_end  = now;
        }
        injected_motion_remaining += delta;
        injected_motion_end        = max(injected_motion_end, now + max(static_cast<double>(seconds), 0.0));
    }

    void Input::InjectMouseWheel(const Vector2& delta)
    {
        injected_wheel += delta;
    }

    Vector2 Input::GetInjectedMouseMotionRemaining()
    {
        return injected_motion_remaining;
    }

    void Input::ClearInjectedMouse()
    {
        injected_motion_remaining = Vector2::Zero;
        injected_wheel            = Vector2::Zero;
    }

    const Vector2& Input::GetMousePosition()
    {
        return mouse_position;
    }

    void Input::SetMousePosition(const math::Vector2& position)
    {
        if (!SDL_WarpMouseGlobal(position.x, position.y))
        {
            SP_LOG_ERROR("Failed to set mouse position.");
            return;
        }

        mouse_position = position;
    }

    const spartan::math::Vector2& Input::GetMouseDelta()
    {
        if (Input::IsBlockedByUi())
        {
            return Vector2::Zero;
        }

        return mouse_delta;
    }

    const spartan::math::Vector2& Input::GetMouseWheelDelta()
    {
        if (Input::IsBlockedByUi())
        {
            return Vector2::Zero;
        }

        return mouse_wheel_delta;
    }

    void Input::SetEditorViewportOffset(const math::Vector2& offset)
    {
        editor_viewport_offset = offset;
    }
}
