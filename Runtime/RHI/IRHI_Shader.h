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

//= INCLUDES ==================
#include <memory>
#include <string>
#include "IRHI_Definition.h"
#include "..\Core\EngineDefs.h"
#include "RHI_ConstantBuffer.h"
//=============================

namespace Directus
{
	class ENGINE_CLASS IRHI_Shader
	{
	public:
		IRHI_Shader(RHI_Device* rhiDevice);
		~IRHI_Shader(){}

		virtual void AddDefine(const std::string& define, const std::string& value = "1") = 0;
		virtual bool Compile(const std::string& filePath, Input_Layout inputLayout) = 0;
		
		template <typename T>
		void AddBuffer(Buffer_Scope bufferScope)
		{
			m_bufferScope = bufferScope;
			m_bufferSize = sizeof(T);
			m_constantBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
			m_constantBuffer->Create(m_bufferSize);
		}

		void BindBuffer(void* data, unsigned int slot);
		virtual void* GetVertexShaderBuffer() = 0;
		virtual void* GetPixelShaderBuffer() = 0;

	protected:	
		unsigned int m_bufferSize;
		Buffer_Scope m_bufferScope;
		std::shared_ptr<RHI_ConstantBuffer> m_constantBuffer;
		RHI_Device* m_rhiDevice;
	};
}