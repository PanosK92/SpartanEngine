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

//= INCLUDES ===========================
#include "pch.h"
#include "RenderDoc.h"
#include "../RHI/RHI_Implementation.h"
#include "../Core/Window.h"
#include "../Core/Definitions.h"
#include "../Rendering/Renderer.h"
#include "../Logging/Log.h"
#if defined(_MSC_VER) // windows
#include "renderdoc/app/renderdoc_app.h"
#include <windows.h>
#else
#include "renderdoc_app.h"
#endif
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    static RENDERDOC_API_1_5_0* rdc_api = nullptr;
    static void* rdc_module             = nullptr;

#if defined(_MSC_VER) // windows
    static vector<wstring> get_renderdoc_dll_paths()
    {
        vector<wstring> dll_paths;
        static const wchar_t* installer_folders_path = TEXT("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\Folders");

        // open installer folders key
        HKEY hkey;
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, installer_folders_path, 0, KEY_READ, &hkey) != ERROR_SUCCESS) 
            return dll_paths;

        TCHAR    achClass[MAX_PATH] = TEXT(""); // buffer for class name
        DWORD    cchClassName = MAX_PATH;       // size of class string
        DWORD    cSubKeys = 0;                  // number of subkeys
        DWORD    cbMaxSubKey;                   // longest subkey size
        DWORD    cchMaxClass;                   // longest class string
        DWORD    c_values;                      // number of values for keyPath
        DWORD    cchMaxValue;                   // longest value name
        DWORD    cbMaxValueData;                // longest value data
        DWORD    cbSecurityDescriptor;          // size of security descriptor
        FILETIME ftLastWriteTime;               // last write time

        // get the class name and the value count
        DWORD query_result = RegQueryInfoKey(
            hkey,                  // keyPath handle
            achClass,              // buffer for class name
            &cchClassName,         // size of class string
            nullptr,               // reserved
            &cSubKeys,             // number of subkeys
            &cbMaxSubKey,          // longest subkey size
            &cchMaxClass,          // longest class string
            &c_values,             // number of values for this keyPath
            &cchMaxValue,          // longest value name
            &cbMaxValueData,       // longest value data
            &cbSecurityDescriptor, // security descriptor
            &ftLastWriteTime);     // last write time

        constexpr uint32_t MAX_VALUE_NAME = 8192;
        TCHAR ach_value[MAX_VALUE_NAME];
        wchar_t enum_value[MAX_VALUE_NAME] = TEXT("");

        if (c_values)
        {
            for (DWORD i = 0, retCode = ERROR_SUCCESS; i < c_values; i++)
            {
                DWORD cchValue = MAX_VALUE_NAME;
                ach_value[0] = '\0';
                DWORD type = REG_SZ;
                DWORD size;
                memset(enum_value, '\0', MAX_VALUE_NAME);

                // MSDN:  https://docs.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluea
                // If the data has the REG_SZ, REG_MULTI_SZ or REG_EXPAND_SZ type, the string may not have been stored with
                // the proper null-terminating characters. Therefore, even if the function returns ERROR_SUCCESS, the application
                // should ensure that the string is properly terminated before using it; otherwise, it may overwrite a buffer.
                retCode = RegEnumValue(hkey, i,
                    ach_value,
                    &cchValue,
                    nullptr,
                    &type,
                    nullptr,
                    &size);

                if (type != REG_SZ || retCode != ERROR_SUCCESS)
                    continue;

                retCode = RegQueryInfoKey(
                    hkey,                  // keyPath handle
                    achClass,              // buffer for class name
                    &cchClassName,         // size of class string
                    nullptr,               // reserved
                    &cSubKeys,             // number of subkeys
                    &cbMaxSubKey,          // longest subkey size
                    &cchMaxClass,          // longest class string
                    &c_values,              // number of values for this keyPath
                    &cchMaxValue,          // longest value name
                    &cbMaxValueData,       // longest value data
                    &cbSecurityDescriptor, // security descriptor
                    &ftLastWriteTime);     // last write time

                wstring path(ach_value);
                if (path.find(L"RenderDoc") != wstring::npos)
                {
                    // many paths qualify:
                    // 
                    // "C:\\Program Files\\RenderDoc\\plugins\\amd\\counters\\"
                    // "C:\\Program Files\\RenderDoc\\"
                    // "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\RenderDoc\\"
                    //
                    // Only consider the ones the contain the dll we want
                    const wstring rdc_dll_path = path += TEXT("renderdoc.dll");
                    WIN32_FIND_DATA find_file_data = { 0 };
                    HANDLE file_handle = FindFirstFile(rdc_dll_path.c_str(), &find_file_data);
                    if (file_handle != INVALID_HANDLE_VALUE)
                    {
                        dll_paths.push_back(path);
                    }
                }
            }
        }

        RegCloseKey(hkey);

        return dll_paths;
    }
