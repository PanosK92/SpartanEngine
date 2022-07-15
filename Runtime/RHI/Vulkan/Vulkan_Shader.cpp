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
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include "../RHI_DirectXShaderCompiler.h"
SP_WARNINGS_OFF
#include <spirv_cross/spirv_hlsl.hpp>
SP_WARNINGS_ON
//=======================================

//= NAMESPACES =======================
using namespace std;
using namespace SPIRV_CROSS_NAMESPACE;
//====================================

namespace Spartan
{
    RHI_Shader::~RHI_Shader()
    {
        if (m_rhi_resource)
        {
            // Wait in case it's still in use by the GPU
            m_rhi_device->QueueWaitAll();

            vkDestroyShaderModule(m_rhi_device->GetContextRhi()->device, static_cast<VkShaderModule>(m_rhi_resource), nullptr);
            m_rhi_resource = nullptr;
        }
    }

    void* RHI_Shader::GetRhiResource() const
    {
        return m_rhi_resource;
    }

    void* RHI_Shader::Compile2()
    {
        // Arguments (and defines)
        vector<string> arguments;

        // Arguments
        {
            // This would be nice but if enabled, it forces you to use extension SPV_GOOGLE_user_type, which I can't seem to enable.
            // Search for "-fspv-reflect" here: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#hlsl-types
            // arguments.emplace_back("-fspv-reflect"); // Emit additional SPIR-V instructions to aid reflection

            arguments.emplace_back("-E"); arguments.emplace_back(GetEntryPoint());
            arguments.emplace_back("-T"); arguments.emplace_back(GetTargetProfile());
            arguments.emplace_back("-spirv");                                                                                                      // Generate SPIR-V code
            arguments.emplace_back("-fspv-target-env=vulkan1.1");                                                                                  // Specify the target environment: vulkan1.0 (default) or vulkan1.1
            arguments.emplace_back("-fvk-b-shift"); arguments.emplace_back(to_string(rhi_shader_shift_register_b)); arguments.emplace_back("all"); // Specify Vulkan binding number shift for b-type (buffer) register
            arguments.emplace_back("-fvk-t-shift"); arguments.emplace_back(to_string(rhi_shader_shift_register_t)); arguments.emplace_back("all"); // Specify Vulkan binding number shift for t-type (texture) register
            arguments.emplace_back("-fvk-s-shift"); arguments.emplace_back(to_string(rhi_shader_shift_register_s)); arguments.emplace_back("all"); // Specify Vulkan binding number shift for s-type (sampler) register
            arguments.emplace_back("-fvk-u-shift"); arguments.emplace_back(to_string(rhi_shader_shift_register_u)); arguments.emplace_back("all"); // Specify Vulkan binding number shift for u-type (read/write buffer) register
            arguments.emplace_back("-fvk-use-dx-position-w");                                                                                      // Reciprocate SV_Position.w after reading from stage input in PS to accommodate the difference between Vulkan and DirectX
            arguments.emplace_back("-fvk-use-dx-layout");                                                                                          // Use DirectX memory layout for Vulkan resources
            arguments.emplace_back("-flegacy-macro-expansion");                                                                                    // Expand the operands before performing token-pasting operation (fxc behavior)
            #ifdef DEBUG
            arguments.emplace_back("-Od");                                                                                                         // Disable optimizations
            arguments.emplace_back("-Zi");                                                                                                         // Enable debug information
            arguments.emplace_back("-Qembed_debug");                                                                                               // Embed PDB in shader container (must be used with /Zi)
            #endif

            // Negate SV_Position.y before writing to stage output in VS/DS/GS to accommodate Vulkan's coordinate system
            if (m_shader_type == RHI_Shader_Vertex)
            {
                arguments.emplace_back("-fvk-invert-y");
            }
        }

        // Defines
        {
            // Add standard defines
            arguments.emplace_back("-D"); arguments.emplace_back("VS="+ to_string(static_cast<uint8_t>(m_shader_type == RHI_Shader_Vertex)));
            arguments.emplace_back("-D"); arguments.emplace_back("PS="+ to_string(static_cast<uint8_t>(m_shader_type == RHI_Shader_Pixel)));
            arguments.emplace_back("-D"); arguments.emplace_back("CS="+ to_string(static_cast<uint8_t>(m_shader_type == RHI_Shader_Compute)));

            // Add the rest of the defines
            for (const auto& define : m_defines)
            {
                arguments.emplace_back("-D"); arguments.emplace_back(define.first + "=" + define.second);
            }
        }

        // Compile
        if (IDxcResult* dxc_result = DirecXShaderCompiler::Get().Compile(m_source, arguments))
        {
            // Get compiled shader buffer
            IDxcBlob* shader_buffer = nullptr;
            dxc_result->GetResult(&shader_buffer);

            // Create shader module
            VkShaderModule shader_module         = nullptr;
            VkShaderModuleCreateInfo create_info = {};
            create_info.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            create_info.codeSize                 = static_cast<size_t>(shader_buffer->GetBufferSize());
            create_info.pCode                    = reinterpret_cast<const uint32_t*>(shader_buffer->GetBufferPointer());

            if (!vulkan_utility::error::check(vkCreateShaderModule(m_rhi_device->GetContextRhi()->device, &create_info, nullptr, &shader_module)))
            {
                LOG_ERROR("Failed to create shader module.");
                shader_module = nullptr;
            }

            // Reflect shader resources (so that descriptor sets can be created later)
            Reflect
            (
                m_shader_type,
                reinterpret_cast<uint32_t*>(shader_buffer->GetBufferPointer()),
                static_cast<uint32_t>(shader_buffer->GetBufferSize() / 4)
            );
            
            // Create input layout
            if (!m_input_layout->Create(m_vertex_type, nullptr))
            {
                LOG_ERROR("Failed to create input layout for %s", m_object_name.c_str());
                return nullptr;
            }

            // Release
            dxc_result->Release();

            return static_cast<void*>(shader_module);
        }

        return nullptr;
    }

