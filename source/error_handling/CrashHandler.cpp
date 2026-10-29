/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "CrashHandler.h"
#include <exception>
#include <csignal>
#include <cstdlib>

#if defined(_WIN32)
#include <Windows.h>
#include <DbgHelp.h>
#include <crtdbg.h>
#pragma comment(lib, "dbghelp.lib")

namespace spartan
{
    namespace
    {
        constexpr DWORD termination_exception = 0xE0000001;
        constexpr size_t path_capacity = 32768;
        struct CrashContext
        {
            EXCEPTION_RECORD exception;
            CONTEXT registers;
            char reason[512];
        };

        // Fixed storage: the faulting thread must not use engine allocation, logging, or locks.
        CrashContext crash = {};
        wchar_t executable[path_capacity] = {};
        wchar_t reporter_command[path_capacity + 256] = {};
        volatile LONG reporting_thread = 0;
        volatile LONG interactive = 1;
        thread_local char termination_reason[512] = {};

        LONG WINAPI on_exception(EXCEPTION_POINTERS* exception)
        {
            const DWORD thread_id = GetCurrentThreadId();
            const LONG owner = InterlockedCompareExchange(&reporting_thread, static_cast<LONG>(thread_id), 0);
            if (owner != 0)
            {
                if (static_cast<DWORD>(owner) == thread_id)
                    TerminateProcess(GetCurrentProcess(), exception->ExceptionRecord->ExceptionCode);
                // Keep another faulting thread alive so the first reporter can capture it too.
                Sleep(INFINITE);
                return EXCEPTION_EXECUTE_HANDLER;
            }

            crash.exception = *exception->ExceptionRecord;
            crash.exception.ExceptionRecord = nullptr;
            crash.registers = *exception->ContextRecord;
            strcpy_s(crash.reason, termination_reason[0] ? termination_reason : "Unhandled native exception");

            swprintf_s(reporter_command, L"\"%s\" --spartan-crash-report %lu %lu %llu %ld", executable,
                GetCurrentProcessId(), thread_id, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(&crash)),
                InterlockedCompareExchange(&interactive, 0, 0));
            STARTUPINFOW startup = {};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION process = {};
            if (CreateProcessW(executable, reporter_command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            {
                CloseHandle(process.hThread);
                // The original exception context remains valid while the helper reads this process.
                WaitForSingleObject(process.hProcess, 120000);
                CloseHandle(process.hProcess);
            }
            else if (InterlockedCompareExchange(&interactive, 0, 0))
            {
                MessageBoxW(nullptr, L"The engine crashed, but the crash reporter could not start. No crash dump was saved.",
                    L"Engine crashed", MB_OK | MB_ICONERROR | MB_TOPMOST);
            }
            TerminateProcess(GetCurrentProcess(), crash.exception.ExceptionCode);
            return EXCEPTION_EXECUTE_HANDLER;
        }

        void report_current_exception(const char* reason)
        {
            // std::terminate has no native fault context. Capture its current stack and label it explicitly.
            try
            {
                if (std::exception_ptr current = std::current_exception())
                    std::rethrow_exception(current);
            }
            catch (const std::exception& error) { CrashHandler::Terminate(error.what()); }
            catch (...) { reason = "Unhandled non-standard C++ exception"; }
            CrashHandler::Terminate(reason);
        }

        void on_terminate()
        {
            report_current_exception("std::terminate called without an active C++ exception");
        }

        bool create_directory(const wchar_t* directory)
        {
            return CreateDirectoryW(directory, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
        }

        // Try beside the executable first, then a user-writable temporary directory.
        HANDLE create_dump_file(wchar_t* path, size_t capacity, DWORD process_id)
        {
            wchar_t directory[path_capacity] = {};
            SYSTEMTIME time = {};
            GetSystemTime(&time);
            for (int attempt = 0; attempt != 2; ++attempt)
            {
                if (attempt == 0)
                {
                    const DWORD length = GetModuleFileNameW(nullptr, directory, static_cast<DWORD>(path_capacity));
                    if (!length || length >= path_capacity)
                        continue;
                    wchar_t* slash = wcsrchr(directory, L'\\');
                    if (!slash) continue;
                    *slash = L'\0';
                }
                else
                {
                    const DWORD length = GetTempPathW(static_cast<DWORD>(path_capacity), directory);
                    if (!length || length >= path_capacity)
                        continue;
                }
                if (wcscat_s(directory, L"\\crashes") != 0 || !create_directory(directory))
                    continue;
                swprintf_s(path, capacity, L"%s\\crash_%04u%02u%02u_%02u%02u%02u_%03u_%lu.dmp", directory,
                    time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, process_id);
                HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE)
                    return file;
            }
            return INVALID_HANDLE_VALUE;
        }
    }

    void CrashHandler::InitializeThread()
    {
        ULONG bytes = 64 * 1024;
        SetThreadStackGuarantee(&bytes);
        // MSVC keeps terminate handlers per thread.
        std::set_terminate(on_terminate);
    }

