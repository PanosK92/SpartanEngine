/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "Definitions.h"
#include <string>
#include <sstream>
#ifdef _WIN32
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h>
#include <cxxabi.h>
#endif
//=================================

namespace spartan
{
    // this function returns a pointer to a static buffer, it's not thread-safe
    const char* get_callstack_c_str()
    {
        static std::string callstack_str;
        std::stringstream callstack;

#ifdef _WIN32
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();

        SymInitialize(process, NULL, TRUE);
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

        CONTEXT context;
        RtlCaptureContext(&context);

        STACKFRAME64 frame = {};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        for (ULONG frame_number = 0; frame_number < 25; frame_number++)  // Limit to 25 frames
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            {
                break;
            }

            char symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
            PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbol_buffer;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;

            DWORD64 displacement = 0;
            IMAGEHLP_MODULE64 module_info;
            module_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);

            callstack << frame_number << ": ";

            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            {
                if (SymGetModuleInfo64(process, frame.AddrPC.Offset, &module_info))
                {
                    callstack << module_info.ModuleName << "!";
                }

                callstack << symbol->Name;

                IMAGEHLP_LINE64 line;
                line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                DWORD displacement_line = 0;
                if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &displacement_line, &line))
                {
                    callstack << " [" << line.FileName << ":" << line.LineNumber << "]";
                }
            }
            else
            {
                callstack << "0x" << std::hex << frame.AddrPC.Offset << std::dec;
            }
            callstack << std::endl;
        }

        SymCleanup(process);
#else
        // Unix-like systems implementation (unchanged)
        void* array[25];
        size_t size = backtrace(array, 25);
        char** strings = backtrace_symbols(array, size);

        for (size_t i = 0; i < size; i++)
        {
            callstack << i << ": " << strings[i] << std::endl;
        }

        free(strings);
#endif

        callstack_str = callstack.str();
        return callstack_str.c_str();
    }
}
