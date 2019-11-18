/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ======================
#include "RHI_Shader.h"
#include "RHI_InputLayout.h"
#include "../Core/Context.h"
#include "../Threading/Threading.h"
#include "../Core/FileSystem.h"
#pragma warning(push, 0) // Hide warnings belonging SPIRV-Cross 
#include <spirv_hlsl.hpp>
#pragma warning(pop)
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_Shader::RHI_Shader(const shared_ptr<RHI_Device>& rhi_device)
	{
		m_rhi_device	= rhi_device;
		m_input_layout	= make_shared<RHI_InputLayout>(rhi_device);
	}

	template <typename T>
	void RHI_Shader::Compile(const Shader_Type type, const string& shader)
	{
        m_shader_type = type;

        // Can also be the source
        bool is_file = FileSystem::IsFile(shader);

		// Deduce name and file path
		if (is_file)
		{
			m_name      = FileSystem::GetFileNameFromFilePath(shader);
			m_file_path = shader;
		}
		else
		{
			m_name.clear();
			m_file_path.clear();
		}

		// Compile
        m_compilation_state = Shader_Compilation_Compiling;
        m_resource          = _Compile<T>(type, shader);
        m_compilation_state = m_resource ? Shader_Compilation_Succeeded : Shader_Compilation_Failed;

		// Log compilation result
        {
            string type_str = "unknown";
            type_str        = type == Shader_Vertex     ? "vertex"   : type_str;
            type_str        = type == Shader_Pixel      ? "pixel"    : type_str;
            type_str        = type == Shader_Compute    ? "compute"  : type_str;

            string defines;
            for (const auto& define : m_defines)
            {
                if (!defines.empty())
                    defines += ", ";

                defines += define.first + " = " + define.second;
            }

            if (m_compilation_state == Shader_Compilation_Succeeded)
            {
                if (defines.empty())
                {
                    LOG_INFO("Successfully compiled %s shader from \"%s\"", type_str.c_str(), shader.c_str());
                }
                else
                {
                    LOG_INFO("Successfully compiled %s shader from \"%s\" with definitions \"%s\"", type_str.c_str(), shader.c_str(), defines.c_str());
                }
            }
            else if (m_compilation_state == Shader_Compilation_Failed)
            {
                if (defines.empty())
                {
                    LOG_ERROR("Failed to compile %s shader from \"%s\"", type_str.c_str(), shader.c_str());
                }
                else
                {
                    LOG_ERROR("Failed to compile %s shader from \"%s\" with definitions \"%s\"", type_str.c_str(), shader.c_str(), defines.c_str());
                }
            }
        }
	}

	template <typename T>
	void RHI_Shader::CompileAsync(Context* context, const Shader_Type type, const string& shader)
	{
		context->GetSubsystem<Threading>()->AddTask([this, type, shader]()
		{
			Compile<T>(type, shader);
		});
	}

    const char* RHI_Shader::GetEntryPoint() const
    {
        static const char* entry_point_empty = nullptr;

        static const char* entry_point_vs = "mainVS";
        static const char* entry_point_ps = "mainPS";
        static const char* entry_point_cs = "mainCS";

        if (m_shader_type == Shader_Vertex)     return entry_point_vs;
        if (m_shader_type == Shader_Pixel)      return entry_point_ps;
        if (m_shader_type == Shader_Compute)    return entry_point_cs;

        return entry_point_empty;
    }

    const char* RHI_Shader::GetTargetProfile() const
    {
        static const char* target_profile_empty = nullptr;

        #if defined(API_GRAPHICS_D3D11)
        static const char* target_profile_vs = "vs_5_0";
        static const char* target_profile_ps = "ps_5_0";
        static const char* target_profile_cs = "cs_5_0";
        #elif defined(API_GRAPHICS_D3D12)
        static const char* target_profile_vs = "vs_6_0";
        static const char* target_profile_ps = "ps_6_0";
        static const char* target_profile_cs = "cs_6_0";
        #elif defined(API_GRAPHICS_VULKAN)
        static const char* target_profile_vs = "vs_6_0";
        static const char* target_profile_ps = "ps_6_0";
        static const char* target_profile_cs = "cs_6_0";
        #endif

        if (m_shader_type == Shader_Vertex)     return target_profile_vs;
        if (m_shader_type == Shader_Pixel)      return target_profile_ps;
        if (m_shader_type == Shader_Compute)    return target_profile_cs;

        return target_profile_empty;
    }

    const char* RHI_Shader::GetShaderModel() const
    {
        #if defined(API_GRAPHICS_D3D11)
        static const char* shader_model = "5_0";
        #elif defined(API_GRAPHICS_D3D12)
        static const char* shader_model = "6_0";
        #elif defined(API_GRAPHICS_VULKAN)
        static const char* shader_model = "6_0";
        #endif

        return shader_model;
    }

    void RHI_Shader::_Reflect(const Shader_Type shader_type, const uint32_t* ptr, const uint32_t size)
	{
		// Initialize compiler with SPIR-V data
		const auto compiler = spirv_cross::CompilerHLSL(ptr, size);

		// The SPIR-V is now parsed, and we can perform reflection on it
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		// Get samplers
		for (const auto& resource : resources.separate_samplers)
		{
            m_descriptors.emplace_back
            (
                RHI_Descriptor_Type::RHI_Descriptor_Sampler,                        // Type
                compiler.get_decoration(resource.id, spv::DecorationBinding),   // Slot
                shader_type                                                     // Stage
            );
		}

		// Get textures
		for (const auto& resource : resources.separate_images)
		{
            m_descriptors.emplace_back
            (
                RHI_Descriptor_Type::RHI_Descriptor_Texture,                        // Type
                compiler.get_decoration(resource.id, spv::DecorationBinding),   // Slot
                shader_type                                                     // Stage
            );
		}

		// Get constant buffers
		for (const auto& resource : resources.uniform_buffers)
		{
            m_descriptors.emplace_back
            (
                RHI_Descriptor_Type::RHI_Descriptor_ConstantBuffer,                 // Type
                compiler.get_decoration(resource.id, spv::DecorationBinding),   // Slot
                shader_type                                                     // Stage
            );
		}
	}
}
