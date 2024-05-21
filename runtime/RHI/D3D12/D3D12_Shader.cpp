/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ============================
#include "pch.h"
#include "../RHI_Shader.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_InputLayout.h"
#include "../RHI_DirectXShaderCompiler.h"
#include "../Rendering/Renderer.h"
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Shader::~RHI_Shader()
    {
        if (m_rhi_resource)
        {
            RHI_Device::QueueWaitAll();
            d3d12_utility::release<IDxcResult>(m_rhi_resource);
        }
    }

    void* RHI_Shader::RHI_Compile()
    {
        // arguments (and defines)
        vector<string> arguments;

        // arguments
        {
            arguments.emplace_back("-E"); arguments.emplace_back(GetEntryPoint());
            arguments.emplace_back("-T"); arguments.emplace_back(GetTargetProfile());
            arguments.emplace_back("-flegacy-macro-expansion"); // expand the operands before performing token-pasting operation (fxc behavior)
            #ifdef DEBUG                                                    
            arguments.emplace_back("-Od");                      // disable optimizations
            arguments.emplace_back("-Zi");                      // enable debug information
            #endif
        }

        // defines
        for (const auto& define : m_defines)
        {
            arguments.emplace_back("-D"); arguments.emplace_back(define.first + "=" + define.second);
        }

        // compile
        if (IDxcResult* dxc_result = DirecXShaderCompiler::Compile(m_preprocessed_source, arguments))
        {
            // Get compiled shader buffer
            IDxcBlob* shader_buffer = nullptr;
            dxc_result->GetResult(&shader_buffer);

            // reflect shader resources (so that descriptor sets can be created later)
            Reflect
            (
                m_shader_type,
                reinterpret_cast<uint32_t*>(shader_buffer->GetBufferPointer()),
                static_cast<uint32_t>(shader_buffer->GetBufferSize() / 4)
            );

            // create input layout
            if (m_shader_type == RHI_Shader_Type::Vertex)
            {
                m_input_layout->Create(m_vertex_type, nullptr);
            }

            m_object_size = shader_buffer->GetBufferSize();

            return static_cast<void*>(shader_buffer->GetBufferPointer());
        }

        return nullptr;
    }

    void RHI_Shader::Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, uint32_t size)
    {

    }
}
