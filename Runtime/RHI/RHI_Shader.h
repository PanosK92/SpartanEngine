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
		void Compile(const Shader_Type type, const std::string& shader, unsigned long input_layout_type = 0);
		void CompileAsync(Context* context, const Shader_Type type, const std::string& shader, unsigned long input_layout_type = 0);
	
		// Vertex & Pixel shaders
		void* GetVertexShaderBuffer() const	{ return m_vertex_shader; }
		void* GetPixelShaderBuffer() const	{ return m_pixel_shader; }
		bool HasVertexShader() const		{ return m_vertex_shader != nullptr; }
		bool HasPixelShader() const			{ return m_pixel_shader != nullptr; }
		const auto& GetVertexEntryPoint()	{ return _RHI_Shader::entry_point_vertex; }
		const auto& GetPixelEntryPoint()	{ return _RHI_Shader::entry_point_pixel; }

		// Misc
		void AddDefine(const std::string& define, const std::string& value = "1")	{ m_defines[define] = value; }
		const std::string& GetName()												{ return m_name;}
		void SetName(const std::string& name)										{ m_name = name; }
		const auto& GetInputLayout() const											{ return m_input_layout; }
		Compilation_State GetCompilationState() const								{ return m_compilation_state; }

	protected:
		std::shared_ptr<RHI_Device> m_rhi_device;

	private:
		bool CreateInputLayout(void* vertex_shader);
		void* _Compile(Shader_Type type, const std::string& shader);
			
		std::string m_name;
		std::string m_file_path;
		std::map<std::string, std::string> m_defines;	
		Compilation_State m_compilation_state = Shader_Uninitialized;

		// Input layout
		std::shared_ptr<RHI_InputLayout> m_input_layout;
		unsigned long m_inputLayoutType;

		// Shader buffers
		void* m_vertex_shader	= nullptr;
		void* m_pixel_shader	= nullptr;
	};
}