#endif

    void RenderDoc::OnPreDeviceCreation()
    {
         // load renderdoc module and get a pointer to it's api
        if (rdc_api == nullptr)
        {
            pRENDERDOC_GetAPI rdc_get_api = nullptr;
#if defined(_MSC_VER) // windows
            // if renderdoc is already injected into the engine, use the existing module
            rdc_module = ::GetModuleHandleA("renderdoc.dll");

            // if renderdoc is not injected, load the module now
            if (rdc_module == nullptr)
            {
                vector<wstring> RDocDllPaths = get_renderdoc_dll_paths();
                SP_ASSERT_MSG(!RDocDllPaths.empty(), "Could not find any install locations for renderdoc.dll");
                wstring module_path = RDocDllPaths[0]; // assuming x64 is reported first
                rdc_module = ::LoadLibraryW(module_path.c_str());
            }

            SP_ASSERT_MSG(rdc_module != nullptr, "Failed to get RenderDoc module");

            // get the address of RENDERDOC_GetAPI
            rdc_get_api = (pRENDERDOC_GetAPI)::GetProcAddress(static_cast<HMODULE>(rdc_module), "RENDERDOC_GetAPI");
#else // linux
            SP_ASSERT_MSG(false, "Not implemented");
#endif
            SP_ASSERT_MSG(rdc_get_api != nullptr, "Failed to RENDERDOC_GetAPI function address from renderdoc.dll");
            SP_ASSERT_MSG(rdc_get_api(eRENDERDOC_API_Version_1_5_0, (void**)&rdc_api) != 0, "Failed to get RenderDoc API pointer");
        }

        SP_ASSERT_MSG(rdc_api != nullptr, "RenderDoc API has not been initialised");

        // disable muting of validation/debug layer messages
        rdc_api->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 1);
        rdc_api->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);
        rdc_api->SetCaptureOptionU32(eRENDERDOC_Option_VerifyBufferAccess, 1);

        // disable overlay
        rdc_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
    }

    void RenderDoc::Shutdown()
    {
        if (rdc_module != nullptr)
        {
#if defined(_MSC_VER) // windows
            ::FreeLibrary(static_cast<HMODULE>(rdc_module));
#else
            SP_ASSERT_MSG(false, "Not implemented");
#endif
        }
    }

    void RenderDoc::FrameCapture()
    {
        SP_ASSERT_MSG(rdc_api != nullptr, "RenderDoc is not initialized");

        // capture the next frame
        rdc_api->TriggerCapture();
        //rdc_api->TriggerMultiFrameCapture(2);

        LaunchRenderDocUi();
    }

    void RenderDoc::StartCapture()
    {
        SP_ASSERT_MSG(rdc_api != nullptr, "RenderDoc is not initialized");

        rdc_api->StartFrameCapture(nullptr, nullptr);
    }

    void RenderDoc::EndCapture()
    {
        SP_ASSERT_MSG(rdc_api != nullptr, "RenderDoc is not initialized");

        rdc_api->EndFrameCapture(nullptr, nullptr);

        LaunchRenderDocUi();
    }

    void RenderDoc::LaunchRenderDocUi()
    {
        // if the renderdoc ui is already running, make sure it's visible
        if (rdc_api->IsTargetControlConnected())
        {
            SP_LOG_INFO("Bringing RenderDoc to foreground...");
            rdc_api->ShowReplayUI();
        }
        // if the renderdoc ui is not running, launch it and connect
        else
        {
            SP_LOG_INFO("Launching RenderDoc...");
            if (rdc_api->LaunchReplayUI(true, "") == 0)
            {
                SP_LOG_ERROR("Failed to launch RenderDoc");
            }
        }
    }
}
