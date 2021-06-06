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

//= INCLUDES =====================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include <d3dcompiler.h>
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Shader::~RHI_Shader()
    {
        d3d11_utility::release(*reinterpret_cast<ID3D11VertexShader**>(&m_resource));
    }

    void* RHI_Shader::Compile3()
    {
        SP_ASSERT(m_rhi_device != nullptr);
        ID3D11Device5* d3d11_device = m_rhi_device->GetContextRhi()->device;
        SP_ASSERT(d3d11_device != nullptr);

        // Compile flags
        uint32_t compile_flags = 0;
        #ifdef DEBUG
        compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_PREFER_FLOW_CONTROL;
        #elif NDEBUG
        compile_flags |= D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
        #endif

        // Defines
        vector<D3D_SHADER_MACRO> defines =
        {
            D3D_SHADER_MACRO{ "VS", m_shader_type == RHI_Shader_Vertex   ? "1" : "0" },
            D3D_SHADER_MACRO{ "PS", m_shader_type == RHI_Shader_Pixel    ? "1" : "0" },
            D3D_SHADER_MACRO{ "CS", m_shader_type == RHI_Shader_Compute  ? "1" : "0" }
        };
        for (const auto& define : m_defines)
        {
            defines.emplace_back(D3D_SHADER_MACRO{ define.first.c_str(), define.second.c_str() });
        }
        defines.emplace_back(D3D_SHADER_MACRO{ nullptr, nullptr });

        // Compile
        ID3DBlob* blob_error    = nullptr;
        ID3DBlob* shader_blob   = nullptr;
        HRESULT result = D3DCompile
        (
            m_source.c_str(),
            static_cast<SIZE_T>(m_source.size()),
            nullptr,
            defines.data(),
            nullptr,
            GetEntryPoint(),
            GetTargetProfile(),
            compile_flags,
            0,
            &shader_blob,
            &blob_error
        );

        // Log any compilation possible warnings and/or errors
        if (blob_error)
        {
            stringstream ss(static_cast<char*>(blob_error->GetBufferPointer()));
            string line;
            while (getline(ss, line, '\n'))
            {
                const auto is_error = line.find("error") != string::npos;
                if (is_error)
                {
                    LOG_ERROR(m_object_name + "(" + FileSystem::GetStringAfterExpression(line, "("));
                }
                else
                {
                    LOG_WARNING(m_object_name + "(" + FileSystem::GetStringAfterExpression(line, "("));
                }
            }

            d3d11_utility::release(blob_error);
        }

        // Log compilation failure
        if (FAILED(result) || !shader_blob)
        {
            LOG_ERROR("An error occurred when trying to load and compile \"%s\"", m_object_name.c_str());
        }

        // Create shader
        void* shader_view = nullptr;
        if (shader_blob)
        {
            if (m_shader_type == RHI_Shader_Vertex)
            {
                if (FAILED(d3d11_device->CreateVertexShader(shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr, reinterpret_cast<ID3D11VertexShader**>(&shader_view))))
                {
                    LOG_ERROR("Failed to create vertex shader, %s", d3d11_utility::dxgi_error_to_string(result));
                }

                // Create input layout
                if (!m_input_layout->Create(m_vertex_type, shader_blob))
                {
                    LOG_ERROR("Failed to create input layout for %s", FileSystem::GetFileNameFromFilePath(m_object_name).c_str());
                }
            }
            else if (m_shader_type == RHI_Shader_Pixel)
            {
                if (FAILED(d3d11_device->CreatePixelShader(shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr, reinterpret_cast<ID3D11PixelShader**>(&shader_view))))
                {
                    LOG_ERROR("Failed to create pixel shader, %s", d3d11_utility::dxgi_error_to_string(result));
                }
            }
            else if (m_shader_type == RHI_Shader_Compute)
            {
                if (FAILED(d3d11_device->CreateComputeShader(shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr, reinterpret_cast<ID3D11ComputeShader**>(&shader_view))))
                {
                    LOG_ERROR("Failed to create compute shader, %s", d3d11_utility::dxgi_error_to_string(result));
                }
            }

            d3d11_utility::release(shader_blob);
        }

        return shader_view;
    }

    void RHI_Shader::Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, uint32_t size)
    {

    }
}
