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

//= INCLUDES ===========================
#include "pch.h"
#include "RHI_RenderDoc.h"
#include "RHI_Implementation.h"
#include "../Core/Window.h"
#include "../Core/SpartanDefinitions.h"
#include "../Rendering/Renderer.h"
#include "../Logging/Log.h"
#include "renderdoc/app/renderdoc_app.h"
#if defined(_MSC_VER) // Windows
#include <windows.h>
#endif
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    static RENDERDOC_API_1_5_0* rdc_api = nullptr;
    static void* rdc_module             = nullptr;

    void RHI_RenderDoc::OnPreDeviceCreation()
    {
         // Load RenderDoc module and get a pointer to it's API
        if (rdc_api == nullptr)
        {
            pRENDERDOC_GetAPI rdc_get_api = nullptr;
#if defined(_MSC_VER) // Windows

            // If RenderDoc is already injected into the engine, use the existing module
            rdc_module = ::GetModuleHandleA("renderdoc.dll");

            // If RenderDoc is not injected, load the module now
            if (rdc_module == nullptr)
            {
                // todo: get module path from system registry
                string module_path = "C:\\Program Files\\RenderDoc\\renderdoc.dll"; 
                rdc_module = ::LoadLibraryA(module_path.c_str());
            }

            SP_ASSERT_MSG(rdc_module != nullptr, "Failed to get RenderDoc module");

            // Get the address of RENDERDOC_GetAPI
            rdc_get_api = (pRENDERDOC_GetAPI)::GetProcAddress(static_cast<HMODULE>(rdc_module), "RENDERDOC_GetAPI");

#else // Linux
            SP_ASSERT_MSG(false, "Not implemented");
#endif
            SP_ASSERT_MSG(rdc_get_api != nullptr, "Failed to RENDERDOC_GetAPI function address from renderdoc.dll");
            SP_ASSERT_MSG(rdc_get_api(eRENDERDOC_API_Version_1_5_0, (void**)&rdc_api) != 0, "Failed to get RenderDoc API pointer");
        }

        SP_ASSERT_MSG(rdc_api != nullptr, "RenderDoc API has not been initialised");

        // Disable muting of validation/debug layer messages
        rdc_api->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);

        // Disable overlay
        rdc_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
    }

    void RHI_RenderDoc::Shutdown()
    {
        if (rdc_module != nullptr)
        {
#if defined(_MSC_VER) // Windows
            ::FreeLibrary(static_cast<HMODULE>(rdc_module));
#else // Linux
            SP_ASSERT_MSG(false, "Not implemented");
#endif
        }
    }

    void RHI_RenderDoc::FrameCapture()
    {
        // Ignore the call if RenderDoc is not initialised/disabled
        if (rdc_api == nullptr)
            return;

        // Trigger
        rdc_api->TriggerCapture();

        // If the RenderDoc UI is already running, make sure it's visible.
        if (rdc_api->IsTargetControlConnected())
        {
            LOG_INFO("Bringing RenderDoc to foreground...");
            rdc_api->ShowReplayUI();
            return;
        }

        // If the RenderDoc UI is not running, launch it and connect.
        LOG_INFO("Launching RenderDoc...");
        if (rdc_api->LaunchReplayUI(true, "") == 0)
        {
            LOG_ERROR("Failed to launch RenderDoc");
        }
    }
}
