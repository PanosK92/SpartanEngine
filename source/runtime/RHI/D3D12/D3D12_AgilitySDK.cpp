/*
Copyright(c) 2016-2026 Panos Karabelas

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

// = INCLUDES =========
#include "pch.h"
//=====================

#if defined(SP_D3D12_AGILITY_SDK_VERSION)

// the d3d12 loader reads these two exports from the exe before any device is created, this opts the
// process into the agility sdk redist instead of the d3d12core.dll that shipped with the os
// D3D12SDKPath is relative to the exe and must contain d3d12core.dll of exactly D3D12SDKVersion
// spelled as unsigned int rather than UINT so this file needs no windows header, the loader only
// matches the exported symbol name and its four byte unsigned layout
extern "C"
{
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = SP_D3D12_AGILITY_SDK_VERSION;
    __declspec(dllexport) extern const char*        D3D12SDKPath    = ".\\D3D12\\";
}

namespace spartan::d3d12_agility
{
    uint32_t requested_sdk_version()
    {
        return SP_D3D12_AGILITY_SDK_VERSION;
    }
}

#else

namespace spartan::d3d12_agility
{
    uint32_t requested_sdk_version()
    {
        return 0;
    }
}

#endif
