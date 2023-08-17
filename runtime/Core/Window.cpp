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


//= INCLUDES =====================
#include "pch.h"
#include "Window.h"
#include "../Input/Input.h"
#include "../Display/Display.h"
#include "../Rendering/Renderer.h"
#include <SDL.h>
// Quick and dirty hack. The need for this kind of preprocessor is because the
// project includes all its libraries. SDL turns out to have a config file where
// certain files are enabled or disabled for each specific platform. Normally this
// would only affect the compilation but it turns out that the `SDL_syswm.h` header
// uses configuration stuff. The proper solution for linux would be to use the SDL
// headers and library provided by the distribution.
#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#   undef SDL_VIDEO_DRIVER_WINDOWS
#endif
#include <SDL_syswm.h>
#if defined(_MSC_VER)
#include <ShellScalingApi.h>
#endif
//================================

//= LINKING ============================
#ifdef _MSC_VER
// Statically linking SDL2 requirements
#pragma comment(lib, "Imm32.lib")
#pragma comment(lib, "Setupapi.lib")
// SetProcessDpiAwareness() requirements
#include <ShellScalingAPI.h>
#pragma comment(lib, "Shcore.lib")
#endif
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    namespace
    { 
        static std::string m_title;
        static Math::Vector2 m_position = Math::Vector2::Zero;
        static uint32_t width           = 640;
        static uint32_t height          = 480;
        static float dpi_scale          = 1.0f;
        static bool close               = false;
        static SDL_Window* window       = nullptr;

        // splash-screen
        static bool m_show_splash_screen              = true;
        static SDL_Window* m_splash_sceen_window      = nullptr;
        static SDL_Renderer* m_splash_screen_renderer = nullptr;
        static SDL_Texture* m_splash_screen_texture   = nullptr;
    }

    void Window::Initialize()
    {
        // Set the process to be per monitor DPI aware
        #ifdef _MSC_VER
        // User32.lib + dll, Windows 10 v1607+ (Creators Update)
        if (HMODULE user32 = LoadLibrary(TEXT("user32.dll")))
        {
            typedef DPI_AWARENESS_CONTEXT(WINAPI* pfn)(DPI_AWARENESS_CONTEXT);
            if (pfn SetThreadDpiAwarenessContext = (pfn)GetProcAddress(user32, "SetThreadDpiAwarenessContext"))
            {
                SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
            }
            FreeLibrary(user32);
        }
        #endif

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
        if (m_show_splash_screen)
        {
            CreateAndShowSplashScreen();
        }

        // Set window flags
        uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;

        // If the swapchain surface is created using SDL_Vulkan_CreateSurface(), then the window needs this flag.
        flags |= SDL_WINDOW_VULKAN;

        // Create window
        m_title  = "Spartan " + to_string(sp_info::version_major) + "." + to_string(sp_info::version_minor) + "." + to_string(sp_info::version_revision);
        window = SDL_CreateWindow(
            m_title.c_str(),         // window title
            SDL_WINDOWPOS_UNDEFINED, // initial x position
            SDL_WINDOWPOS_UNDEFINED, // initial y position
            width,                   // width in pixels
            height,                  // height in pixels
            flags                    // flags - see below
        );

        if (!window)
        {
            SP_LOG_ERROR("Could not create window: %s.", SDL_GetError());
            return;
        }

        if (m_show_splash_screen)
        {
            // Hide the window until the engine is able to present
            Hide();

            // Show the window and destroy the splash screen, after the first frame has been rendered successfully
            SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnFirstFrameCompleted, SP_EVENT_HANDLER_STATIC(OnFirstFrameCompleted));
        }


        // Get the DPI scale - has to be done after window creation
        #ifdef _MSC_VER
        dpi_scale = static_cast<float>(GetDpiForWindow(static_cast<HWND>(GetHandleRaw()))) / 96.0f;
        #endif

        // Register library
        string version = to_string(SDL_MAJOR_VERSION) + "." + to_string(SDL_MINOR_VERSION) + "." + to_string(SDL_PATCHLEVEL);
        Settings::RegisterThirdPartyLib("SDL", version, "https://www.libsdl.org/");
    }

    void Window::Shutdown()
    {
        // Destroy window
        SDL_DestroyWindow(window);

        // Shutdown SDL2
        SDL_Quit();
    }

    void Window::Tick()
    {
        // Process events
        SDL_Event sdl_event;
        while (SDL_PollEvent(&sdl_event))
        {
            if (sdl_event.type == SDL_WINDOWEVENT)
            {
                if (sdl_event.window.windowID == SDL_GetWindowID(window))
                {
                    switch (sdl_event.window.event)
                    {
                    case SDL_WINDOWEVENT_SHOWN:
                        break;
                    case SDL_WINDOWEVENT_HIDDEN:
                        break;
                    case SDL_WINDOWEVENT_EXPOSED:
                        break;
                    case SDL_WINDOWEVENT_MOVED:
                        break;
                    case SDL_WINDOWEVENT_RESIZED:
                        width  = static_cast<uint32_t>(sdl_event.window.data1);
                        height = static_cast<uint32_t>(sdl_event.window.data2);
                        SP_FIRE_EVENT(EventType::WindowResized);
                        break;
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        width  = static_cast<uint32_t>(sdl_event.window.data1);
                        height = static_cast<uint32_t>(sdl_event.window.data2);
                        break;
                    case SDL_WINDOWEVENT_MINIMIZED:
                        break;
                    case SDL_WINDOWEVENT_MAXIMIZED:
                        break;
                    case SDL_WINDOWEVENT_RESTORED:
                        break;
                    case SDL_WINDOWEVENT_ENTER:
                        // Window has gained mouse focus
                        break;
                    case SDL_WINDOWEVENT_LEAVE:
                        // Window has lost mouse focus
                        break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        // Window has gained keyboard focus
                        break;
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        // Window has lost keyboard focus
                        break;
                    case SDL_WINDOWEVENT_CLOSE:
                        close = true;
                        break;
                    case SDL_WINDOWEVENT_TAKE_FOCUS:
                        // Window is being offered a focus (should SetWindowInputFocus() on itself or a subwindow, or ignore)
                        break;
                    case SDL_WINDOWEVENT_HIT_TEST:
                        // Window had a hit test that wasn't SDL_HITTEST_NORMAL.
                        break;
                    case SDL_WINDOWEVENT_ICCPROF_CHANGED:
                        SP_LOG_INFO("The ICC profile of the window's display has changed");
                        break;
                    case SDL_WINDOWEVENT_DISPLAY_CHANGED:
                        SP_LOG_INFO("Display has been changed, detecting new display modes");
                        Display::DetectDisplayModes();
                        break;
                    default:
                        SP_LOG_ERROR("Unhandled window event");
                        break;
                    }
                }
            }

            SP_FIRE_EVENT_DATA(EventType::Sdl, &sdl_event);
        }

        // Handle shortcuts

        // Toggle full screen
        if (Input::GetKey(KeyCode::Alt_Right) && Input::GetKeyDown(KeyCode::Enter))
        {
            ToggleFullScreen();
        }
    }

    void Window::Show()
    {
        SP_ASSERT(window != nullptr);

        SDL_ShowWindow(window);
    }

    void Window::Hide()
    {
        SP_ASSERT(window != nullptr);

        SDL_HideWindow(window);
    }

    void Window::Focus()
    {
        SP_ASSERT(window != nullptr);

        SDL_RaiseWindow(window);
    }

    void Window::FullScreen()
    {
        SP_ASSERT(window != nullptr);

        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

        SP_FIRE_EVENT(EventType::WindowFullscreen);
    }

    void Window::Windowed()
    {
        SP_ASSERT(window != nullptr);

        SDL_SetWindowFullscreen(window, 0);
    }

    void Window::ToggleFullScreen()
	{
        if (IsFullScreen())
        {
            Windowed();
        }
        else
        {
            FullScreen();
        }

        SP_FIRE_EVENT(EventType::WindowFullscreenWindowedToggled);
	}

	void Window::FullScreenBorderless()
    {
        SP_ASSERT(window != nullptr);

        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    void Window::Minimise()
    {
        SP_ASSERT(window != nullptr);

        SDL_SetWindowFullscreen(window, SDL_WINDOW_MINIMIZED);
    }

    void Window::Maximise()
    {
        SP_ASSERT(window != nullptr);

        SDL_SetWindowFullscreen(window, SDL_WINDOW_MAXIMIZED);
    }

    void Window::SetSize(const uint32_t width, const uint32_t height)
    {
        SP_ASSERT(window != nullptr);

        SDL_SetWindowSize(window, static_cast<int>(width), static_cast<int>(height));
    }

    uint32_t Window::GetWidth()
    {
        SP_ASSERT(window != nullptr);

        int width = 0;
        SDL_GetWindowSize(window, &width, nullptr);
        return static_cast<uint32_t>(width);
    }

    uint32_t Window::GetHeight()
    {
        if (!window)
            return 0;

        int height = 0;
        SDL_GetWindowSize(window, nullptr, &height);
        return static_cast<uint32_t>(height);
    }

    float Window::GetDpiScale()
    {
        return dpi_scale;
    }

    void* Window::GetHandleSDL()
    {
        return window;
    }

    void* Window::GetHandleRaw()
    {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(window, &wmInfo);
        if(SDL_FALSE == SDL_GetWindowWMInfo(window, &wmInfo))
        {
            printf("Error: %s", SDL_GetError());
            return static_cast<void*>(nullptr);
        }
#ifdef _WIN32
        return static_cast<void*>(wmInfo.info.win.window);
#elif __linux__
        return reinterpret_cast<void*>(wmInfo.info.x11.window);
#endif
    }

    bool Window::WantsToClose()
    {
        return close;
    }

    bool Window::IsMinimised()
    {
        return SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED;
    }

    bool Window::IsFullScreen()
    {
        return SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
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
            "splash_screen",                         // window title
            pos_x,                                   // initial x position
            pos_y,                                   // initial y position
            width,                                   // width in pixels
            height,                                  // height in pixels
            SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS // flags
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
