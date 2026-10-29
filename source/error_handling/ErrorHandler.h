/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include <cstdint>

namespace spartan
{
    enum class ErrorCategory
    {
        GraphicsMemory,
        SystemMemory,
        DeviceLost,
        GraphicsApi,
        Internal
    };

    class ErrorHandler
    {
    public:
        // Call only after any supported fallback has failed. Never resumes with invalid resources.
        [[noreturn]] static void Fatal(ErrorCategory category, const char* operation, const char* detail,
            const char* resource = nullptr, uint64_t requested_bytes = 0);
    };
}