    void CrashHandler::Initialize()
    {
        GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(path_capacity));
        InitializeThread();
        SetUnhandledExceptionFilter(on_exception);
        std::signal(SIGABRT, [](int) { report_current_exception("abort() called"); });
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_purecall_handler([] { Terminate("Invalid pure virtual function call"); });
        _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
            { Terminate("Invalid C runtime function parameter"); });
    }

    void CrashHandler::SetInteractive(bool enabled)
    {
        InterlockedExchange(&interactive, enabled ? 1 : 0);
    }

    void CrashHandler::Terminate(const char* reason)
    {
        strncpy_s(termination_reason, reason ? reason : "Unknown termination reason", _TRUNCATE);
        __try
        {
            RaiseException(termination_exception, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        }
        __except (on_exception(GetExceptionInformation())) {}
        std::_Exit(EXIT_FAILURE);
    }

    int CrashHandler::RunReporterIfRequested(const char* command_line)
    {
        constexpr char prefix[] = "--spartan-crash-report ";
        if (strncmp(command_line, prefix, sizeof(prefix) - 1) != 0)
            return -1;
        DWORD process_id = 0, thread_id = 0;
        unsigned long long address = 0;
        unsigned int show_dialog = 0;
        if (sscanf_s(command_line + sizeof(prefix) - 1, "%lu %lu %llu %u", &process_id, &thread_id, &address, &show_dialog) != 4)
            return 1;

        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, process_id);
        CrashContext context = {};
        SIZE_T read = 0;
        const bool context_read = process && ReadProcessMemory(process, reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),
            &context, sizeof(context), &read) && read == sizeof(context);
        DWORD error = context_read ? ERROR_SUCCESS : GetLastError();
        wchar_t dump_path[path_capacity] = {};
        HANDLE file = create_dump_file(dump_path, path_capacity, process_id);
        bool saved = false;
        if (file != INVALID_HANDLE_VALUE && context_read)
        {
            EXCEPTION_POINTERS pointers = { &context.exception, &context.registers };
            MINIDUMP_EXCEPTION_INFORMATION info = { thread_id, &pointers, FALSE };
            const MINIDUMP_TYPE flags = static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo |
                MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpIgnoreInaccessibleMemory);
            saved = MiniDumpWriteDump(process, process_id, file, flags, &info, nullptr, nullptr) != FALSE;
            if (!saved) error = GetLastError();
        }
        else if (file == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
        }
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (process) CloseHandle(process);
        if (!saved && dump_path[0]) DeleteFileW(dump_path);

        // A companion report is readable without a debugger and identifies the matching binary/symbols.
        char report[2048];
        context.reason[sizeof(context.reason) - 1] = '\0';
        snprintf(report, sizeof(report), "Engine crash\nBuild: %s %s\nProcess: %lu\nThread: %lu\nException: 0x%08lx\nAddress: %p\nReason: %s\nDump saved: %s\nWindows error: 0x%08lx\nOpen the .dmp with the matching executable and PDB in Visual Studio or WinDbg.\n",
            __DATE__, __TIME__, process_id, thread_id, context.exception.ExceptionCode, context.exception.ExceptionAddress,
            context_read ? context.reason : "Could not read the crashing process", saved ? "yes (full memory)" : "no", error);
        bool report_saved = false;
        if (dump_path[0])
        {
            wchar_t report_path[path_capacity];
            wcscpy_s(report_path, dump_path);
            wchar_t* extension = wcsrchr(report_path, L'.');
            if (extension) wcscpy_s(extension, 5, L".txt");
            HANDLE report_file = CreateFileW(report_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (report_file != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                const DWORD length = static_cast<DWORD>(strlen(report));
                report_saved = WriteFile(report_file, report, length, &written, nullptr) && written == length;
                CloseHandle(report_file);
            }
        }
        if (show_dialog)
        {
            wchar_t message[path_capacity + 512];
            if (saved)
                swprintf_s(message, L"The engine crashed and must close.\n\nA crash dump was saved here:\n%s\n\n%s\nPlease send the crash report to the developer.",
                    dump_path, report_saved ? L"A text report was saved alongside it." : L"The companion text report could not be saved.");
            else
                swprintf_s(message, L"The engine crashed and must close.\n\nThe crash dump could not be saved (Windows error 0x%08lx).", error);
            MessageBoxW(nullptr, message, L"Engine crashed", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        }
        return saved ? 0 : 1;
    }
}
#else
namespace spartan
{
    void CrashHandler::Initialize() { std::set_terminate([] { Terminate("Unhandled C++ exception"); }); }
    void CrashHandler::InitializeThread() {}
    void CrashHandler::SetInteractive(bool) {}
    int CrashHandler::RunReporterIfRequested(const char*) { return -1; }
    void CrashHandler::Terminate(const char* reason)
    {
        // Leave fatal signals to the OS core-dump facility; no engine work is safe in a signal handler.
        fprintf(stderr, "Engine crashed: %s\n", reason ? reason : "unknown reason");
        std::abort();
    }
}
#endif
