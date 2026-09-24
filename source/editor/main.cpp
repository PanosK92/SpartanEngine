/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Editor.h"
#include <vector>
#include <string>
#include <filesystem>
#include <system_error>
#ifdef _WIN32 // windows
#include <Windows.h>
#include <shellapi.h>
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
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    set_working_directory_to_executable();

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
    Editor editor = Editor(args);
    editor.Tick();

    return 0;
}
