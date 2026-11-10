/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// libc pieces emscripten declares but doesn't implement, only the browser build compiles this
#include <emscripten/heap.h>
#include <sys/sysinfo.h>
#include <cstring>

extern "C" int sysinfo(struct sysinfo* info)
{
    if (!info)
    {
        return -1;
    }

    // the wasm heap is all the memory there is
    memset(info, 0, sizeof(*info));
    info->totalram = static_cast<unsigned long>(emscripten_get_heap_max());
    info->freeram  = static_cast<unsigned long>(emscripten_get_heap_max() - emscripten_get_heap_size());
    info->mem_unit = 1;
    info->procs    = 1;
    return 0;
}
