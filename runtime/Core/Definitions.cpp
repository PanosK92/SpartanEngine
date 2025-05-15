/*
Copyright(c) 2015-2025 Panos Karabelas

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
    // This function returns a pointer to a static buffer. It's not thread-safe,
    // but it avoids memory allocation issues.
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
                break;

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
                    callstack << module_info.ModuleName << "!";

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
