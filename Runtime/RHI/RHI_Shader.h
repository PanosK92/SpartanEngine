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

//= INCLUDES ======================
#include <memory>
#include <string>
#include <map>
#include "RHI_Definition.h"
#include "RHI_Object.h"
#include "..\Core\EngineDefs.h"
#include "..\Core\Context.h"
#include "..\Threading\Threading.h"
#include "..\Logging\Log.h"
//=================================

namespace Directus
{
	static const char* VERTEX_SHADER_ENTRYPOINT = "mainVS";
	static const char* PIXEL_SHADER_ENTRYPOINT	= "mainPS";
	static const char* VERTEX_SHADER_MODEL		= "vs_5_0";
	static const char* PIXEL_SHADER_MODEL		= "ps_5_0";

	enum Shader_State
	{
		Shader_Uninitialized,
		Shader_Compiling,
		Shader_Built,
		Shader_Failed
	};

	class ENGINE_CLASS RHI_Shader : public RHI_Object
	{
	public:
		//= GRAPHICS API =================================
		RHI_Shader(std::shared_ptr<RHI_Device> rhiDevice);
		~RHI_Shader();
		//================================================

		#define LOG_STATE(state, filePath)								\
		if (state == Shader_Built)										\
		{																\
			LOGF_INFO("Successfully compiled %s", filePath.c_str());	\
		}																\
		else if (state == Shader_Failed)								\
		{																\
			LOGF_ERROR("Failed to compile %s", filePath.c_str());		\
		}

		virtual void CompileVertex(const std::string& shader, unsigned long inputLayout)
		{
			m_shaderState	= Shader_Compiling;
			bool vertex		= API_CompileVertex(shader, inputLayout);

			m_shaderState = (vertex) ? Shader_Built : Shader_Failed;
			LOG_STATE(m_shaderState, shader);
		}

		virtual void CompileVertex_Async(const std::string& shader, unsigned long inputLayout, Context* context)
		{
			context->GetSubsystem<Threading>()->AddTask([this, shader, inputLayout]()
			{
				CompileVertex(shader, inputLayout);
			});
		}

		virtual void CompilePixel(const std::string& shader)
		{
			m_shaderState	= Shader_Compiling;
			bool pixel		= API_CompilePixel(shader);

			m_shaderState= (pixel) ? Shader_Built : Shader_Failed;
			LOG_STATE(m_shaderState, shader);
		}


		virtual void CompilePixel_Async(const std::string& shader, Context* context)
		{
			context->GetSubsystem<Threading>()->AddTask([this, shader]()
			{
				CompilePixel(shader);
			});
		}

		virtual void CompileVertexPixel(const std::string& shader, unsigned long inputLayout)
		{
			m_shaderState	= Shader_Compiling;
			bool vertex		= API_CompileVertex(shader, inputLayout);
			bool pixel		= API_CompilePixel(shader);

			m_shaderState = (vertex && pixel) ? Shader_Built : Shader_Failed;
			LOG_STATE(m_shaderState, shader);
		}

		virtual void CompileVertexPixel_Async(const std::string& shader, unsigned long inputLayout, Context* context)
		{
			context->GetSubsystem<Threading>()->AddTask([this, shader, inputLayout]()
			{
				CompileVertexPixel(shader, inputLayout);
			});
		}

		void AddDefine(const std::string& define, const std::string& value = "1");

		template <typename T>
		void AddBuffer()
		{
			m_bufferSize = sizeof(T);
			CreateConstantBuffer(m_bufferSize);
		}
		void UpdateBuffer(void* data);
		void* GetVertexShaderBuffer()								{ return m_vertexShader; }
		void* GetPixelShaderBuffer()								{ return m_pixelShader; }
		std::shared_ptr<RHI_ConstantBuffer>& GetConstantBuffer()	{ return m_constantBuffer; }
		void SetName(const std::string& name)						{ m_name = name; }
		bool HasVertexShader()										{ return m_hasVertexShader; }
		bool HasPixelShader()										{ return m_hasPixelShader; }
		std::shared_ptr<RHI_InputLayout> GetInputLayout()			{ return m_inputLayout; }
		Shader_State GetState()										{ return m_shaderState; }

	protected:
		std::shared_ptr<RHI_Device> m_rhiDevice;

	private:
		//= API =============================================================================
		virtual bool API_CompileVertex(const std::string& shader, unsigned long inputLayout);
		virtual bool API_CompilePixel(const std::string& shader);
		//===================================================================================
		void CreateConstantBuffer(unsigned int size);

		unsigned int m_bufferSize;	
		std::shared_ptr<RHI_ConstantBuffer> m_constantBuffer;	
		std::string m_name;
		std::string m_filePath;
		std::string m_entrypoint;
		std::string m_profile;
		std::map<std::string, std::string> m_macros;
		std::shared_ptr<RHI_InputLayout> m_inputLayout;	
		bool m_hasVertexShader		= false;
		bool m_hasPixelShader		= false;
		Shader_State m_shaderState	= Shader_Uninitialized;

		// D3D11
		void* m_vertexShader	= nullptr;
		void* m_pixelShader		= nullptr;
	};
}