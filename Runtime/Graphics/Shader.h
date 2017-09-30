/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ============================
#include "D3D11/D3D11GraphicsDevice.h"
#include "../../Math/Matrix.h"
#include "../Math/Vector2.h"
#include <memory>
//=======================================

namespace Directus
{
	class D3D11ConstantBuffer;
	class D3D11Shader;
	class Context;

	enum ConstantBufferType
	{
		mWVP,
		mWVPvColor,
		mWVPvResolution,
	};

	enum ConstantBufferScope
	{
		VertexShader,
		PixelShader,
		Both
	};

	class Shader
	{
	public:
		Shader(Context* context);
		~Shader();

		void Load(const std::string& filePath);

		void AddDefine(const std::string& define);
		void AddBuffer(ConstantBufferType bufferType, ConstantBufferScope bufferScope);
		void AddSampler(TextureSampler samplerType);

		void Set();
		void SetInputLaytout(InputLayout inputLayout);
		void SetTexture(ID3D11ShaderResourceView* texture, unsigned int slot);

		void SetBuffer(const Math::Matrix& mWorld, const Math::Matrix& mView, const Math::Matrix& mProjection, unsigned int slot);
		void SetBuffer(const Math::Matrix& mWorld, const Math::Matrix& mView, const Math::Matrix& mProjection, const Math::Vector4& color, unsigned int slot);
		void SetBuffer(const Math::Matrix& mWorld, const Math::Matrix& mView, const Math::Matrix& mProjection, const Math::Vector2& resolution, unsigned int slot);

		void DrawIndexed(int indexCount);

	private:
		void SetBufferScope(std::shared_ptr<D3D11ConstantBuffer> buffer, unsigned int slot);

		struct Struct_mWVP
		{
			Math::Matrix mMVP;
		};

		struct Struct_mWVPvColor
		{
			Math::Matrix mMVP;
			Math::Vector4 vColor;
		};

		struct Struct_mWVP_vResolution
		{
			Math::Matrix mMVP;
			Math::Vector2 vResolution;
			Math::Vector2 vPadding;
		};

		std::shared_ptr<D3D11ConstantBuffer> m_constantBuffer;
		std::shared_ptr<D3D11Shader> m_shader;
		Graphics* m_graphics;
		ConstantBufferType m_bufferType;
		ConstantBufferScope m_bufferScope;
	};
}