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

//= INCLUDES ===============
#include <memory>
#include <vector>
#include "RHI_Definition.h"
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
//==========================

namespace Directus
{
	class Light;
	class Camera;

	class ENGINE_CLASS RHI_Shader
	{
	public:
		RHI_Shader(RHI_Device* rhiDevice);
		~RHI_Shader(){}

		void AddDefine(const char* define);
		bool Compile(const std::string& filePath, Input_Layout inputLayout);
		
		template <typename T>
		void AddBuffer(BufferScope_Mode bufferScope)
		{
			m_bufferScope = bufferScope;
			m_bufferSize = sizeof(T);
			m_constantBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
			m_constantBuffer->Create(m_bufferSize);
		}

		// Bind - Constant Buffer
		void Bind_Buffer(const Math::Matrix& matrix, unsigned int slot = 0);
		void Bind_Buffer(const Math::Matrix& matrix, const Math::Vector4& vector4, unsigned int slot = 0);
		void Bind_Buffer(const Math::Matrix& matrix, const Math::Vector3& vector3, unsigned int slot = 0);
		void Bind_Buffer(const Math::Matrix& matrix, const Math::Vector2& vector2, unsigned int slot = 0);
		void Bind_Buffer(const Math::Matrix& m1, const Math::Matrix& m2, const Math::Matrix& m3, unsigned int slot = 0);
		void Bind_Buffer(const Math::Matrix& matrix, const Math::Vector3& vector3A, const Math::Vector3& vector3B, unsigned int slot = 0);
		void Bind_Buffer(
			const Math::Matrix& mWVPortho, 
			const Math::Matrix& mWVPinv, 
			const Math::Matrix& mView, 
			const Math::Matrix& mProjection,		
			const Math::Vector2& vector2,
			Light* dirLight,
			Camera* camera,
			unsigned int slot = 0
		);

		D3D11_Shader* GetShader() { return m_shader.get(); }

	private:
		std::shared_ptr<RHI_ConstantBuffer> m_constantBuffer;
		unsigned int m_bufferSize;
		BufferScope_Mode m_bufferScope;
		std::shared_ptr<D3D11_Shader> m_shader;
		RHI_Device* m_rhiDevice;
	};
}