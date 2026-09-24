/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include <string>
#include <vector>
#include "../core/Definitions.h"
// dxcapi.h assumes the com base types (IUnknown, HRESULT, REFCLSID) are already declared
#include <unknwn.h>
SP_WARNINGS_OFF
#include <dxc/dxcapi.h>
SP_WARNINGS_ON
//================================

namespace spartan
{
    // hlsl compiler, dxil for d3d12 and spir-v for vulkan, the arguments are built in D3D12_Shader.cpp and Vulkan_Shader.cpp
    class DirectXShaderCompiler
    {
    public:
        // null on failure, the caller owns the result and must release it
        static IDxcResult* Compile(const std::string& source, std::vector<std::string>& arguments);
    };
}
