/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#include "../RHI_Shader.h"
#include "../RHI_Device.h"
#include "../RHI_InputLayout.h"
#include "../RHI_DirectXShaderCompiler.h"
#include "Metal_Internal.h"
#include <metal_irconverter/metal_irconverter.h>
SP_WARNINGS_OFF
// the directx headers spell com interfaces with the msvc keyword, dxc's adapter doesn't define it
#define interface struct
#include <directx/d3d12shader.h>
#undef interface
SP_WARNINGS_ON
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        IRShaderStage to_ir_stage(const RHI_Shader_Type type)
        {
            switch (type)
            {
                case RHI_Shader_Type::Vertex:        return IRShaderStageVertex;
                case RHI_Shader_Type::Hull:          return IRShaderStageHull;
                case RHI_Shader_Type::Domain:        return IRShaderStageDomain;
                case RHI_Shader_Type::Pixel:         return IRShaderStageFragment;
                case RHI_Shader_Type::Compute:       return IRShaderStageCompute;
                case RHI_Shader_Type::RayGeneration: return IRShaderStageRayGeneration;
                case RHI_Shader_Type::RayMiss:       return IRShaderStageMiss;
                case RHI_Shader_Type::RayHit:        return IRShaderStageClosestHit;
                case RHI_Shader_Type::MeshShader:    return IRShaderStageMesh;
                default:                             return IRShaderStageInvalid;
            }
        }

        // d3d12 behavior the engine's shaders rely on, out of bounds reads return zero and nan/inf checks survive
        const IRCompatibilityFlags compatibility_flags = static_cast<IRCompatibilityFlags>(IRCompatibilityFlagBoundsCheck | IRCompatibilityFlagDisableNanInfOptimization);

        // compilers aren't reentrant, shaders compile on many threads, so each thread keeps its own
        IRCompiler* get_compiler()
        {
            thread_local unique_ptr<IRCompiler, decltype(&IRCompilerDestroy)> compiler(nullptr, IRCompilerDestroy);
            if (!compiler)
            {
                compiler.reset(IRCompilerCreate());
                IRCompilerSetGlobalRootSignature(compiler.get(), static_cast<IRRootSignature*>(metal_bindings::GetRootSignature()));
                IRCompilerSetMinimumDeploymentTarget(compiler.get(), IROperatingSystem_macOS, "26.0.0");
                IRCompilerSetMinimumGPUFamily(compiler.get(), IRGPUFamilyMetal3);
                IRCompilerSetCompatibilityFlags(compiler.get(), compatibility_flags);
            }
            return compiler.get();
        }

        // ray generation becomes a kernel, miss and hit shaders become visible functions it calls through a table
        IRCompiler* get_compiler_ray_tracing()
        {
            thread_local unique_ptr<IRCompiler, decltype(&IRCompilerDestroy)> compiler(nullptr, IRCompilerDestroy);
            if (!compiler)
            {
                IRRayTracingPipelineConfiguration* configuration = IRRayTracingPipelineConfigurationCreate();
                IRRayTracingPipelineConfigurationSetRayGenerationCompilationMode(configuration, IRRayGenerationCompilationKernel);
                IRRayTracingPipelineConfigurationSetIntersectionFunctionCompilationMode(configuration, IRIntersectionFunctionCompilationVisibleFunction);

                compiler.reset(IRCompilerCreate());
                IRCompilerSetGlobalRootSignature(compiler.get(), static_cast<IRRootSignature*>(metal_bindings::GetRootSignature()));
                IRCompilerSetMinimumDeploymentTarget(compiler.get(), IROperatingSystem_macOS, "26.0.0");
                IRCompilerSetMinimumGPUFamily(compiler.get(), IRGPUFamilyMetal3);
                IRCompilerSetCompatibilityFlags(compiler.get(), compatibility_flags);
                IRCompilerSetRayTracingPipelineConfiguration(compiler.get(), configuration);
                IRCompilerSetHitgroupType(compiler.get(), IRHitGroupTypeTriangles);
                IRRayTracingPipelineConfigurationDestroy(configuration);
            }
            return compiler.get();
        }

        bool is_ray_tracing(const RHI_Shader_Type type)
        {
            return type == RHI_Shader_Type::RayGeneration || type == RHI_Shader_Type::RayMiss || type == RHI_Shader_Type::RayHit;
        }

        // dxil to a metal library holding one function, plus what pipelines need to know about it
        metal_shader::Shader* convert(IDxcBlob* dxil, const RHI_Shader_Type type, const char* name, const char* entry_point, uint64_t& size)
        {
            const IRShaderStage stage = to_ir_stage(type);
            IRObject* input           = IRObjectCreateFromDXIL(static_cast<const uint8_t*>(dxil->GetBufferPointer()), dxil->GetBufferSize(), IRBytecodeOwnershipNone);
            IRError* error            = nullptr;
            IRCompiler* compiler      = is_ray_tracing(type) ? get_compiler_ray_tracing() : get_compiler();
            IRObject* output          = IRCompilerAllocCompileAndLink(compiler, is_ray_tracing(type) ? entry_point : nullptr, input, &error);
            IRObjectDestroy(input);
            if (!output)
            {
                SP_LOG_ERROR("Metal shader converter failed on %s with error code %u", name, error ? IRErrorGetCode(error) : 0);
                if (error)
                {
                    IRErrorDestroy(error);
                }
                return nullptr;
            }

            IRMetalLibBinary* binary = IRMetalLibBinaryCreate();
            IRObjectGetMetalLibBinary(output, stage, binary);
            vector<uint8_t> bytes(IRMetalLibGetBytecodeSize(binary));
            IRMetalLibGetBytecode(binary, bytes.data());
            IRMetalLibBinaryDestroy(binary);
            size = bytes.size();

            metal_shader::Shader* shader   = new metal_shader::Shader();
            IRShaderReflection* reflection = IRShaderReflectionCreate();
            IRObjectGetReflection(output, stage, reflection);

            dispatch_data_t data     = dispatch_data_create(bytes.data(), bytes.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            NS::Error* error_library = nullptr;
            shader->library          = RHI_Context::device->newLibrary(data, &error_library);
            dispatch_release(data);
            if (shader->library)
            {
                NS::String* function_name = metal_utility::ns_string(IRShaderReflectionGetEntryPointFunctionName(reflection));
                shader->function          = shader->library->newFunction(function_name);
                function_name->release();
            }

            if (type == RHI_Shader_Type::Compute)
            {
                IRVersionedCSInfo info = {};
                if (IRShaderReflectionCopyComputeInfo(reflection, IRReflectionVersion_1_0, &info))
                {
                    shader->threadgroup_size = MTL::Size(info.info_1_0.tg_size[0], info.info_1_0.tg_size[1], info.info_1_0.tg_size[2]);
                    IRShaderReflectionReleaseComputeInfo(&info);
                }
            }
            else if (type == RHI_Shader_Type::MeshShader)
            {
                IRVersionedMSInfo info = {};
                if (IRShaderReflectionCopyMeshInfo(reflection, IRReflectionVersion_1_0, &info))
                {
                    shader->threadgroup_size = MTL::Size(info.info_1_0.num_threads[0], info.info_1_0.num_threads[1], info.info_1_0.num_threads[2]);
                    IRShaderReflectionReleaseMeshInfo(&info);
                }
            }
            else if (type == RHI_Shader_Type::Vertex)
            {
                IRVersionedVSInfo info = {};
                if (IRShaderReflectionCopyVertexInfo(reflection, IRReflectionVersion_1_0, &info))
                {
                    for (size_t i = 0; i < info.info_1_0.num_vertex_inputs; i++)
                    {
                        shader->vertex_inputs.push_back({ info.info_1_0.vertex_inputs[i].name, info.info_1_0.vertex_inputs[i].attributeIndex });
                    }
                    IRShaderReflectionReleaseVertexInfo(&info);
                }
            }

            IRShaderReflectionDestroy(reflection);
            IRObjectDestroy(output);

            if (!shader->function)
            {
                SP_LOG_ERROR("Failed to create a metal function for %s: %s", name, error_library ? error_library->localizedDescription()->utf8String() : "missing entry point");
                metal_shader::Destroy(shader);
                return nullptr;
            }

            metal_utility::set_label(shader->library, name);
            metal_utility::set_label(shader->function, name);
            return shader;
        }
    }

    void metal_shader::Destroy(void* resource)
    {
        Shader* shader = static_cast<Shader*>(resource);
        if (shader->function)
        {
            shader->function->release();
        }

        if (shader->library)
        {
            shader->library->release();
        }

        delete shader;
    }

    void* RHI_Shader::RHI_Compile()
    {
        // the same arguments as d3d12, converted shaders bind through the d3d12 root signature
        vector<string> arguments;
        {
            arguments.emplace_back("-E");
            arguments.emplace_back(GetEntryPoint());
            arguments.emplace_back("-T");
            arguments.emplace_back(GetTargetProfile());
            arguments.emplace_back("-flegacy-macro-expansion");
            arguments.emplace_back("-Wno-ignored-attributes");
            arguments.emplace_back("-D");
            arguments.emplace_back("API_D3D12=1");
            arguments.emplace_back("-D");
            arguments.emplace_back("API_METAL=1");
            // the converter has no view instancing and metal has no xr, so the view id becomes a vertex input that reads 0
            arguments.emplace_back("-D");
            arguments.emplace_back("SV_ViewID=SP_VIEW_ID");
            if (RHI_Device::IsSupportedShaderBaseInstance())
            {
                arguments.emplace_back("-D");
                arguments.emplace_back("SP_BASE_INSTANCE=1");
            }
            if (m_shader_type == RHI_Shader_Type::Compute)
            {
                arguments.emplace_back("-D");
                arguments.emplace_back("SP_SHADER_STAGE_COMPUTE=1");
            }
            if (m_shader_type == RHI_Shader_Type::MeshShader)
            {
                arguments.emplace_back("-D");
                arguments.emplace_back("SP_SHADER_STAGE_MESH=1");
            }
            for (const auto& [define, value] : m_defines)
            {
                arguments.emplace_back("-D");
                arguments.emplace_back(define + "=" + value);
            }
        }

        IDxcBlob* dxil = DirectXShaderCompiler::Compile(m_preprocessed_source, arguments, m_object_name);
        if (!dxil)
            return nullptr;

        Reflect(m_shader_type, static_cast<uint32_t*>(dxil->GetBufferPointer()), static_cast<uint32_t>(dxil->GetBufferSize() / 4));
        metal_shader::Shader* shader = convert(dxil, m_shader_type, m_object_name.c_str(), GetEntryPoint(), m_object_size);
        dxil->Release();
        if (!shader)
            return nullptr;

        if (m_shader_type == RHI_Shader_Type::Vertex && m_input_layout)
        {
            m_input_layout->Create(m_vertex_type);
        }

        return shader;
    }

    namespace
    {
        // one dxil binding as an rhi descriptor, the same mapping d3d12 makes, see D3D12_Shader.cpp
        void emit_descriptor(const D3D12_SHADER_INPUT_BIND_DESC& bind, ID3D12ShaderReflection* reflection, const RHI_Shader_Type stage, vector<RHI_Descriptor>& descriptors)
        {
            RHI_Descriptor_Type type = RHI_Descriptor_Type::Max;
            RHI_Image_Layout layout  = RHI_Image_Layout::Max;
            uint32_t struct_size     = 0;
            uint32_t slot            = bind.BindPoint;
            const bool is_buffer     = bind.Dimension == D3D_SRV_DIMENSION_BUFFER;

            switch (bind.Type)
            {
                case D3D_SIT_CBUFFER:
                {
                    type = RHI_Descriptor_Type::ConstantBuffer;
                    if (reflection)
                    {
                        D3D12_SHADER_BUFFER_DESC desc = {};
                        if (SUCCEEDED(reflection->GetConstantBufferByName(bind.Name)->GetDesc(&desc)))
                        {
                            struct_size = desc.Size;
                        }
                    }
                    slot += rhi_shader_register_shift_b;
                    break;
                }
                case D3D_SIT_TEXTURE:
                    type    = is_buffer ? RHI_Descriptor_Type::StructuredBuffer : RHI_Descriptor_Type::Image;
                    layout  = is_buffer ? RHI_Image_Layout::Max : RHI_Image_Layout::General;
                    slot   += rhi_shader_register_shift_t;
                    break;
                case D3D_SIT_STRUCTURED:
                case D3D_SIT_BYTEADDRESS:
                    type  = RHI_Descriptor_Type::StructuredBuffer;
                    slot += rhi_shader_register_shift_t;
                    break;
                case D3D_SIT_UAV_RWTYPED:
                    type    = is_buffer ? RHI_Descriptor_Type::StructuredBuffer : RHI_Descriptor_Type::TextureStorage;
                    layout  = is_buffer ? RHI_Image_Layout::Max : RHI_Image_Layout::General;
                    slot   += rhi_shader_register_shift_u;
                    break;
                case D3D_SIT_UAV_RWSTRUCTURED:
                case D3D_SIT_UAV_RWBYTEADDRESS:
                case D3D_SIT_UAV_APPEND_STRUCTURED:
                case D3D_SIT_UAV_CONSUME_STRUCTURED:
                case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                    type  = RHI_Descriptor_Type::StructuredBuffer;
                    slot += rhi_shader_register_shift_u;
                    break;
                case D3D_SIT_RTACCELERATIONSTRUCTURE:
                    type  = RHI_Descriptor_Type::AccelerationStructure;
                    slot += rhi_shader_register_shift_t;
                    break;
                default: // samplers are bindless
                    return;
            }

            // a bind count of 0 is an unbounded array, 1 a single resource
            const bool is_array         = bind.BindCount != 1;
            const uint32_t array_length = bind.BindCount == 0 ? rhi_max_array_size : (bind.BindCount == 1 ? 0 : bind.BindCount);
            descriptors.emplace_back(string(bind.Name ? bind.Name : ""), type, layout, slot, rhi_shader_type_to_mask(stage), struct_size, is_array, array_length, bind.Space);
        }
    }

    void RHI_Shader::Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, uint32_t size)
    {
        SP_ASSERT(ptr != nullptr);
        SP_ASSERT(size != 0);

        static IDxcUtils* utils = nullptr;
        static once_flag utils_flag;
        call_once(utils_flag, []()
        {
            DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
        });
        SP_ASSERT_MSG(utils != nullptr, "Failed to create dxc utils for reflection");

        DxcBuffer buffer = { ptr, static_cast<size_t>(size) * sizeof(uint32_t), DXC_CP_ACP };

        // ray tracing shaders are libraries, every exported function contributes bindings
        if (shader_type == RHI_Shader_Type::RayGeneration || shader_type == RHI_Shader_Type::RayMiss || shader_type == RHI_Shader_Type::RayHit)
        {
            ID3D12LibraryReflection* library = nullptr;
            if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&library))))
            {
                SP_LOG_ERROR("Failed to reflect %s", m_object_name.c_str());
                return;
            }

            D3D12_LIBRARY_DESC library_desc = {};
            library->GetDesc(&library_desc);
            for (UINT function_index = 0; function_index < library_desc.FunctionCount; function_index++)
            {
                ID3D12FunctionReflection* function = library->GetFunctionByIndex(static_cast<INT>(function_index));
                D3D12_FUNCTION_DESC function_desc  = {};
                function->GetDesc(&function_desc);
                for (UINT i = 0; i < function_desc.BoundResources; i++)
                {
                    D3D12_SHADER_INPUT_BIND_DESC bind = {};
                    if (SUCCEEDED(function->GetResourceBindingDesc(i, &bind)))
                    {
                        emit_descriptor(bind, nullptr, shader_type, m_descriptors);
                    }
                }
            }
            library->Release();
            return;
        }

        ID3D12ShaderReflection* reflection = nullptr;
        if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection))))
        {
            SP_LOG_ERROR("Failed to reflect %s", m_object_name.c_str());
            return;
        }

        D3D12_SHADER_DESC desc = {};
        reflection->GetDesc(&desc);
        for (UINT i = 0; i < desc.BoundResources; i++)
        {
            D3D12_SHADER_INPUT_BIND_DESC bind = {};
            if (SUCCEEDED(reflection->GetResourceBindingDesc(i, &bind)))
            {
                emit_descriptor(bind, reflection, shader_type, m_descriptors);
            }
        }
        reflection->Release();
    }
}
