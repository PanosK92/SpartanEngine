#pragma once

//= INCLUDES ========
#include <string>
#include <functional>
#include "Engine.h"
//===================

//= FORWARD DECLARATIONS =========
struct SDL_Window;
typedef union SDL_Event SDL_Event;
//================================

namespace Spartan
{
    class SPARTAN_CLASS Window : public ISubsystem
    {
    public:
        Window(Context* context);
        ~Window();

        //= ISubsystem ======================
        void OnTick(float delta_time) override;
        //===================================

        void Show();
        void Hide();
        void Focus();
        void FullScreen();
        void FullScreenBorderless();
        void Minimise();
        void Maximise();
        void SetSize(const uint32_t width, const uint32_t height);
        uint32_t GetWidth();
        uint32_t GetHeight();
        void* GetHandle();
        void* GetHandleSDL()    const { return m_window; }
        bool WantsToClose()     const { return m_close; }
        bool IsMinimised()      const { return m_minimised; }
        bool IsFullScreen()     const { return m_fullscreen; }

    private:
        std::string m_title         = "Spartan " + std::string(sp_version);
        Math::Vector2 m_position    = Math::Vector2::Zero;
        uint32_t m_width            = 640;
        uint32_t m_height           = 480;
        bool m_shown                = false;
        bool m_hidden               = false;
        bool m_minimised            = false;
        bool m_maximised            = false;
        bool m_close                = false;
        bool m_fullscreen           = false;
        SDL_Window* m_window        = nullptr;
    };
}
