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


//= INCLUDES =========================
#include "pch.h"
#include "Window.h"
#include "../Input/Input.h"
#include "../Display/Display.h"
#include "../RHI/RHI_Implementation.h"
SP_WARNINGS_OFF
#include <SDL3/SDL.h>
#pragma comment(lib, "winmm.lib") 
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "version.lib")
SP_WARNINGS_ON
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    { 
        std::string m_title;
        math::Vector2 m_position = math::Vector2::Zero;
        uint32_t width           = 1280;
        uint32_t height          = 720;
        float dpi_scale          = 1.0f;
        bool wants_to_close      = false;
        SDL_Window* window       = nullptr;

        // splash-screen
        bool m_show_splash_screen              = true;
        SDL_Window* m_splash_screen_window      = nullptr;
        SDL_Renderer* m_splash_screen_renderer = nullptr;
        SDL_Texture* m_splash_screen_texture   = nullptr;


        void sdl_initialize_subystems()
        {
            if (!SDL_WasInit(SDL_INIT_AUDIO))
            {
                if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                {
                    SP_LOG_ERROR("Failed to initialise SDL audio subsystem: %s.", SDL_GetError());
                }
            }

            if (!SDL_WasInit(SDL_INIT_VIDEO))
            {
                if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
                {
                    SP_LOG_ERROR("Failed to initialise SDL video subsystem: %s.", SDL_GetError());
                }
            }

            if (!SDL_WasInit(SDL_INIT_GAMEPAD))
            {
                if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
                {
                    SP_LOG_ERROR("Failed to initialise SDL gamepad subsystem: %s.", SDL_GetError());
                }
            }
        }
    }

    void Window::Initialize()
    {
        // set the process to be per monitor DPI aware - Windows 10 v1607+ (Creators Update)
        #ifdef _WIN32
        #pragma comment(lib, "Shcore.lib")
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

        sdl_initialize_subystems();

        // show a splash screen
        if (m_show_splash_screen)
        {
            CreateAndShowSplashScreen();
        }

        // set window flags
        uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
        if (RHI_Context::api_type == RHI_Api_Type::Vulkan)
        {
            flags |= SDL_WINDOW_VULKAN;
        }

        // create window
        m_title = string(sp_info::name) + " " + to_string(sp_info::version_major) + "." + to_string(sp_info::version_minor) + "." + to_string(sp_info::version_revision);
        window  = SDL_CreateWindow(
            m_title.c_str(),
            width,
            height,
            flags
        );

        if (!window)
        {
            SP_LOG_ERROR("Could not create window: %s.", SDL_GetError());
            return;
        }

        if (m_show_splash_screen)
        {
            // hide the window until the engine is able to present
            Hide();

            // show the window and destroy the splash screen, after the first frame has been rendered successfully
            SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnFirstFrameCompleted, SP_EVENT_HANDLER_STATIC(OnFirstFrameCompleted));
        }
 
        // get the DPI scale - has to be done after window creation
        #ifdef _WIN32
        dpi_scale = static_cast<float>(GetDpiForWindow(static_cast<HWND>(GetHandleRaw()))) / 96.0f;
        #endif

        // register library
        string version = to_string(SDL_MAJOR_VERSION) + "." + to_string(SDL_MINOR_VERSION) + "." + to_string(SDL_MICRO_VERSION);
        Settings::RegisterThirdPartyLib("SDL", version, "https://www.libsdl.org/");
    }

    void Window::Shutdown()
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void Window::Tick()
    {
        // process events
        SDL_Event sdl_event;
        while (SDL_PollEvent(&sdl_event))
        {
            if (sdl_event.window.windowID == SDL_GetWindowID(window))
            {
                switch (sdl_event.type)
                {
                case SDL_EVENT_WINDOW_SHOWN:
                    break;
                case SDL_EVENT_WINDOW_HIDDEN:
                    break;
                case SDL_EVENT_WINDOW_EXPOSED:
                    break;
                case SDL_EVENT_WINDOW_MOVED:
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                    width  = static_cast<uint32_t>(sdl_event.window.data1);
                    height = static_cast<uint32_t>(sdl_event.window.data2);
                    SP_FIRE_EVENT(EventType::WindowResized);
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                    break;
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                    break;
                case SDL_EVENT_WINDOW_MOUSE_ENTER:
                    break;
                case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    wants_to_close = true;
                    break;
                case SDL_EVENT_WINDOW_HIT_TEST:
                    break;
                case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
                    SP_LOG_INFO("The ICC profile of the window's display has changed");
                    break;
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                    SP_LOG_INFO("Display has been changed, detecting new display modes");
                    Display::Initialize();
                    break;
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    break;
                case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
                    break;
                case SDL_EVENT_WINDOW_OCCLUDED:
                    break;
                case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
                    break;
                case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                    break;
                case SDL_EVENT_KEY_DOWN:
                    break;
                case SDL_EVENT_KEY_UP:
                    break;
                case SDL_EVENT_TEXT_EDITING:
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    break;
                default:
                    SP_LOG_ERROR("Unhandled window event");
                    break;
                }
            }

            SP_FIRE_EVENT_DATA(EventType::Sdl, &sdl_event);
        }

        // handle shortcuts
        {
            // toggle full screen
            if (Input::GetKey(KeyCode::Alt_Right) && Input::GetKeyDown(KeyCode::Enter))
            {
                ToggleFullScreen();
            }
        }
    }

    void Window::Show()
    {
        SP_ASSERT(window);

        SDL_ShowWindow(window);
    }

    void Window::Hide()
    {
        SP_ASSERT(window != nullptr);

        SDL_HideWindow(window);
    }

    void Window::Focus()
    {
       SP_ASSERT(window);

        SDL_RaiseWindow(window);
    }

    void Window::FullScreen()
    {
        SP_ASSERT(window);

        bool was_windowed = !IsFullScreen();

        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

        SP_FIRE_EVENT(EventType::WindowResized);

        if (was_windowed)
        {
            Engine::SetFlag(EngineMode::EditorVisible, false);
            SP_FIRE_EVENT(EventType::WindowFullScreenToggled);
        }
    }

    void Window::Windowed()
    {
        SP_ASSERT(window);

        bool was_fullscreen = IsFullScreen();

        SDL_SetWindowFullscreen(window, 0);

        SP_FIRE_EVENT(EventType::WindowResized);

        if (was_fullscreen)
        {
            Engine::SetFlag(EngineMode::EditorVisible, true);
            SP_FIRE_EVENT(EventType::WindowFullScreenToggled);
        }
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
	}

	void Window::FullScreenBorderless()
    {
        SP_ASSERT(window);

        SDL_SetWindowFullscreen(window, true);
    }

    void Window::Minimize()
    {
        SP_ASSERT(window);

        SDL_MinimizeWindow(window);
    }

    void Window::Maximize()
    {
        SP_ASSERT(window);

        SDL_WindowFlags flags = SDL_GetWindowFlags(window);
        if (flags & SDL_WINDOW_MAXIMIZED)
        {
            SDL_RestoreWindow(window);
        }
        else
        {
            SDL_MaximizeWindow(window);
        }
    }

    void Window::SetSize(const uint32_t width, const uint32_t height)
    {
        SP_ASSERT(window);

        SDL_SetWindowSize(window, static_cast<int>(width), static_cast<int>(height));
    }

    uint32_t Window::GetWidth()
    {
        SP_ASSERT(window);

        int width = 0;
        SDL_GetWindowSize(window, &width, nullptr);
        return static_cast<uint32_t>(width);
    }

    uint32_t Window::GetHeight()
    {
        SP_ASSERT(window);

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
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        
        // windows
        if (void* handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
            return handle;
        
        // wayland
        if (void* handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr))
            return handle;
        
        // x11
        if (Uint64 x11_window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0))
            return reinterpret_cast<void*>(x11_window);

        return nullptr;
    }

    void Window::Close()
    {
        wants_to_close = true;
    }

    bool Window::IsMinimized()
    {
        return SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED;
    }

    bool Window::IsFullScreen()
    {
        return SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
    }

    bool Window::WantsToClose()
    {
        return wants_to_close;
    }

    void Window::CreateAndShowSplashScreen()
    {
        // load splash screen image
        SDL_Surface* image = SDL_LoadBMP("data\\textures\\banner.bmp");
        if (!image)
        {
            SP_LOG_ERROR("Failed to load splash screen image: %s", SDL_GetError());
        }
        
        // create splash screen window centered on screen
        if (image)
        { 
            m_splash_screen_window = SDL_CreateWindow(
                "splash_screen",
                image->w,
                image->h,
                SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP
            );
            if (!m_splash_screen_window)
            { 
                SP_LOG_ERROR("Failed to create splash screen window: %s", SDL_GetError());
            }
        }

        // create a renderer
        if (m_splash_screen_window)
        { 
            m_splash_screen_renderer = SDL_CreateRenderer(m_splash_screen_window, "vulkan");
            if (!m_splash_screen_renderer)
            { 
                SP_LOG_ERROR("Failed to create renderer: %s", SDL_GetError());
            }
        }

        // create texture (GPU) and free image (CPU)
        if (m_splash_screen_renderer)
        { 
            m_splash_screen_texture = SDL_CreateTextureFromSurface(m_splash_screen_renderer, image);
            if (!m_splash_screen_texture)
            { 
                SP_LOG_ERROR("Failed to create texture from surface: %s", SDL_GetError());
            }
        }
        SDL_DestroySurface(image);

        SDL_SetRenderDrawColor(m_splash_screen_renderer, 0, 0, 0, 255);
        SDL_RenderClear(m_splash_screen_renderer);
        SDL_RenderTexture(m_splash_screen_renderer, m_splash_screen_texture, nullptr, nullptr);
        SDL_RenderPresent(m_splash_screen_renderer);
    }

    void Window::OnFirstFrameCompleted()
    {
        // show engine window
        Show();

        // hide and destroy splash screen window
        SDL_DestroyTexture(m_splash_screen_texture);
        SDL_DestroyRenderer(m_splash_screen_renderer);
        SDL_DestroyWindow(m_splash_screen_window);
    }
}
