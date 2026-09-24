/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===========
#include "Definitions.h"
//======================

namespace spartan
{
    class Window
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Tick();

        // behaviour
        static void Show();
        static void Hide();
        static void Focus();
        static void FullScreen();
        static void Windowed();
        static void ToggleFullScreen();
        static void FullScreenBorderless();
        static bool IsFullScreenTogglePending();
        static void ProcessFullScreenToggle();
        static void Minimize();
        static void Maximize();

        // size
        static void SetSize(const uint32_t width_new, const uint32_t height_new);
        static uint32_t GetWidth();
        static uint32_t GetHeight();
        static uint32_t GetWidthInPixels();
        static uint32_t GetHeightInPixels();
        static float GetDpiScale();

        // misc
        static void* GetHandleSDL();
        static void* GetHandleRaw();
        static void Close();
        static bool IsMinimized();
        static bool IsMaximized();
        static bool IsFullScreen();
        static bool WantsToClose();
        static void Restore();

        // custom title bar
        static void SetTitleBarHeight(float height);
        static void SetTitleBarButtonWidth(float width);
        static void SetTitleBarHovered(bool hovered);
        static void PumpEvents();

        // splash screen
        static void SetSplashScreenVisible(bool visible);

    private:
        static void CreateAndShowSplashScreen();
        static void OnFirstFrameCompleted();
    };
}
