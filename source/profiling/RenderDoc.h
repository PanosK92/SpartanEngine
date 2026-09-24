/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    class RenderDoc
    {
    public:
        static void OnPreDeviceCreation();
        static void Shutdown();
        static void Tick();
        static void FrameCapture();
        static void StartCapture();
        static void EndCapture();
        static void LaunchRenderDocUi();
    };
}
