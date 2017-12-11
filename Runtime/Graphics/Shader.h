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

//= INCLUDES =========================
#include "D3D11/D3D11GraphicsDevice.h"
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
#include <memory>
//====================================

namespace Directus
{
	class D3D11ConstantBuffer;
	class D3D11Shader;
	class Context;

	enum ConstantBufferType
	{
		WVP,
		W_V_P,
		WVP_Color,
		WVP_Resolution,
		WVP_WVPInverse_Resolution_Planes
	};

	enum ConstantBufferScope
	{
		VertexShader,
		PixelShader,
		Global
	};

	class Shader
	{
	public:
		Shader(Context* context);
		~Shader();

		void Load(const std::string& filePath);

		void AddDefine(LPCSTR define);
		void AddBuffer(ConstantBufferType bufferType, ConstantBufferScope bufferScope);
		void AddSampler(TextureSampler samplerType);

		void Set();
		void SetInputLaytout(InputLayout inputLayout);
		void SetTexture(ID3D11ShaderResourceView* texture, unsigned int slot);
		void SetTextures(std::vector<ID3D11ShaderResourceView*> textures);

		void SetBuffer(const Math::Matrix& mWorld, const Math::Matrix& mView, const Math::Matrix& mProjection, unsigned int slot);
		void SetBuffer(const Math::Matrix& mWorld, const Math::Matrix& mView, const Math::Matrix& mProjection, const Math::Vector4& color, unsigned int slot);
		void SetBuffer(const Math::Matrix& mWorld, const Math::Matrix& mView, const Math::Matrix& mProjection, const Math::Vector2& resolution, unsigned int slot);
		void SetBuffer(
			const Math::Matrix& mWorldViewProjection, 
			const Math::Matrix& mWorldViewProjectionInverse, 
			const Math::Matrix& mView, 
			const Math::Matrix& mProjection, 
			const Math::Vector2& resolution, 
			float nearPlane,
			float farPlane, 
			unsigned int slot
		);

		void Draw(unsigned int vertexCount);
		void DrawIndexed(unsigned int indexCount);

	private:
		void SetBufferScope(D3D11ConstantBuffer* buffer, unsigned int slot);

		struct Struct_WVP
		{
			Math::Matrix wvp;
		};

		struct Struct_W_V_P
		{
			Math::Matrix world;
			Math::Matrix view;
			Math::Matrix projection;
		};

		struct Struct_WVP_Color
		{
			Math::Matrix wvp;
			Math::Vector4 color;
		};

		struct Struct_WVP_Resolution
		{
			Math::Matrix wvp;
			Math::Vector2 resolution;
			Math::Vector2 padding;
		};

		struct Struct_WVP_WVPInverse_Resolution_Planes
		{
			Math::Matrix wvp;
			Math::Matrix wvpInverse;
			Math::Matrix view;
			Math::Matrix projection;
			Math::Matrix projectionInverse;
			Math::Vector2 resolution;
			float nearPlane;
			float farPlane;
		};

		std::unique_ptr<D3D11ConstantBuffer> m_constantBuffer;
		std::unique_ptr<D3D11Shader> m_shader;
		Graphics* m_graphics;
		ConstantBufferType m_bufferType;
		ConstantBufferScope m_bufferScope;
	};
}