    void RHI_Shader::Reflect(const RHI_Shader_Type shader_type, const uint32_t* ptr, const uint32_t size)
    {
        SP_ASSERT(ptr != nullptr);
        SP_ASSERT(size != 0);
        
        // Initialize compiler with SPIR-V data
        const CompilerHLSL compiler = CompilerHLSL(ptr, size);

        // The SPIR-V is now parsed, and we can perform reflection on it
        ShaderResources resources = compiler.get_shader_resources();

        // Pre-allocate enough memory for the descriptor vector
        uint32_t count = static_cast<uint32_t>
        (
            resources.storage_images.size()  +
            resources.storage_buffers.size() +
            resources.uniform_buffers.size() +
            resources.separate_images.size() +
            resources.separate_samplers.size()
        );

        m_descriptors.reserve(count);

        // Get storage images
        for (const Resource& resource : resources.storage_images)
        {
            m_descriptors.emplace_back
            (
                resource.name,                                                // name
                RHI_Descriptor_Type::TextureStorage,                          // type
                RHI_Image_Layout::General,                                    // layout
                compiler.get_decoration(resource.id, spv::DecorationBinding), // slot
                compiler.get_type(resource.type_id).array[0],                 // array size
                shader_type                                                   // stage
            );
        }

        // Get storage buffers
        for (const Resource& resource : resources.storage_buffers)
        {
            m_descriptors.emplace_back
            (
                resource.name,                                                // name
                RHI_Descriptor_Type::StructuredBuffer,                        // type
                RHI_Image_Layout::Undefined,                                  // layout
                compiler.get_decoration(resource.id, spv::DecorationBinding), // slot
                compiler.get_type(resource.type_id).array[0],                 // array size
                shader_type                                                   // stage
            );
        }

        // Get constant buffers
        for (const Resource& resource : resources.uniform_buffers)
        {
            m_descriptors.emplace_back
            (
                resource.name,                                                // name
                RHI_Descriptor_Type::ConstantBuffer,                          // type
                RHI_Image_Layout::Undefined,                                  // layout
                compiler.get_decoration(resource.id, spv::DecorationBinding), // slot
                compiler.get_type(resource.type_id).array[0],                 // array size
                shader_type                                                   // stage
            );
        }

        // Get textures
        for (const Resource& resource : resources.separate_images)
        {
            m_descriptors.emplace_back
            (
                resource.name,                                                // name
                RHI_Descriptor_Type::Texture,                                 // type
                RHI_Image_Layout::Shader_Read_Only_Optimal,                   // layout
                compiler.get_decoration(resource.id, spv::DecorationBinding), // slot
                compiler.get_type(resource.type_id).array[0],                 // array size
                shader_type                                                   // stage
            );
        }

        // Get samplers
        for (const Resource& resource : resources.separate_samplers)
        {
            m_descriptors.emplace_back
            (
                resource.name,                                                // name
                RHI_Descriptor_Type::Sampler,                                 // type
                RHI_Image_Layout::Undefined,                                  // layout
                compiler.get_decoration(resource.id, spv::DecorationBinding), // slot
                compiler.get_type(resource.type_id).array[0],                 // array size
                shader_type                                                   // stage
            );
        }
    }

    const char* RHI_Shader::GetTargetProfile() const
    {
        if (m_shader_type == RHI_Shader_Vertex)  return "vs_6_7";
        if (m_shader_type == RHI_Shader_Pixel)   return "ps_6_7";
        if (m_shader_type == RHI_Shader_Compute) return "cs_6_7";

        return nullptr;
    }
}
