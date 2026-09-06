/*
Copyright(c) 2015-2026 Panos Karabelas

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

// Headless adapters only: production heightfield/placement/geometry code is compiled unchanged.
#include "pch.h"
#include "ThreadPool.h"
#include "rhi/RHI_Texture.h"

namespace spartan
{
    void Log::SetLogToFile(bool) {}
    void Log::FormatBuffer(char* buffer, const char*, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, SP_LOG_BUFFER_SIZE, format, args);
        va_end(args);
    }
    void Log::WriteBuffer(const char* text, LogType) { fprintf(stderr, "%s\n", text); }
    const char* get_callstack_c_str() { return "headless terrain regression"; }
    void ThreadPool::ParallelLoop(std::function<void(uint32_t, uint32_t)>&& loop, uint32_t count) { loop(0, count); }
    RHI_Texture_Mip* RHI_Texture::GetMip(uint32_t, uint32_t) { return nullptr; }
}
