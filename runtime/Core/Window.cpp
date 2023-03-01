/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ==================
#include "pch.h"
#include "Window.h"
#include "sdl/SDL.h"
#include "sdl/SDL_syswm.h"
#include "../Input/Input.h"
#include "../Display/Display.h"
//=============================

//= LINKING ============================
// Statically linking SDL2 requires that
 // we link to all the libraries it uses
#ifdef _MSC_VER
#pragma comment(lib, "Imm32.lib")
#pragma comment(lib, "Setupapi.lib")
#endif

//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Window::Window(Context* context) : ISystem(context)
    {
        // Initialise video subsystem (if needed)
        if (SDL_WasInit(SDL_INIT_VIDEO) != 1)
        {
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
            {
                SP_LOG_ERROR("Failed to initialise SDL video subsystem: %s.", SDL_GetError());
                return;
            }
        }

        // Initialise events subsystem (if needed)
        if (SDL_WasInit(SDL_INIT_EVENTS) != 1)
        {
            if (SDL_InitSubSystem(SDL_INIT_EVENTS) != 0)
            {
                SP_LOG_ERROR("Failed to initialise SDL events subsystem: %s.", SDL_GetError());
                return;
            }
        }

        // Show a splash screen
        CreateAndShowSplashScreen();

        // Set window flags
        uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;

        // If the swapchain surface is created using SDL_Vulkan_CreateSurface(), then the window needs this flag.
        flags |= SDL_WINDOW_VULKAN;

        // Create window
        m_title        = "Spartan " + to_string(sp_version_major) + "." + to_string(sp_version_minor) + "." + to_string(sp_version_revision);
        m_window       = SDL_CreateWindow(
            m_title.c_str(),         // window title
            SDL_WINDOWPOS_UNDEFINED, // initial x position
            SDL_WINDOWPOS_UNDEFINED, // initial y position
            m_width,                 // width in pixels
            m_height,                // height in pixels
            flags                    // flags - see below
        );

        if (!m_window)
        {
            SP_LOG_ERROR("Could not create window: %s.", SDL_GetError());
            return;
        }

        // Hide the window until the engine is able to present
        Hide();

        // Show the window and destroy the splash screen, after the first frame has been renderered successfully
        SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnFirstFrameCompleted, SP_EVENT_HANDLER(OnFirstFrameCompleted));
    }

    Window::~Window()
    {
        // Destroy window
        SDL_DestroyWindow(m_window);

        // Shutdown SDL2
        SDL_Quit();
    }

    void Window::OnTick(double delta_time)
    {
        // Process events
        SDL_Event sdl_event;
        while (SDL_PollEvent(&sdl_event))
        {
            if (sdl_event.type == SDL_WINDOWEVENT)
            {
                switch (sdl_event.window.event)
                {
                case SDL_WINDOWEVENT_SHOWN:
                    m_shown = true;
                    break;
                case SDL_WINDOWEVENT_HIDDEN:
                    m_shown = false;
                    break;
                case SDL_WINDOWEVENT_EXPOSED:
                    //Window has been exposed and should be redrawn
                    break;
                case SDL_WINDOWEVENT_MOVED:
                    //Window has been moved to data1, data2
                    break;
                case SDL_WINDOWEVENT_RESIZED:
                    m_width  = static_cast<uint32_t>(sdl_event.window.data1);
                    m_height = static_cast<uint32_t>(sdl_event.window.data2);
                    break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    m_width = static_cast<uint32_t>(sdl_event.window.data1);
                    m_height = static_cast<uint32_t>(sdl_event.window.data2);
                    break;
                case SDL_WINDOWEVENT_MINIMIZED:
                    m_minimised = true;
                    m_maximised = false;
                    break;
                case SDL_WINDOWEVENT_MAXIMIZED:
                    m_maximised = true;
                    m_minimised = false;
                    break;
                case SDL_WINDOWEVENT_RESTORED:
                    //SDL_Log("Window %d restored", event->window.windowID);
                    break;
                case SDL_WINDOWEVENT_ENTER:
                    //Window has gained mouse focus
                    break;
                case SDL_WINDOWEVENT_LEAVE:
                    //Window has lost mouse focus
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED:
                    //Window has gained keyboard focus
                    break;
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    //Window has lost keyboard focus
                    break;
                case SDL_WINDOWEVENT_CLOSE:
                    m_close = true;
                    break;
                case SDL_WINDOWEVENT_TAKE_FOCUS:
                    //Window is being offered a focus (should SetWindowInputFocus() on itself or a subwindow, or ignore)
                    break;
                case SDL_WINDOWEVENT_HIT_TEST:
                    //Window had a hit test that wasn't SDL_HITTEST_NORMAL.
                    break;
                case SDL_WINDOWEVENT_ICCPROF_CHANGED:
                    SP_LOG_INFO("The ICC profile of the window's display has changed");
                    break;
                default:
                    SP_LOG_ERROR("Unhandled window event");
                    break;
                }
            }

            SP_FIRE_EVENT_DATA(EventType::EventSDL, &sdl_event);
        }

        // Handle shortcuts
        if (Input* input = m_context->GetSystem<Input>())
        {
            // Toggle full screen
            if (input->GetKey(KeyCode::Alt_Right) && input->GetKeyDown(KeyCode::Enter))
            {
                ToggleFullScreen();
            }
        }
    }

    void Window::OnInitialise()
    {
        // Register library
        string version = to_string(SDL_MAJOR_VERSION) + "." + to_string(SDL_MINOR_VERSION) + "." + to_string(SDL_PATCHLEVEL);
        Settings::RegisterThirdPartyLib("SDL", version, "https://www.libsdl.org/");
    }

    void Window::Show()
    {
        SP_ASSERT(m_window != nullptr);

        SDL_ShowWindow(m_window);
    }

    void Window::Hide()
    {
        SP_ASSERT(m_window != nullptr);

        SDL_HideWindow(m_window);
    }

    void Window::Focus()
    {
        SP_ASSERT(m_window != nullptr);

        SDL_RaiseWindow(m_window);
    }

    void Window::FullScreen()
    {
        SP_ASSERT(m_window != nullptr);

        SP_LOG_INFO("Entering full screen mode...");

        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN);
        m_fullscreen = true;
    }

    void Window::Windowed()
    {
        SP_ASSERT(m_window != nullptr);

        SP_LOG_INFO("Entering windowed mode...");

        SDL_SetWindowFullscreen(m_window, 0);
        m_fullscreen = false;
    }

    void Window::ToggleFullScreen()
	{
        if (!m_fullscreen)
        {
            FullScreen();
        }
        else
        {
            Windowed();
        }

        SP_FIRE_EVENT(EventType::WindowOnFullScreenToggled);
	}

	void Window::FullScreenBorderless()
    {
        SP_ASSERT(m_window != nullptr);

        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        m_fullscreen = true;
    }

    void Window::Minimise()
    {
        SP_ASSERT(m_window != nullptr);

        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_MINIMIZED);
    }

    void Window::Maximise()
    {
        SP_ASSERT(m_window != nullptr);

        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_MAXIMIZED);
    }

    void Window::SetSize(const uint32_t width, const uint32_t height)
    {
        SP_ASSERT(m_window != nullptr);

        SDL_SetWindowSize(m_window, static_cast<int>(width), static_cast<int>(height));
    }

    uint32_t Window::GetWidth()
    {
        SP_ASSERT(m_window != nullptr);

        int width = 0;
        SDL_GetWindowSize(m_window, &width, nullptr);
        return static_cast<uint32_t>(width);
    }

    uint32_t Window::GetHeight()
    {
        if (!m_window)
            return 0;

        int height = 0;
        SDL_GetWindowSize(m_window, nullptr, &height);
        return static_cast<uint32_t>(height);
    }

    void* Window::GetNativeHandle()
    {
        if (!m_window)
        {
            return nullptr;
        }
#ifdef _WIN32
        SDL_SysWMinfo system_info;
        SDL_VERSION(&system_info.version);
        SDL_GetWindowWMInfo(m_window, &system_info);
        return system_info.info.win.window;
#else
        return nullptr;
#endif
    }

    void Window::CreateAndShowSplashScreen()
    {
        // Load banner image - todo: remove hardcoded path
        SDL_Surface* image = SDL_LoadBMP("data\\textures\\banner.bmp");
        SP_ASSERT_MSG(image != nullptr, "Failed to load splash screen image");

        // Compute window position
        uint32_t width  = image->w;
        uint32_t height = image->h;
        uint32_t pos_x  = (Display::GetWidth() / 2)  - (width / 2);
        uint32_t pos_y  = (Display::GetHeight() / 2) - (height / 2);

        // Create splash screen
        m_splash_sceen_window = SDL_CreateWindow(
            "splash_screen",                                                    // window title
            pos_x,                                                              // initial x position
            pos_y,                                                              // initial y position
            width,                                                              // width in pixels
            height,                                                             // height in pixels
            SDL_WINDOW_SHOWN | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_BORDERLESS // flags
        );

        // Create a renderer
        m_splash_screen_renderer = SDL_CreateRenderer(m_splash_sceen_window, -1, 0);

        // Create texture (GPU) and free image (CPU)
        m_splash_screen_texture = SDL_CreateTextureFromSurface(m_splash_screen_renderer, image);
        SDL_FreeSurface(image);

        // Draw/copy and present
        SDL_RenderCopy(m_splash_screen_renderer, m_splash_screen_texture, nullptr, nullptr);
        SDL_RenderPresent(m_splash_screen_renderer);
    }

    void Window::OnFirstFrameCompleted()
    {
        // Show engine window
        Show();

        // Hide and destroy splash screen window
        SDL_DestroyTexture(m_splash_screen_texture);
        SDL_DestroyRenderer(m_splash_screen_renderer);
        SDL_DestroyWindow(m_splash_sceen_window);
    }
}
