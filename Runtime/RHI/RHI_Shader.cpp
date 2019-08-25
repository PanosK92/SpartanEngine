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

//= INCLUDES ========================
#include "RHI_Shader.h"
#include "RHI_InputLayout.h"
#include <spirv_hlsl.hpp>
#include "../Core/Context.h"
#include "../Threading/Threading.h"
#include "../FileSystem/FileSystem.h"
//===================================

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

		// Deduce name and file path
		if (!FileSystem::IsDirectory(shader))
		{
			m_name = FileSystem::GetFileNameFromFilePath(shader);
			m_file_path = shader;
		}
		else
		{
			m_name.clear();
			m_file_path.clear();
		}

		// Compile
		if (type == Shader_Vertex)
		{
			m_compilation_state = Shader_Compiling;
			m_resource_vertex	= _Compile<T>(type, shader);
			m_compilation_state = m_resource_vertex ? Shader_Compiled : Shader_Failed;
		}
		else if (type == Shader_Pixel)
		{
			m_compilation_state = Shader_Compiling;
			m_resource_pixel    = _Compile(type, shader);
			m_compilation_state = m_resource_pixel ? Shader_Compiled : Shader_Failed;
		}
        else if (type == Shader_Compute)
        {
            m_compilation_state = Shader_Compiling;
            m_resource_compute  = _Compile(type, shader);
            m_compilation_state = m_resource_compute ? Shader_Compiled : Shader_Failed;
        }
		else if (type == Shader_VertexPixel)
		{
			m_compilation_state = Shader_Compiling;
            m_shader_type       = Shader_Vertex; // temp switch
			m_resource_vertex	= _Compile<T>(Shader_Vertex, shader);
            m_shader_type       = Shader_Pixel; // temp switch
			m_resource_pixel	= _Compile(Shader_Pixel, shader);
            m_shader_type       = Shader_VertexPixel; // revert to original
			m_compilation_state = (m_resource_vertex && m_resource_pixel) ? Shader_Compiled : Shader_Failed;
		}

		// Log compilation result
        {
            string shader_type;
            shader_type = type == Shader_Vertex         ? "vertex shader"           : shader_type;
            shader_type = type == Shader_Pixel          ? "pixel shader"            : shader_type;
            shader_type = type == Shader_Compute        ? "compute shader"          : shader_type;
            shader_type = type == Shader_VertexPixel    ? "vertex and pixel shader" : shader_type;

            if (m_compilation_state == Shader_Compiled)
            {
                LOGF_INFO("Successfully compiled %s from \"%s\"", shader_type.c_str(), shader.c_str());
            }
            else if (m_compilation_state == Shader_Failed)
            {
                LOGF_ERROR("Failed to compile %s from \"%s\"", shader_type.c_str(), shader.c_str());
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

    string RHI_Shader::GetEntryPoint() const
    {
        if (m_shader_type == Shader_Vertex)     return "mainVS";
        if (m_shader_type == Shader_Pixel)      return "mainPS";
        if (m_shader_type == Shader_Compute)    return "mainCS";

        return "";
    }

    string RHI_Shader::GetTargetProfile() const
    {
        if (m_shader_type == Shader_Vertex)     return "vs_" + GetShaderModel();
        if (m_shader_type == Shader_Pixel)      return "ps_" + GetShaderModel();
        if (m_shader_type == Shader_Compute)    return "cs_" + GetShaderModel();

        return "";
    }

    const std::string& RHI_Shader::GetShaderModel() const
    {
        #if defined(API_GRAPHICS_D3D11)
        static const std::string shader_model = "5_0";
        #elif defined(API_GRAPHICS_VULKAN)
        static const std::string shader_model = "6_0";
        #endif

        return shader_model;
    }

    void RHI_Shader::_Reflect(const Shader_Type type, const uint32_t* ptr, const uint32_t size)
	{
		using namespace spirv_cross;

		// Initialize compiler with SPIR-V data
		const auto compiler = CompilerHLSL(ptr, size);

		// The SPIR-V is now parsed, and we can perform reflection on it
		auto resources = compiler.get_shader_resources();

		// Get samplers
		for (const auto& sampler : resources.separate_samplers)
		{
			auto slot = compiler.get_decoration(sampler.id, spv::DecorationBinding);
			m_resources.emplace_back(sampler.name, Descriptor_Sampler, slot, type);
		}

		// Get textures
		for (const auto& image : resources.separate_images)
		{
			auto slot = compiler.get_decoration(image.id, spv::DecorationBinding);
			m_resources.emplace_back(image.name, Descriptor_Texture, slot, type);
		}

		// Get constant buffers
		for (const auto& buffer : resources.uniform_buffers)
		{
			auto slot = compiler.get_decoration(buffer.id, spv::DecorationBinding);
			m_resources.emplace_back(buffer.name, Descriptor_ConstantBuffer, slot, type);
		}
	}
}
