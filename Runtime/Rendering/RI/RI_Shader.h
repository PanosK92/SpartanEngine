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
#include <vector>
#include "RI_Device.h"
#include "Backend_Def.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector2.h"
//=============================

namespace Directus
{
	class Context;
	class Light;
	class Camera;

	enum ConstantBufferType
	{
		CB_Matrix,	
		CB_Matrix_Vector4,
		CB_Matrix_Vector3,
		CB_Matrix_Vector2,
		CB_Matrix_Matrix_Matrix,
		CB_Matrix_Vector3_Vector3,
		CB_Shadowing
	};

	enum ConstantBufferScope
	{
		VertexShader,
		PixelShader,
		Global
	};

	class RI_Shader
	{
	public:
		RI_Shader(Context* context);
		~RI_Shader();

		void Compile(const std::string& filePath);

		void AddDefine(const char* define);
		void AddBuffer(ConstantBufferType bufferType, ConstantBufferScope bufferScope);
		bool AddSampler(
			Texture_Sampler_Filter filter				= Texture_Sampler_Anisotropic, 
			Texture_Address_Mode addressMode			= Texture_Address_Wrap, 
			Texture_Comparison_Function comparisonFunc	= Texture_Comparison_Always
		);

		bool Bind();
		void SetInputLaytout(Input_Layout inputLayout);

		// Bind - Texture
		void Bind_Texture(void* texture, unsigned int slot = 0);
		void Bind_Textures(const std::vector<void*>& textures);

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

		void Draw(unsigned int vertexCount);
		void DrawIndexed(unsigned int indexCount, unsigned int indexOffset = 0, unsigned int vertexOffset = 0);

	private:
		void SetBufferScope(D3D11_ConstantBuffer* buffer, unsigned int slot = 0);

		struct Struct_Matrix
		{
			Math::Matrix matrix;
		};

		struct Struct_Matrix_Vector4
		{
			Math::Matrix matrix;
			Math::Vector4 vector4;
		};

		struct Struct_Matrix_Vector3
		{
			Math::Matrix matrix;
			Math::Vector3 vector3;
			float padding;
		};

		struct Struct_Matrix_Vector2
		{
			Math::Matrix matrix;
			Math::Vector2 vector2;
			Math::Vector2 padding;
		};

		struct Struct_Shadowing
		{
			Math::Matrix wvpOrtho;
			Math::Matrix wvpInv;
			Math::Matrix view;
			Math::Matrix projection;
			Math::Matrix projectionInverse;
			Math::Matrix mLightViewProjection[3];
			Math::Vector4 shadowSplits;
			Math::Vector3 lightDir;
			float shadowMapResolution;
			Math::Vector2 resolution;
			float nearPlane;
			float farPlane;
			float doShadowMapping;
			Math::Vector3 padding;
		};

		struct Struct_Matrix_Matrix_Matrix
		{
			Math::Matrix m1;
			Math::Matrix m2;
			Math::Matrix m3;
		};

		struct Struct_Matrix_Vector3_Vector3
		{
			Math::Matrix matrix;
			Math::Vector3 vector3A;
			float padding;
			Math::Vector3 vector3B;
			float padding2;
		};

		std::unique_ptr<D3D11_ConstantBuffer> m_constantBuffer;
		std::unique_ptr<D3D11_Shader> m_shader;
		RenderingDevice* m_renderingDevice;
		ConstantBufferType m_bufferType;
		ConstantBufferScope m_bufferScope;
	};
}