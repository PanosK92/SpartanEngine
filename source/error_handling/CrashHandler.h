/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    class CrashHandler
    {
    public:
        // Install before engine initialization. Native dumps are currently supported on Windows.
        static void Initialize();
        static void SetInteractive(bool interactive);
        // Reserve emergency stack space on each engine-owned thread.
        static void InitializeThread();
        [[noreturn]] static void Terminate(const char* reason);

        // Internal helper mode: return -1 for a normal launch, otherwise the reporter exit code.
        static int RunReporterIfRequested(const char* command_line);
    };
}
