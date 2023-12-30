﻿/*
Copyright(c) 2023 Nick Polyderopoulos

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

//= INCLUDES ===============
#include "pch.h"
#include "ScriptEngine.h"

//==========================

//= NAMESPACES ===============
//using namespace std;
//============================


//== NativeHost ======================================================

// Provided by the AppHost NuGet package and installed as an SDK pack
#include <nethost.h>

// Header files copied from https://github.com/dotnet/core-setup

#include <coreclr_delegates.h>
#include <hostfxr.h>

#ifdef WINDOWS
#include <Windows.h>

#define STR(s) L ## s
#define CH(c) L ## c
#define DIR_SEPARATOR L'\\'

#define string_compare wcscmp

#else
#include <dlfcn.h>
#include <limits.h>

#define STR(s) s
#define CH(c) c
#define DIR_SEPARATOR '/'
#define MAX_PATH PATH_MAX

#define string_compare strcmp

#endif

using string_t = std::basic_string<char_t>;

//==========================================================================
namespace Spartan
{

    namespace
    {
        // Globals to hold hostfxr exports
        hostfxr_initialize_for_dotnet_command_line_fn init_for_cmd_line_fptr;
        hostfxr_initialize_for_runtime_config_fn init_for_config_fptr;
        hostfxr_get_runtime_delegate_fn get_delegate_fptr;
        hostfxr_run_app_fn run_app_fptr;
        hostfxr_close_fn close_fptr;


        // private fields
        typedef void (CORECLR_DELEGATE_CALLTYPE* initialize_entry_point_fn)();
        initialize_entry_point_fn initialize = nullptr;

        typedef void (CORECLR_DELEGATE_CALLTYPE* tick_entry_point_fn)();
        tick_entry_point_fn tick = nullptr;

        typedef void (CORECLR_DELEGATE_CALLTYPE* shutdown_entry_point_fn)();
        shutdown_entry_point_fn shutdown = nullptr;

        load_assembly_and_get_function_pointer_fn function_pointer_getter = nullptr;

        // Constants
        string_t root_path;
        string_t app_path;
        string_t config_path;
        const char_t* dotnet_type = STR("Scripting.SDK.Engine, Scripting.SDK");

        // Forward declarations
        bool load_hostfxr(const char_t* app);
        load_assembly_and_get_function_pointer_fn get_dotnet_load_assembly(const char_t* assembly);

    }

    void ScriptEngine::Initialize()
    {
        root_path = std::filesystem::current_path().wstring() + DIR_SEPARATOR + STR("gameplay");
        app_path = root_path + DIR_SEPARATOR + STR("scripting.sdk.dll");
        config_path = root_path + DIR_SEPARATOR + STR("scripting.sdk.runtimeconfig.json");

        //
       // STEP 1: Load HostFxr and get exported hosting functions
       //
        if (!load_hostfxr(nullptr))
        {
            SP_ASSERT_MSG(false, "Failure: load_hostfxr()");
            return;
        }

        //
        // STEP 2: Initialize and start the .NET Core runtime
        //
        function_pointer_getter = get_dotnet_load_assembly(config_path.c_str());

        SP_ASSERT_MSG(function_pointer_getter != nullptr , "Failure: get_dotnet_load_assembly()");

        //
        // STEP 3: Load managed assembly and get function pointer to a managed method
        //
        
        
        //
        // STEP 4: Run managed code
        //
       
        // UnmanagedCallersOnly
        int rc = function_pointer_getter(
            app_path.c_str(),
            dotnet_type,
            STR("Initialize") /*method_name*/,
            UNMANAGEDCALLERSONLY_METHOD,
            nullptr,
            (void**)&initialize);

        SP_ASSERT_MSG(rc == 0 && initialize != nullptr, "Failure: load_assembly_and_get_function_pointer() initialize");

        // UnmanagedCallersOnly
        rc = function_pointer_getter(
            app_path.c_str(),
            dotnet_type,
            STR("Tick") /*method_name*/,
            UNMANAGEDCALLERSONLY_METHOD,
            nullptr,
            (void**)&tick);

        SP_ASSERT_MSG(rc == 0 && tick != nullptr, "Failure: load_assembly_and_get_function_pointer() tick");

        // UnmanagedCallersOnly
        rc = function_pointer_getter(
            app_path.c_str(),
            dotnet_type,
            STR("Shutdown") /*method_name*/,
            UNMANAGEDCALLERSONLY_METHOD,
            nullptr,
            (void**)&shutdown);

        SP_ASSERT_MSG(rc == 0 && shutdown != nullptr, "Failure: load_assembly_and_get_function_pointer() shutdown");

        initialize();



    }

    void ScriptEngine::Shutdown()
    {
        shutdown();

        // Maybe that will clean up the resources ?
        initialize = nullptr;
        tick = nullptr;
        shutdown = nullptr;

        init_for_cmd_line_fptr = nullptr;
        init_for_config_fptr = nullptr;
        get_delegate_fptr = nullptr;
        run_app_fptr = nullptr;
        close_fptr = nullptr;

        function_pointer_getter = nullptr;
    }

    void ScriptEngine::Tick()
    {
        tick();
    }

    /********************************************************************************************
     * Function used to load and activate .NET Core
     ********************************************************************************************/

    namespace
    {
        // Forward declarations
        void* load_library(const char_t*);
        void* get_export(void*, const char*);

#ifdef WINDOWS
        void* load_library(const char_t* path)
        {
            HMODULE h = ::LoadLibraryW(path);
            SP_ASSERT_MSG(h != nullptr, "ScriptEngine: Library Not Loaded");
            return (void*)h;
        }
        void* get_export(void* h, const char* name)
        {
            void* f = ::GetProcAddress((HMODULE)h, name);
            SP_ASSERT_MSG(f != nullptr, "ScriptEngine: Process Not Found");
            return f;
        }
#else
        void* load_library(const char_t* path)
        {
            void* h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
            SP_ASSERT_MSG(h != nullptr, "ScriptEngine: Library Not Loaded");
            return h;
        }
        void* get_export(void* h, const char* name)
        {
            void* f = dlsym(h, name);
            SP_ASSERT_MSG(f != nullptr, "ScriptEngine: Process Not Found");
            return f;
        }
#endif

        // <SnippetLoadHostFxr>
        // Using the nethost library, discover the location of hostfxr and get exports
        bool load_hostfxr(const char_t* assembly_path)
        {
            get_hostfxr_parameters params{ sizeof(get_hostfxr_parameters), assembly_path, nullptr };
            // Pre-allocate a large buffer for the path to hostfxr
            char_t buffer[MAX_PATH];
            size_t buffer_size = sizeof(buffer) / sizeof(char_t);
            int rc = get_hostfxr_path(buffer, &buffer_size, &params);
            if (rc != 0)
                return false;

            // Load hostfxr and get desired exports
            void* lib = load_library(buffer);
            init_for_cmd_line_fptr = (hostfxr_initialize_for_dotnet_command_line_fn)get_export(lib, "hostfxr_initialize_for_dotnet_command_line");
            init_for_config_fptr = (hostfxr_initialize_for_runtime_config_fn)get_export(lib, "hostfxr_initialize_for_runtime_config");
            get_delegate_fptr = (hostfxr_get_runtime_delegate_fn)get_export(lib, "hostfxr_get_runtime_delegate");
            run_app_fptr = (hostfxr_run_app_fn)get_export(lib, "hostfxr_run_app");
            close_fptr = (hostfxr_close_fn)get_export(lib, "hostfxr_close");

            return (init_for_config_fptr && get_delegate_fptr && close_fptr);
        }
        // </SnippetLoadHostFxr>

        // <SnippetInitialize>
        // Load and initialize .NET Core and get desired function pointer for scenario
        load_assembly_and_get_function_pointer_fn get_dotnet_load_assembly(const char_t* config_path)
        {
            // Load .NET Core
            void* load_assembly_and_get_function_pointer = nullptr;
            hostfxr_handle cxt = nullptr;
            int rc = init_for_config_fptr(config_path, nullptr, &cxt);
            if (rc != 0 || cxt == nullptr)
            {
                std::cerr << "Init failed: " << std::hex << std::showbase << rc << std::endl;
                close_fptr(cxt);
                return nullptr;
            }

            // Get the load assembly function pointer
            rc = get_delegate_fptr(
                cxt,
                hdt_load_assembly_and_get_function_pointer,
                &load_assembly_and_get_function_pointer);
            if (rc != 0 || load_assembly_and_get_function_pointer == nullptr)
                std::cerr << "Get delegate failed: " << std::hex << std::showbase << rc << std::endl;

            close_fptr(cxt);
            return (load_assembly_and_get_function_pointer_fn)load_assembly_and_get_function_pointer;
        }
        // </SnippetInitialize>
    }

}
