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

#pragma once

//= INCLUDES =====================
#include <string>
#include <vector>
#include "../Core/Definitions.h"
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
