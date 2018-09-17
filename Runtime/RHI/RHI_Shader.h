/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ======================
#include <memory>
#include <string>
#include <map>
#include <vector>
#include "RHI_Definition.h"
#include "..\Core\EngineDefs.h"
#include "..\Core\Context.h"
#include "..\Threading\Threading.h"
#include "..\Logging\Log.h"
//=================================

namespace Directus
{
	class ENGINE_CLASS RHI_Shader
	{
	public:
		//= GRAPHICS API ==========================================================
		RHI_Shader(std::shared_ptr<RHI_Device> rhiDevice);
		~RHI_Shader();	
		bool Compile_Vertex(const std::string& filePath, Input_Layout inputLayout);
		bool Compile_Pixel(const std::string& filePath);
		//=========================================================================

		void Compile_VertexPixel_Async(const std::string& filePath, Input_Layout inputLayout, Context* context)
		{
			context->GetSubsystem<Threading>()->AddTask([this, filePath, inputLayout]()
			{
				LOGF_INFO("RHI_Shader::Compile_VertexPixel_Async: Compiling %s...", filePath.c_str());
				Compile_Vertex(filePath, inputLayout);
				Compile_Pixel(filePath);
			});
		}

		void AddDefine(const std::string& define, const std::string& value = "1");

		template <typename T>
		void AddBuffer(unsigned int slot, Buffer_Scope scope)
		{
			m_bufferSize = sizeof(T);
			CreateConstantBuffer(m_bufferSize, slot, scope);
		}
		void UpdateBuffer(void* data);
		void* GetVertexShaderBuffer()								{ return m_vertexShader; }
		void* GetPixelShaderBuffer()								{ return m_pixelShader; }
		std::shared_ptr<RHI_ConstantBuffer>& GetConstantBuffer()	{ return m_constantBuffer; }
		void SetName(const std::string& name)						{ m_name = name; }
		bool IsCompiled()											{ return m_compiled; }
		bool HasVertexShader()										{ return m_hasVertexShader; }
		bool HasPixelShader()										{ return m_hasPixelShader; }
		std::shared_ptr<RHI_InputLayout> GetInputLayout()			{ return m_inputLayout; }
		unsigned int GetID()										{ return m_id; }

	private:
		void CreateConstantBuffer(unsigned int size, unsigned int slot, Buffer_Scope scope);

		unsigned int m_bufferSize;	
		std::shared_ptr<RHI_ConstantBuffer> m_constantBuffer;
		std::shared_ptr<RHI_Device> m_rhiDevice;
		std::string m_name;
		std::string m_filePath;
		std::string m_entrypoint;
		std::string m_profile;
		std::map<std::string, std::string> m_macros;
		std::shared_ptr<RHI_InputLayout> m_inputLayout;
		bool m_compiled;
		bool m_hasVertexShader;
		bool m_hasPixelShader;
		unsigned int m_id;

		// D3D11
		void* m_vertexShader;
		void* m_pixelShader;
	};
}