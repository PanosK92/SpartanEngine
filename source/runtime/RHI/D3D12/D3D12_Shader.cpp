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
#include "D3D12_Internal.h"
SP_WARNINGS_OFF
#include <dxc/d3d12shader.h>
SP_WARNINGS_ON
#include <mutex>
#include <unordered_map>
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::d3d12_shader
{
    // keeps each compiled shader's IDxcBlob alive for as long as the bytecode pointer is referenced
    static std::mutex g_bytecode_mutex;
    static std::unordered_map<void*, IDxcBlob*> g_bytecode_blobs;

    static void register_bytecode_blob(void* bytecode_ptr, IDxcBlob* blob)
    {
        if (!bytecode_ptr || !blob)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_bytecode_mutex);
        g_bytecode_blobs[bytecode_ptr] = blob;
    }

    void release_bytecode_blob(void* bytecode_ptr)
    {
        if (!bytecode_ptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_bytecode_mutex);
        auto it = g_bytecode_blobs.find(bytecode_ptr);
        if (it != g_bytecode_blobs.end())
        {
            it->second->Release();
            g_bytecode_blobs.erase(it);
        }
    }
}

namespace spartan
{
    void* RHI_Shader::RHI_Compile()
    {
        // arguments (and defines)
        vector<string> arguments;

        // arguments
        {
            arguments.emplace_back("-E"); arguments.emplace_back(GetEntryPoint());
            arguments.emplace_back("-T"); arguments.emplace_back(GetTargetProfile());
            arguments.emplace_back("-flegacy-macro-expansion"); // expand the operands before performing token-pasting operation (fxc behavior)
            arguments.emplace_back("-Wno-ignored-attributes");  // silence vk::image_format and similar vulkan-only attributes on the d3d12 path
            // api define so shared hlsl can pick d3d12 specific register bindings
            arguments.emplace_back("-D"); arguments.emplace_back("API_D3D12=1");
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
        if (IDxcResult* dxc_result = DirectXShaderCompiler::Compile(m_preprocessed_source, arguments))
        {
            // get compiled shader buffer, GetResult hands us an owning reference to the blob
            IDxcBlob* shader_buffer = nullptr;
            dxc_result->GetResult(&shader_buffer);

            // the dxc result is no longer needed once we hold the blob
            dxc_result->Release();

            if (!shader_buffer)
            {
                return nullptr;
            }

            void* bytecode_ptr = shader_buffer->GetBufferPointer();

            // reflect shader resources (so that descriptor sets can be created later)
            Reflect
            (
                m_shader_type,
                reinterpret_cast<uint32_t*>(bytecode_ptr),
                static_cast<uint32_t>(shader_buffer->GetBufferSize() / 4)
            );

            // create input layout
            if (m_shader_type == RHI_Shader_Type::Vertex && m_input_layout)
            {
                m_input_layout->Create(m_vertex_type);
            }

            m_object_size = shader_buffer->GetBufferSize();

            // keep the blob alive, the deletion queue releases it via release_bytecode_blob on destroy
            d3d12_shader::register_bytecode_blob(bytecode_ptr, shader_buffer);

            return bytecode_ptr;
        }

        return nullptr;
    }

    namespace
    {
        // map a single d3d12 binding desc to one rhi descriptor entry
        // mirrors the spirv reflection path so descriptor sets line up across both backends
        void emit_descriptor(
            const D3D12_SHADER_INPUT_BIND_DESC& bind_desc,
            ID3D12ShaderReflection* shader_reflection,
            const RHI_Shader_Type shader_stage,
            std::vector<RHI_Descriptor>& descriptors
        )
        {
            // skip samplers, both backends bind them bindlessly
            if (bind_desc.Type == D3D_SIT_SAMPLER)
            {
                return;
            }

            RHI_Descriptor_Type descriptor_type = RHI_Descriptor_Type::Max;
            RHI_Image_Layout layout             = RHI_Image_Layout::Max;
            uint32_t struct_size                = 0;
            uint32_t shifted_slot               = bind_desc.BindPoint;

            switch (bind_desc.Type)
            {
                case D3D_SIT_CBUFFER:
                {
                    descriptor_type = RHI_Descriptor_Type::ConstantBuffer;
                    if (shader_reflection)
                    {
                        if (ID3D12ShaderReflectionConstantBuffer* cb = shader_reflection->GetConstantBufferByName(bind_desc.Name))
                        {
                            D3D12_SHADER_BUFFER_DESC cb_desc = {};
                            if (SUCCEEDED(cb->GetDesc(&cb_desc)))
                            {
                                struct_size = cb_desc.Size;
                            }
                        }
                    }
                    shifted_slot += rhi_shader_register_shift_b;
                    break;
                }

                case D3D_SIT_TEXTURE:
                {
                    if (bind_desc.Dimension == D3D_SRV_DIMENSION_BUFFER)
                    {
                        descriptor_type = RHI_Descriptor_Type::StructuredBuffer;
                    }
                    else
                    {
                        descriptor_type = RHI_Descriptor_Type::Image;
                        layout          = RHI_Image_Layout::Shader_Read;
                    }
                    shifted_slot += rhi_shader_register_shift_t;
                    break;
                }

                case D3D_SIT_STRUCTURED:
                case D3D_SIT_BYTEADDRESS:
                {
                    descriptor_type = RHI_Descriptor_Type::StructuredBuffer;
                    shifted_slot += rhi_shader_register_shift_t;
                    break;
                }

                case D3D_SIT_UAV_RWTYPED:
                {
                    if (bind_desc.Dimension == D3D_SRV_DIMENSION_BUFFER)
                    {
                        descriptor_type = RHI_Descriptor_Type::StructuredBuffer;
                    }
                    else
                    {
                        descriptor_type = RHI_Descriptor_Type::TextureStorage;
                        layout          = RHI_Image_Layout::General;
                    }
                    shifted_slot += rhi_shader_register_shift_u;
                    break;
                }

                case D3D_SIT_UAV_RWSTRUCTURED:
                case D3D_SIT_UAV_RWBYTEADDRESS:
                case D3D_SIT_UAV_APPEND_STRUCTURED:
                case D3D_SIT_UAV_CONSUME_STRUCTURED:
                case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                {
                    descriptor_type = RHI_Descriptor_Type::StructuredBuffer;
                    shifted_slot += rhi_shader_register_shift_u;
                    break;
                }

                case D3D_SIT_RTACCELERATIONSTRUCTURE:
                {
                    descriptor_type = RHI_Descriptor_Type::AccelerationStructure;
                    shifted_slot += rhi_shader_register_shift_t;
                    break;
                }

                default:
                    return;
            }

            if (descriptor_type == RHI_Descriptor_Type::Max)
            {
                return;
            }

            // bindcount semantics, 0 means unbounded, 1 means single, >1 means fixed array
            bool is_array         = bind_desc.BindCount != 1;
            uint32_t array_length = bind_desc.BindCount;
            if (bind_desc.BindCount == 0)
            {
                array_length = rhi_max_array_size;
            }
            else if (bind_desc.BindCount == 1)
            {
                array_length = 0;
            }

            descriptors.emplace_back(
                std::string(bind_desc.Name ? bind_desc.Name : ""),
                descriptor_type,
                layout,
                shifted_slot,
                rhi_shader_type_to_mask(shader_stage),
                struct_size,
                is_array,
                array_length
            );
        }
    }

    void RHI_Shader::Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, uint32_t size)
    {
        SP_ASSERT(ptr != nullptr);
        SP_ASSERT(size != 0);

        // dxc utils, cached because creation is expensive and the interface is reentrant
        static IDxcUtils* utils = nullptr;
        if (!utils)
        {
            if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))))
            {
                SP_LOG_ERROR("Failed to create IDxcUtils for shader reflection");
                return;
            }
        }

        DxcBuffer buffer = {};
        buffer.Ptr       = ptr;
        buffer.Size      = static_cast<size_t>(size) * sizeof(uint32_t);
        buffer.Encoding  = DXC_CP_ACP;

        const bool is_library = shader_type == RHI_Shader_Type::RayGeneration ||
                                shader_type == RHI_Shader_Type::RayMiss       ||
                                shader_type == RHI_Shader_Type::RayHit;

        if (is_library)
        {
            // ray tracing shaders are compiled as libraries, walk all exported functions
            ID3D12LibraryReflection* lib_reflection = nullptr;
            if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&lib_reflection))))
            {
                SP_LOG_ERROR("Failed to create d3d12 library reflection");
                return;
            }

            D3D12_LIBRARY_DESC lib_desc = {};
            lib_reflection->GetDesc(&lib_desc);

            // collect bindings across all functions, dedupe by name+slot+type
            for (UINT f = 0; f < lib_desc.FunctionCount; ++f)
            {
                ID3D12FunctionReflection* fn = lib_reflection->GetFunctionByIndex(static_cast<INT>(f));
                if (!fn)
                {
                    continue;
                }

                D3D12_FUNCTION_DESC fn_desc = {};
                fn->GetDesc(&fn_desc);

                for (UINT i = 0; i < fn_desc.BoundResources; ++i)
                {
                    D3D12_SHADER_INPUT_BIND_DESC bind_desc = {};
                    if (FAILED(fn->GetResourceBindingDesc(i, &bind_desc)))
                    {
                        continue;
                    }

                    emit_descriptor(bind_desc, nullptr, shader_type, m_descriptors);
                }
            }

            lib_reflection->Release();
        }
        else
        {
            ID3D12ShaderReflection* shader_reflection = nullptr;
            if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&shader_reflection))))
            {
                SP_LOG_ERROR("Failed to create d3d12 shader reflection");
                return;
            }

            D3D12_SHADER_DESC shader_desc = {};
            shader_reflection->GetDesc(&shader_desc);

            for (UINT i = 0; i < shader_desc.BoundResources; ++i)
            {
                D3D12_SHADER_INPUT_BIND_DESC bind_desc = {};
                if (FAILED(shader_reflection->GetResourceBindingDesc(i, &bind_desc)))
                {
                    continue;
                }

                emit_descriptor(bind_desc, shader_reflection, shader_type, m_descriptors);
            }

            shader_reflection->Release();
        }
    }
}
