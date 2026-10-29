/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "ErrorHandler.h"
#include "Breadcrumbs.h"
#include "../core/Engine.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <SDL3/SDL.h>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace spartan
{
    void ErrorHandler::Fatal(ErrorCategory category, const char* operation, const char* detail,
        const char* resource, uint64_t requested_bytes)
    {
        static std::atomic_flag reporting = ATOMIC_FLAG_INIT;
        static thread_local bool is_reporting = false;
        if (is_reporting)
            std::_Exit(EXIT_FAILURE);
        if (reporting.test_and_set())
        {
            // Another thread owns the report and dialog. Do not overwrite its original failure.
            for (;;)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        is_reporting = true;

        const char* title = "Engine error";
        const char* explanation = "The engine encountered an internal error and must close. Please send the error report to the developer.";
        switch (category)
        {
            case ErrorCategory::GraphicsMemory:
                title = "Not enough graphics memory";
                explanation = "The engine could not allocate graphics memory and must close. Try lowering graphics settings or closing other applications that use the GPU.";
                break;
            case ErrorCategory::SystemMemory:
                title = "Not enough memory";
                explanation = "The engine could not allocate memory and must close. Try closing other applications or loading a smaller world.";
                break;
            case ErrorCategory::DeviceLost:
                title = "Graphics device stopped responding";
                explanation = "The graphics device became unavailable and the engine must close. Please send the error report to the developer.";
                break;
            case ErrorCategory::GraphicsApi:
                title = "Graphics operation failed";
                explanation = "A required graphics operation failed and the engine must close. Please send the error report to the developer.";
                break;
            case ErrorCategory::Internal: break;
        }

        char report[2048];
        std::snprintf(report, sizeof(report), "%s\n%s\nOperation: %s\nDetails: %s\nResource: %s\nRequested bytes: %llu",
            title, explanation, operation ? operation : "unknown", detail ? detail : "unknown",
            resource ? resource : "not applicable", static_cast<unsigned long long>(requested_bytes));
        std::fprintf(stderr, "%s\n", report);
        std::fflush(stderr);

        // Save the primary failure before optional diagnostics. These paths bypass editor callbacks.
        bool saved = false;
        try { saved = Log::WriteFatal(report); } catch (...) {}
        const char* report_path = Log::GetFileName();
        if (!saved)
        {
            // A failure on a logging thread must still have somewhere to write its primary error.
            report_path = "error_report.txt";
            FILE* file = nullptr;
#if defined(_WIN32)
            fopen_s(&file, report_path, "w");
#else
            file = std::fopen(report_path, "w");
#endif
            if (file)
            {
                saved = std::fprintf(file, "%s\n", report) >= 0;
                saved = std::fclose(file) == 0 && saved;
            }
        }
        if (category != ErrorCategory::GraphicsMemory && category != ErrorCategory::SystemMemory)
        {
            try { Log::WriteFatal(get_callstack_c_str()); } catch (...) {}
        }
        // The existing snapshot formatter allocates CPU memory; skip it when that is exhausted.
        if (category != ErrorCategory::SystemMemory)
        {
            try { Breadcrumbs::Report(); } catch (...) {}
        }

        char absolute_path[4096] = {};
#if defined(_WIN32)
        const DWORD path_length = GetFullPathNameA(report_path, sizeof(absolute_path), absolute_path, nullptr);
        if (path_length > 0 && path_length < sizeof(absolute_path))
            report_path = absolute_path;
#endif
        char message[4096];
        std::snprintf(message, sizeof(message), "%s\n\nWhile performing: %s\n\n%s: %s", explanation,
            operation ? operation : "unknown operation",
            saved ? "Error report saved to" : "Could not save the error report to", report_path);
        if (!Engine::IsHeadless() && !Engine::IsStartupSmokeTest())
        {
#if defined(_WIN32)
            MessageBoxA(nullptr, message, title, MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
#endif
        }

        // GPU waits and resource teardown are unsafe after a fatal graphics failure.
        std::_Exit(EXIT_FAILURE);
    }
}
