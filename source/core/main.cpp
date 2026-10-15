/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#ifdef SP_RUNTIME
#include "runtime/Runtime.h"
#else
#include "editor/Editor.h"
#endif
#include <vector>
#include <string>
#include <filesystem>
#include <system_error>
#ifdef _WIN32 // windows
#include <Windows.h>
#include <shellapi.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

// force the working directory to the executable's own, the shell can hand us a different cwd
static void set_working_directory_to_executable()
{
    const std::string exe_dir = spartan::FileSystem::GetExecutableDirectory();
    if (!exe_dir.empty())
    {
        std::error_code ec;
        std::filesystem::current_path(exe_dir, ec);
    }
}

#ifdef _WIN32 // windows
// the system crash dump is a minidump without the heap, a use after free cannot be traced from it
static LONG WINAPI write_full_crash_dump(EXCEPTION_POINTERS* exception)
{
    HANDLE file = CreateFileA("crash_full.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION info = {};
        info.ThreadId          = GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers    = FALSE;
        const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    set_working_directory_to_executable();
    SetUnhandledExceptionFilter(write_full_crash_dump);

    // convert command line to argv-like format
    int argc;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i)
    {
        // convert wide characters to normal string
        char arg[1024];
        WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, arg, sizeof(arg), nullptr, nullptr);
        args.push_back(std::string(arg));
    }
    LocalFree(argv_w);
#else // linux
int main(int argc, char** argv)
{
    set_working_directory_to_executable();

    std::vector<std::string> args(argv, argv + argc);
#endif
#ifdef SP_RUNTIME
    return spartan::RunRuntime(args);
#else
    try
    {
        {
            Editor editor(args);
            editor.Tick();
        }
        // Only report success after the editor and engine have finished shutting down.
        if (spartan::Engine::IsStartupSmokeTest())
            SP_LOG_INFO("STARTUP_SMOKE_TEST_PASSED");
    }
    catch (const std::exception& error)
    {
        SP_LOG_ERROR("Editor startup/run failed: %s", error.what());
        return 1;
    }

    return 0;
#endif
}
