/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "../error_handling/CrashHandler.h"
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

// Keep writable state separate from shipped files. Never overwrite newer settings during migration.
static bool initialize_runtime_directories()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const char* directory : { "settings", "logs", "cache" })
    {
        fs::create_directories(directory, ec);
        if (ec) return false;
    }
    const std::pair<const char*, const char*> migrations[] = {
        { "spartan.xml", "settings/spartan.xml" },
        { "editor.ini", "settings/editor.ini" },
        { "imgui_style_user.bin", "settings/imgui_style_user.bin" },
        { "spartan_worlds.xml", "settings/spartan_worlds.xml" },
        { "spartan_update.txt", "settings/spartan_update.txt" },
        { "game_build.xml", "settings/game_build.xml" },
        { "log.txt", "logs/log.txt" },
        { "log_previous.txt", "logs/log_previous.txt" },
        { "log_headless.txt", "logs/log_headless.txt" },
        { "log_headless_previous.txt", "logs/log_headless_previous.txt" },
        { "pipeline_cache.bin", "cache/pipeline_cache.bin" },
        { "pipeline_cache_d3d12.bin", "cache/pipeline_cache_d3d12.bin" },
        { "pipeline_cache_d3d12_v2.bin", "cache/pipeline_cache_d3d12_v2.bin" },
        { "generated_cache", "cache/generated_cache" }
    };
    for (const auto& [source, destination] : migrations)
    {
        if (fs::exists(source, ec) && !fs::exists(destination, ec))
        {
            fs::rename(source, destination, ec);
            if (ec) return false;
        }
    }
#ifdef _WIN32
    // The import libraries use /DELAYLOAD so this runs before any vendor DLL is needed.
    // Exclude the working directory and PATH from dependency searches.
    const fs::path libraries = fs::path(spartan::FileSystem::GetExecutableDirectory()) / "third_party";
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) || !AddDllDirectory(libraries.c_str()))
        return false;
#endif
    return true;
}

#ifdef _WIN32 // windows
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    const int reporter_result = spartan::CrashHandler::RunReporterIfRequested(lpCmdLine);
    if (reporter_result >= 0)
        return reporter_result;
    spartan::CrashHandler::Initialize();
    set_working_directory_to_executable();
    if (!initialize_runtime_directories())
    {
        std::fprintf(stderr, "Unable to initialize Spartan runtime directories.\n");
        return 1;
    }

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
    spartan::CrashHandler::Initialize();
    set_working_directory_to_executable();
    if (!initialize_runtime_directories())
    {
        std::fprintf(stderr, "Unable to initialize Spartan runtime directories.\n");
        return 1;
    }

    std::vector<std::string> args(argv, argv + argc);
#endif
    const auto has_argument = [&](const char* argument) { return std::find(args.begin(), args.end(), argument) != args.end(); };
    spartan::CrashHandler::SetInteractive(!(has_argument("--headless") || has_argument("-headless") || has_argument("--ci-smoke-test") || has_argument("--bake-shaders") ||
        (has_argument("--mcp-control") && has_argument("--mcp-hidden"))));
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
        spartan::CrashHandler::Terminate(error.what());
    }

    return 0;
#endif
}
