/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include <freetype/freetype.h>
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
        bool fullscreen_toggle_pending = false;
        SDL_Window* window             = nullptr;

        // splash-screen
        bool m_show_splash_screen          = true;
        SDL_Window* m_splash_screen_window = nullptr;

        void draw_splash_version(SDL_Surface* surface)
        {
            FT_Library library = nullptr;
            FT_Face face       = nullptr;
            if (FT_Init_FreeType(&library) != 0)
            {
                return;
            }

            if (FT_New_Face(library, "data/fonts/OpenSans/OpenSans-Bold.ttf", 0, &face) != 0)
            {
                FT_Done_FreeType(library);
                return;
            }

            constexpr int font_size = 20;
            constexpr int margin_x  = 14;
            constexpr int margin_y  = 12;
            FT_Set_Pixel_Sizes(face, 0, font_size);

            const char* text = version::c_str();
            int pen_x        = margin_x;
            int pen_y        = surface->h - margin_y;

            for (const char* p = text; *p != '\0'; ++p)
            {
                if (FT_Load_Char(face, static_cast<FT_ULong>(*p), FT_LOAD_RENDER) != 0)
                {
                    continue;
                }

                FT_GlyphSlot glyph = face->glyph;
                for (unsigned int row = 0; row < glyph->bitmap.rows; ++row)
                {
                    for (unsigned int col = 0; col < glyph->bitmap.width; ++col)
                    {
                        const int x = pen_x + glyph->bitmap_left + static_cast<int>(col);
                        const int y = pen_y - glyph->bitmap_top + static_cast<int>(row);
                        if (x < 0 || y < 0 || x >= surface->w || y >= surface->h)
                        {
                            continue;
                        }

                        const uint8_t coverage = glyph->bitmap.buffer[row * glyph->bitmap.pitch + col];
                        if (coverage == 0)
                        {
                            continue;
                        }

                        uint8_t r = 0;
                        uint8_t g = 0;
                        uint8_t b = 0;
                        uint8_t a = 0;
                        SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);

                        // blend white text over the banner
                        const uint32_t inv = 255 - coverage;
                        r = static_cast<uint8_t>((r * inv + 255 * coverage) / 255);
                        g = static_cast<uint8_t>((g * inv + 255 * coverage) / 255);
                        b = static_cast<uint8_t>((b * inv + 255 * coverage) / 255);
                        SDL_WriteSurfacePixel(surface, x, y, r, g, b, a);
                    }
                }

                pen_x += glyph->advance.x >> 6;
            }

            FT_Done_Face(face);
            FT_Done_FreeType(library);
        }

        // custom title bar
        float titlebar_height       = 40.0f;  // default height, updated by editor
        float titlebar_button_width = 150.0f; // default width, updated by editor  
        const float resize_border   = 8.0f;   // thickness of resize borders
        int titlebar_hovered_frames = 0;      // persistence counter for hover state

        SDL_HitTestResult hit_test_callback(SDL_Window* win, const SDL_Point* area, void* data)
        {
            int w, h;
            SDL_GetWindowSize(win, &w, &h);

            const int x = area->x;
            const int y = area->y;
            const int resize_margin = static_cast<int>(resize_border * dpi_scale);
            const bool is_maximized = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;

            // titlebar buttons take priority so the close button stays clickable at the top right corner
            if (y < static_cast<int>(titlebar_height) && x >= w - static_cast<int>(titlebar_button_width))
            {
                return SDL_HITTEST_NORMAL;
            }

            // resize hit tests, skipped when maximized since resizing is a no op
            if (!is_maximized)
            {
                bool top    = y < resize_margin;
                bool bottom = y >= h - resize_margin;
                bool left   = x < resize_margin;
                bool right  = x >= w - resize_margin;

                if (top && left)
                {
                    return SDL_HITTEST_RESIZE_TOPLEFT;
                }
                if (top && right)
                {
                    return SDL_HITTEST_RESIZE_TOPRIGHT;
                }
                if (bottom && left)
                {
                    return SDL_HITTEST_RESIZE_BOTTOMLEFT;
                }
                if (bottom && right)
                {
                    return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
                }

                if (top)
                {
                    return SDL_HITTEST_RESIZE_TOP;
                }
                if (bottom)
                {
                    return SDL_HITTEST_RESIZE_BOTTOM;
                }
                if (left)
                {
                    return SDL_HITTEST_RESIZE_LEFT;
                }
                if (right)
                {
                    return SDL_HITTEST_RESIZE_RIGHT;
                }
            }

            // remaining titlebar area is draggable when imgui is not interacting with anything
            if (y < static_cast<int>(titlebar_height) && titlebar_hovered_frames == 0)
            {
                return SDL_HITTEST_DRAGGABLE;
            }

            return SDL_HITTEST_NORMAL;
        }


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

        // set window flags - borderless for custom title bar
        // rhi-specific flags (e.g. SDL_WINDOW_VULKAN) come from RHI_Context, set at compile time per-api
        uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_BORDERLESS | RHI_Context::sdl_window_flags;

        // create window
        window  = SDL_CreateWindow(
            spartan::version::c_str(),
            width,
            height,
            flags
        );

        if (!window)
        {
            SP_LOG_ERROR("Could not create window: %s.", SDL_GetError());
            return;
        }

        // set up hit test callback for custom title bar dragging and resizing
        if (!SDL_SetWindowHitTest(window, hit_test_callback, nullptr))
        {
            SP_LOG_WARNING("Failed to set window hit test callback: %s", SDL_GetError());
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
        fullscreen_toggle_pending = true;
	}

    bool Window::IsFullScreenTogglePending()
    {
        return fullscreen_toggle_pending;
    }

    void Window::ProcessFullScreenToggle()
    {
        if (!fullscreen_toggle_pending)
        {
            return;
        }

        fullscreen_toggle_pending = false;

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

    void Window::SetSize(const uint32_t width_new, const uint32_t height_new)
    {
        SP_ASSERT(window);

        SDL_SetWindowSize(window, static_cast<int>(width_new), static_cast<int>(height_new));
    }

    uint32_t Window::GetWidth()
    {
        SP_ASSERT(window);

        int width_size = 0;
        SDL_GetWindowSize(window, &width_size, nullptr);
        return static_cast<uint32_t>(width_size);
    }

    uint32_t Window::GetHeight()
    {
        SP_ASSERT(window);

        int height_size = 0;
        SDL_GetWindowSize(window, nullptr, &height_size);
        return static_cast<uint32_t>(height_size);
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
        {
            return handle;
        }
        
        // wayland
        if (void* handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr))
        {
            return handle;
        }
        
        // x11
        if (Uint64 x11_window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0))
        {
            return reinterpret_cast<void*>(x11_window);
        }

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
        SDL_Surface* image = SDL_LoadBMP("data/textures/banner.bmp");
        if (!image)
        {
            SP_LOG_ERROR("Failed to load splash screen image: %s", SDL_GetError());
            return;
        }

        // create splash screen window centered on screen
        m_splash_screen_window = SDL_CreateWindow(
            "splash_screen",
            image->w,
            image->h,
            SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP
        );
        if (!m_splash_screen_window)
        {
            SP_LOG_ERROR("Failed to create splash screen window: %s", SDL_GetError());
            SDL_DestroySurface(image);
            return;
        }

        // get window surface
        SDL_Surface* window_surface = SDL_GetWindowSurface(m_splash_screen_window);
        if (!window_surface)
        {
            SP_LOG_ERROR("Failed to get window surface: %s", SDL_GetError());
            SDL_DestroyWindow(m_splash_screen_window);
            SDL_DestroySurface(image);
            return;
        }

        // blit image to window surface
        if (!SDL_BlitSurface(image, nullptr, window_surface, nullptr))
        {
            SP_LOG_ERROR("Failed to blit surface: %s", SDL_GetError());
            SDL_DestroyWindow(m_splash_screen_window);
            SDL_DestroySurface(image);
            return;
        }

        draw_splash_version(window_surface);

        // update window surface to display the image
        if (!SDL_UpdateWindowSurface(m_splash_screen_window))
        {
            SP_LOG_ERROR("Failed to update window surface: %s", SDL_GetError());
            SDL_DestroyWindow(m_splash_screen_window);
            SDL_DestroySurface(image);
            return;
        }

        SDL_DestroySurface(image);
    }

    void Window::OnFirstFrameCompleted()
    {
        // show engine window
        Show();

        // hide and destroy splash screen window
        SDL_DestroyWindow(m_splash_screen_window);
        m_splash_screen_window = nullptr;
    }

    void Window::SetSplashScreenVisible(bool visible)
    {
        if (!m_splash_screen_window)
        {
            return;
        }

        if (visible)
        {
            SDL_ShowWindow(m_splash_screen_window);
        }
        else
        {
            SDL_HideWindow(m_splash_screen_window);
        }
    }

    void Window::PumpEvents()
    {
        SDL_PumpEvents();
    }

    void Window::SetTitleBarHeight(float height)
    {
        titlebar_height = height;
    }

    void Window::SetTitleBarButtonWidth(float width)
    {
        titlebar_button_width = width;
    }

    bool Window::IsMaximized()
    {
        return SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED;
    }

    void Window::Restore()
    {
        SP_ASSERT(window);
        SDL_RestoreWindow(window);
    }

    void Window::SetTitleBarHovered(bool hovered)
    {
        // use persistence to avoid timing issues between sdl hit test and imgui frame
        // when hovered, set counter high; when not hovered, decrement until zero
        const int persistence_frames = 3;
        if (hovered)
        {
            titlebar_hovered_frames = persistence_frames;
        }
        else if (titlebar_hovered_frames > 0)
        {
            titlebar_hovered_frames--;
        }
    }
}
