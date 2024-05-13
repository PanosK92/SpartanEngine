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

#pragma once

//= INCLUDES ===========
#include "Definitions.h"
//======================

namespace Spartan
{
    class SP_CLASS Window
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
        static void Minimize();
        static void Maximize();

        // size
        static void SetSize(const uint32_t width, const uint32_t height);
        static uint32_t GetWidth();
        static uint32_t GetHeight();
        static float GetDpiScale();

        // misc
        static void* GetHandleSDL();
        static void* GetHandleRaw();
        static void Close();
        static bool IsMinimized();
        static bool IsFullScreen();
        static bool WantsToClose();

    private:
        static void CreateAndShowSplashScreen();
        static void OnFirstFrameCompleted();
    };
}
