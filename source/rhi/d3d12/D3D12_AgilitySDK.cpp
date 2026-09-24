/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// = INCLUDES ==
#include "pch.h"
//==============

#if defined(SP_D3D12_AGILITY_SDK_VERSION)

// the d3d12 loader reads these two exports from the exe before any device is created, this opts the
// process into the agility sdk redist instead of the d3d12core.dll that shipped with the os
// D3D12SDKPath is relative to the exe and must contain d3d12core.dll of exactly D3D12SDKVersion
// spelled as unsigned int rather than UINT so this file needs no windows header, the loader only
// matches the exported symbol name and its four byte unsigned layout
extern "C"
{
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = SP_D3D12_AGILITY_SDK_VERSION;
    __declspec(dllexport) extern const char*        D3D12SDKPath    = ".\\";
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
