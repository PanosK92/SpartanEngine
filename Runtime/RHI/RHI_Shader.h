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

#pragma once

//= INCLUDES ==============
#include <memory>
#include <string>
#include <map>
#include <vector>
#include "RHI_Object.h"
#include "RHI_Definition.h"
//=========================

namespace Spartan
{
	// Forward declarations
	class Context;

	enum Shader_Type
	{
		Shader_Vertex,
		Shader_Pixel,
		Shader_VertexPixel
	};

	struct Shader_Resource
	{
		Shader_Resource() = default;
		Shader_Resource(const std::string& name, RHI_Descriptor_Type type, uint32_t slot, Shader_Type shader_type)
		{
			this->name			= name;
			this->type			= type;
			this->slot			= slot;
			this->shader_type	= shader_type;
		}
	
		std::string name;
		RHI_Descriptor_Type type;
		uint32_t slot;
		Shader_Type shader_type;
	};

	enum Compilation_State
	{
		Shader_Uninitialized,
		Shader_Compiling,
		Shader_Compiled,
		Shader_Failed
	};

	namespace _RHI_Shader
	{
		static const std::string entry_point_vertex	= "mainVS";
		static const std::string entry_point_pixel	= "mainPS";
		#if defined(API_GRAPHICS_D3D11)
		static const std::string shader_model		= "5_0";
		#elif defined(API_GRAPHICS_VULKAN)
		static const std::string shader_model		= "6_0";
		#endif
	}

	class SPARTAN_CLASS RHI_Shader : public RHI_Object
	{
	public:

		RHI_Shader(const std::shared_ptr<RHI_Device>& rhi_device);
		~RHI_Shader();

		// Compilation
		void Compile(const Shader_Type type, const std::string& shader, const RHI_Vertex_Attribute_Type vertex_attributes = Vertex_Attribute_None);
		void CompileAsync(Context* context, const Shader_Type type, const std::string& shader, const RHI_Vertex_Attribute_Type vertex_attributes = Vertex_Attribute_None);
	
		// Properties
		auto GetResource_VertexShader() const										{ return m_vertex_shader; }
		auto GetResource_PixelShader() const										{ return m_pixel_shader; }
		auto HasVertexShader() const												{ return m_vertex_shader != nullptr; }
		auto HasPixelShader() const													{ return m_pixel_shader != nullptr; }
		const auto& GetVertexEntryPoint() const										{ return _RHI_Shader::entry_point_vertex; }
		const auto& GetPixelEntryPoint() const										{ return _RHI_Shader::entry_point_pixel; }
		const auto& GetResources() const											{ return m_resources; }
		const auto& GetInputLayout() const											{ return m_input_layout; }
		auto GetCompilationState() const											{ return m_compilation_state; }
		const auto& GetName() const													{ return m_name; }
		void SetName(const std::string& name)										{ m_name = name; }
		void AddDefine(const std::string& define, const std::string& value = "1")	{ m_defines[define] = value; }

	protected:
		std::shared_ptr<RHI_Device> m_rhi_device;

	private:
		void* _Compile(Shader_Type type, const std::string& shader, RHI_Vertex_Attribute_Type vertex_attributes = Vertex_Attribute_None);
		void _Reflect(const Shader_Type type, const uint32_t* ptr, uint32_t size);

		std::string m_name;
		std::string m_file_path;
		std::map<std::string, std::string> m_defines;
		std::vector<Shader_Resource> m_resources;
		std::shared_ptr<RHI_InputLayout> m_input_layout;
		Compilation_State m_compilation_state = Shader_Uninitialized;

		// API 
		void* m_vertex_shader	= nullptr;
		void* m_pixel_shader	= nullptr;
	};
}